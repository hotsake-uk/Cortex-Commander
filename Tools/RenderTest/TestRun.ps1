param(
	[string]$Label = "",      # The results folder name; the short commit hash when left out.
	[switch]$NoBuild,         # Test the build that is there.
	[string[]]$Suites = @("AIGym", "AICombat", "AIBunker", "AIBywater"),
	[int]$Repeat = 1          # Runs of each suite (the indoor results vary from run to run, so one run proves little).
)
# The whole AI test pass, as run for every push to ai-overhaul: builds the debug build, runs each gym in turn and copies its console log
# and result lines into Tools\RenderTest\Results\<label>\ (results.txt has the result lines of every suite). The diagnosis goes into
# SUMMARY.md in the same folder, written by whoever reads the results.
$rt = $PSScriptRoot
$repo = Split-Path (Split-Path $rt -Parent) -Parent
Set-Location $repo
$Suites = @($Suites | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (-not $Label) { $Label = (git rev-parse --short HEAD).Trim() }
$out = Join-Path $rt "Results\$Label"
New-Item -ItemType Directory -Force $out | Out-Null
$results = Join-Path $out "results.txt"
"commit $(git rev-parse HEAD)  $(git log -1 --format=%s)" | Set-Content $results -Encoding utf8
if (-not $NoBuild) {
	$build = & "$rt\Build.ps1" | Out-String
	"--- build" | Add-Content $results
	$build.Trim() | Add-Content $results
	if ($build -notmatch "EXIT 0") { "build failed"; Get-Content $results; exit 1 }
}
foreach ($pass in 1..$Repeat) {
foreach ($suite in $Suites) {
	$start = Get-Date
	switch ($suite) {
		"AIGym"     { $lines = & "$rt\AIGym.ps1";    $log = "$rt\Output\aigym_1.txt" }
		"AICombat"  { $lines = & "$rt\AICombat.ps1"; $log = "$rt\Output\aicombat_1.txt" }
		"AIBunker"  { $lines = & "$rt\AIBunker.ps1" -Scenario AIBunker;  $log = "$rt\Output\aibunker_1.txt" }
		"AIBywater" { $lines = & "$rt\AIBunker.ps1" -Scenario AIBywater; $log = "$rt\Output\aibunker_1.txt" }
		"AIHemslock" { $lines = & "$rt\AIBunker.ps1" -Scenario AIHemslock; $log = "$rt\Output\aibunker_1.txt" }
		default     { "unknown suite $suite"; continue }
	}
	"--- $suite run $pass ($([int]((Get-Date) - $start).TotalSeconds) s)" | Add-Content $results
	if ($lines) { $lines | Where-Object { $_ -notmatch '^--- run' } | Add-Content $results } else { "(no result lines)" | Add-Content $results }
	if (Test-Path $log) { Copy-Item $log (Join-Path $out $(if ($Repeat -gt 1) { "${suite}_$pass.log" } else { "$suite.log" })) -Force } else { "(no console log)" | Add-Content $results }
}
}
Get-Content $results
