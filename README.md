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
Loaded from the game folder in place of the system `d3d9.dll`; forwards to `System32\d3d9.dll` and hooks
`IDirect3D9::CreateDevice`, `IDirect3DDevice9::Reset` and `::Present`. Settings in `hexfix.ini`:

| Key | Default | Meaning |
|---|---|---|
| `FrameRateLimit` | `0` | FPS cap, 0 = off (vanilla) |
| `VSync` | `-1` | -1 = game's setting (vanilla), 0 = force off, 1 = force on |

Build: `hexfix\build.bat` (MSVC x86 Native Tools prompt) or `hexfix/build.sh` (clang-cl + lld-link, no SDK needed).

## Notes for hacking
* Settings/saves: `%LOCALAPPDATA%\Hexisle\` (`init\graphics.ini`, `init\audio.ini`, `sav\`, `player.hex`).
* `init\*.ini` are encrypted: header `u32 magic 0x0D874288, u32 plaintext size`, then AES-128-ECB.
  Key + magic are stored XOR `0xCF092676` in `plasma.exe` at VA `0x9A1D8C`. `tools/hexini.py d|e <in> <out>`.
* CD-key check lives at `0x44B130`.
* Retail `plasma.exe` SHA-256: `517dd45e220e2c1eba702038edaeb35985b93a7fcd1760a7b7d0a700651fc47e`.
