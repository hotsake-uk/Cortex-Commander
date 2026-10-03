# Real keyboard and mouse input for playtesting, via SendInput (the game sees it like hardware input). Dot-source this.
if (-not ("PlayInput" -as [type])) {
	Add-Type @"
using System; using System.Runtime.InteropServices;
public static class PlayInput {
	[StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr extra; }
	[StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr extra; }
	[StructLayout(LayoutKind.Explicit)] struct INPUTUNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
	[StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public INPUTUNION u; }
	[DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] inputs, int size);
	[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
	[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
	public struct RECT { public int L, T, R, B; }
	public static void Key(ushort scan, bool up) {
		var i = new INPUT[1]; i[0].type = 1; i[0].u.ki.wScan = scan; i[0].u.ki.dwFlags = 0x0008u | (up ? 0x0002u : 0u);
		SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
	}
	public static void Mouse(uint flags, int dx, int dy) {
		var i = new INPUT[1]; i[0].type = 0; i[0].u.mi.dwFlags = flags; i[0].u.mi.dx = dx; i[0].u.mi.dy = dy;
		SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
	}
}
"@
}

# Set 1 scancodes.
$script:Scan = @{ Esc = 0x01; One = 0x02; Two = 0x03; Q = 0x10; W = 0x11; E = 0x12; R = 0x13; A = 0x1E; S = 0x1F; D = 0x20; F = 0x21; G = 0x22; LCtrl = 0x1D; LShift = 0x2A; Space = 0x39; Enter = 0x1C; Tab = 0x0F; C = 0x2E; Z = 0x2C; F1 = 0x3B; F2 = 0x3C; F3 = 0x3D; F4 = 0x3E; F5 = 0x3F; F6 = 0x40; F7 = 0x41; F8 = 0x42; F9 = 0x43; F10 = 0x44; F11 = 0x57; F12 = 0x58 }

function Focus-Game([System.Diagnostics.Process]$Process) { [PlayInput]::SetForegroundWindow($Process.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 300 }
function Key-Down([string]$Name) { [PlayInput]::Key($Scan[$Name], $false) }
function Key-Up([string]$Name) { [PlayInput]::Key($Scan[$Name], $true) }
function Hold-Key([string]$Name, [int]$Ms) { Key-Down $Name; Start-Sleep -Milliseconds $Ms; Key-Up $Name }
function Tap-Key([string]$Name) { Hold-Key $Name 80 }
function Move-Mouse([int]$Dx, [int]$Dy) { [PlayInput]::Mouse(0x0001, $Dx, $Dy) }
function Mouse-Down([string]$Button = "Left") { [PlayInput]::Mouse($(if ($Button -eq "Left") { 0x0002 } else { 0x0008 }), 0, 0) }
function Mouse-Up([string]$Button = "Left") { [PlayInput]::Mouse($(if ($Button -eq "Left") { 0x0004 } else { 0x0010 }), 0, 0) }
function Hold-Mouse([string]$Button, [int]$Ms) { Mouse-Down $Button; Start-Sleep -Milliseconds $Ms; Mouse-Up $Button }
# Moves the cursor to a point in the game window (window coordinates, including the title bar), for menus.
function Point-At([System.Diagnostics.Process]$Process, [int]$X, [int]$Y) {
	$r = New-Object PlayInput+RECT; [PlayInput]::GetWindowRect($Process.MainWindowHandle, [ref]$r) | Out-Null
	for ($i = 4; $i -ge 0; $i--) { [PlayInput]::SetCursorPos($r.L + $X + $i * 5, $r.T + $Y + $i * 3) | Out-Null; Start-Sleep -Milliseconds 50 }
}
