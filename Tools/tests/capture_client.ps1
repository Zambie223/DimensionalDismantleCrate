# Captures the test client's window to a PNG without moving or focusing it (PrintWindow with PW_RENDERFULLCONTENT,
# which also reads the DirectX content of an off-screen window on Windows 10). Used to check that the mod's chat
# notices show up on a joined player's screen, including a player without the mod.
# Only processes started from -GameDir are touched (the user's own game may be running from the real install).
param(
    [Parameter(Mandatory)][string]$Out,
    [string]$GameDir = 'M:\Satismod\GameCopy-1.2.5.1'
)
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace DDCCap -Name Win -MemberDefinition @'
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int L, T, R, B; }
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr hdc, uint flags);
'@
$prefix = $GameDir.TrimEnd('\', '/')
$game = Get-Process FactoryGameSteam-Win64-Shipping -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like "$prefix*" -and $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $game) { 'capture: client window not found'; exit 1 }
$h = $game.MainWindowHandle
$r = New-Object DDCCap.Win+RECT
[void][DDCCap.Win]::GetWindowRect($h, [ref]$r)
$w = $r.R - $r.L; $ht = $r.B - $r.T
if ($w -le 0 -or $ht -le 0) { "capture: window has no size (${w}x${ht})"; exit 1 }
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [DDCCap.Win]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc); $g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$nonBlack = 0; $samples = 0
for ($y = 0; $y -lt $ht; $y += 8) {
    for ($x = 0; $x -lt $w; $x += 8) {
        $c = $bmp.GetPixel($x, $y); $samples++
        if (($c.R + $c.G + $c.B) -gt 30) { $nonBlack++ }
    }
}
$bmp.Dispose()
"capture ${w}x${ht} (PrintWindow $ok): $nonBlack/$samples sampled pixels not black -> $Out"
