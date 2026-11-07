# DMFix — Dark Messiah of Might and Magic on modern Windows

Dark Messiah (2006, Source engine) often crashes on modern PCs right after the intro,
inside `server.dll`. DMFix is a small launcher that fixes this crash and sets the game up
for current hardware. **It contains no game files**; it sits next to your own install.

[Türkçe açıklama aşağıda](#türkçe)

## Install

1. Download `DMFix.exe` from [Releases](../../releases).
2. Put it in the game folder, next to `mm.exe`.
3. Run `DMFix_Setup.bat` once (or `DMFix.exe --setup`).
4. From now on, start the game with `DMFix.exe`. Make a desktop shortcut to it if you like.

## What it does

**Launcher (`DMFix.exe`)**
- **Crash fix.** Some builds ship `server.dll` / `client.dll` with an incomplete relocation
  table. If their preferred address (`ImageBase`, e.g. `0x22000000`) is already taken,
  Windows moves them, and the game reads an address that was never fixed up and crashes.
  Modern GPU drivers (Intel/Nvidia) often take that address.
  DMFix does what `mm.exe` does (adds `bin\` to PATH, loads `bin\launcher.dll`, calls
  `LauncherMain`), but first loads both DLLs into its own process while their addresses are
  still free. When the engine asks for them, Windows returns the copies already loaded.
  No other process is touched, nothing is injected. The game runs as `DMFix.exe`.
- Opens the game at your screen's own resolution, borderless windowed. Skips the intro.
- `DMFix.exe` is Large Address Aware, so the game can use 4GB of memory instead of 2GB.
  `mm.exe` is not modified.

**One-time setup (`--setup`)**
- **DXVK 3.1.1** (`d3d9.dll`, downloaded from the official GitHub release): turns DirectX 9
  into Vulkan. Smoother frame times, and avoids crashes in old D3D9 drivers. The game looks
  the same; no post-processing is added. An existing `d3d9.dll` is kept as `d3d9.dll.bak`.
- High-performance GPU for `mm.exe` and `DMFix.exe` in Windows graphics settings (laptops pick Nvidia/AMD),
  fullscreen optimizations off. Current user and `mm.exe` only.
- `mm/cfg/autoexec.cfg`: the engine's highest settings (16x AF, 8x MSAA, HDR, expensive water,
  full LOD), FPS cap = your refresh rate, VSync on. Never overwrites an existing file.

## Options

- `--lang en|tr` sets the message language (default: your Windows language).
- Anything else goes to the game, e.g. `DMFix.exe -w 2560 -h 1440` or `DMFix.exe +map l00`.

## Uninstall

Delete `DMFix.exe` and `d3d9.dll` (rename `d3d9.dll.bak` back if you had one).
Delete `mm/cfg/autoexec.cfg` to reset the settings.

## Notes

- Needs 64-bit Windows 10/11 and a Vulkan-capable GPU for DXVK (anything from ~2016 on).
- Translations and mods are not included; install them with their own installers.
- Tested on: i7-11800H, RTX 3050 Ti Laptop + Intel UHD, Windows 11, 1080p 144Hz.

## Build

Visual Studio (C++ workload), x86 toolchain: run `build.bat`. Output: `dist\DMFix.exe`.
One C file, Win32 API only, no dependencies.

DXVK is © Philip Rebohle and contributors, zlib license: https://github.com/doitsujin/dxvk

License: MIT

---

## Türkçe

Dark Messiah yeni bilgisayarlarda intro'dan sonra `server.dll` içinde çöküyor. DMFix bu
çökmeyi gideren ve oyunu modern donanıma göre ayarlayan küçük bir başlatıcı. Oyun dosyası içermez.

**Kurulum:** `DMFix.exe`'yi oyun klasörüne, `mm.exe`'nin yanına koy. Bir kez `DMFix_Setup.bat`
çalıştır. Oyunu artık hep `DMFix.exe` ile aç.

**Yaptıkları:** çökme düzeltmesi, ekranının kendi çözünürlüğünde kenarlıksız pencere,
DXVK (DirectX 9 → Vulkan, görüntüye efekt eklemez), 4GB bellek, laptopta harici
ekran kartı, motorun en yüksek grafik ayarları ve ekran yenileme hızına göre FPS sınırı.

Mesajlar Windows diline göre Türkçe ya da İngilizce çıkar; `--lang tr` ile zorlanabilir.
