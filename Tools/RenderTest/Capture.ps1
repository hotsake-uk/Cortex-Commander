param(
	[string]$Scenario = "Bunker", # Name of a Userdata\RenderTest\<Scenario>.ini written by Setup.ps1.
	[string]$Name = "",           # Output name, defaults to the scenario.
	[string]$Repo = "",           # Game folder, defaults to this repository. Point at another checkout to capture a baseline.
	[int]$ExtraWait = 25,         # Seconds to wait after loading settles, for the scene and effects to get going.
	[int]$Burst = 1,              # Number of frames to capture.
	[int]$BurstIntervalMs = 250,
	[string]$CameraPOI = "",      # Pins the Camera Tour script to one point of interest (1-6) for comparable shots.
	[string]$Exe = "Cortex Command.debug.release.exe",
	[switch]$KeepRunning
)
# Launches the game with a scenario's settings, waits for it to load, and captures the window (PrintWindow, so it works even when covered).
# Assert dialogs are captured instead of the game window, so failures show up in the output.
. (Join-Path $PSScriptRoot "Common.ps1")
if (-not $Repo) { $Repo = $RepoRoot }
if (-not $Name) { $Name = $Scenario }
Add-Type -AssemblyName System.Drawing
if (-not ("RenderTestWin" -as [type])) {
	Add-Type @"
using System; using System.Runtime.InteropServices;
public class RenderTestWin {
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
	[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
	public struct RECT { public int L, T, R, B; }
	public delegate bool EnumProc(IntPtr h, IntPtr l);
	[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
	[DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
	[DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
	[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
	public static IntPtr FindTitled(int target, string needle) {
		IntPtr found = IntPtr.Zero;
		EnumWindows((h, l) => { int pid; GetWindowThreadProcessId(h, out pid); if (pid == target && IsWindowVisible(h)) { var sb = new System.Text.StringBuilder(512); GetWindowText(h, sb, 512); if (sb.ToString().Contains(needle)) { found = h; return false; } } return true; }, IntPtr.Zero);
		return found;
	}
}
"@
}

$procName = [IO.Path]::GetFileNameWithoutExtension($Exe)
$p = Get-Process $procName -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Repo*" } | Select-Object -First 1
if (-not $p -and -not $PSBoundParameters.ContainsKey("Scenario")) {
	# Capturing a running game that has gone away (e.g. it quit): report that rather than silently launching a different scenario.
	"NOT RUNNING: no game process to capture, and no -Scenario given to launch."
	exit 1
}
if (-not $p) {
	if ($CameraPOI) { $env:CCCP_CAMERA_POI = $CameraPOI } else { Remove-Item Env:CCCP_CAMERA_POI -ErrorAction SilentlyContinue }
	$env:CCCP_SETTINGSPATH = "Userdata/RenderTest/$Scenario.ini"
	$p = Start-Process -FilePath (Join-Path $Repo $Exe) -WorkingDirectory $Repo -PassThru
	Start-Sleep 5
	# Wait for LogLoading.txt to stop changing (it's buffered, so ExtraWait covers the rest of loading).
	$last = 0; $stable = 0
	for ($i = 0; $i -lt 240; $i++) {
		Start-Sleep 1
		if ($p.HasExited) { break }
		$t = (Get-Item (Join-Path $Repo "LogLoading.txt") -ErrorAction SilentlyContinue).LastWriteTime.Ticks
		if ($t -eq $last) { $stable++ } else { $stable = 0; $last = $t }
		if ($stable -ge 6) { break }
	}
	Start-Sleep $ExtraWait
}
$p.Refresh()
if ($p.HasExited) { "EXITED code=$($p.ExitCode)"; Get-Content (Join-Path $Repo "LogLoading.txt") -Tail 5; exit 1 }
$h = $p.MainWindowHandle
$assert = [RenderTestWin]::FindTitled($p.Id, "Assert")
if ($assert -eq [IntPtr]::Zero) { $assert = [RenderTestWin]::FindTitled($p.Id, "ERROR") }
if ($assert -ne [IntPtr]::Zero) { "ERROR DIALOG present - capturing it instead"; $h = $assert }
[RenderTestWin]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 1500
$r = New-Object RenderTestWin+RECT
[RenderTestWin]::GetWindowRect($h, [ref]$r) | Out-Null
$saved = @()
for ($b = 0; $b -lt $Burst; $b++) {
	$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	for ($try = 0; $try -lt 5; $try++) {
		$hdc = $g.GetHdc(); [RenderTestWin]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc)
		# PrintWindow occasionally returns a black frame under GPU load; retry those.
		$lit = 0
		for ($sx = 20; $sx -lt $bmp.Width; $sx += 61) { for ($sy = 40; $sy -lt $bmp.Height; $sy += 47) { $c = $bmp.GetPixel($sx, $sy); if ($c.R + $c.G + $c.B -gt 15) { $lit++ } } }
		if ($lit -gt 10) { break }
		Start-Sleep -Milliseconds 700
	}
	$file = if ($Burst -gt 1) { Join-Path $OutputDir "$Name`_$b.png" } else { Join-Path $OutputDir "$Name.png" }
	$bmp.Save($file); $saved += $file
	$g.Dispose(); $bmp.Dispose()
	if ($Burst -gt 1) { Start-Sleep -Milliseconds $BurstIntervalMs }
}
$saved
if (-not $KeepRunning) { Stop-Process $p -Force; $p.WaitForExit(10000) | Out-Null; Start-Sleep -Milliseconds 1500 }
