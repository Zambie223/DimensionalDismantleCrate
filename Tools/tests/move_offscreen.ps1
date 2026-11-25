# Moves the test GameCopy window off-screen without activating it, so test clients don't cover the user's work.
# Only processes started from -GameDir are touched (the user's own game may be running from the real install).
# Call it repeatedly: Unreal's splash window is replaced by the real game window, which opens on-screen again.
param([string]$GameDir = 'M:\Satismod\GameCopy-1.2.5.1')
Add-Type -Namespace DDCOff -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr a, int x, int y, int cx, int cy, uint f);
'@
$prefix = $GameDir.TrimEnd('\', '/')
# Every window: a listen-host test runs two instances (host and client).
$games = @(Get-Process FactoryGameSteam-Win64-Shipping -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like "$prefix*" -and $_.MainWindowHandle -ne 0 })
foreach ($game in $games) { [DDCOff.Win]::SetWindowPos($game.MainWindowHandle, [IntPtr]::Zero, -4000, 0, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010) | Out-Null }
if ($games) { "moved $($games.Count)" } else { 'no window' }
