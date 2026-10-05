param([string]$List = "", [switch]$Force)
# Downloads the mods named in modlist.txt from mod.io into ModDownloads\ and unpacks each into Mods\.
# A mod already downloaded is left alone unless -Force. Mods are other people's work: both folders are ignored by git.
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $List) { $List = Join-Path $PSScriptRoot "modlist.txt" }
$downloads = Join-Path $repo "ModDownloads"
$mods = Join-Path $repo "Mods"
foreach ($dir in $downloads, $mods) { if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null } }

foreach ($line in Get-Content $List) {
	if ($line -match '^\s*#' -or -not $line.Trim()) { continue }
	$name, $file, $url = $line.Split('|') | ForEach-Object { $_.Trim() }
	$zip = Join-Path $downloads $file
	if ($Force -or -not (Test-Path $zip)) {
		& curl.exe -L -s -S --fail -o $zip $url
		if ($LASTEXITCODE -ne 0) { "FAILED  $name (curl $LASTEXITCODE)"; Remove-Item $zip -ErrorAction SilentlyContinue; continue }
	}
	# Unpack to a scratch folder, then move every *.rte folder found (at any depth) into Mods\.
	$scratch = Join-Path $downloads "_unpack"
	if (Test-Path $scratch) { Remove-Item -Recurse -Force $scratch }
	try { Expand-Archive -Path $zip -DestinationPath $scratch -Force } catch { "FAILED  $name (not a zip: $($_.Exception.Message))"; continue }
	$found = Get-ChildItem $scratch -Recurse -Directory -Filter "*.rte" | Where-Object { $_.FullName.Substring($scratch.Length) -notmatch '\.rte\\.*\.rte$' }
	if (-not $found) { "FAILED  $name (no .rte folder inside)"; continue }
	foreach ($module in $found) {
		$target = Join-Path $mods $module.Name
		if (Test-Path $target) { Remove-Item -Recurse -Force $target }
		Move-Item $module.FullName $target
		"{0,-40} {1,6:N1} MB  -> Mods\{2}" -f $name, ((Get-Item $zip).Length / 1MB), $module.Name
	}
	Remove-Item -Recurse -Force $scratch
}
