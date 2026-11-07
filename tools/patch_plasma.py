# Hex Isle plasma.exe patcher (Windows 10/11 fixes)
# usage: python patch_plasma.py <path\to\plasma.exe>   (writes plasma.exe.bak first)
import sys, hashlib, shutil
ORIG_SHA256 = "517dd45e220e2c1eba702038edaeb35985b93a7fcd1760a7b7d0a700651fc47e"
PATCHES = [
    # 0x443D2D: main loop calls 0x443D20 every frame; when no joystick is open it re-runs
    # DirectInput8Create + EnumDevices(GAMECTRL) each frame -> seconds per frame on modern HID-heavy PCs.
    # je -> jmp: skip the per-frame re-enumeration (startup enumeration at 0x440B7D is kept).
    ("joystick per-frame re-enum", 0x43D2D, bytes([0x74, 0x0C]), bytes([0xEB, 0x0C])),
]
p = sys.argv[1]
d = bytearray(open(p, "rb").read())
if hashlib.sha256(d).hexdigest() != ORIG_SHA256:
    print("warning: plasma.exe is not the known retail build (or already patched); checking bytes anyway")
for name, off, old, new in PATCHES:
    cur = bytes(d[off:off+len(old)])
    if cur == new: print("already applied:", name); continue
    if cur != old: sys.exit(f"unexpected bytes at {off:#x} for {name}: {cur.hex()}")
    d[off:off+len(old)] = new; print("applied:", name)
shutil.copyfile(p, p + ".bak")
open(p, "wb").write(d)
