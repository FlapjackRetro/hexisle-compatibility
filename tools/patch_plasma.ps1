# Applies the Hex Isle plasma.exe fixes (same patches as patch_plasma.py). Keeps plasma.exe.orig.
param([Parameter(Mandatory = $true)][string]$Exe)
$ErrorActionPreference = 'Stop'
# offset, original bytes, patched bytes, description
$patches = @(
    @(0x43D2D, [byte[]](0x74, 0x0C), [byte[]](0xEB, 0x0C), 'joystick per-frame re-enumeration (0.25 fps fix)')
)
$b = [IO.File]::ReadAllBytes($Exe)
$changed = $false
foreach ($p in $patches) {
    $off = $p[0]; $old = $p[1]; $new = $p[2]
    $cur = $b[$off..($off + $old.Length - 1)]
    if (-not (Compare-Object $cur $new -SyncWindow 0)) { Write-Host "already applied: $($p[3])"; continue }
    if (Compare-Object $cur $old -SyncWindow 0) { Write-Host "unexpected bytes at 0x$('{0:X}' -f $off); is this the retail plasma.exe?"; exit 1 }
    for ($i = 0; $i -lt $new.Length; $i++) { $b[$off + $i] = $new[$i] }
    Write-Host "applied: $($p[3])"; $changed = $true
}
if ($changed) {
    if (-not (Test-Path "$Exe.orig")) { Copy-Item $Exe "$Exe.orig" }
    [IO.File]::WriteAllBytes($Exe, $b)
}
