param(
	[int]$Runs = 1,      # How many times to run the gym.
	[int]$Wait = 75,     # Seconds to let the courses run (a unit that can't make it gives up at 60).
	[double]$Speed = 1,  # The simulation's speed against real time (CCCP_TIME_SCALE): the waits shrink to match.
	[switch]$Foreground, # Show the game window and let it take focus (by default it runs hidden in the background, muted, and leaves the mouse alone).
	[switch]$Trace,      # Also print the movement script's AITRACE lines for the traced unit.
	[string]$Scenario = "AIBunker" # AIBunker (the bunker of modules in the sky), AIBywater or AIHemslock (real maps from the BB+ mod).
)
# Runs the indoor gym (Tools/RenderTest/RenderTest.rte/AIBunker.lua): a bunker of modules with units sent through it and prints the result lines: what the pathfinder made of each course and when
# each unit arrived, died or gave up. The full console log of each run is left in Output\aigym_<n>.txt. See AI_PLAN.md.
$rt = $PSScriptRoot
$repo = Split-Path $rt -Parent
Set-Location $repo
# (This folder's own debug build only: the original AI's build in its own folder has the same name, and may run alongside.)
if (Get-Process "Cortex Command.debug.release" -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$repo*" }) {
	"The debug build is already running; the capture would attach to it and close it. Close it first."
	exit 1
}
if ($Foreground) { Remove-Item Env:\CCCP_BACKGROUND -ErrorAction SilentlyContinue } else { $env:CCCP_BACKGROUND = "1" }
if ($Speed -ne 1) { $env:CCCP_TIME_SCALE = "$Speed"; $Wait = [int][Math]::Ceiling($Wait / $Speed) } else { Remove-Item Env:\CCCP_TIME_SCALE -ErrorAction SilentlyContinue }
& "$rt\Setup.ps1" | Out-Null
for ($i = 1; $i -le $Runs; $i++) {
	$log = "$rt\Output\aibunker_$i.txt"
	$env:CCCP_CONSOLE_LOG = $log
	if ($Trace) { $env:CCCP_AI_LOG = "1" }
	& "$rt\Capture.ps1" -Scenario $Scenario -ExtraWait $Wait -Burst 1 | Out-Null
	Remove-Item Env:\CCCP_CONSOLE_LOG
	if ($Trace) { Remove-Item Env:\CCCP_AI_LOG }
	"--- run $i"
	$pattern = if ($Trace) { "AIBUNKER|GYM|AITRACE" } else { "AIBUNKER (path for|.*: (arrived|died|GAVE UP)|done|running)|GYM " }
	Select-String -Path $log -Pattern $pattern | ForEach-Object { $_.Line.Substring(0, [Math]::Min(220, $_.Line.Length)) }
}
