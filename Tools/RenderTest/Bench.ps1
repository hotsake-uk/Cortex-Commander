param(
	[ValidateSet("ours", "base")] [string]$Build = "ours", # Our build, or the original AI's (../cccp-ai-baseline, patched gyms).
	[int]$Repeat = 3,
	[string]$Label = "bench",
	[string[]]$Suites = @("AIGym", "Sky", "Tower", "AIBywater", "AIHemslock"),
	[double]$Speed = 1 # The simulation's speed against real time, passed to the gym scripts (a pass at 3 takes a third of the time).
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
# A manifest for the dashboard (Tools/RenderTest/Dashboard.py): what this job will run, and when it began and ended.
$manifest = Join-Path $out "job.json"
@{ label = $Label; build = $Build; suites = @($Suites); repeat = $Repeat; speed = $Speed; startedAt = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss"); finishedAt = $null } | ConvertTo-Json | Set-Content $manifest -Encoding utf8
foreach ($run in 1..$Repeat) {
	foreach ($suite in $Suites) {
		Remove-Item Env:\CCCP_BUNKER_TOWER -ErrorAction SilentlyContinue
		switch ($suite) {
			"AIGym" { & "$rt\AIGym.ps1" -Speed $Speed | Out-Null; $log = "$rt\Output\aigym_1.txt"; $pattern = 'AIGYM (.+?): (arrived in ([\d\.]+) s|GAVE UP|died)' }
			"Sky" { & "$rt\AIBunker.ps1" -Scenario AIBunker -Speed $Speed | Out-Null; $log = "$rt\Output\aibunker_1.txt"; $pattern = 'AIBUNKER (.+?): (arrived in ([\d\.]+) s|GAVE UP|died)' }
			"Tower" { $env:CCCP_BUNKER_TOWER = "1"; & "$rt\AIBunker.ps1" -Scenario AIBunker -Speed $Speed | Out-Null; $log = "$rt\Output\aibunker_1.txt"; $pattern = 'AIBUNKER (.+?): (arrived in ([\d\.]+) s|GAVE UP|died)'; Remove-Item Env:\CCCP_BUNKER_TOWER }
			"AIBywater" { & "$rt\AIBunker.ps1" -Scenario AIBywater -Speed $Speed | Out-Null; $log = "$rt\Output\aibunker_1.txt"; $pattern = 'AIBUNKER (.+?): (arrived in ([\d\.]+) s|GAVE UP|died)' }
			"Recover" { & "$rt\AIBunker.ps1" -Scenario AIRecover -Wait 30 -Speed $Speed | Out-Null; $log = "$rt\Output\aibunker_1.txt"; $pattern = 'RECOVER (.+?): (arrived in ([\d\.]+) s|GAVE UP|died)' }
			"Flight" {
				# Both batches of the flight gym (FlightGym.lua), one game each, their logs joined.
				$log = "$rt\Output\flight_joined.txt"
				Set-Content $log "" -Encoding utf8
				foreach ($batch in 1, 2) {
					$env:CCCP_FLIGHT_BATCH = "$batch"
					& "$rt\AIBunker.ps1" -Scenario AIFlight -Wait 45 -Speed $Speed | Out-Null
					Get-Content "$rt\Output\aibunker_1.txt" | Add-Content $log
				}
				Remove-Item Env:\CCCP_FLIGHT_BATCH
				$pattern = 'FLIGHT (.+?): (landed in ([\d\.]+) s|GAVE UP|died)'
			}
			"AIHemslock" { & "$rt\AIBunker.ps1" -Scenario AIHemslock -Speed $Speed | Out-Null; $log = "$rt\Output\aibunker_1.txt"; $pattern = 'AIBUNKER (.+?): (arrived in ([\d\.]+) s|GAVE UP|died)' }
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
$m = Get-Content $manifest -Raw | ConvertFrom-Json
$m.finishedAt = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
$m | ConvertTo-Json | Set-Content $manifest -Encoding utf8
