# Writes the Hex Isle CD-key registry entry the same way plInstaller.exe does.
# Usage: set_cdkey.ps1 [-Key <CD key from your case>] [-RegFile <registry fix from Cyan>] [-InstallPath C:\Cosmo]
# Requires Administrator (HKLM). The key itself is never stored in this repository.
param([string]$Key, [string]$RegFile, [string]$InstallPath = 'C:\Cosmo')
$ErrorActionPreference = 'Stop'

if ($RegFile) {
    & reg.exe import $RegFile
    if ($LASTEXITCODE -ne 0) { Write-Host 'Registry import failed.'; exit 1 }
    exit 0
}

if (-not $Key) { $Key = Read-Host 'Enter the CD key printed in your game case' }
$k = ($Key.ToUpperInvariant() -replace '[^A-Z0-9]', '')
if ($k -notmatch '^[A-Z0-9]{9}$') { Write-Host 'The CD key should be 9 letters/digits.'; exit 1 }

# plInstaller obfuscation: base-36 (A-Z = 0-25, 0-9 = 26-35), add the mask character-wise, mod 36.
function ToVal([char]$c) { if ($c -ge [char]'A' -and $c -le [char]'Z') { [int]$c - 65 } else { [int]$c - 22 } }
function ToChr([int]$n)  { if ($n -le 25) { [char]($n + 65) } else { [char]($n + 22) } }
$mask = 'EASTEREGG'
$enc = ''
for ($i = 0; $i -lt $k.Length; $i++) {
    $enc += ToChr (((ToVal $k[$i]) + (ToVal $mask[$i % $mask.Length])) % 36)
}

# plasma.exe is 32-bit, so write to the 32-bit registry view (WOW6432Node on 64-bit Windows).
$sub  = 'SOFTWARE\Cyan Worlds, Inc.\Installer\{B9E0F969-021D-4037-B3E8-E1E15635F164}'
$base = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', 'Registry32')
$rk   = $base.CreateSubKey($sub)
$rk.SetValue('AntiGrandmaCopyProt', $enc, 'String')
$rk.SetValue('InstallPath', $InstallPath, 'String')
$rk.SetValue('Installed', 1, 'DWord')
$rk.SetValue('InstalledProductVer', 1, 'DWord')
$rk.SetValue('InstallerVersion', 2, 'DWord')
$rk.Close(); $base.Close()
Write-Host 'CD key registry entry written. (The game itself validates the key at startup.)'
