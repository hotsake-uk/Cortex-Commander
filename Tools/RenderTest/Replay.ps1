param(
	[string]$Suite = "AIBywater", # AIBywater, AIHemslock, Sky (the sky bunker) or Tower.
	[int]$Course = 1,             # Which course's unit is traced and followed (1 = the first in the gym's list).
	[string]$Label = "replay",
	[double]$Speed = 1
)
# A replay of one course of a bunker gym: the traced unit followed by the camera with the navigation overlay on, a frame saved five times
# a second, and the route-follower's trace lines laid against the frames in one HTML page (MakeReplay.py), to watch what a unit did and
# read what it decided at that moment. Runs hidden like the other gyms. Output: Results\<Label>\<Suite>_<Course>.html
$ErrorActionPreference = "Stop"
$rt = $PSScriptRoot
$repo = Resolve-Path (Join-Path $rt "..\..")
$shots = Join-Path $repo "ScreenShots"
$out = Join-Path $rt "Results\$Label"
New-Item -ItemType Directory -Force $out | Out-Null
Get-ChildItem $shots -Filter "Replay_*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
$scenario = switch ($Suite) { "Sky" { "AIBunker" } "Tower" { "AIBunker" } default { $Suite } }
$env:CCCP_BUNKER_TRACE = "$Course"
$env:CCCP_RECORD = "1"
if ($Suite -eq "Tower") { $env:CCCP_BUNKER_TOWER = "1" }
try {
	& "$rt\AIBunker.ps1" -Scenario $scenario -Trace -Speed $Speed | Out-Null
} finally {
	Remove-Item Env:\CCCP_BUNKER_TRACE, Env:\CCCP_RECORD -ErrorAction SilentlyContinue
	Remove-Item Env:\CCCP_BUNKER_TOWER -ErrorAction SilentlyContinue
}
$log = Join-Path $out "${Suite}_$Course.log"
Copy-Item "$rt\Output\aibunker_1.txt" $log -Force
$page = Join-Path $out "${Suite}_$Course.html"
python (Join-Path $rt "MakeReplay.py") $log $shots $page "$Suite course $Course"
Get-ChildItem $shots -Filter "Replay_*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
$page
