param([Parameter(Mandatory)][string[]]$Mod, [int]$SecondsPerMod = 18)
# Tries one mod (or a few together) from ModsParked\ or Mods\ on its own in the test folder and prints the full report, for working out what's wrong with it.
# Nothing is moved: use Sort-Mods.ps1 to sort it once it's fixed. The whole console of the run is in ModSweepConsole.txt afterwards.
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $repo
$stage = Join-Path $repo "ModsTest"
if (-not (Test-Path $stage)) { New-Item -ItemType Directory -Path $stage | Out-Null }
Get-ChildItem $stage -Directory -Filter "*.rte" | Where-Object { $_.Name -ne "RenderTest.rte" } | Remove-Item -Recurse -Force
foreach ($name in $Mod) {
	$source = @("ModsParked", "Mods") | ForEach-Object { Join-Path $repo "$_\$name" } | Where-Object { Test-Path $_ } | Select-Object -First 1
	if (-not $source) { "No such mod in ModsParked\ or Mods\: $name"; return }
	Copy-Item -Recurse $source (Join-Path $stage $name)
}
$env:CCCP_MODS_DIR = "ModsTest"
try { & "$PSScriptRoot\Test-Mods.ps1" -Play -SecondsPerMod $SecondsPerMod } finally {
	Remove-Item Env:\CCCP_MODS_DIR -ErrorAction SilentlyContinue
	foreach ($name in $Mod) { Remove-Item -Recurse -Force (Join-Path $stage $name) -ErrorAction SilentlyContinue }
}
