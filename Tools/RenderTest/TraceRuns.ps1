param(
	[Parameter(Mandatory)] [string]$Label, # The results folder to add the traced logs to.
	[string[]]$Runs = @()                  # Each "Suite:course", e.g. "AIGym:15", "AIBywater:1", "AIBunker:1".
)
# Traced reruns of single courses for a results folder: the movement script's AITRACE lines for the chosen course, the whole console log
# kept as <Suite>_trace<course>.log next to the plain run's.
$rt = $PSScriptRoot
$out = Join-Path $rt "Results\$Label"
New-Item -ItemType Directory -Force $out | Out-Null
$Runs = @($Runs | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($run in $Runs) {
	$suite, $course = $run -split ':'
	switch ($suite) {
		"AIGym"     { $env:CCCP_GYM_TRACE = $course; & "$rt\AIGym.ps1" -Trace | Out-Null; $log = "$rt\Output\aigym_1.txt"; Remove-Item Env:\CCCP_GYM_TRACE }
		"AIBunker"  { $env:CCCP_BUNKER_TRACE = $course; & "$rt\AIBunker.ps1" -Scenario AIBunker -Trace | Out-Null; $log = "$rt\Output\aibunker_1.txt"; Remove-Item Env:\CCCP_BUNKER_TRACE }
		"AIBywater" { $env:CCCP_BUNKER_TRACE = $course; & "$rt\AIBunker.ps1" -Scenario AIBywater -Trace | Out-Null; $log = "$rt\Output\aibunker_1.txt"; Remove-Item Env:\CCCP_BUNKER_TRACE }
		default     { "unknown suite $suite"; continue }
	}
	$dest = Join-Path $out "${suite}_trace$course.log"
	if (Test-Path $log) { Copy-Item $log $dest -Force; "${run} -> $dest ($((Get-Content $dest).Count) lines)" } else { "${run}: no console log" }
}
