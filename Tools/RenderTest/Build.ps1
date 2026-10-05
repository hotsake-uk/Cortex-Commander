param([string]$Config = "Debug Release", [string]$Repo = "")
# Builds RTEA.sln with MSBuild (found via vswhere) and prints any errors.
. (Join-Path $PSScriptRoot "Common.ps1")
if (-not $Repo) { $Repo = $RepoRoot }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild not found. Install Visual Studio 2022 with the C++ desktop workload." }
$log = Join-Path $OutputDir "build.log"
Push-Location $Repo
& $msbuild RTEA.sln /p:Configuration="$Config" /p:Platform=x64 /m /v:minimal /nologo "/clp:ErrorsOnly" *> $log
$code = $LASTEXITCODE
Pop-Location
"EXIT $code"
$errs = @(Select-String -Path $log -Pattern ": (fatal )?error" | ForEach-Object { $_.Line -replace ' \[.*\.vcxproj\]$', '' } | Select-Object -Unique)
"$($errs.Count) unique errors"
$errs | Select-Object -First 60
if ($code -ne 0) { exit 1 }
