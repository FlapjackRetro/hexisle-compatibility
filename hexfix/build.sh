#!/bin/sh
# Builds hexfix d3d9.dll (x86) with clang-cl + lld-link, no SDK/CRT needed.
set -e
cd "$(dirname "$0")"
llvm-dlltool -m i386 -d kernel32.def -l kernel32.lib -k
llvm-dlltool -m i386 -d user32.def -l user32.lib -k
clang-cl --target=i686-pc-windows-msvc /c /O2 /GS- /Zl /W3 -fno-builtin -mno-sse hexfix.c /Fohexfix.obj
lld-link /dll /machine:x86 /nodefaultlib /entry:DllMain@12 /def:d3d9.def /out:d3d9.dll hexfix.obj kernel32.lib user32.lib /safeseh:no
