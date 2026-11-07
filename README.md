# Cosmic Osmo's Hex Isle — Windows 10/11 compatibility

Fixes and optional enhancements for running **Cosmic Osmo's Hex Isle** (Cyan Worlds, 2007; Plasma 3.0 engine)
on modern Windows. You need your own copy of the game disc; no game files are included here.

## Status
| Issue | Cause | Fix |
|---|---|---|
| "Your CD key is missing or invalid" | `plasma.exe` reads a value the installer derives from your CD key | See [CD key](#cd-key) |
| Installer not needed | Game files live in the Mac Cider wrapper on the hybrid disc | `install_hexisle.bat` copies them to `C:\Cosmo` |
| ~0.25 fps | Main loop calls `0x443D20` every frame; with no joystick open it re-runs `DirectInput8Create` + `EnumDevices(GAMECTRL)` (and leaks the objects) — seconds per frame on HID-heavy PCs | `tools/patch_plasma.ps1` / `.py`: `je`→`jmp` at file offset `0x43D2D` (startup enumeration kept) |
| No frame cap / VSync control | Engine has `Graphics.EnableVSync` but no limiter | `hexfix` d3d9.dll proxy (optional, off by default) |
| Broken view on ultrawide (21:9, 32:9) | Horizontal FOV is `52.5° × aspect` (−3.3° above 80°) at `0x443293`/`0x4F7242`: 90° at 16:9 but ~183° at 32:9 | `hexfix` `WidescreenFOV=1`: vanilla up to 16:9, Hor+ beyond (21:9 ≈ 106°, 32:9 ≈ 127°) |
| Squeezed GUI (e.g. in-level badge) on ultrawide | GUI dialogs use `fovX = fovY × max(aspect, 4/3)` (linear in degrees) at `0x5E9B31` (dialog load) and `0x5EAEB7` (aspect change) | `hexfix` `WidescreenFOV=1`: same Hor+ treatment as the 3D camera |
| Wide fullscreen modes (3840×1080, 5120×1440) reset to 800×600 on restart | Startup check (`0x4412D5`) looks for graphics.ini's mode in the device record, whose mode list omits modes wider than 2560 although D3D reports them (the Settings menu reads a different, complete list) | `hexfix` `KeepWideModes=1`: if D3D supports the saved mode, a duplicate record of the same depth is retargeted to it |
| Mouse camera occasionally leaps | While steering, the mouse device (`0x4A6950`) sets its last position to the window centre and warps the cursor there (`0x4A6690`) whenever it leaves the 0.3–0.7 band. Queued `WM_MOUSEMOVE`s from before the warp, and (on some systems) the warp being undone by in-flight motion, are then measured against a centre the cursor isn't at. `lParam` is also decoded unsigned (`0x4A6AA2`) | `hexfix` `MouseFix=1`: feed the device the real cursor position, and correct the first move after each warp with raw-input motion |
| GUI text boxes tiny at high resolutions, and stale after a resolution change | `pfGUIControlMod::ISetUpDynTextMap` (`0x603B10`) sizes each text box's `plDynamicTextMap` as bounds × screen size and draws a fixed-pixel font into it (text shrinks as resolution grows); the maps are only built when a dialog loads. The engine's resolution-independent path (control flag 4 → 1024×768 virtual size) is unused | `hexfix` `ScaleText=1`: force the virtual-size path for all controls (`jne`→`jmp` at `0x603B51`); reference height configurable |
| Lighting Glow turned the scene white on ultrawide | Side effect of the >180° FOV above (bloom from near geometry stretched across the screen); the bloom render targets themselves are created correctly at 5120×1440 | Fixed by `WidescreenFOV=1` |

## Install
1. Copy the disc contents into this folder (so `plInstaller.exe` and `Cosmic Osmo's Hex Isle.app\` sit next to this README).
2. Optional: build `hexfix` (`hexfix\build.bat`) if you want the frame cap / VSync options.
3. Run `install_hexisle.bat` **as Administrator**:
   * `install_hexisle.bat` — asks for the CD key printed in your game case
   * `install_hexisle.bat <CDKEY>` — same, non-interactive
   * `install_hexisle.bat path\to\fix.reg` — imports a registry fix obtained from Cyan instead

   It copies the game to `C:\Cosmo`, writes the CD-key registry entry, patches `plasma.exe`
   (original kept as `plasma.exe.orig`), installs hexfix if built, and creates a desktop shortcut.

## CD key
On startup `plasma.exe` checks a registry value that the original installer (`plInstaller.exe`) writes from your CD key:

```
HKLM\SOFTWARE\WOW6432Node\Cyan Worlds, Inc.\Installer\{B9E0F969-021D-4037-B3E8-E1E15635F164}
    AntiGrandmaCopyProt  (REG_SZ)
```

If it is missing or wrong the game exits with "Your CD key is missing or invalid, please re-install."
The value is your CD key, obfuscated by the installer (base-36, character-wise shift by a fixed mask).
`tools/set_cdkey.ps1` performs the same encoding from the key you enter. **No CD key is included in this
repository** — use the one printed in your own copy. Alternatives:

* run the disc's `plInstaller.exe` as Administrator and enter your CD key there, or
* import a registry fix Cyan Worlds provides for this error (`install_hexisle.bat path\to\fix.reg`).

Note that the game does not read `InstallPath`, so the entry works regardless of where the game is installed.

## hexfix (d3d9.dll proxy)
Loaded from the game folder in place of the system `d3d9.dll` (the game `LoadLibrary`s `D3D9.DLL`); forwards to
`System32\d3d9.dll`. It hooks `IDirect3D9::CreateDevice` and `IDirect3DDevice9::Reset` (VSync override), and
subclasses the game window for raw mouse input, and redirects the game's single `IDirect3DDevice9::Present` call site (`0x4F4285`) — the D3D9 runtime can swap a
device's vtable at runtime (observed when a level loads), so vtable hooks on `Present` alone are not reliable.
The FOV, GUI-FOV, mode-list and text fixes patch call sites in `plasma.exe` at load time. All patches verify the original bytes first.

Settings in `hexfix.ini`:

| Section | Key | Default | Meaning |
|---|---|---|---|
| `Display` | `FrameRateLimit` | `0` | FPS cap, 0 = off (vanilla) |
| `Display` | `VSync` | `-1` | -1 = game's setting (vanilla), 0 = force off, 1 = force on |
| `Display` | `WidescreenFOV` | `1` | 1 = Hor+ FOV for the 3D camera and GUI dialogs wider than 16:9 (unchanged at ≤16:9), 0 = original formulas |
| `Display` | `KeepWideModes` | `1` | 1 = keep a saved fullscreen mode the startup list omits (if D3D supports it), 0 = original reset to defaults |
| `Display` | `ScaleText` | `1` | 1 = resolution-independent GUI text, 0 = original (text shrinks at high resolutions) |
| `Display` | `TextReferenceHeight` | `768` | virtual layout height for `ScaleText` (width = ×4/3); lower = bigger text |
| `Input` | `MouseFix` | `1` | 1 = mouse camera jump fix (real cursor position + raw-input correction after warps), 0 = original |
| `Debug` | `ShowFPS` | `0` | 1 = FPS counter (green digits, top-left) |
| `Debug` | `Log` | `0` | 1 = write `hexfix.log` next to the game |

Build: `hexfix\build.bat` (MSVC x86 Native Tools prompt) or `hexfix/build.sh` (clang-cl + lld-link, no SDK needed).

## Notes for hacking
* Settings/saves: `%LOCALAPPDATA%\Hexisle\` (`init\graphics.ini`, `init\audio.ini`, `sav\`, `player.hex`).
* `init\*.ini` are encrypted: header `u32 magic 0x0D874288, u32 plaintext size`, then AES-128-ECB.
  Key + magic are stored XOR `0xCF092676` in `plasma.exe` at VA `0x9A1D8C`. `tools/hexini.py d|e <in> <out>`.
* CD-key check lives at `0x44B130`.
* Camera FOV globals: FOVw `0x9A09E4` (70.0), FOVh `0x9A09E8` (52.5); setter `0x5CC720`.
* `Python\python.pak`: same AES layer as the INIs; then `u32 count`, entries `u16 len, obfuscated name, u32 offset`,
  each module = `u32 size` + marshalled Python 2.x code object. Video options live in `xAVSettings.py`
  (windowed list `kWindowedResolutions`, fullscreen list from `PtGetSupportedDisplayModes`).
* Retail `plasma.exe` SHA-256: `517dd45e220e2c1eba702038edaeb35985b93a7fcd1760a7b7d0a700651fc47e`.
