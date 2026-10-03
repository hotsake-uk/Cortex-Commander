# Shared paths for the render test tools. Dot-source this.
$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$script:OutputDir = Join-Path $PSScriptRoot "Output"
if (-not (Test-Path $script:OutputDir)) {
	New-Item -ItemType Directory -Path $script:OutputDir | Out-Null
}
