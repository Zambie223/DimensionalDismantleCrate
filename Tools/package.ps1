# Packages the mod with UAT PackagePlugin (Shipping). Does not install it anywhere; see Tools/gamecopy_install.ps1.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File Tools\package.ps1 [-Servers] [-LogPath <file>]
#   default:  Win64 client only (dev testing)
#   -Servers: also Win64 + Linux dedicated servers (release)
param([switch]$Servers, [string]$LogPath = '')
$ErrorActionPreference = 'Stop'
$Project = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\FactoryGame.uproject')).Path
$Engine = 'M:\Program Files\Unreal Engine - CSS'
# Keep linker temp paths (recorded in PDBs) off the user profile.
$env:TEMP = 'M:\Satismod\BuildTemp'; $env:TMP = 'M:\Satismod\BuildTemp'
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
# -ScriptsForProject loads the project's automation modules; PackagePlugin comes from Alpakit's.
$uatArgs = @("-ScriptsForProject=$Project", 'PackagePlugin', "-Project=$Project", '-DLCName=DimensionalDismantleCrate', '-build', '-platform=Win64',
    '-clientconfig=Shipping', '-nocompileeditor', '-installed', '-utf8output', '-WaitForUATMutex')
if ($Servers) {
    # Machine-level variable that shells started earlier don't inherit; needed for the Linux server.
    $env:LINUX_MULTIARCH_ROOT = [Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT', 'Machine')
    $uatArgs += @('-server', '-serverplatform=Win64+Linux', '-serverconfig=Shipping')
}
if ($LogPath) {
    & "$Engine\Engine\Build\BatchFiles\RunUAT.bat" @uatArgs *> $LogPath
} else {
    & "$Engine\Engine\Build\BatchFiles\RunUAT.bat" @uatArgs
}
exit $LASTEXITCODE
