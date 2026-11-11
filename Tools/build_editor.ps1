# Builds the FactoryEditor target for this project (needed before the headless asset scripts and before packaging).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File Tools\build_editor.ps1 [-LogPath <file>]
param([string]$LogPath = '')
$ErrorActionPreference = 'Stop'
$Project = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\FactoryGame.uproject')).Path
$Engine = 'M:\Program Files\Unreal Engine - CSS'
# Keep linker temp paths (recorded in PDBs) off the user profile.
$env:TEMP = 'M:\Satismod\BuildTemp'; $env:TMP = 'M:\Satismod\BuildTemp'
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
$args = @('FactoryEditor', 'Win64', 'Development', "-Project=$Project", '-WaitMutex', '-NoHotReload')
if ($LogPath) {
    & "$Engine\Engine\Build\BatchFiles\Build.bat" @args *> $LogPath
} else {
    & "$Engine\Engine\Build\BatchFiles\Build.bat" @args
}
exit $LASTEXITCODE
