/* DMFix - Dark Messiah of Might and Magic launcher for modern Windows.
 *
 * Does what mm.exe does (put bin\ on PATH, load bin\launcher.dll, call LauncherMain),
 * but first loads mm\bin\server.dll and mm\bin\client.dll into its own process while
 * their preferred addresses are still free. Their relocation tables are incomplete, so
 * if Windows has to move them the game crashes. When the engine later asks for them,
 * Windows hands back the copies already loaded. No other process is touched.
 *
 *   DMFix.exe            start the game (borderless, native resolution, no intro)
 *   DMFix.exe --setup    one-time setup: DXVK, high-performance GPU, autoexec
 *                        (DMFix.exe is Large Address Aware itself, so the game gets 4GB)
 *   --lang en|tr         message language (default: Windows UI language)
 *   anything else        passed to the game, e.g. -w 2560 -h 1440
 *
 * Build (x86): cl /O1 /W3 dmfix.c /link /LARGEADDRESSAWARE /SUBSYSTEM:WINDOWS user32.lib gdi32.lib advapi32.lib shell32.lib
 */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

#define DXVK_VERSION L"3.1.1"
#define GAME_ARGS L"-game mm -novid -windowed -noborder -heapsize 524288"

static int tr;            /* Turkish messages */
static wchar_t dir[MAX_PATH];

static const wchar_t *T(const wchar_t *en, const wchar_t *trk) { return tr ? trk : en; }

static void info(const wchar_t *s) { MessageBoxW(NULL, s, L"DMFix", MB_ICONINFORMATION); }
static void fail(const wchar_t *s) { MessageBoxW(NULL, s, L"DMFix", MB_ICONERROR); }

static int exists(const wchar_t *rel)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%s\\%s", dir, rel);
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

/* Preferred ImageBase from the DLL's own header. */
static DWORD image_base(const wchar_t *path)
{
    BYTE h[4096];
    DWORD n = 0, base = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (ReadFile(f, h, sizeof h, &n, NULL) && n == sizeof h) {
        DWORD pe = *(DWORD *)(h + 0x3c);
        if (pe + 56 < sizeof h) base = *(DWORD *)(h + pe + 52);
    }
    CloseHandle(f);
    return base;
}

static void preload(const wchar_t *rel)
{
    wchar_t p[MAX_PATH], m[600];
    HMODULE h;
    DWORD want;
    swprintf(p, MAX_PATH, L"%s\\%s", dir, rel);
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return;
    want = image_base(p);
    h = LoadLibraryW(p);
    if (h && (DWORD)(UINT_PTR)h != want) {
        swprintf(m, 600, T(L"%s was loaded at 0x%08X instead of 0x%08X.\nThe game may crash. Try again or reboot.",
                           L"%s 0x%08X yerine 0x%08X adresine yuklendi.\nOyun cokebilir. Tekrar dene ya da yeniden baslat."),
                 rel, (DWORD)(UINT_PTR)h, want);
        fail(m);
    }
}

static int screen_hz(void)
{
    HDC dc = GetDC(NULL);
    int hz = GetDeviceCaps(dc, VREFRESH);
    ReleaseDC(NULL, dc);
    return (hz >= 30 && hz <= 500) ? hz : 60;
}

/* ---------------- setup ---------------- */

static int run(wchar_t *cmd)
{
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi)) return 1;
    WaitForSingleObject(pi.hProcess, 180000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

/* Uses curl.exe and tar.exe that ship with Windows 10/11. */
static int install_dxvk(void)
{
    wchar_t tmp[MAX_PATH], cmd[2048], src[MAX_PATH], dst[MAX_PATH], bak[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wcscat(tmp, L"dmfix_dxvk");
    CreateDirectoryW(tmp, NULL);
    swprintf(cmd, 2048, L"curl.exe -fsSL -o \"%s\\dxvk.tar.gz\" https://github.com/doitsujin/dxvk/releases/download/v"
             DXVK_VERSION L"/dxvk-" DXVK_VERSION L".tar.gz", tmp);
    if (run(cmd)) return 0;
    swprintf(cmd, 2048, L"tar.exe -xzf \"%s\\dxvk.tar.gz\" -C \"%s\" dxvk-" DXVK_VERSION L"/x32/d3d9.dll", tmp, tmp);
    if (run(cmd)) return 0;
    swprintf(src, MAX_PATH, L"%s\\dxvk-" DXVK_VERSION L"\\x32\\d3d9.dll", tmp);
    swprintf(dst, MAX_PATH, L"%s\\d3d9.dll", dir);
    swprintf(bak, MAX_PATH, L"%s\\d3d9.dll.bak", dir);
    if (GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(bak) == INVALID_FILE_ATTRIBUTES)
        MoveFileW(dst, bak); /* keep whatever wrapper was there */
    return CopyFileW(src, dst, FALSE);
}

static void set_reg(const wchar_t *key, const wchar_t *name, const wchar_t *val)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)val, (DWORD)(wcslen(val) + 1) * 2);
        RegCloseKey(k);
    }
}

static int write_autoexec(void)
{
    wchar_t p[MAX_PATH];
    FILE *f;
    swprintf(p, MAX_PATH, L"%s\\mm\\cfg", dir);
    CreateDirectoryW(p, NULL);
    wcscat(p, L"\\autoexec.cfg");
    if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) return 0; /* never overwrite */
    if (!(f = _wfopen(p, L"w"))) return 0;
    fprintf(f,
        "// DMFix - highest engine settings\n"
        "mat_queue_mode 0\nsnd_mixahead 0.1\n"
        "fps_max %d\nmat_vsync 1\nmat_triplebuffered 1\n"
        "mat_picmip -1\nmat_forceaniso 16\nmat_trilinear 0\nmat_antialias 8\nmat_hdr_level 2\n"
        "r_rootlod 0\nr_lod 0\nr_shadowrendertotexture 1\n"
        "r_waterforceexpensive 1\nr_waterforcereflectentities 1\ncl_detaildist 2000\nr_decals 2048\n",
        screen_hz());
    fclose(f);
    return 1;
}

static void setup(void)
{
    wchar_t exe[MAX_PATH], m[2048] = L"";
    swprintf(exe, MAX_PATH, L"%s\\mm.exe", dir);
    wcscat(m, T(L"Setup complete:\n\n", L"Kurulum tamam:\n\n"));
    wcscat(m, install_dxvk() ? T(L"- DXVK " DXVK_VERSION L" installed (DirectX 9 -> Vulkan)\n",
                                 L"- DXVK " DXVK_VERSION L" kuruldu (DirectX 9 -> Vulkan)\n")
                             : T(L"- DXVK could not be downloaded; the game still works without it\n",
                                 L"- DXVK indirilemedi; oyun onsuz da calisir\n"));
    /* high-performance GPU and no fullscreen optimizations, current user and these two exes only */
    set_reg(L"Software\\Microsoft\\DirectX\\UserGpuPreferences", exe, L"GpuPreference=2;");
    set_reg(L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", exe, L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE");
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    set_reg(L"Software\\Microsoft\\DirectX\\UserGpuPreferences", exe, L"GpuPreference=2;");
    set_reg(L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", exe, L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE HIGHDPIAWARE");
    wcscat(m, T(L"- High-performance GPU selected\n", L"- Yuksek performansli GPU secildi\n"));
    if (write_autoexec()) wcscat(m, T(L"- mm/cfg/autoexec.cfg written\n", L"- mm/cfg/autoexec.cfg yazildi\n"));
    wcscat(m, T(L"\nStart the game with DMFix.exe from now on.", L"\nOyunu artik DMFix.exe ile ac."));
    info(m);
}

/* ---------------- launch ---------------- */

typedef int (*LauncherMain_t)(HINSTANCE, HINSTANCE, LPSTR, int);

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    int argc, i, do_setup = 0, child = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t self[MAX_PATH], *p, extra[4096] = L"", env[32768], m[700];
    (void)prev; (void)cmdline;

    tr = (GetUserDefaultUILanguage() & 0x3ff) == LANG_TURKISH;
    for (i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--setup")) do_setup = 1;
        else if (!wcscmp(argv[i], L"--lang") && i + 1 < argc) tr = !wcscmp(argv[++i], L"tr");
        else if (!wcscmp(argv[i], L"-game")) child = 1;
        else if (wcslen(extra) + wcslen(argv[i]) + 2 < 4096) { wcscat(extra, L" "); wcscat(extra, argv[i]); }
    }

    GetModuleFileNameW(NULL, self, MAX_PATH);
    wcscpy(dir, self);
    if ((p = wcsrchr(dir, L'\\'))) *p = 0;
    if (!exists(L"bin\\launcher.dll") || !exists(L"mm\\gameinfo.txt")) {
        swprintf(m, 700, T(L"Game files not found.\nPut DMFix.exe in the game folder, next to mm.exe.\n\n%s",
                           L"Oyun dosyalari bulunamadi.\nDMFix.exe dosyasini oyun klasorune, mm.exe yanina koy.\n\n%s"), dir);
        fail(m);
        return 1;
    }
    if (do_setup) { setup(); return 0; }

    if (!child) {
        /* The engine reads its options from the process command line, so start ourselves
           again with the full game command line. */
        STARTUPINFOW si = { sizeof si };
        PROCESS_INFORMATION pi;
        static wchar_t cmd[8192];
        SetProcessDPIAware();
        swprintf(cmd, 8192, L"\"%s\" " GAME_ARGS L" -w %d -h %d%s", self,
                 GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), extra);
        if (!CreateProcessW(self, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
            swprintf(m, 700, T(L"Could not start the game (error %lu).", L"Oyun baslatilamadi (hata %lu)."), GetLastError());
            fail(m);
            return 1;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    }

    /* Child: same environment mm.exe sets up. */
    SetCurrentDirectoryW(dir);
    if (!GetEnvironmentVariableW(L"DXVK_LOG_LEVEL", env, 8)) SetEnvironmentVariableW(L"DXVK_LOG_LEVEL", L"none");
    swprintf(m, 700, L"%s\\bin\\;", dir);
    wcsncpy(env, m, 32768);
    GetEnvironmentVariableW(L"PATH", env + wcslen(env), (DWORD)(32768 - wcslen(env)));
    SetEnvironmentVariableW(L"PATH", env);

    preload(L"mm\\bin\\server.dll");
    preload(L"mm\\bin\\client.dll");

    {
        HMODULE l = LoadLibraryExW(L"bin\\launcher.dll", NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        LauncherMain_t main_fn = l ? (LauncherMain_t)GetProcAddress(l, "LauncherMain") : NULL;
        char *cl = GetCommandLineA();
        if (!main_fn) {
            fail(T(L"Could not load bin\\launcher.dll.", L"bin\\launcher.dll yuklenemedi."));
            return 1;
        }
        /* mm.exe passes the arguments after the exe name */
        if (*cl == '"') { cl = strchr(cl + 1, '"'); cl = cl ? cl + 1 : GetCommandLineA(); }
        else while (*cl && *cl != ' ') cl++;
        while (*cl == ' ') cl++;
        return main_fn(inst, NULL, cl, show);
    }
}
