# Installs or removes this mod in one of the local test installs (a game copy and two dedicated servers). Installs
# that are shared with other mods' tests should only carry this mod while it is being tested.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File Tools\gamecopy_mod.ps1 -Install | -Remove [-Where GameCopy|WindowsServer|LinuxServer] [-Zip <path>]
# -Zip installs that zip instead of the last package (e.g. a published release zip from Saved\Released).
# The targets are the game 1.2.5.1 (CL 520140) copies; the 1.2.5.0 ones (GameCopy, DedicatedServer, DedicatedServerLinux) are retired.
param([switch]$Install, [switch]$Remove, [ValidateSet('GameCopy', 'WindowsServer', 'LinuxServer')][string]$Where = 'GameCopy', [string]$Zip = '')
$ErrorActionPreference = 'Stop'
$ModName = 'DimensionalDismantleCrate'
$Layout = @{
    GameCopy      = @{ Root = 'M:\Satismod\GameCopy-1.2.5.1\FactoryGame'; Zip = 'Windows'; Proc = 'FactoryGameSteam*', 'FactoryGame-*' }
    WindowsServer = @{ Root = 'M:\Satismod\PatchCheck\DS-new\FactoryGame'; Zip = 'WindowsServer'; Proc = 'FactoryServer*' }
    LinuxServer   = @{ Root = 'M:\Satismod\PatchCheck\DS-new-linux\FactoryGame'; Zip = 'LinuxServer'; Proc = @() }
}[$Where]
$Zip = if ($Zip) { Resolve-Path $Zip } else { Resolve-Path (Join-Path $PSScriptRoot "..\..\..\Saved\ArchivedPlugins\$ModName\$ModName-$($Layout.Zip).zip") -ErrorAction SilentlyContinue }
$Target = Join-Path $Layout.Root "Mods\$ModName"

$InstallDir = Split-Path $Layout.Root -Parent
if ($Layout.Proc.Count -gt 0 -and (Get-Process $Layout.Proc -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$InstallDir\*" })) { throw "$Where is running; close it first." }
if ($Where -eq 'LinuxServer') {
    $running = & wsl.exe -d EasyDDLinux -- pgrep -f FactoryServer-Linux 2>$null
    if ($running) { throw 'The Linux server is running; stop it first.' }
}
if ($Install -eq $Remove) { throw 'Pass exactly one of -Install or -Remove.' }

if ($Remove) {
    if (Test-Path $Target) { Remove-Item -Recurse -Force $Target; "removed $Target" } else { "not installed in $Where" }
    # SML writes the mod's settings on first load; remove them too so the install is left as it was.
    $Cfg = Join-Path $Layout.Root "Configs\$ModName.cfg"
    if (Test-Path $Cfg) { Remove-Item -Force $Cfg; "removed $Cfg" }
    return
}
if (-not $Zip) { throw "No $($Layout.Zip) zip; run Tools\package.ps1$(if ($Where -ne 'GameCopy') { ' -Servers' }) first." }
if (Test-Path $Target) { Remove-Item -Recurse -Force $Target }
New-Item -ItemType Directory -Force $Target | Out-Null
Expand-Archive -Path $Zip -DestinationPath $Target
# Alpakit zips hold the plugin's files at the root; check the descriptor landed where SML looks for it.
if (-not (Test-Path "$Target\$ModName.uplugin")) { throw "Unexpected zip layout: $Target\$ModName.uplugin missing" }
"installed into $Target"
