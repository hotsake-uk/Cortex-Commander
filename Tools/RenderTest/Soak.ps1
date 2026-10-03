# Soak test: drives the running game with move/fire/jump/reload input and samples memory and responsiveness each round.
# Usage: .\Capture.ps1 -Scenario Play -Exe $Exe -KeepRunning; .\Soak.ps1 -Rounds 30
param([int]$Rounds = 24, [string]$Exe = "Cortex Command.exe")
$t = $PSScriptRoot
. "$t\Input.ps1"
$p = Get-Process ([IO.Path]::GetFileNameWithoutExtension($Exe)) | Select-Object -First 1
Focus-Game $p
$mem = @()
for ($r = 0; $r -lt $Rounds; $r++) {
	$dir = if ($r % 2) { "A" } else { "D" }
	Key-Down $dir; Mouse-Down Left; Move-Mouse (Get-Random -Min -60 -Max 60) (Get-Random -Min -30 -Max 30)
	Start-Sleep -Milliseconds 1500
	Key-Down W; Start-Sleep -Milliseconds 600; Key-Up W
	Start-Sleep -Milliseconds 1500
	Mouse-Up Left; Key-Up $dir; Tap-Key R
	Start-Sleep 5
	$p.Refresh()
	if ($p.HasExited) { "EXITED at round $r code=$($p.ExitCode)"; break }
	$mem += "{0,2}: {1} MB, responding={2}" -f $r, [int]($p.WorkingSet64 / 1MB), $p.Responding
	if ($r % 8 -eq 7) { & "$t\Capture.ps1" -Exe $Exe -Name "soak_$r" -ExtraWait 0 -KeepRunning | Out-Null; Focus-Game $p }
}
$mem
