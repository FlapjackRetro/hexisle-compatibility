# Applies the Hex Isle plasma.exe fixes (same patches as patch_plasma.py). Keeps plasma.exe.orig.
param([Parameter(Mandatory = $true)][string]$Exe)
$ErrorActionPreference = 'Stop'

$patches = @(
    [pscustomobject]@{ Offset = 0x43D2D; Old = [byte[]](0x74, 0x0C); New = [byte[]](0xEB, 0x0C); Name = 'joystick per-frame re-enumeration (0.25 fps fix)' }
)

function Test-Bytes([byte[]]$buf, [int]$off, [byte[]]$expect) {
    for ($i = 0; $i -lt $expect.Length; $i++) { if ($buf[$off + $i] -ne $expect[$i]) { return $false } }
    return $true
}

$b = [IO.File]::ReadAllBytes($Exe)
$changed = $false
foreach ($p in $patches) {
    if (Test-Bytes $b $p.Offset $p.New) { Write-Host "already applied: $($p.Name)"; continue }
    if (-not (Test-Bytes $b $p.Offset $p.Old)) {
        Write-Host ("unexpected bytes at 0x{0:X}; is this the retail plasma.exe?" -f $p.Offset); exit 1
    }
    for ($i = 0; $i -lt $p.New.Length; $i++) { $b[$p.Offset + $i] = $p.New[$i] }
    Write-Host "applied: $($p.Name)"; $changed = $true
}
if ($changed) {
    if (-not (Test-Path "$Exe.orig")) { Copy-Item $Exe "$Exe.orig" }
    [IO.File]::WriteAllBytes($Exe, $b)
}
exit 0
