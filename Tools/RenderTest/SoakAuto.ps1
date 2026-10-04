# Input-free soak test: launches a scenario that plays itself and samples memory and responsiveness.
# It sends no keyboard or mouse input (unlike Soak.ps1), so it's safe to run while the machine is in use.
# Usage: .\Setup.ps1; .\SoakAuto.ps1 [-Samples 18] [-IntervalSeconds 10] [-Exe "Cortex Command.exe"]
param([string]$Scenario = "SoakAuto", [int]$Samples = 18, [int]$IntervalSeconds = 10, [string]$Exe = "Cortex Command.debug.release.exe")
$t = $PSScriptRoot
$repo = (Resolve-Path "$t\..\..").Path
& "$t\Capture.ps1" -Scenario $Scenario -Exe $Exe -ExtraWait 2 -KeepRunning | Out-Null
$p = Get-Process ([IO.Path]::GetFileNameWithoutExtension($Exe)) -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$repo*" } | Select-Object -First 1
if (-not $p) { "NOT RUNNING: the game didn't start."; exit 1 }
for ($i = 0; $i -lt $Samples; $i++) {
	Start-Sleep -Seconds $IntervalSeconds
	$p.Refresh()
	if ($p.HasExited) { "EXITED at sample $i with code $($p.ExitCode)"; exit 1 }
	"{0,4}s: {1} MB, responding={2}" -f (($i + 1) * $IntervalSeconds), [int]($p.WorkingSet64 / 1MB), $p.Responding
}
& "$t\Capture.ps1" -Exe $Exe -Name "soakauto_end" -ExtraWait 0 -KeepRunning | Out-Null
Stop-Process -Id $p.Id -Force
"Finished: still running after $($Samples * $IntervalSeconds) seconds."
