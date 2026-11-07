@echo off
rem Build hexfix d3d9.dll with MSVC. Run from an "x86 Native Tools Command Prompt for VS".
cd /d "%~dp0"
cl /nologo /O2 /GS- /Zl /W3 /c hexfix.c || exit /b 1
link /nologo /DLL /MACHINE:X86 /NODEFAULTLIB /ENTRY:DllMain /SAFESEH:NO /DEF:d3d9.def /OUT:d3d9.dll hexfix.obj kernel32.lib || exit /b 1
echo Built d3d9.dll
