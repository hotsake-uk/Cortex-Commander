param([string]$Repo = "")
# Installs the RenderTest.rte dev mod into Mods/ and writes the scenario settings files used by Capture.ps1 into Userdata/RenderTest/.
# Each scenario is the player's Settings.ini with a few keys overridden, so it starts straight into a known scene with known effects.
. (Join-Path $PSScriptRoot "Common.ps1")
if (-not $Repo) { $Repo = $RepoRoot }

$modsDir = Join-Path $Repo "Mods"
if (-not (Test-Path $modsDir)) { New-Item -ItemType Directory -Path $modsDir | Out-Null }
Copy-Item -Recurse -Force (Join-Path $PSScriptRoot "RenderTest.rte") $modsDir

$baseSettings = Join-Path $Repo "Userdata\Settings.ini"
if (-not (Test-Path $baseSettings)) { throw "Run the game once first so Userdata\Settings.ini exists." }
$scenarioDir = Join-Path $Repo "Userdata\RenderTest"
if (-not (Test-Path $scenarioDir)) { New-Item -ItemType Directory -Path $scenarioDir | Out-Null }

function Write-Scenario([string]$Name, [hashtable]$Overrides, [string[]]$GlobalScripts) {
	$lines = Get-Content $baseSettings | Where-Object { $_ -notmatch '^\s*EnableGlobalScript\s*=' }
	$set = @{}
	$lines = foreach ($line in $lines) {
		if ($line -match '^(\s*)(\w+)\s*=') {
			$key = $Matches[2]
			if ($Overrides.ContainsKey($key)) { $set[$key] = $true; "$($Matches[1])$key = $($Overrides[$key])"; continue }
		}
		$line
	}
	foreach ($key in $Overrides.Keys) { if (-not $set.ContainsKey($key)) { $lines += "`t$key = $($Overrides[$key])" } }
	foreach ($script in $GlobalScripts) { $lines += "`tEnableGlobalScript = RenderTest.rte/$script" }
	Set-Content -Path (Join-Path $scenarioDir "$Name.ini") -Value $lines -Encoding ascii
	"wrote Userdata\RenderTest\$Name.ini"
}

$bunker = @{ LaunchIntoActivity = 1; SkipIntro = 1; DefaultActivityType = "GAScripted"; DefaultActivityName = "Test Activity"; DefaultSceneName = "Zekarra Mining Outpost" }
$caves = $bunker.Clone(); $caves.DefaultSceneName = "Dvorak Caves"

Write-Scenario "Bunker" $bunker @("Render Test FX")
Write-Scenario "BunkerNight" ($bunker + @{ TimeOfDay = 23 }) @("Render Test FX")
Write-Scenario "BunkerDusk" ($bunker + @{ TimeOfDay = 18.5 }) @("Render Test FX")
Write-Scenario "BunkerRain" ($bunker + @{ TimeOfDay = 23; WeatherType = 1; WeatherIntensity = 0.8 }) @("Render Test FX")
Write-Scenario "BunkerFog" $bunker @("Render Test FX", "Render Test Fog")
Write-Scenario "Caves" $caves @("Render Test Camera Tour", "Render Test FX")
Write-Scenario "CavesNightLights" ($caves + @{ TimeOfDay = 23 }) @("Render Test Camera Tour", "Render Test Lights")
Write-Scenario "Primitives" $bunker @("Render Test Primitives")
Write-Scenario "Classic" ($bunker + @{ LightingEnabled = 0; BloomEnabled = 0; DistortionEnabled = 0; ScorchMarks = 0; Embers = 0 }) @("Render Test FX")
$split = $bunker.Clone(); $split.DefaultActivityName = "Render Test Split Screen"; $split.TimeOfDay = 23
Write-Scenario "SplitScreen" $split @("Render Test FX")
Write-Scenario "BunkerPerf" ($bunker + @{ TimeOfDay = 23 }) @("Render Test FX", "Render Test Perf")
Write-Scenario "Menu" @{ LaunchIntoActivity = 0; SkipIntro = 1 } @()
