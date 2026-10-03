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

function Write-Scenario([string]$Name, [hashtable]$Overrides, [string[]]$GlobalScripts, [switch]$DefaultLighting) {
	# Captures are compared against each other, so always use the native 960x540 window whatever the player's own settings are.
	$Overrides = @{ ResolutionMultiplier = 1; Fullscreen = 0 } + $Overrides
	$lines = Get-Content $baseSettings | Where-Object { $_ -notmatch '^\s*EnableGlobalScript\s*=' }
	if ($DefaultLighting) {
		# Golden scenarios use the built-in lighting defaults, so the player's own tweaks (time of day, quality, weather) can't change the baselines.
		$inLighting = $false
		$lines = foreach ($line in $lines) {
			if ($line -match '^// Lighting and Post-Processing') { $inLighting = $true; continue }
			if ($inLighting -and $line -match '^////') { $inLighting = $false }
			if (-not $inLighting) { $line }
		}
	}
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
Write-Scenario "Classic" ($bunker + @{ LightingEnabled = 0; BloomEnabled = 0; DistortionEnabled = 0; ScorchMarks = 0; Embers = 0; EffectsParticles = 0; Stains = 0; LivingWorld = 0; SmokeScattering = 0 }) @("Render Test FX")
$split = $bunker.Clone(); $split.DefaultActivityName = "Render Test Split Screen"; $split.TimeOfDay = 23
Write-Scenario "SplitScreen" $split @("Render Test FX")
Write-Scenario "BunkerPerf" ($bunker + @{ TimeOfDay = 23 }) @("Render Test FX", "Render Test Perf")
$play = @{ LaunchIntoActivity = 1; SkipIntro = 1; DefaultActivityType = "GAScripted"; DefaultActivityName = "One-Man Army"; DefaultSceneName = "Ketanot Hills" }
Write-Scenario "Play" $play @()
Write-Scenario "PlayDusk" ($play + @{ TimeOfDay = 19 }) @()
Write-Scenario "PlayNight" ($play + @{ TimeOfDay = 23 }) @()
Write-Scenario "PlayStorm" ($play + @{ TimeOfDay = 21; WeatherType = 1; WeatherIntensity = 0.95 }) @()
$tutorial = @{ LaunchIntoActivity = 1; SkipIntro = 1; DefaultActivityType = "GATutorial"; DefaultActivityName = "Tutorial Mission"; DefaultSceneName = "Tutorial Bunker" }
Write-Scenario "TutorialDusk" ($tutorial + @{ TimeOfDay = 19 }) @()
Write-Scenario "TutorialNight" ($tutorial + @{ TimeOfDay = 23 }) @()
Write-Scenario "Menu" @{ LaunchIntoActivity = 0; SkipIntro = 1 } @()
Write-Scenario "Stains" $play @("Render Test Stains")
Write-Scenario "Fire" $play @("Render Test Fire")
Write-Scenario "FireNight" ($play + @{ TimeOfDay = 22 }) @("Render Test Fire")
Write-Scenario "Collapse" $play @("Render Test Collapse")
Write-Scenario "Liquids" $play @("Render Test Liquids")
Write-Scenario "LiquidsNight" ($play + @{ TimeOfDay = 22 }) @("Render Test Liquids")
Write-Scenario "FlareNight" ($play + @{ TimeOfDay = 23 }) @("Render Test Flare")
Write-Scenario "PlainSave" $play @("Render Test Save")
Write-Scenario "FireSave" ($play + @{ TimeOfDay = 22 }) @("Render Test Fire", "Render Test Save")
Write-Scenario "SmokeNight" ($bunker + @{ TimeOfDay = 23 }) @("Render Test Smoke")
Write-Scenario "SmokeNightOff" ($bunker + @{ TimeOfDay = 23; SmokeScattering = 0 }) @("Render Test Smoke")

# Golden scenarios: calm, fixed shots compared against committed baselines by Golden.ps1. No explosions, default lighting.
Write-Scenario "GoldenNoon" $bunker @() -DefaultLighting
Write-Scenario "GoldenNight" ($bunker + @{ TimeOfDay = 23 }) @() -DefaultLighting
Write-Scenario "GoldenCaves" $caves @("Render Test Camera Tour") -DefaultLighting
Write-Scenario "GoldenClassic" ($bunker + @{ LightingEnabled = 0; BloomEnabled = 0; DistortionEnabled = 0; ScorchMarks = 0; Embers = 0; EffectsParticles = 0; Stains = 0; LivingWorld = 0; SmokeScattering = 0 }) @() -DefaultLighting
Write-Scenario "GoldenLightingOnly" ($bunker + @{ TimeOfDay = 19; LightingDebugView = 1 }) @() -DefaultLighting
Write-Scenario "GoldenInterior" ($tutorial + @{ TimeOfDay = 23 }) @() -DefaultLighting
