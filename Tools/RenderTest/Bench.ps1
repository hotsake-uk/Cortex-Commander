param(
	[ValidateSet("ours", "base")] [string]$Build = "ours", # Our build, or the original AI's (../cccp-ai-baseline, patched gyms).
	[int]$Repeat = 3,
	[string]$Label = "bench",
	[string[]]$Suites = @("AIGym", "Sky", "Tower", "AIBywater", "AIHemslock")
)
# The AI benchmark: every gym, Repeat times, on one build, each course's result written as a line of results.csv
# (build,suite,run,course,result,seconds). Tools/RenderTest/Bench.py compares two builds' files.
$here = $PSScriptRoot
$rt = if ($Build -eq "ours") { $here } else { Join-Path (Split-Path (Split-Path (Split-Path $here -Parent) -Parent) -Parent) "cccp-ai-baseline\Tools\RenderTest" }
$out = Join-Path $here "Results\$Label\$Build"
New-Item -ItemType Directory -Force $out | Out-Null
$csv = Join-Path $out "results.csv"
if (-not (Test-Path $csv)) { "build,suite,run,course,result,seconds" | Set-Content $csv -Encoding utf8 }
$Suites = @($Suites | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($run in 1..$Repeat) {
	foreach ($suite in $Suites) {
		Remove-Item Env:\CCCP_BUNKER_TOWER -ErrorAction SilentlyContinue
		switch ($suite) {
			"AIGym" { & "$rt\AIGym.ps1" | Out-Null; $log = "$rt\Output\aigym_1.txt"; $pattern = 'AIGYM (.+?): (arrived in ([\d\.]+) s|GAVE UP|died)' }
			"Sky" { & "$rt\AIBunker.ps1" -Scenario AIBunker | Out-Null; $log = "$rt\Output\aibunker_1.txt"; $pattern = 'AIBUNKER (.+?): (arrived in ([\d\.]+) s|GAVE UP|died)' }
			"Tower" { $env:CCCP_BUNKER_TOWER = "1"; & "$rt\AIBunker.ps1" -Scenario AIBunker | Out-Null; $log = "$rt\Output\aibunker_1.txt"; $pattern = 'AIBUNKER (.+?): (arrived in ([\d\.]+) s|GAVE UP|died)'; Remove-Item Env:\CCCP_BUNKER_TOWER }
			"AIBywater" { & "$rt\AIBunker.ps1" -Scenario AIBywater | Out-Null; $log = "$rt\Output\aibunker_1.txt"; $pattern = 'AIBUNKER (.+?): (arrived in ([\d\.]+) s|GAVE UP|died)' }
			"Flight" { & "$rt\AIBunker.ps1" -Scenario AIFlight -Wait 45 | Out-Null; $log = "$rt\Output\aibunker_1.txt"; $pattern = 'FLIGHT (.+?): (landed in ([\d\.]+) s|GAVE UP|died)' }
			"AIHemslock" { & "$rt\AIBunker.ps1" -Scenario AIHemslock | Out-Null; $log = "$rt\Output\aibunker_1.txt"; $pattern = 'AIBUNKER (.+?): (arrived in ([\d\.]+) s|GAVE UP|died)' }
		}
		if (Test-Path $log) {
			Copy-Item $log (Join-Path $out "${suite}_$run.log") -Force
			foreach ($line in Get-Content $log) {
				if ($line -match $pattern) {
					$result = if ($Matches[2] -like "arrived*" -or $Matches[2] -like "landed*") { "arrived" } elseif ($Matches[2] -eq "died") { "died" } else { "gaveup" }
					$secs = if ($Matches[3]) { $Matches[3] } else { "60" }
					"$Build,$suite,$run,""$($Matches[1])"",$result,$secs" | Add-Content $csv
				}
			}
		}
		"$Build $suite run $run done"
	}
}
