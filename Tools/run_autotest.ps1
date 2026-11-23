# Runs the dev-only automatic test (-DDCAutoTest) in the shared M: GameCopy on the test save, then cleans up.
# Requires a DEV package (DDC_DEV_COMMANDS=1) installed with Tools\gamecopy_mod.ps1 -Install.
# -SmokeSeconds N: for a RELEASE package (no auto-test). Waits until the mod reports its upload gate, keeps the game
# running N more seconds, then reports the mod's log lines and any crash (result SMOKE-OK / CRASH / TIMEOUT).
#
# Save safety (the game reads and writes the REAL save folder even with -userdir):
#  - Use a test save whose in-save session name is unique (-TestSession), so its autosaves get their own names.
#    A copy of a real save keeps the original session name; patch it in the save header first, or its autosaves will
#    overwrite the original session's.
#  - As a second line of defence, the autosaves of -ProtectSession (the session the test save was copied from) are
#    hashed before and after; if any changed, they are restored from the backup.
#  - New files created by the run are listed and the test session's autosaves are deleted.
# A cold start from a hard disk can take a long time while other builds use the disk, so the default timeout is generous.
param(
    # Example for a copied save: -TestSave MyCopy -TestSession "MyCopy test" -ProtectSession "My Real Session"
    [int]$TimeoutMinutes = 45,
    [string]$TestSave = 'DDCTest',              # save file name without .sav, in the Steam save folder
    [string]$TestSession = 'DDCTest',           # the session name stored inside that save
    [string]$ProtectSession = '',               # optional: real session whose autosaves must stay untouched
    [string]$GameDir = 'M:\Satismod\GameCopy-1.2.5.1',  # test copy of the game (never the real install)
    [string]$UserDir = 'M:/Satismod/GameCopy1251User',
    [int]$SmokeSeconds = 0,
    [string]$BackupRoot = 'M:\Satismod\backups'
)
$ErrorActionPreference = 'Stop'

$Exe = Join-Path $GameDir 'FactoryGameSteam.exe'
$Log = Join-Path ($UserDir -replace '/', '\') 'Saved\Logs\FactoryGame.log'
# The Steam save folder is named after the account id; take the only one present.
$SaveDirs = @(Get-ChildItem (Join-Path $env:LOCALAPPDATA 'FactoryGame\Saved\SaveGames') -Directory | Where-Object Name -match '^\d{17}$')
if ($SaveDirs.Count -ne 1) { throw "Expected exactly one Steam save folder, found $($SaveDirs.Count)" }
$Saves = $SaveDirs[0].FullName
$RealSession = $ProtectSession
$Backup = Join-Path $BackupRoot "ddc-autotest-$(Get-Date -Format yyyyMMdd-HHmmss)"

if (Get-Process FactoryGameSteam*, FactoryGame-* -ErrorAction SilentlyContinue) { throw 'A game instance is already running.' }
if (-not (Test-Path "$Saves\$TestSave.sav")) { throw "Missing test save $Saves\$TestSave.sav" }
if (-not (Test-Path (Join-Path $GameDir 'FactoryGame\Mods\DimensionalDismantleCrate\DimensionalDismantleCrate.uplugin'))) { throw "Mod not installed in $GameDir." }

# 1. Snapshot the save folder and back up the real session's autosaves.
New-Item -ItemType Directory -Force $Backup | Out-Null
$before = Get-ChildItem $Saves -File | Select-Object Name, Length, LastWriteTime
$realAutosaves = if ($RealSession) { @(Get-ChildItem $Saves -File -Filter "$RealSession`_autosave_*.sav") } else { @() }
$hashes = @{}
foreach ($f in $realAutosaves) { Copy-Item $f.FullName $Backup; $hashes[$f.Name] = (Get-FileHash $f.FullName).Hash }
"backed up $($realAutosaves.Count) real autosave(s) to $Backup"

# 2. Launch (listen: sessions without it have ignored input; the test itself needs no input) and move the window away.
$logStart = if (Test-Path $Log) { (Get-Item $Log).LastWriteTime } else { Get-Date 0 }
$url = "open Persistent_Level?loadgame=${TestSave}?listen"
$proc = Start-Process $Exe -ArgumentList @("-userdir=$UserDir", '-windowed', '-ResX=1280', '-ResY=720', '-DDCAutoTest', "-ExecCmds=`"$url`"") -PassThru
"launched pid $($proc.Id): $url"

Add-Type -Namespace DDC -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr a, int x, int y, int cx, int cy, uint f);
'@
# Only processes started from the test copy are ours (the user's own game may run from the real install).
function Get-TestGame { Get-Process FactoryGameSteam-Win64-Shipping -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$GameDir\*" } }
$moved = $false
$smokeUntil = $null
$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
$result = 'TIMEOUT'
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    # Every pass: Unreal's splash window is replaced by the game window, so moving the first handle once isn't enough.
    $game = Get-TestGame | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($game) {
        [DDC.Win]::SetWindowPos($game.MainWindowHandle, [IntPtr]::Zero, -4000, 0, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010) | Out-Null
        if (-not $moved) { 'window moved off-screen' }
        $moved = $true
    }
    if ((Test-Path $Log) -and (Get-Item $Log).LastWriteTime -gt $logStart) {
        $text = Get-Content $Log -Raw -ErrorAction SilentlyContinue
        if ($SmokeSeconds -le 0 -and $text -match '\[DDCTest\] DONE') { $result = 'DONE'; break }
        if ($SmokeSeconds -gt 0 -and -not $smokeUntil -and $text -match 'LogDimensionalDismantleCrate: Crate uploading') { $smokeUntil = (Get-Date).AddSeconds($SmokeSeconds); "upload gate reported; watching $SmokeSeconds s" }
        if ($smokeUntil -and (Get-Date) -ge $smokeUntil) { $result = 'SMOKE-OK'; break }
        if ($text -match 'Critical error|Fatal error|=== Critical') { $result = 'CRASH'; break }
    }
    if (-not (Get-TestGame) -and $moved) { $result = 'EXITED'; break }
}
"result: $result"

# 3. Delete the test session's new autosaves WHILE the game still runs: Steam Auto-Cloud syncs the save folder when the
#    game exits, and a save deleted after that stays in the cloud and is downloaded again later.
Get-ChildItem $Saves -File -Filter "$TestSession*" | Where-Object { $_.Name -notin $before.Name } |
    ForEach-Object { Remove-Item $_.FullName -Force; "deleted test autosave $($_.Name) (game still running)" }
# Then close the game by force (graceful close doesn't work from the shell).
Get-Process FactoryGameSteam*, CrashReportClient*, crashpad_handler -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$GameDir\*" } | Stop-Process -Force
Start-Sleep -Seconds 3

# 4. Results.
Select-String -Path $Log -Pattern '\[DDCTest\]|LogDimensionalDismantleCrate: |LogSatisfactoryModLoader: .*(DimensionalDismantleCrate|Mod Loader|version)|Critical error|Fatal error' | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
Copy-Item $Log "$Backup\FactoryGame.log"

# 5. Save folder: restore changed real autosaves, report and delete new test files.
foreach ($name in $hashes.Keys) {
    $now = Join-Path $Saves $name
    if (-not (Test-Path $now) -or (Get-FileHash $now).Hash -ne $hashes[$name]) { Copy-Item (Join-Path $Backup $name) $now -Force; "RESTORED real autosave $name" }
}
$after = Get-ChildItem $Saves -File
$new = $after | Where-Object { $_.Name -notin $before.Name }
foreach ($f in $new) {
    if ($f.Name -like "$TestSession*") { Remove-Item $f.FullName -Force; "deleted test autosave $($f.Name)" } else { "NEW FILE (left in place, check it): $($f.Name)" }
}
$changed = $after | Where-Object { $b = $before | Where-Object Name -eq $_.Name; $b -and ($b.LastWriteTime -ne $_.LastWriteTime -or $b.Length -ne $_.Length) }
foreach ($f in $changed) { "CHANGED existing save: $($f.Name)" }
# Steam Cloud must not list test autosaves (they would come back). Steam updates this file shortly after the game exits.
Start-Sleep -Seconds 30
foreach ($vdf in Get-ChildItem "${env:ProgramFiles(x86)}\Steam\userdata\*\526870\remotecache.vdf" -ErrorAction SilentlyContinue) {
    $listed = Select-String -Path $vdf.FullName -Pattern ([regex]::Escape($TestSession) + '[^"]*_autosave_') | ForEach-Object { $_.Line.Trim() }
    if ($listed) { "STEAM CLOUD still lists test autosaves (delete them in Steam's cloud storage page): $listed" } else { 'steam cloud: no test autosaves listed' }
}
"log copy: $Backup\FactoryGame.log"
