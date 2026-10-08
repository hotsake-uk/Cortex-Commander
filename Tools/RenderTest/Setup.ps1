param([string]$Repo = "")
# Installs the RenderTest.rte dev mod into Mods/ and writes the scenario settings files used by Capture.ps1 into Userdata/RenderTest/.
# Each scenario is the player's Settings.ini with a few keys overridden, so it starts straight into a known scene with known effects.
. (Join-Path $PSScriptRoot "Common.ps1")
if (-not $Repo) { $Repo = $RepoRoot }

$modsDir = Join-Path $Repo $(if ($env:CCCP_MODS_DIR) { $env:CCCP_MODS_DIR } else { "Mods" })
if (-not (Test-Path $modsDir)) { New-Item -ItemType Directory -Path $modsDir | Out-Null }
Copy-Item -Recurse -Force (Join-Path $PSScriptRoot "RenderTest.rte") $modsDir

$baseSettings = Join-Path $Repo "Userdata\Settings.ini"
if (-not (Test-Path $baseSettings)) { throw "Run the game once first so Userdata\Settings.ini exists." }
$scenarioDir = Join-Path $Repo "Userdata\RenderTest"
if (-not (Test-Path $scenarioDir)) { New-Item -ItemType Directory -Path $scenarioDir | Out-Null }

function Write-Scenario([string]$Name, [hashtable]$Overrides, [string[]]$GlobalScripts, [switch]$DefaultLighting) {
	# Captures are compared against each other, so always use the native 960x540 window whatever the player's own settings are.
	# (A scenario may ask for another size, for things that only show at a size: its own keys win.)
	$defaults = @{ ResolutionX = 960; ResolutionY = 540; ResolutionMultiplier = 1; Fullscreen = 0 }
	foreach ($key in $Overrides.Keys) { $defaults[$key] = $Overrides[$key] }
	$Overrides = $defaults
	# CCCP_NO_MANTLE=1: mantling off, for telling a mantle fault from others.
	if ($env:CCCP_NO_MANTLE) { $Overrides.EnableMantling = 0 }
	# CCCP_BACKGROUND: muted, so a run in the background isn't heard either.
	if ($env:CCCP_BACKGROUND) { $Overrides.MuteMaster = 1 }
	$lines = Get-Content $baseSettings | Where-Object { $_ -notmatch '^\s*EnableGlobalScript\s*=' }
	if ($DefaultLighting) {
		# Cloud shadows and the clouds in the sky drift, so a golden scene would differ from run to run with them on.
		if (-not $Overrides.ContainsKey('CloudShadows')) { $Overrides.CloudShadows = 0 }
		if (-not $Overrides.ContainsKey('CloudLayer')) { $Overrides.CloudLayer = 0 }
		# Golden scenarios use the built-in lighting defaults, so the player's own tweaks (time of day, quality, weather) can't change the baselines.
		# The grade's answers to blasts, wounds and fire depend on the run, so they're off too.
		if (-not $Overrides.ContainsKey('EventLooks')) { $Overrides.EventLooks = 0 }
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
Write-Scenario "Sky1" ($play + @{ TimeOfDay = 1; WeatherType = 0; DayLengthMinutes = 0 }) @()
Write-Scenario "Sky5_8" ($play + @{ TimeOfDay = 5.8; WeatherType = 0; DayLengthMinutes = 0 }) @()
Write-Scenario "Sky6_5" ($play + @{ TimeOfDay = 6.5; WeatherType = 0; DayLengthMinutes = 0 }) @()
Write-Scenario "Sky12" ($play + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0 }) @()
Write-Scenario "Sky17_5" ($play + @{ TimeOfDay = 17.5; WeatherType = 0; DayLengthMinutes = 0 }) @()
Write-Scenario "Sky18_3" ($play + @{ TimeOfDay = 18.3; WeatherType = 0; DayLengthMinutes = 0 }) @()
Write-Scenario "Sky21" ($play + @{ TimeOfDay = 21; WeatherType = 0; DayLengthMinutes = 0 }) @()
Write-Scenario "Sky23_5" ($play + @{ TimeOfDay = 23.5; WeatherType = 0; DayLengthMinutes = 0 }) @()
Write-Scenario "SkyStorm" ($play + @{ TimeOfDay = 12; WeatherType = 1; WeatherIntensity = 1; DayLengthMinutes = 0 }) @()
Write-Scenario "Stains" $play @("Render Test Stains")
Write-Scenario "Fire" $play @("Render Test Fire")
Write-Scenario "FireNight" ($play + @{ TimeOfDay = 22 }) @("Render Test Fire")
Write-Scenario "Collapse" $play @("Render Test Collapse")
Write-Scenario "Physics" ($play + @{ TimeOfDay = 12 }) @("Render Test Physics")
Write-Scenario "Liquids" $play @("Render Test Liquids")
Write-Scenario "LiquidsNight" ($play + @{ TimeOfDay = 22 }) @("Render Test Liquids")
Write-Scenario "FlareNight" ($play + @{ TimeOfDay = 23 }) @("Render Test Flare")
Write-Scenario "PlainSave" $play @("Render Test Save")
Write-Scenario "FireSave" ($play + @{ TimeOfDay = 22 }) @("Render Test Fire", "Render Test Save")
Write-Scenario "Grenades" $play @("Render Test Grenades")
Write-Scenario "GrenadesNoBlock" ($play + @{ SmokeBlocksSight = 0 }) @("Render Test Grenades")
Write-Scenario "LiquidWeapons" ($play + @{ TimeOfDay = 20 }) @("Render Test Liquid Weapons")
Write-Scenario "WeatherCalm" ($play + @{ WeatherType = 0; Wind = 0 }) @("Render Test Weather")
Write-Scenario "WeatherRain" ($play + @{ WeatherType = 1; WeatherIntensity = 0.9; Wind = 0 }) @("Render Test Weather")
Write-Scenario "WeatherSnow" ($play + @{ WeatherType = 2; WeatherIntensity = 1; Wind = 0 }) @("Render Test Weather")
Write-Scenario "WeatherWindRight" ($play + @{ WeatherType = 0; Wind = 150 }) @("Render Test Weather")
Write-Scenario "WeatherWindLeft" ($play + @{ WeatherType = 0; Wind = -150 }) @("Render Test Weather")
Write-Scenario "SoakFeatures" ($play + @{ TimeOfDay = 19; WeatherType = 1; WeatherIntensity = 0.7; Wind = 90 }) @("Render Test Grenades", "Render Test Liquid Weapons", "Render Test Weather")
$sandbox = @{ LaunchIntoActivity = 1; SkipIntro = 1; DefaultActivityType = "GAScripted"; DefaultActivityName = "Sandbox"; DefaultSceneName = "Ketanot Hills" }
Write-Scenario "Sandbox" $sandbox @()
Write-Scenario "SandboxBattle" $sandbox @("Render Test Sandbox")
Write-Scenario "FireUnits" $sandbox @("Render Test Fire Units")
Write-Scenario "SandboxArmies" $sandbox @("Render Test Sandbox Armies")
Write-Scenario "PauseAI" $sandbox @("Render Test Pause AI")
Write-Scenario "Swim" $sandbox @("Render Test Swim")
Write-Scenario "Cut" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0 }) @("Render Test Cut")
Write-Scenario "CutNoBuildings" ($bunker + @{ CollapseBuildings = 0 }) @("Render Test Blast")
Write-Scenario "Dunk" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0 }) @("Render Test Dunk")
Write-Scenario "Island" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0 }) @("Render Test Island")
Write-Scenario "Scrap" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0 }) @("Render Test Scrap")
Write-Scenario "ScrapOff" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; CollapseCrushPixels = 0 }) @("Render Test Scrap")
Write-Scenario "Dig" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0 }) @("Render Test Dig")
Write-Scenario "Strike" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0 }) @("Render Test Strike")
Write-Scenario "Crash" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0 }) @("Render Test Crash")
Write-Scenario "Pour" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Pour")
Write-Scenario "PourNight" ($sandbox + @{ TimeOfDay = 23.5; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Pour")
Write-Scenario "PourNoFoam" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; WaterFoam = 0 }) @("Render Test Pour")
Write-Scenario "Heap" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0 }) @("Render Test Heap")
Write-Scenario "Splash" ($sandbox + @{ TimeOfDay = 12 }) @("Render Test Splash")
Write-Scenario "SplashSnow" ($sandbox + @{ TimeOfDay = 12; WeatherType = 2; WeatherIntensity = 1 }) @("Render Test Splash")
Write-Scenario "ModFeatures" ($play + @{ TimeOfDay = 23; Headlamps = 0 }) @("Render Test Mod Features")
$sandboxDusk = $sandbox.Clone(); $sandboxDusk.TimeOfDay = 19.5; Write-Scenario "Zoom" $sandboxDusk @("Render Test Zoom", "Render Test Sandbox Armies")
Write-Scenario "ZoomSplit" ($split) @("Render Test Zoom", "Render Test FX")
$sandboxStrom = $sandbox.Clone(); $sandboxStrom.DefaultSceneName = "Stromatolites"; Write-Scenario "SandboxStress" $sandboxStrom @("Render Test Sandbox Stress")
Write-Scenario "SmokeNight" ($bunker + @{ TimeOfDay = 23 }) @("Render Test Smoke")
Write-Scenario "SmokeNightOff" ($bunker + @{ TimeOfDay = 23; SmokeScattering = 0 }) @("Render Test Smoke")

# Golden scenarios: calm, fixed shots compared against committed baselines by Golden.ps1. No explosions, default lighting.
Write-Scenario "GoldenNoon" $bunker @() -DefaultLighting
Write-Scenario "GoldenNight" ($bunker + @{ TimeOfDay = 23 }) @() -DefaultLighting
Write-Scenario "GoldenCaves" $caves @("Render Test Camera Tour") -DefaultLighting
Write-Scenario "GoldenClassic" ($bunker + @{ LightingEnabled = 0; BloomEnabled = 0; DistortionEnabled = 0; ScorchMarks = 0; Embers = 0; EffectsParticles = 0; Stains = 0; LivingWorld = 0; SmokeScattering = 0 }) @() -DefaultLighting
Write-Scenario "GoldenLightingOnly" ($bunker + @{ TimeOfDay = 19; LightingDebugView = 1 }) @() -DefaultLighting
Write-Scenario "GoldenInterior" ($tutorial + @{ TimeOfDay = 23 }) @() -DefaultLighting

Write-Scenario "BunkerAsh" ($bunker + @{ WeatherType = 3; WeatherIntensity = 0.9 }) @()
Write-Scenario "BunkerDust" ($bunker + @{ WeatherType = 4; WeatherIntensity = 0.9; Wind = 120 }) @()
Write-Scenario "Flood" $sandbox @("Render Test Flood")
Write-Scenario "Level" ($sandbox + @{ TerrainCollapse = 0; WeatherType = 0 }) @("Render Test Level") # The block of earth between the two pits would rightly fall into the tunnel.
# Plays itself with everything going at once, for SoakAuto.ps1: an auto battle with dropships, a flood, napalm and fuel barrels, and the camera zooming, in dusk rain.
$soakAuto = $sandbox.Clone(); $soakAuto.TimeOfDay = 19.5; $soakAuto.WeatherType = 1; $soakAuto.WeatherIntensity = 0.7; $soakAuto.Wind = 90
Write-Scenario "SoakAuto" $soakAuto @("Render Test Sandbox Armies", "Render Test Flood", "Render Test Fire Units", "Render Test Zoom")
# For performance runs with the CCCP_PERF_LOG environment variable set: blasts on the bunker and the ground, by night (many lights) and by day.
Write-Scenario "Blast" ($bunker + @{ TimeOfDay = 23 }) @("Render Test Blast")
Write-Scenario "RainCalm" ($bunker + @{ TimeOfDay = 12; WeatherType = 1; WeatherIntensity = 1; Wind = 0 }) @()
Write-Scenario "RainWindRight" ($bunker + @{ TimeOfDay = 12; WeatherType = 1; WeatherIntensity = 1; Wind = 380 }) @()
Write-Scenario "RainWindLeft" ($bunker + @{ TimeOfDay = 12; WeatherType = 1; WeatherIntensity = 1; Wind = -380 }) @()
Write-Scenario "BlastDay" $bunker @("Render Test Blast")
# Shadows: soldiers and a lamp in the tutorial bunker by night and by day, each with the shadow effects off for comparison, and the open hills late in the afternoon.
$noShadows = @{ UnitShadows = 0; SunShadows = 0; ContactShading = 0; CloudShadows = 0; SunDisc = 0 }
# (The tutorial activity doesn't run global scripts, so the quiet sandbox activity is used on its scene, with the script placing the camera.)
$shadowBunker = $sandbox.Clone(); $shadowBunker.DefaultSceneName = "Tutorial Bunker"; $shadowBunker.Headlamps = 0
Write-Scenario "ShadowsNight" ($shadowBunker + @{ TimeOfDay = 23 }) @("Render Test Shadows")
Write-Scenario "ShadowsNightOff" ($shadowBunker + @{ TimeOfDay = 23 } + $noShadows) @("Render Test Shadows")
Write-Scenario "ShadowsDay" ($shadowBunker + @{ TimeOfDay = 15.5 }) @("Render Test Shadows")
Write-Scenario "ShadowsDayOff" ($shadowBunker + @{ TimeOfDay = 15.5 } + $noShadows) @("Render Test Shadows")
Write-Scenario "ShadowsHills" ($play + @{ TimeOfDay = 16.5 }) @()
Write-Scenario "ShadowsHillsOff" ($play + @{ TimeOfDay = 16.5 } + $noShadows) @()
Write-Scenario "ShadowsNightMask" ($shadowBunker + @{ TimeOfDay = 23; LightingDebugView = 7 }) @("Render Test Shadows")
Write-Scenario "ShadowsDaySun" ($shadowBunker + @{ TimeOfDay = 15.5; LightingDebugView = 8 }) @("Render Test Shadows")
Write-Scenario "ShadowsNightLight" ($shadowBunker + @{ TimeOfDay = 23; LightingDebugView = 3 }) @("Render Test Shadows")
# The same with the lighting shown on plain grey, which is the clearest way to see shadows (the bunker walls are very dark).
Write-Scenario "ShadowsNightGrey" ($shadowBunker + @{ TimeOfDay = 23; LightingDebugView = 1 }) @("Render Test Shadows")
Write-Scenario "ShadowsNightGreyOff" ($shadowBunker + @{ TimeOfDay = 23; LightingDebugView = 1 } + $noShadows) @("Render Test Shadows")
Write-Scenario "ShadowsDayGrey" ($shadowBunker + @{ TimeOfDay = 15.5; LightingDebugView = 1 }) @("Render Test Shadows")
Write-Scenario "ShadowsDayGreyOff" ($shadowBunker + @{ TimeOfDay = 15.5; LightingDebugView = 1 } + $noShadows) @("Render Test Shadows")
Write-Scenario "GoldenShadows" ($shadowBunker + @{ TimeOfDay = 15.5; LightingDebugView = 1; ModernHUD = 0; CloudShadows = 0 }) @("Render Test Shadows") -DefaultLighting
# Materials: units of different makes with a lamp in the tutorial bunker, by night and by day, each with metal reflections and relief off for comparison.
$noMaterials = @{ LightingMetals = 0; LightingRelief = 0 }
Write-Scenario "MaterialsNight" ($shadowBunker + @{ TimeOfDay = 23 }) @("Render Test Materials")
Write-Scenario "MaterialsNightOff" ($shadowBunker + @{ TimeOfDay = 23 } + $noMaterials) @("Render Test Materials")
Write-Scenario "MaterialsDay" ($shadowBunker + @{ TimeOfDay = 14 }) @("Render Test Materials")
Write-Scenario "MaterialsDayOff" ($shadowBunker + @{ TimeOfDay = 14 } + $noMaterials) @("Render Test Materials")
Write-Scenario "MaterialsNormals" ($shadowBunker + @{ TimeOfDay = 23; LightingDebugView = 4 }) @("Render Test Materials")
# The ready-made looks, on the bunker with explosions: gritty and noir (set as the values ApplyLook gives).
Write-Scenario "LookGritty" ($bunker + @{ TimeOfDay = 17; PostSaturation = 0.78; GradeContrast = 1.16; GradeTemperature = -0.08; PostVignette = 0.32; FilmGrain = 0.22 }) @("Render Test FX")
Write-Scenario "LookNoir" ($bunker + @{ TimeOfDay = 17; PostSaturation = 0; GradeContrast = 1.28; PostVignette = 0.42; FilmGrain = 0.3 }) @("Render Test FX")
# The sky: the sun in clear weather, and cloud shadows drifting over the hills (with them off for comparison).
Write-Scenario "Sky" ($play + @{ TimeOfDay = 14; WeatherType = 0; Wind = 120 }) @()
Write-Scenario "SkyOff" ($play + @{ TimeOfDay = 14; WeatherType = 0; Wind = 120; CloudShadows = 0; CloudLayer = 0; SunDisc = 0 }) @()
Write-Scenario "SkyGrey" ($shadowBunker + @{ TimeOfDay = 14; WeatherType = 0; Wind = 200; CloudShadows = 1; LightingDebugView = 1; ModernHUD = 0 }) @("Render Test Shadows")
# Surface states: units that are wet, sooty, snowed on and glowing hot, by night with a lamp and by day, and with the states switched off.
Write-Scenario "SurfacesNight" ($shadowBunker + @{ TimeOfDay = 23 }) @("Render Test Surfaces")
Write-Scenario "SurfacesDay" ($shadowBunker + @{ TimeOfDay = 14 }) @("Render Test Surfaces")
Write-Scenario "SurfacesOff" ($shadowBunker + @{ TimeOfDay = 14; SurfaceStates = 0 }) @("Render Test Surfaces")
# A soldier standing out in heavy snow and rain: after half a minute he's snowed on or wet.
Write-Scenario "SurfacesSnow" ($play + @{ TimeOfDay = 14; WeatherType = 2; WeatherIntensity = 1 }) @()
Write-Scenario "SurfacesRain" ($play + @{ TimeOfDay = 14; WeatherType = 1; WeatherIntensity = 1 }) @()
# Lights of the scenery: the tutorial bunker by night with its own lamps, with the placeable fixtures added along the bottom corridor, and in the lighting-only view.
Write-Scenario "LightsFixtures" ($shadowBunker + @{ TimeOfDay = 23; WeatherType = 0 }) @("Render Test Light Fixtures")
Write-Scenario "LightsFixturesOnly" ($shadowBunker + @{ TimeOfDay = 23; WeatherType = 0; LightingDebugView = 1 }) @("Render Test Light Fixtures")
# The same with the interiors' own light turned well down, so the lamps do the lighting.
Write-Scenario "LightsDarkInterior" ($shadowBunker + @{ TimeOfDay = 23; WeatherType = 0; LightingAmbient = "0.12 0.12 0.15"; LightingForegroundAmbient = "0.12 0.12 0.14" }) @("Render Test Light Fixtures")
# Two of the bunker's lamps destroyed: they should be dark (compare with GoldenShadows, the same view with them whole).
Write-Scenario "LightsBroken" ($shadowBunker + @{ TimeOfDay = 23; WeatherType = 0; LightingDebugView = 1 }) @("Render Test Light Break")
# Every mod in turn in a short fight (see Tools\Mods\Test-Mods.ps1 -Play).
Write-Scenario "ModSweep" ($sandbox + @{ TimeOfDay = 13; WeatherType = 0 }) @("Render Test Mod Sweep")
Write-Scenario "SkyDeep" ($play + @{ TimeOfDay = 23.5; WeatherType = 0; DayLengthMinutes = 0; DeepNightDarkness = 0.95 }) @()
Write-Scenario "RaysEvening" ($bunker + @{ TimeOfDay = 16.5; WeatherType = 0; DayLengthMinutes = 0 }) @()
Write-Scenario "DunkNight" ($sandbox + @{ TimeOfDay = 22; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Dunk", "Render Test Flare")
Write-Scenario "UnderwaterLight" ($sandbox + @{ TimeOfDay = 23.5; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Underwater Light")
Write-Scenario "Effects" ($sandbox + @{ TimeOfDay = 23.5; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Effects")
Write-Scenario "EffectsDay" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Effects")
Write-Scenario "ZoomLights" ($sandbox + @{ TimeOfDay = 23.5; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Zoom", "Render Test Effects")
Write-Scenario "Colony" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Colony")
Write-Scenario "Build" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Build")
Write-Scenario "Move" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; ShowAIPaths = 1 }) @("Render Test Move") # The paths the units take are drawn.
Write-Scenario "Reorder" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Reorder")
Write-Scenario "Attack" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Attack")
Write-Scenario "AIGym" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; TerrainCollapse = 0 }) @("Render Test AI Gym") # The courses are floating concrete; a shot at one must not bring it down.
Write-Scenario "Backgrounds" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Backgrounds")
Write-Scenario "BackgroundsHD" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; ResolutionX = 2560; ResolutionY = 1440 }) @("Render Test Backgrounds")
Write-Scenario "BackgroundsFHD" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; ResolutionX = 1920; ResolutionY = 1080 }) @("Render Test Backgrounds")
Write-Scenario "AIBunker" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; ShowAIPaths = 1; TerrainCollapse = 0 }) @("Render Test AI Bunker")
# The same on real bunker maps from the BB+ mod (when it is installed).
$sandboxBywater = $sandbox.Clone(); $sandboxBywater.DefaultSceneName = "Bywater Barracks"; Write-Scenario "AIBywater" ($sandboxBywater + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; ShowAIPaths = 1 }) @("Render Test AI Bunker")
$sandboxHemslock = $sandbox.Clone(); $sandboxHemslock.DefaultSceneName = "Hemslock Hold"; Write-Scenario "AIHemslock" ($sandboxHemslock + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; ShowAIPaths = 1 }) @("Render Test AI Bunker")
Write-Scenario "AIOrders" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; ShowAIPaths = 1; TerrainCollapse = 0 }) @("Render Test AI Bunker", "Render Test Order") # Run with CCCP_BUNKER_ONLY=99; see OrderTest.lua.
$sandboxSpaceport = $sandbox.Clone(); $sandboxSpaceport.DefaultSceneName = "Neverending Spaceport"; Write-Scenario "OrdersSpaceport" ($sandboxSpaceport + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; ShowAIPaths = 1 }) @("Render Test Order") # With CCCP_ORDER_PROBE; see OrderTest.lua.
Write-Scenario "AIRecover" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; ShowAIPaths = 1; TerrainCollapse = 0 }) @("Render Test Recover Gym") # See RecoverGym.lua.
Write-Scenario "AIFlight" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; ShowAIPaths = 1; TerrainCollapse = 0 }) @("Render Test Flight Gym") # Concrete pads in the sky; see FlightGym.lua.
Write-Scenario "AICombat" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0; TerrainCollapse = 0 }) @("Render Test AI Combat")
$sandboxOutpost = $sandbox.Clone(); $sandboxOutpost.DefaultSceneName = "Zekarra Mining Outpost"; Write-Scenario "AIMap" ($sandboxOutpost + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test AI Map")
Write-Scenario "PlayMenu" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Play Menu")
Write-Scenario "Play" ($sandbox + @{ TimeOfDay = 12; WeatherType = 0; DayLengthMinutes = 0 }) @("Render Test Play")
