# Moves the GameCopy window off-screen without activating it, so test clients don't cover the user's work.
Add-Type -Namespace DDCOff -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr a, int x, int y, int cx, int cy, uint f);
'@
$game = Get-Process FactoryGameSteam-Win64-Shipping -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if ($game) { [DDCOff.Win]::SetWindowPos($game.MainWindowHandle, [IntPtr]::Zero, -4000, 0, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010) | Out-Null; 'moved' } else { 'no window' }
