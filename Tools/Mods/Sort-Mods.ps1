param([string[]]$Only = @(), [int]$SecondsPerMod = 18, [switch]$Recheck)
# Tests each parked mod on its own and sorts it: into Mods\ if it loads and plays through the scripted fight with a clean console; if not it stays in ModsParked\,
# with what went wrong in ModsParked\<mod>.errors.txt. With -Recheck the mods already in Mods\ are tested too, and parked if they fail.
# Mods that belong together (UniTec and its factions) are tested together. Takes about a minute per mod.
# The tests run in their own folder (ModsTest\, through CCCP_MODS_DIR), so the game can be played meanwhile; a mod only moves between folders when its verdict is in.
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $repo
$mods = Join-Path $repo "Mods"; $parked = Join-Path $repo "ModsParked"; $stage = Join-Path $repo "ModsTest"
foreach ($dir in $parked, $stage) { if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null } }
Get-ChildItem $stage -Directory -Filter "*.rte" | Where-Object { $_.Name -ne "RenderTest.rte" } | ForEach-Object { Move-Item $_.FullName (Join-Path $parked $_.Name) }

$candidates = @(Get-ChildItem $parked -Directory -Filter "*.rte" | ForEach-Object { @{ Name = $_.Name; From = $parked } })
if ($Recheck) { $candidates += @(Get-ChildItem $mods -Directory -Filter "*.rte" | Where-Object { $_.Name -ne "RenderTest.rte" } | ForEach-Object { @{ Name = $_.Name; From = $mods } }) }
$groups = [ordered]@{}
foreach ($candidate in $candidates | Sort-Object { $_.Name }) { $key = if ($candidate.Name -like "UniTec*") { "UniTec" } else { $candidate.Name }; if (-not $groups.Contains($key)) { $groups[$key] = @() }; $groups[$key] += $candidate }

$env:CCCP_MODS_DIR = "ModsTest"
try {
	foreach ($key in $groups.Keys) {
		$members = $groups[$key]
		if ($Only.Count -gt 0 -and -not ($Only | Where-Object { $key -like $_ -or ($members.Name -contains $_) })) { continue }
		# A copy goes on the stage, so a mod the player has in Mods\ is never taken away mid-game.
		foreach ($member in $members) { Copy-Item -Recurse (Join-Path $member.From $member.Name) (Join-Path $stage $member.Name) }
		$report = & "$PSScriptRoot\Test-Mods.ps1" -Play -SecondsPerMod $SecondsPerMod
		foreach ($member in $members) { Remove-Item -Recurse -Force (Join-Path $stage $member.Name) }
		$problems = @(); $section = ""
		foreach ($line in $report) {
			if ($line -match '^MOD NOT LOADED|^THE GAME ABORTED|^THE SWEEP DID NOT FINISH|^EXITED') { $problems += $line }
			if ($line -match '^== (.+)$') { $section = $Matches[1]; continue }
			if ($line -match '^\s+x\d+ ' -and $section -ne "(before the sweep)") { $problems += "[$section] " + $line.Trim() }
		}
		$clean = $problems.Count -eq 0
		foreach ($member in $members) {
			$errorFile = Join-Path $parked "$($member.Name).errors.txt"
			if ($clean) {
				Remove-Item $errorFile -ErrorAction SilentlyContinue
				if ($member.From -eq $parked) { Move-Item (Join-Path $parked $member.Name) (Join-Path $mods $member.Name) }
			} else {
				Set-Content -Path $errorFile -Value $problems -Encoding utf8
				if ($member.From -eq $mods) { Move-Item (Join-Path $mods $member.Name) (Join-Path $parked $member.Name) }
			}
		}
		"{0,-9} {1}" -f $(if ($clean) { "CLEAN" } else { "PROBLEMS" }), ($members.Name -join ", ")
		$problems | Select-Object -First 6 | ForEach-Object { "          " + $(if ($_.Length -gt 230) { $_.Substring(0, 230) } else { $_ }) }
	}
} finally {
	Remove-Item Env:\CCCP_MODS_DIR -ErrorAction SilentlyContinue
}
