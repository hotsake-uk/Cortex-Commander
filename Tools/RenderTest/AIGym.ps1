param(
	[int]$Runs = 1,      # How many times to run the gym.
	[int]$Wait = 75,     # Seconds to let the courses run (a unit that can't make it gives up at 60).
	[switch]$Trace       # Also print the movement script's AITRACE lines for the traced unit.
)
# Runs the AI gym (Tools/RenderTest/RenderTest.rte/AIGym.lua) and prints the result lines: what the pathfinder made of each course and when
# each unit arrived, died or gave up. The full console log of each run is left in Output\aigym_<n>.txt. See AI_PLAN.md.
$rt = $PSScriptRoot
$repo = Split-Path $rt -Parent
Set-Location $repo
if (Get-Process "Cortex Command.debug.release" -ErrorAction SilentlyContinue) {
	"The debug build is already running; the capture would attach to it and close it. Close it first."
	exit 1
}
& "$rt\Setup.ps1" | Out-Null
for ($i = 1; $i -le $Runs; $i++) {
	$log = "$rt\Output\aigym_$i.txt"
	$env:CCCP_CONSOLE_LOG = $log
	if ($Trace) { $env:CCCP_AI_LOG = "1" }
	& "$rt\Capture.ps1" -Scenario AIGym -ExtraWait $Wait -Burst 1 | Out-Null
	Remove-Item Env:\CCCP_CONSOLE_LOG
	if ($Trace) { Remove-Item Env:\CCCP_AI_LOG }
	"--- run $i"
	$pattern = if ($Trace) { "AIGYM|AITRACE" } else { "AIGYM (path for|unit|.*: (arrived|died|GAVE UP)|done)" }
	Select-String -Path $log -Pattern $pattern | ForEach-Object { $_.Line.Substring(0, [Math]::Min(220, $_.Line.Length)) }
}
