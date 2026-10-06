param(
	[string[]]$Maps = @("Bywater Barracks"), # Scene names. Several run at once with -Run.
	[switch]$Run,            # Run every course of each map's gym and print the results, instead of opening the map to play in.
	[int]$Wait = 90,         # -Run: seconds to allow each map's courses (a unit that can't make it gives up at 60).
	[switch]$Trace,          # -Run: the movement script's AITRACE lines too (the first unit of each map is traced).
	[string]$Exe = ""        # The game to run; the Final build if it is there, else the debug build.
)
# The AI gym, on its own: opens a map in the sandbox with the Gym tab up, where courses are made by clicking a start and a goal and run
# with a timer (they are kept in Userdata\Gyms\<map>.txt). With -Run, the courses of every map named are run in the test harness, all the
# maps at the same time in their own game windows, and the GYM result lines of each are printed at the end (the full console logs are in
# Output\gym_<map>.txt).
#
# Settings for a gym go in Userdata\Gyms\<map>.settings.txt, one "Key = Value" a line as in Settings.ini (TerrainCollapse = 0, say): they
# are put into the settings the map is opened with, in play and under -Run, and the Gym tab shows and edits them.
$rt = $PSScriptRoot
$repo = Split-Path (Split-Path $rt -Parent) -Parent
Set-Location $repo
# (From the command line, several maps come as one string with commas in it.)
$Maps = @($Maps | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (-not $Exe) { $Exe = if (Test-Path (Join-Path $repo "Cortex Command.exe")) { "Cortex Command.exe" } else { "Cortex Command.debug.release.exe" } }
$baseSettings = Join-Path $repo "Userdata\Settings.ini"
if (-not (Test-Path $baseSettings)) { throw "Run the game once first so Userdata\Settings.ini exists." }
$modsDir = Join-Path $repo "Mods"
Copy-Item -Recurse -Force (Join-Path $rt "RenderTest.rte") $modsDir
New-Item -ItemType Directory -Force (Join-Path $repo "Userdata\RenderTest") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $repo "Userdata\Gyms") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $rt "Output") | Out-Null

function Write-GymSettings([string]$Map, [bool]$Harness) {
	# The player's own settings, with the map, the sandbox and the gym's own settings put in.
	$overrides = [ordered]@{ LaunchIntoActivity = 1; SkipIntro = 1; DefaultActivityType = "GAScripted"; DefaultActivityName = "Sandbox"; DefaultSceneName = $Map; ShowAIPaths = 1 }
	if ($Harness) { $overrides.ResolutionX = 960; $overrides.ResolutionY = 540; $overrides.ResolutionMultiplier = 1; $overrides.Fullscreen = 0 }
	$gymSettings = Join-Path $repo "Userdata\Gyms\$Map.settings.txt"
	if (Test-Path $gymSettings) {
		foreach ($line in Get-Content $gymSettings) {
			if ($line -match '^\s*(\w+)\s*=\s*(.*?)\s*$') { $overrides[$Matches[1]] = $Matches[2] }
		}
	}
	$lines = Get-Content $baseSettings | Where-Object { $_ -notmatch '^\s*EnableGlobalScript\s*=' }
	$set = @{}
	$lines = foreach ($line in $lines) {
		if ($line -match '^(\s*)(\w+)\s*=') {
			$key = $Matches[2]
			if ($overrides.Contains($key)) { $set[$key] = $true; "$($Matches[1])$key = $($overrides[$key])"; continue }
		}
		$line
	}
	foreach ($key in $overrides.Keys) { if (-not $set.ContainsKey($key)) { $lines += "`t$key = $($overrides[$key])" } }
	if ($Harness) { $lines += "`tEnableGlobalScript = RenderTest.rte/Render Test AI Bunker" }
	$safe = ($Map -replace '[^\w]', '_')
	$path = Join-Path $repo "Userdata\RenderTest\Gym_$safe.ini"
	Set-Content -Path $path -Value $lines -Encoding ascii
	return "Userdata/RenderTest/Gym_$safe.ini"
}

if (-not $Run) {
	# Open the first map to play in, with the Gym tab up.
	$map = $Maps[0]
	$env:CCCP_SETTINGSPATH = Write-GymSettings $map $false
	$env:CCCP_TEST_TAB = "Gym"
	Remove-Item Env:CCCP_HIDE_PANELS -ErrorAction SilentlyContinue
	Remove-Item Env:CCCP_CONSOLE_LOG -ErrorAction SilentlyContinue
	Start-Process -FilePath (Join-Path $repo $Exe) -WorkingDirectory $repo | Out-Null
	"opened $map in the sandbox with the Gym tab up ($Exe)"
	exit 0
}

# The harness: every map at once, each in its own game, until its courses are done or the time is up.
$runs = @()
foreach ($map in $Maps) {
	$safe = ($map -replace '[^\w]', '_')
	$log = Join-Path $rt "Output\gym_$safe.txt"
	Remove-Item $log -ErrorAction SilentlyContinue
	$env:CCCP_SETTINGSPATH = Write-GymSettings $map $true
	$env:CCCP_CONSOLE_LOG = $log
	$env:CCCP_NO_GAMEPAD = "1"
	$env:CCCP_HIDE_PANELS = "1"
	if ($Trace) { $env:CCCP_AI_LOG = "1" } else { Remove-Item Env:CCCP_AI_LOG -ErrorAction SilentlyContinue }
	Remove-Item Env:CCCP_TEST_TAB -ErrorAction SilentlyContinue
	$p = Start-Process -FilePath (Join-Path $repo $Exe) -WorkingDirectory $repo -PassThru
	$runs += [pscustomobject]@{ Map = $map; Log = $log; Process = $p; Done = $false }
	"started $map (pid $($p.Id))"
	Start-Sleep 2
}
$deadline = (Get-Date).AddSeconds($Wait + 30)
while ((Get-Date) -lt $deadline) {
	foreach ($job in $runs) {
		if ($job.Done) { continue }
		if ($job.Process.HasExited) { $job.Done = $true; continue }
		if ((Test-Path $job.Log) -and (Select-String -Path $job.Log -Pattern "GYM done|AIBUNKER done|AIBUNKER no courses" -Quiet)) { $job.Done = $true }
	}
	if (-not ($runs | Where-Object { -not $_.Done })) { break }
	Start-Sleep 2
}
foreach ($job in $runs) {
	if (-not $job.Process.HasExited) { Stop-Process -Id $job.Process.Id -Force; $job.Process.WaitForExit(10000) | Out-Null }
	"--- $($job.Map)"
	if (Test-Path $job.Log) {
		$pattern = if ($Trace) { "GYM |AIBUNKER|AITRACE" } else { "GYM |AIBUNKER (path for|.*: (arrived|died|GAVE UP)|done|running|no courses)" }
		Select-String -Path $job.Log -Pattern $pattern | ForEach-Object { $_.Line.Substring(0, [Math]::Min(220, $_.Line.Length)) }
	} else {
		"(no console log: the game didn't get far enough to write one)"
	}
}
