# Cortex Mod Studio's local server. Browsers won't let a web page read or save .ini files in a folder, so this serves the page
# and gives it the game folder: list a folder, read a file, save a file. It only answers this computer (localhost), and only
# requests carrying the token it puts in the page's address. It uses nothing but Windows PowerShell, so there is nothing to install.
#
#   Start-ModStudio.ps1 [-GameDir <game folder>] [-Port <port>] [-NoBrowser]
#
# Without -GameDir it uses the game folder this file sits in (<game>\Tools\ModStudio), or asks for one.
param(
	[string]$GameDir = '',
	[int]$Port = 0,
	[switch]$NoBrowser
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Test-GameDir([string]$dir) {
	return $dir -and (Test-Path -LiteralPath (Join-Path $dir 'Data\Base.rte') -PathType Container)
}

if (-not $GameDir) {
	$guess = [System.IO.Path]::GetFullPath((Join-Path $here '..\..'))
	if (Test-GameDir $guess) { $GameDir = $guess }
}
if (-not (Test-GameDir $GameDir)) {
	Add-Type -AssemblyName System.Windows.Forms
	$dialog = New-Object System.Windows.Forms.FolderBrowserDialog
	$dialog.Description = 'Pick the Cortex Command game folder (the one with Data inside)'
	if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 }
	$GameDir = $dialog.SelectedPath
	if (-not (Test-GameDir $GameDir)) {
		[System.Windows.Forms.MessageBox]::Show("That folder has no Data\Base.rte, so it isn't the game folder.", 'Mod Studio') | Out-Null
		exit 1
	}
}
$GameDir = [System.IO.Path]::GetFullPath($GameDir).TrimEnd('\')

if ($Port -eq 0) {
	$probe = New-Object System.Net.Sockets.TcpListener ([System.Net.IPAddress]::Loopback, 0)
	$probe.Start()
	$Port = $probe.LocalEndpoint.Port
	$probe.Stop()
}
$token = [guid]::NewGuid().ToString('N')
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://localhost:$Port/")
$listener.Start()
$url = "http://localhost:$Port/?token=$token"

$host.UI.RawUI.WindowTitle = 'Cortex Mod Studio'
Write-Host "Cortex Mod Studio is running for $GameDir"
Write-Host "Open: $url"
Write-Host 'Close this window to stop it.'
if (-not $NoBrowser) { Start-Process $url }

$contentTypes = @{
	'.html' = 'text/html; charset=utf-8'; '.js' = 'text/javascript; charset=utf-8'; '.css' = 'text/css; charset=utf-8'
	'.png' = 'image/png'; '.svg' = 'image/svg+xml'; '.ico' = 'image/x-icon'; '.json' = 'application/json'
}

# A path from the page, made absolute under $root; $null if it would leave $root.
function Resolve-Inside([string]$root, [string]$relative) {
	$relative = ($relative -replace '/', '\').TrimStart('\')
	$full = [System.IO.Path]::GetFullPath((Join-Path $root $relative)).TrimEnd('\')
	if ($full -ne $root -and -not $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { return $null }
	return $full
}

function Send-Bytes($response, [byte[]]$bytes, [string]$type) {
	$response.ContentType = $type
	$response.ContentLength64 = $bytes.Length
	$response.OutputStream.Write($bytes, 0, $bytes.Length)
}

function Send-Text($response, [int]$status, [string]$text, [string]$type = 'text/plain; charset=utf-8') {
	$response.StatusCode = $status
	Send-Bytes $response ([System.Text.Encoding]::UTF8.GetBytes($text)) $type
}

# System.Web's encoder escapes names and paths for the small JSON answers.
Add-Type -AssemblyName System.Web
function ConvertTo-JsonString([string]$s) {
	return [System.Web.HttpUtility]::JavaScriptStringEncode($s, $true)
}

function Invoke-Api($context, [string]$action) {
	$request = $context.Request
	$response = $context.Response
	if ($request.Headers['X-ModStudio-Token'] -ne $token) { Send-Text $response 403 'Wrong or missing token.'; return }
	$path = $request.QueryString['path']
	if ($null -eq $path) { $path = '' }
	$full = Resolve-Inside $GameDir $path
	if (-not $full) { Send-Text $response 400 'That path is outside the game folder.'; return }

	switch ($action) {
		'info' {
			Send-Text $response 200 ('{"name":' + (ConvertTo-JsonString (Split-Path -Leaf $GameDir)) + ',"path":' + (ConvertTo-JsonString $GameDir) + '}') 'application/json'
		}
		'inis' {
			# Every .ini in Data and Mods in one answer, so the page doesn't ask for two thousand files one by one. Each file is a
			# header line "path<TAB>byte count<LF>" followed by its bytes as they are on disk: no text handling here, which keeps it fast.
			$response.ContentType = 'application/octet-stream'
			$response.SendChunked = $true
			$out = $response.OutputStream
			$utf8 = New-Object System.Text.UTF8Encoding $false
			foreach ($top in 'Data', 'Mods') {
				$folder = Join-Path $GameDir $top
				if (-not [System.IO.Directory]::Exists($folder)) { continue }
				foreach ($file in [System.IO.Directory]::EnumerateFiles($folder, '*.ini', [System.IO.SearchOption]::AllDirectories)) {
					$bytes = [System.IO.File]::ReadAllBytes($file)
					$header = $utf8.GetBytes(($file.Substring($GameDir.Length + 1) -replace '\\', '/') + "`t" + $bytes.Length + "`n")
					$out.Write($header, 0, $header.Length)
					$out.Write($bytes, 0, $bytes.Length)
				}
			}
		}
		'list' {
			if (-not [System.IO.Directory]::Exists($full)) { Send-Text $response 200 '[]' 'application/json'; return }
			$items = foreach ($entry in [System.IO.Directory]::EnumerateFileSystemEntries($full)) {
				$kind = if ([System.IO.Directory]::Exists($entry)) { 'directory' } else { 'file' }
				'{"name":' + (ConvertTo-JsonString ([System.IO.Path]::GetFileName($entry))) + ',"kind":"' + $kind + '"}'
			}
			Send-Text $response 200 ('[' + (@($items) -join ',') + ']') 'application/json'
		}
		'file' {
			if ($request.HttpMethod -eq 'PUT') {
				$folder = [System.IO.Path]::GetDirectoryName($full)
				[void][System.IO.Directory]::CreateDirectory($folder)
				$memory = New-Object System.IO.MemoryStream
				$request.InputStream.CopyTo($memory)
				[System.IO.File]::WriteAllBytes($full, $memory.ToArray())
				Send-Text $response 200 'ok'
			} elseif ([System.IO.File]::Exists($full)) {
				$response.AddHeader('X-Last-Modified', [string]([DateTimeOffset][System.IO.File]::GetLastWriteTimeUtc($full)).ToUnixTimeMilliseconds())
				Send-Bytes $response ([System.IO.File]::ReadAllBytes($full)) 'application/octet-stream'
			} else {
				Send-Text $response 404 'Not found.'
			}
		}
		default { Send-Text $response 404 'Unknown request.' }
	}
}

function Send-Static($context) {
	$relative = [Uri]::UnescapeDataString($context.Request.Url.AbsolutePath)
	if ($relative -eq '/') { $relative = '/ModStudio.html' }
	$full = Resolve-Inside $here $relative
	if (-not $full -or -not [System.IO.File]::Exists($full)) { Send-Text $context.Response 404 'Not found.'; return }
	$type = $contentTypes[[System.IO.Path]::GetExtension($full).ToLowerInvariant()]
	if (-not $type) { $type = 'application/octet-stream' }
	$context.Response.AddHeader('Cache-Control', 'no-cache')
	Send-Bytes $context.Response ([System.IO.File]::ReadAllBytes($full)) $type
}

try {
	while ($listener.IsListening) {
		$context = $listener.GetContext()
		try {
			$requestPath = $context.Request.Url.AbsolutePath
			if ($requestPath.StartsWith('/api/')) {
				Invoke-Api $context $requestPath.Substring(5)
			} else {
				Send-Static $context
			}
		} catch {
			try { Send-Text $context.Response 500 $_.Exception.Message } catch { }
		} finally {
			try { $context.Response.Close() } catch { }
		}
	}
} finally {
	$listener.Stop()
}
