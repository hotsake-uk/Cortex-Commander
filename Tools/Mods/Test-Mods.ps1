param([string]$Exe = "Cortex Command.debug.release.exe", [int]$ExtraWait = 6, [string]$Scenario = "Bunker", [switch]$Play, [int]$SecondsPerMod = 18)
# Starts the game with every mod in Mods\ that isn't switched off, waits for it to finish loading, and reports which mods loaded and what was wrong with the rest.
# With -Play it goes on to put each mod's units, craft and weapons into a short fight, one mod after another, and reports what the console said for each (ModSweepConsole.txt has it all).
# Uses the RenderTest harness (run Tools\RenderTest\Setup.ps1 once first). The full list of notes is in LogLoadingWarning.txt afterwards.
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $repo
Remove-Item LogLoadingWarning.txt, AbortLog.txt -ErrorAction SilentlyContinue
& "$repo\Tools\RenderTest\Setup.ps1" | Out-Null
$modsFolder = if ($env:CCCP_MODS_DIR) { $env:CCCP_MODS_DIR } else { "Mods" }
$mods = Get-ChildItem (Join-Path $repo $modsFolder) -Directory -Filter "*.rte" | Where-Object { $_.Name -ne "RenderTest.rte" }
if ($Play) {
	$Scenario = "ModSweep"
	$ExtraWait = 10 + $SecondsPerMod * $mods.Count
	$env:CCCP_CONSOLE_LOG = Join-Path $repo "ModSweepConsole.txt"
}
$result = & "$repo\Tools\RenderTest\Capture.ps1" -Scenario $Scenario -Name "ModTest" -ExtraWait $ExtraWait -Exe $Exe
Remove-Item Env:\CCCP_CONSOLE_LOG -ErrorAction SilentlyContinue
$result | Select-Object -Last 2
$warnings = if (Test-Path LogLoadingWarning.txt) { Get-Content LogLoadingWarning.txt } else { @() }
$failed = $warnings | Where-Object { $_ -match '^MOD NOT LOADED: ' }
"{0} mods in $modsFolder\, {1} failed to load, {2} other notes (see LogLoadingWarning.txt)" -f $mods.Count, @($failed).Count, (@($warnings).Count - @($failed).Count - 1)
$failed
if (Test-Path AbortLog.txt) { "THE GAME ABORTED:"; Get-Content AbortLog.txt -Tail 12 }
if ($Play -and (Test-Path ModSweepConsole.txt)) {
	# What the console said while each mod was in play, with repeats counted.
	$current = "(before the sweep)"; $perMod = [ordered]@{}
	foreach ($line in Get-Content ModSweepConsole.txt) {
		if ($line -match '^(PRINT: )?MODSWEEP BEGIN (.+)$') { $current = $Matches[2]; continue }
		if ($line -match 'MODSWEEP (END|DONE)') { if ($line -match 'DONE') { $current = "(after the sweep)" }; continue }
		if ($line -match 'ERROR|MODSWEEP PROBLEM|attempt to|nil value|No such|not found') {
			if (-not $perMod.Contains($current)) { $perMod[$current] = @{} }
			$key = if ($line.Length -gt 260) { $line.Substring(0, 260) } else { $line }
			$perMod[$current][$key] = 1 + [int]$perMod[$current][$key]
		}
	}
	if (-not (Select-String -Path ModSweepConsole.txt -Pattern "MODSWEEP DONE" -Quiet)) { "THE SWEEP DID NOT FINISH (crash or too little time): last lines:"; Get-Content ModSweepConsole.txt -Tail 6 }
	"{0} mods with console errors in play:" -f $perMod.Count
	foreach ($mod in $perMod.Keys) { "== $mod"; foreach ($entry in $perMod[$mod].GetEnumerator()) { "   x{0}  {1}" -f $entry.Value, $entry.Key } }
}
