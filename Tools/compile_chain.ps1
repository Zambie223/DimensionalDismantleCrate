# One COMPILE slot: editor build -> config asset script -> asset verify -> PackagePlugin (client, or client + servers).
# Pre-reads the shared PCHs before each compile step (cold reads from the M: disk stall cl.exe for many minutes).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File Tools\compile_chain.ps1 [-Servers] [-SkipEditor]
# Logs: Saved\Logs\compile_chain.log (summary) plus build_editor.log / package.log.
# -SelfTest: writes the start line, then throws on purpose to prove the trap logs FATAL; compiles nothing.
param([switch]$Servers, [switch]$SkipEditor, [switch]$SelfTest)
$ErrorActionPreference = 'Stop'
$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$Tools = $PSScriptRoot
$LogDir = Join-Path $ProjectDir 'Saved\Logs'
$Summary = Join-Path $LogDir 'compile_chain.log'
$Editor = 'M:\Program Files\Unreal Engine - CSS\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
# The summary log is only ever APPENDED to (never truncated or recreated) with FileShare.ReadWrite and retries:
# a reader holding the file (e.g. `tail -F`) makes Add-Content and truncating writes fail, and with
# ErrorActionPreference Stop that would end the chain. Note() is defined before any write; the trap doesn't depend on it.
function Note($text) {
    $line = "$(Get-Date -Format 'HH:mm:ss') $text"
    for ($i = 0; $i -lt 20; $i++) {
        try {
            $fs = [IO.File]::Open($Summary, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
            $sw = New-Object IO.StreamWriter($fs); $sw.WriteLine($line); $sw.Close()
            break
        } catch { Start-Sleep -Milliseconds 250 }
    }
    # Emits nothing on purpose: PowerShell functions return ALL pipeline output, so a Note() that wrote to the pipeline
    # inside Warm-Pch would make Run-Build return @("<log line>", 0) and report a successful build as failed.
    [Console]::Out.WriteLine($line)
}
trap {
    $msg = "$(Get-Date -Format 'HH:mm:ss') FATAL: $($_.Exception.Message) at line $($_.InvocationInfo.ScriptLineNumber)"
    try { $fs = [IO.File]::Open($Summary, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite); $sw = New-Object IO.StreamWriter($fs); $sw.WriteLine($msg); $sw.Close() } catch { }
    [Console]::Error.WriteLine($msg)
    exit 1
}
New-Item -ItemType Directory -Force $LogDir | Out-Null
Note "===== compile chain started $(Get-Date -Format s) (args: $($PSBoundParameters.Keys -join ' ')) =====" | Out-Null
if ($SelfTest) {
    Note 'self-test: start line written'
    # A function that logs through Note() and then returns a number must return exactly that number (no leaked output).
    function Test-Return { Note 'self-test: logging inside a function'; return [int]7 }
    $r = @(Test-Return)
    if ($r.Count -ne 1 -or $r[0] -isnot [int] -or $r[0] -ne 7) { throw "self-test: function return leaked output: $($r -join ' | ')" }
    Note 'self-test: function return is a scalar int'
    throw 'self-test: deliberate failure to exercise the trap'
}

function Warm-Pch([string]$Pattern) {
    $files = Get-ChildItem (Join-Path $ProjectDir 'Intermediate\Build') -Recurse -Filter 'SharedPCH*.pch' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match $Pattern }
    # Shared open, and a busy file is skipped: a PCH that cl.exe is still writing (first build of a target) can't be
    # opened with FileShare.Read and needs no warming anyway.
    $read = 0; $skipped = 0
    foreach ($f in $files) {
        try {
            $s = [IO.File]::Open($f.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
            try { $buf = New-Object byte[] (8MB); while ($s.Read($buf, 0, $buf.Length) -gt 0) {} } finally { $s.Close() }
            $read++
        } catch { $skipped++ }
    }
    Note "pre-read $read PCH file(s) matching '$Pattern'$(if ($skipped) { ", skipped $skipped busy" })"
}

# Runs a build script in a child process; re-reads the PCHs as soon as cl.exe appears (a game load or other
# activity between the warm-up and the compile can evict them), then waits for it.
function Run-Build([string]$Script, [string[]]$ScriptArgs, [string]$Log, [string]$PchPattern, [string]$Title) {
    Set-Content $Log ''
    Start-Process powershell -WindowStyle Hidden -RedirectStandardError "$Log.progress.err" -RedirectStandardOutput "$Log.progress.out" -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'M:\Satismod\tools\build-progress.ps1', '-LogPath', $Log, '-Title', "`"$Title`"" | Out-Null
    $p = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Script) + $ScriptArgs + @('-LogPath', $Log))
    $t0 = Get-Date
    while (-not $p.HasExited -and -not (Get-Process cl -ErrorAction SilentlyContinue) -and ((Get-Date) - $t0).TotalMinutes -lt 15) { Start-Sleep 5 }
    if (Get-Process cl -ErrorAction SilentlyContinue) { Warm-Pch $PchPattern }
    $p.WaitForExit()
    return [int]$p.ExitCode
}

if (-not $SkipEditor) {
    Warm-Pch 'UnrealEditor\\Development|FactoryEditor'
    $code = Run-Build "$Tools\build_editor.ps1" @() (Join-Path $LogDir 'build_editor.log') 'UnrealEditor\\Development|FactoryEditor' 'DDC editor build'
    Note "editor build exit $code"
    if ($code -ne 0) { Note 'STOP: editor build failed'; exit 1 }

    foreach ($script in 'create_config_asset.py', 'verify_config_asset.py') {
        & $Editor (Join-Path $ProjectDir 'FactoryGame.uproject') -run=pythonscript "-script=$Tools\$script" -unattended -nullrhi -nosplash -nop4 *> $null
        $lines = Select-String -Path (Join-Path $LogDir 'FactoryGame.log') -Pattern '\[DDC\]|LogPython: Error|Traceback' | ForEach-Object { $_.Line -replace '^.*LogPython: ', '' }
        Note "$script exit $LASTEXITCODE"; $lines | ForEach-Object { Note "  $_" }
        if ($lines -match 'Error|Traceback') { Note "STOP: $script reported an error"; exit 1 }
    }
}

Warm-Pch 'Shipping'
$pkgArgs = @(); if ($Servers) { $pkgArgs += '-Servers' }
$code = Run-Build "$Tools\package.ps1" $pkgArgs (Join-Path $LogDir 'package.log') 'Shipping' "DDC package$(if ($Servers) { ' (client + servers)' })"
Note "package exit $code"
Select-String -Path (Join-Path $LogDir 'package.log') -Pattern 'DimensionalDismantleCrate.*(error|warning C)|DDC\w*\.(h|cpp).*(error|warning)|BUILD SUCCESSFUL|BUILD FAILED' | Select-Object -Last 12 | ForEach-Object { Note "  $($_.Line.Trim())" }
Get-ChildItem (Join-Path $ProjectDir 'Saved\ArchivedPlugins\DimensionalDismantleCrate') -File -ErrorAction SilentlyContinue | ForEach-Object { Note ("  zip {0} {1:N1} MB {2:t}" -f $_.Name, ($_.Length / 1MB), $_.LastWriteTime) }
exit $code
