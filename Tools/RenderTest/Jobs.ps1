param(
	[int]$Hours = 6 # How far back to look for result folders.
)
# What the test harness has been doing lately: each results folder (Tools/RenderTest/Results/<label>/<build>) touched in the last few
# hours, with the suites and runs it has logs for, when it was last written to, whether its results.csv says it is complete, and an
# estimate of what's left from the pace of the runs so far. Run it any time; it only reads files.
$results = Join-Path $PSScriptRoot "Results"
$since = (Get-Date).AddHours(-$Hours)
$suiteNames = @("AIGym", "Sky", "Tower", "AIBywater", "AIHemslock", "Flight", "Recover")
# Roughly how long each suite's run takes (minutes), from past runs: for the estimate of what's left.
$suiteMinutes = @{ AIGym = 1.8; Sky = 1.5; Tower = 2.5; AIBywater = 1.8; AIHemslock = 1.8; Flight = 3.0; Recover = 1.2 }
$running = Get-Process "Cortex Command.debug.release" -ErrorAction SilentlyContinue
"Harness game running: " + $(if ($running) { "yes (pid $($running.Id), since $($running.StartTime.ToString('HH:mm')))" } else { "no" })
""
$rows = @()
foreach ($label in Get-ChildItem $results -Directory) {
	foreach ($build in Get-ChildItem $label.FullName -Directory) {
		$logs = Get-ChildItem $build.FullName -Filter "*_*.log" -ErrorAction SilentlyContinue
		if (-not $logs) { continue }
		$last = ($logs | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
		if ($last -lt $since) { continue }
		$first = ($logs | Sort-Object LastWriteTime | Select-Object -First 1).LastWriteTime
		$bySuite = @{}
		foreach ($log in $logs) {
			if ($log.BaseName -match '^(.+)_(\d+)$') { $bySuite[$Matches[1]] = [Math]::Max([int]$Matches[2], $(if ($bySuite.ContainsKey($Matches[1])) { $bySuite[$Matches[1]] } else { 0 })) }
		}
		$suites = ($suiteNames | Where-Object { $bySuite.ContainsKey($_) } | ForEach-Object { "$_ x$($bySuite[$_])" }) -join ", "
		$repeats = ($bySuite.Values | Measure-Object -Maximum).Maximum
		# Active if written to in the last 4 minutes; then estimate what a full set of repeats of the suites seen would still take.
		$active = ((Get-Date) - $last).TotalMinutes -lt 4
		$eta = ""
		if ($active) {
			$doneMinutes = 0.0; $totalMinutes = 0.0
			foreach ($suite in $bySuite.Keys) { $doneMinutes += $bySuite[$suite] * $suiteMinutes[$suite]; $totalMinutes += $repeats * $suiteMinutes[$suite] }
			$left = [Math]::Max(0, $totalMinutes - $doneMinutes)
			$eta = "about $([Math]::Round($left)) min left of this repeat set"
		}
		$rows += [PSCustomObject]@{ Job = "$($label.Name)/$($build.Name)"; Started = $first.ToString("HH:mm"); Last = $last.ToString("HH:mm"); State = $(if ($active) { "RUNNING" } else { "done" }); Suites = $suites; Estimate = $eta }
	}
}
$rows | Sort-Object Last | Format-Table -AutoSize -Wrap
