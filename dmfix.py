"""DMFix - Dark Messiah of Might and Magic launcher/fixer for modern Windows.

Contains no game files. Put it next to mm.exe.
  DMFix.exe            launch the game safely
  DMFix.exe --setup    one-time tweaks (LAA, high-performance GPU, autoexec)
  --lang en|tr         UI language (default: Windows language)

Crash cause: some builds ship server.dll/client.dll with an incomplete relocation
table. If their preferred ImageBase is taken, Windows rebases them and the game
reads an unrelocated address and crashes. DMFix keeps those ranges reserved while
the game starts and loads both DLLs at their preferred base right after engine.dll.
"""
import ctypes
import ctypes.wintypes as W
import os
import shutil
import struct
import sys
import time

TEXT = {
    'en': {
        'no_exe': 'mm.exe not found.\nPut DMFix.exe in the game folder, next to mm.exe.\n\n{p}',
        'no_start': 'Could not start the game (error {e}).',
        'laa': 'mm.exe 4GB (Large Address Aware) patch - backup: mm.exe.bak',
        'gpu': 'High-performance GPU selected for mm.exe',
        'cfg': 'mm/cfg/autoexec.cfg written',
        'dxvk': 'DXVK {v} installed (DirectX 9 -> Vulkan)',
        'dxvk_fail': 'DXVK could not be downloaded ({e}). The game still works without it.',
        'done': 'Setup complete:\n\n- {l}\n\nLaunch the game with DMFix.exe from now on.',
    },
    'tr': {
        'no_exe': 'mm.exe bulunamadi.\nDMFix.exe dosyasini oyun klasorune, mm.exe yanina koy.\n\n{p}',
        'no_start': 'Oyun baslatilamadi (hata {e}).',
        'laa': 'mm.exe 4GB (Large Address Aware) yamasi - yedek: mm.exe.bak',
        'gpu': 'mm.exe icin yuksek performansli GPU secildi',
        'cfg': 'mm/cfg/autoexec.cfg yazildi',
        'dxvk': 'DXVK {v} kuruldu (DirectX 9 -> Vulkan)',
        'dxvk_fail': 'DXVK indirilemedi ({e}). Oyun onsuz da calisir.',
        'done': 'Kurulum tamam:\n\n- {l}\n\nOyunu artik DMFix.exe ile ac.',
    },
}


def lang():
    for i, a in enumerate(sys.argv):
        if a == '--lang' and i + 1 < len(sys.argv):
            return sys.argv[i + 1] if sys.argv[i + 1] in TEXT else 'en'
    return 'tr' if ctypes.windll.kernel32.GetUserDefaultUILanguage() & 0x3ff == 0x1f else 'en'


def t(key, **kw):
    return TEXT[LANG][key].format(**kw)

ARGS = '-game mm -novid -windowed -noborder -w {w} -h {h} -heapsize 524288'
WAIT_ENGINE = 30  # seconds
DXVK_VERSION = '3.1.1'
DXVK_URL = f'https://github.com/doitsujin/dxvk/releases/download/v{DXVK_VERSION}/dxvk-{DXVK_VERSION}.tar.gz'

AUTOEXEC = """// DMFix - modern system settings
mat_queue_mode 0
snd_mixahead 0.1
fps_max {hz}
mat_vsync 1
mat_triplebuffered 1
mat_picmip -1
mat_forceaniso 16
mat_trilinear 0
mat_antialias 8
mat_hdr_level 2
r_rootlod 0
r_lod 0
r_shadowrendertotexture 1
r_waterforceexpensive 1
r_waterforcereflectentities 1
cl_detaildist 2000
r_decals 2048
"""

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi')
k32.VirtualAllocEx.restype = ctypes.c_void_p
k32.VirtualAllocEx.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.c_size_t, W.DWORD, W.DWORD]
k32.VirtualFreeEx.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.c_size_t, W.DWORD]
k32.WriteProcessMemory.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p]
k32.CreateRemoteThread.restype = W.HANDLE
k32.CreateRemoteThread.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p, W.DWORD, ctypes.c_void_p]
k32.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
k32.CloseHandle.argtypes = [W.HANDLE]
k32.ResumeThread.argtypes = [W.HANDLE]
psapi.EnumProcessModulesEx.argtypes = [W.HANDLE, ctypes.c_void_p, W.DWORD, ctypes.POINTER(W.DWORD), W.DWORD]
psapi.GetModuleBaseNameW.argtypes = [W.HANDLE, ctypes.c_void_p, W.LPWSTR, W.DWORD]


class STARTUPINFO(ctypes.Structure):
    _fields_ = ([('cb', W.DWORD), ('r', W.LPWSTR), ('d', W.LPWSTR), ('t', W.LPWSTR)]
                + [(f'x{i}', W.DWORD) for i in range(8)]
                + [('w', W.WORD), ('c', W.WORD), ('p', ctypes.c_void_p),
                   ('i', W.HANDLE), ('o', W.HANDLE), ('e', W.HANDLE)])


class PROCINFO(ctypes.Structure):
    _fields_ = [('hp', W.HANDLE), ('ht', W.HANDLE), ('pid', W.DWORD), ('tid', W.DWORD)]


def game_dir():
    base = os.path.dirname(sys.executable if getattr(sys, 'frozen', False) else os.path.abspath(__file__))
    if not os.path.exists(os.path.join(base, 'mm.exe')):
        msg(t('no_exe', p=base))
        sys.exit(1)
    return base


def msg(text):
    ctypes.windll.user32.MessageBoxW(None, text, 'DMFix', 0x40)


def pe_image(path):
    """(ImageBase, SizeOfImage): the address range the DLL wants."""
    with open(path, 'rb') as f:
        d = f.read(4096)
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    return struct.unpack_from('<I', d, pe + 52)[0], struct.unpack_from('<I', d, pe + 80)[0]


def screen_size():
    u = ctypes.windll.user32
    u.SetProcessDPIAware()
    return u.GetSystemMetrics(0), u.GetSystemMetrics(1)


def refresh_rate():
    hz = ctypes.windll.gdi32.GetDeviceCaps(ctypes.windll.user32.GetDC(None), 116)  # VREFRESH
    return hz if 30 <= hz <= 360 else 60


def launch(gd):
    dlls = [p for p in (os.path.join(gd, 'mm', 'bin', n) for n in ('server.dll', 'client.dll')) if os.path.exists(p)]
    ranges = [pe_image(p) for p in dlls]
    w, h = screen_size()
    cmd = f'"{os.path.join(gd, "mm.exe")}" {ARGS.format(w=w, h=h)} {" ".join(game_args())}'
    si, pi = STARTUPINFO(), PROCINFO()
    si.cb = ctypes.sizeof(si)
    os.environ.setdefault('DXVK_LOG_LEVEL', 'none')
    if not k32.CreateProcessW(None, cmd, None, None, False, 4, None, gd, ctypes.byref(si), ctypes.byref(pi)):
        msg(t('no_start', e=ctypes.get_last_error()))
        return
    held = [b for b, s in ranges if k32.VirtualAllocEx(pi.hp, b, s, 0x2000, 1)]  # MEM_RESERVE, NOACCESS
    k32.ResumeThread(pi.ht)
    # When engine.dll appears the ranges are still free: drop the reservation and load
    # both DLLs at their base. The engine's later LoadLibrary returns the loaded module.
    t0 = time.time()
    while time.time() - t0 < WAIT_ENGINE and not remote_module(pi.hp, 'engine.dll'):
        time.sleep(0.002)
    for b in held:
        k32.VirtualFreeEx(pi.hp, b, 0, 0x8000)  # MEM_RELEASE
    for dll in dlls:
        preload(pi.hp, dll)
    k32.CloseHandle(pi.ht)
    k32.CloseHandle(pi.hp)


def remote_module(hp, want):
    mods, need = (ctypes.c_void_p * 2048)(), W.DWORD()
    name = ctypes.create_unicode_buffer(260)
    if not psapi.EnumProcessModulesEx(hp, mods, ctypes.sizeof(mods), ctypes.byref(need), 1):  # 32-bit list
        return None
    for m in mods[:need.value // ctypes.sizeof(ctypes.c_void_p)]:
        psapi.GetModuleBaseNameW(hp, m, name, 260)
        if name.value.lower() == want:
            return m
    return None


def export_rva(path, func):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    secs = [struct.unpack_from('<8sIIII', d, pe + 24 + struct.unpack_from('<H', d, pe + 20)[0] + 40 * i)
            for i in range(struct.unpack_from('<H', d, pe + 6)[0])]
    o = lambda r: next(ra + r - va for _, vs, va, rs, ra in secs if va <= r < va + max(vs, rs))
    e = o(struct.unpack_from('<I', d, pe + 120)[0])
    n = struct.unpack_from('<I', d, e + 24)[0]
    funcs, names, ords = struct.unpack_from('<III', d, e + 28)
    for i in range(n):
        p = o(struct.unpack_from('<I', d, o(names) + 4 * i)[0])
        if d[p:d.index(b'\0', p)] == func:
            return struct.unpack_from('<I', d, o(funcs) + 4 * struct.unpack_from('<H', d, o(ords) + 2 * i)[0])[0]


def preload(hp, dll):
    k32base = remote_module(hp, 'kernel32.dll')
    fn = k32base + export_rva(os.path.join(os.environ['WINDIR'], 'SysWOW64', 'kernel32.dll'), b'LoadLibraryW')
    data = (dll + '\0').encode('utf-16le')
    mem = k32.VirtualAllocEx(hp, None, len(data), 0x3000, 4)
    k32.WriteProcessMemory(hp, ctypes.c_void_p(mem), data, len(data), None)
    th = k32.CreateRemoteThread(hp, None, 0, ctypes.c_void_p(fn), ctypes.c_void_p(mem), 0, None)
    k32.WaitForSingleObject(th, 15000)
    k32.CloseHandle(th)
    k32.VirtualFreeEx(hp, mem, 0, 0x8000)


def setup(gd):
    done = []
    # 1) Large Address Aware: 4GB instead of 2GB
    exe = os.path.join(gd, 'mm.exe')
    with open(exe, 'r+b') as f:
        d = f.read(4096)
        off = struct.unpack_from('<I', d, 0x3c)[0] + 22
        ch = struct.unpack_from('<H', d, off)[0]
        if not ch & 0x20:
            if not os.path.exists(exe + '.bak'):
                shutil.copy2(exe, exe + '.bak')
            f.seek(off)
            f.write(struct.pack('<H', ch | 0x20))
            done.append(t('laa'))
    # 2) discrete GPU + fullscreen optimizations off (current user, mm.exe only)
    import winreg
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r'Software\Microsoft\DirectX\UserGpuPreferences') as r:
        winreg.SetValueEx(r, exe, 0, winreg.REG_SZ, 'GpuPreference=2;')
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r'Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers') as r:
        winreg.SetValueEx(r, exe, 0, winreg.REG_SZ, '~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE')
    done.append(t('gpu'))
    # 3) graphics/stability settings (never overwrites an existing file)
    cfg = os.path.join(gd, 'mm', 'cfg', 'autoexec.cfg')
    if not os.path.exists(cfg):
        os.makedirs(os.path.dirname(cfg), exist_ok=True)
        with open(cfg, 'w') as f:
            f.write(AUTOEXEC.format(hz=refresh_rate()))
        done.append(t('cfg'))
    # 4) DXVK: DirectX 9 -> Vulkan. Faster, and avoids old D3D9 driver crashes.
    try:
        install_dxvk(gd)
        done.append(t('dxvk', v=DXVK_VERSION))
    except Exception as e:
        done.append(t('dxvk_fail', e=e))
    msg(t('done', l='\n- '.join(done)))


def install_dxvk(gd):
    import io, tarfile, urllib.request
    data = urllib.request.urlopen(DXVK_URL, timeout=60).read()
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        dll = tar.extractfile(f'dxvk-{DXVK_VERSION}/x32/d3d9.dll').read()
    dst = os.path.join(gd, 'd3d9.dll')
    if os.path.exists(dst) and not os.path.exists(dst + '.bak'):
        os.replace(dst, dst + '.bak')  # e.g. an older wrapper; keep it
    with open(dst, 'wb') as f:
        f.write(dll)


def game_args():
    out, skip = [], False
    for a in sys.argv[1:]:
        if skip:
            skip = False
        elif a == '--lang':
            skip = True
        elif a != '--setup':
            out.append(a)
    return out


LANG = lang()

if __name__ == '__main__':
    gd = game_dir()
    if '--setup' in sys.argv:
        setup(gd)
    else:
        launch(gd)
