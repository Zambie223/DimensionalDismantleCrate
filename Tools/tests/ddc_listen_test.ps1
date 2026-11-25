# Listen-host test: the test copy hosts the test save (?listen) and a second instance of the same copy joins it as a
# player, so this mod runs on a host that is also a player while someone else is connected. Both instances have the same
# mods (one install), so the joined player has this mod too.
#   default: DEV package. The host runs the auto-test (-DDCAutoTest) once both players are in the game
#            (-DDCAutoTestPlayers=2); the client logs what it sees and its chat (-DDCClientWatch).
#   -Smoke:  RELEASE package (no dev commands). The host runs SmokeSeconds after its upload gate opens; the client joins
#            meanwhile. Checks that the client got in and that neither side logs a crash or an error from the mod.
# Uses Tools\run_autotest.ps1 for the host (save-folder safety, cleanup; it also closes the client at the end).
param(
    [switch]$Smoke,
    [int]$SmokeSeconds = 240,
    [string]$TestSave = 'DDCTest',
    [string]$TestSession = 'DDCTest',
    [string]$ProtectSession = '',
    [string]$GameDir = 'M:\Satismod\GameCopy-1.2.5.1',
    [string]$HostUserDir = 'M:/Satismod/GameCopy1251User',
    [string]$ClientUserDir = 'M:/Satismod/DDCClientUser',
    [int]$TimeoutMinutes = 45
)
$ErrorActionPreference = 'Stop'
$Here = $PSScriptRoot
$Tools = Split-Path $Here -Parent
$HostLog = Join-Path ($HostUserDir -replace '/', '\') 'Saved\Logs\FactoryGame.log'
$ClientDir = $ClientUserDir -replace '/', '\'
$ClientLog = Join-Path $ClientDir 'Saved\Logs\FactoryGame.log'
$Exe = Join-Path $GameDir 'FactoryGameSteam.exe'

# A fresh client user folder gets the host's settings (no first-run prompts in the way).
if (-not (Test-Path "$ClientDir\Saved\Config")) {
    New-Item -ItemType Directory -Force "$ClientDir\Saved" | Out-Null
    Copy-Item -Recurse (Join-Path ($HostUserDir -replace '/', '\') 'Saved\Config') "$ClientDir\Saved\Config"
    "client settings copied from the host's user folder"
}

# 1. The host, in its own PowerShell (it waits for the result, then closes every instance of the test copy).
$hostOut = Join-Path $env:TEMP "ddc_listen_host_$(Get-Date -Format yyyyMMdd-HHmmss).txt"
$hostArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$Tools\run_autotest.ps1", '-TestSave', $TestSave, '-TestSession', "`"$TestSession`"",
    '-GameDir', $GameDir, '-UserDir', $HostUserDir, '-TimeoutMinutes', $TimeoutMinutes)
if ($ProtectSession) { $hostArgs += @('-ProtectSession', "`"$ProtectSession`"") }
if ($Smoke) { $hostArgs += @('-SmokeSeconds', $SmokeSeconds) } else { $hostArgs += @('-WaitForPlayers', 2) }
$hostStart = Get-Date
$hostProc = Start-Process powershell.exe -ArgumentList $hostArgs -RedirectStandardOutput $hostOut -NoNewWindow -PassThru
"host driver pid $($hostProc.Id), output $hostOut"

# 2. Once the host's world is up, start the client and point it at the host.
$client = $null
$deadline = (Get-Date).AddMinutes(15)
while (-not $client -and (Get-Date) -lt $deadline -and -not $hostProc.HasExited) {
    Start-Sleep -Seconds 5
    if ((Test-Path $HostLog) -and (Get-Item $HostLog).LastWriteTime -gt $hostStart -and
        (Select-String -Path $HostLog -Pattern 'LogDimensionalDismantleCrate: Init on listen server' -Quiet)) {
        $client = Start-Process $Exe -ArgumentList @("-userdir=$ClientUserDir", '-windowed', '-ResX=1280', '-ResY=720', '-DDCClientWatch', '-ExecCmds="open 127.0.0.1:7777"') -PassThru
        "host world loaded; client launcher pid $($client.Id) at $(Get-Date -Format HH:mm:ss)"
    }
}
if (-not $client) { "!! the host world never came up; no client started" }

# 3. Keep both windows off-screen until the host driver finishes (it closes the client too).
while (-not $hostProc.HasExited) {
    & "$Here\move_offscreen.ps1" -GameDir $GameDir | Out-Null
    Start-Sleep -Seconds 5
}
Get-Process FactoryGameSteam*, CrashReportClient*, crashpad_handler -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$GameDir\*" } | Stop-Process -Force

# 4. Results.
'== host'
Get-Content $hostOut
'== host log: joins'
Select-String -Path $HostLog -Pattern 'Join succeeded|Login request|\[DDCTest\] .*player' | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' } | Select-Object -First 6
'== client log'
if (Test-Path $ClientLog) {
    Select-String -Path $ClientLog -Pattern 'Welcomed by server|NMT_Failure|PendingConnectionFailure|DimensionalDismantleCrate.*(Warning|Error)|Critical error|Fatal error' |
        ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' } | Select-Object -First 10
    '-- client view (last lines)'
    Select-String -Path $ClientLog -Pattern '\[DDCClient\]' | Where-Object { $_.Line -notmatch '\] chat' } | Select-Object -Last 6 | ForEach-Object { $l = $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', ''; $l.Substring(0, [Math]::Min(300, $l.Length)) }
    '-- chat the client received'
    Select-String -Path $ClientLog -Pattern '\[DDCClient\] chat' | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
} else { '!! no client log' }
