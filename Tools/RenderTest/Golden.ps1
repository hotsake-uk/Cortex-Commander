param(
	[switch]$Update,                # Capture new baselines instead of comparing.
	[string[]]$Only = @(),          # Limit to these scenarios (names without the Golden prefix also work).
	[string]$Exe = "Cortex Command.debug.release.exe",
	[int]$ExtraWait = 10,
	[double]$MaxMeanDiff = 7.0,     # Mean absolute difference per channel (0-255) allowed.
	[double]$MaxBadPercent = 6.0    # Percentage of pixels allowed to differ by more than $BadThreshold.
)
# Golden image regression check: captures a few calm, fixed scenes and compares them against the baselines in Golden\.
# The game isn't deterministic (AI, particles, twinkling stars), so the comparison allows a small camera shift and has tolerances: it catches black screens,
# missing layers, broken passes and colour shifts, not a soldier that moved. Exit code 0 when everything passes, 1 otherwise.
. (Join-Path $PSScriptRoot "Common.ps1")
Add-Type -AssemblyName System.Drawing

$goldenDir = Join-Path $PSScriptRoot "Golden"
if (-not (Test-Path $goldenDir)) { New-Item -ItemType Directory -Path $goldenDir | Out-Null }
$scenarios = @(
	@{ Name = "GoldenNoon" },
	@{ Name = "GoldenNight" },
	@{ Name = "GoldenCaves"; CameraPOI = "2" },
	@{ Name = "GoldenClassic" },
	@{ Name = "GoldenLightingOnly" },
	@{ Name = "GoldenInterior" }
)
if ($Only.Count -gt 0) {
	$wanted = $Only | ForEach-Object { if ($_ -like "Golden*") { $_ } else { "Golden$_" } }
	$scenarios = $scenarios | Where-Object { $wanted -contains $_.Name }
}

# Captures are cropped to the 960x540 game view, the same as the stored baselines.
function Get-Reduced([string]$path) {
	$source = [Drawing.Bitmap]::FromFile($path)
	# Crop the window title bar and borders away, leaving the 960x540 game view.
	$gameArea = New-Object Drawing.Rectangle 8, 31, ([Math]::Min(960, $source.Width - 16)), ([Math]::Min(540, $source.Height - 39))
	$reduced = New-Object Drawing.Bitmap $gameArea.Width, $gameArea.Height
	$g = [Drawing.Graphics]::FromImage($reduced)
	$g.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
	$g.DrawImage($source, (New-Object Drawing.Rectangle 0, 0, $reduced.Width, $reduced.Height), $gameArea, [Drawing.GraphicsUnit]::Pixel)
	$g.Dispose(); $source.Dispose()
	return $reduced
}

if (-not ("GoldenCompare" -as [type])) {
	Add-Type @"
using System;
public static class GoldenCompare {
	// Mean absolute channel difference and percentage of clearly different pixels between a and b, with b shifted by (dx, dy).
	static void Measure(byte[] a, byte[] b, int w, int h, int stride, int dx, int dy, int margin, byte[] diff, out double mean, out double badPercent) {
		double total = 0; long bad = 0, count = 0;
		for (int y = margin; y < h - margin; y++) {
			for (int x = margin; x < w - margin; x++) {
				int i = y * stride + x * 4;
				int j = (y + dy) * stride + (x + dx) * 4;
				int d0 = Math.Abs(a[i] - b[j]), d1 = Math.Abs(a[i + 1] - b[j + 1]), d2 = Math.Abs(a[i + 2] - b[j + 2]);
				total += d0 + d1 + d2;
				int m = Math.Max(d0, Math.Max(d1, d2));
				if (m > 40) { bad++; }
				count++;
				if (diff != null) {
					diff[i] = (byte)(b[j] / 4); diff[i + 1] = (byte)(b[j + 1] / 4); diff[i + 2] = (byte)Math.Min(255, b[j + 2] / 4 + m * 3); diff[i + 3] = 255;
				}
			}
		}
		mean = total / (count * 3.0);
		badPercent = 100.0 * bad / count;
	}

	// The game camera can land a pixel or two differently between runs, so compare at the best global shift within maxShift.
	public static double[] Compare(byte[] a, byte[] b, int w, int h, int stride, int maxShift, byte[] diff) {
		double bestMean = double.MaxValue, bestBad = 0; int bestX = 0, bestY = 0;
		// Coarse pass every 4 pixels, then refine around the best.
		for (int pass = 0; pass < 2; pass++) {
			int step = pass == 0 ? 4 : 1;
			int range = pass == 0 ? maxShift : 3;
			int centerX = bestX, centerY = bestY;
			for (int dy = centerY - range; dy <= centerY + range; dy += step) {
				for (int dx = centerX - range; dx <= centerX + range; dx += step) {
					if (Math.Abs(dx) > maxShift || Math.Abs(dy) > maxShift) { continue; }
					double mean, badPercent;
					Measure(a, b, w, h, stride, dx, dy, maxShift, null, out mean, out badPercent);
					if (mean < bestMean) { bestMean = mean; bestBad = badPercent; bestX = dx; bestY = dy; }
				}
			}
		}
		Measure(a, b, w, h, stride, bestX, bestY, maxShift, diff, out bestMean, out bestBad);
		return new double[] { bestMean, bestBad, bestX, bestY };
	}
}
"@
}

function Compare-Images([Drawing.Bitmap]$a, [Drawing.Bitmap]$b, [string]$diffPath) {
	$w = [Math]::Min($a.Width, $b.Width); $h = [Math]::Min($a.Height, $b.Height)
	$rect = New-Object Drawing.Rectangle 0, 0, $w, $h
	$format = [Drawing.Imaging.PixelFormat]::Format32bppArgb
	$da = $a.LockBits($rect, [Drawing.Imaging.ImageLockMode]::ReadOnly, $format)
	$db = $b.LockBits($rect, [Drawing.Imaging.ImageLockMode]::ReadOnly, $format)
	$bytes = $da.Stride * $h
	$pa = New-Object byte[] $bytes; $pb = New-Object byte[] $bytes; $pd = New-Object byte[] $bytes
	[Runtime.InteropServices.Marshal]::Copy($da.Scan0, $pa, 0, $bytes)
	[Runtime.InteropServices.Marshal]::Copy($db.Scan0, $pb, 0, $bytes)
	$stride = $da.Stride
	$a.UnlockBits($da); $b.UnlockBits($db)
	$r = [GoldenCompare]::Compare($pa, $pb, $w, $h, $stride, 32, $pd)
	$diff = New-Object Drawing.Bitmap $w, $h
	$dd = $diff.LockBits($rect, [Drawing.Imaging.ImageLockMode]::WriteOnly, $format)
	[Runtime.InteropServices.Marshal]::Copy($pd, 0, $dd.Scan0, $bytes)
	$diff.UnlockBits($dd); $diff.Save($diffPath); $diff.Dispose()
	return @{ Mean = $r[0]; BadPercent = $r[1]; ShiftX = $r[2]; ShiftY = $r[3] }
}

$failures = 0
foreach ($scenario in $scenarios) {
	$name = $scenario.Name
	Get-Process ([IO.Path]::GetFileNameWithoutExtension($Exe)) -ErrorAction SilentlyContinue | Stop-Process -Force
	Start-Sleep 2
	$captureArgs = @{ Scenario = $name; Name = "golden_$name"; Exe = $Exe; ExtraWait = $ExtraWait }
	if ($scenario.CameraPOI) { $captureArgs.CameraPOI = $scenario.CameraPOI }
	$captured = & (Join-Path $PSScriptRoot "Capture.ps1") @captureArgs | Select-Object -Last 1
	if (-not $captured -or -not (Test-Path $captured)) { "FAIL  $name : capture failed ($captured)"; $failures++; continue }
	$reduced = Get-Reduced $captured
	$baselinePath = Join-Path $goldenDir "$name.png"
	if ($Update) {
		$reduced.Save($baselinePath); $reduced.Dispose()
		"UPDATED $name"
		continue
	}
	if (-not (Test-Path $baselinePath)) { "FAIL  $name : no baseline (run with -Update)"; $failures++; $reduced.Dispose(); continue }
	$baseline = [Drawing.Bitmap]::FromFile($baselinePath)
	$result = Compare-Images $baseline $reduced (Join-Path $OutputDir "golden_$name`_diff.png")
	$baseline.Dispose(); $reduced.Dispose()
	$ok = $result.Mean -le $MaxMeanDiff -and $result.BadPercent -le $MaxBadPercent
	if (-not $ok) { $failures++ }
	"{0}  {1,-20} mean diff {2,5:N2}  pixels off {3,5:N2}%  (aligned at {4},{5})" -f $(if ($ok) { "PASS" } else { "FAIL" }), $name, $result.Mean, $result.BadPercent, $result.ShiftX, $result.ShiftY
}
if (-not $Update) { if ($failures -eq 0) { "All golden images match." } else { "$failures golden image(s) differ; see Output\golden_*_diff.png" } }
exit $(if ($failures -eq 0) { 0 } else { 1 })
