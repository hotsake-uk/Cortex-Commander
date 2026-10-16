# Cortex Mod Studio's local server. Browsers won't let a web page read or save .ini files in a folder, so this serves the page
# and gives it the game folder: list a folder, read a file, save a file. It only answers this computer (localhost), and only
# requests carrying the token it puts in the page's address. It uses nothing but Windows PowerShell, so there is nothing to install.
#
#   Start-ModStudio.ps1 [-GameDir <game folder>] [-Port <port>] [-NoBrowser] [-NoBridge]
#
# Without -GameDir it uses the game folder this file sits in (<game>\Tools\ModStudio), or asks for one.
#
# It is also a bridge for editing in your own editor: it watches Data and Mods, and when a .lua or .ini is saved by anything but Studio,
# it asks a game started with -modstudio to reload it (through ModStudio/Scripts.ini) and tells an open Studio page, which reads it again.
# -NoBrowser runs it without opening the page (Mod Studio Bridge.bat); -NoBridge turns the watching off.
param(
	[string]$GameDir = '',
	[int]$Port = 0,
	[switch]$NoBrowser,
	[switch]$NoBridge
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

# The file bridge. FileSystemWatcher calls back on its own threads, so this is C#: it keeps working while the loop below waits for requests,
# and with no page open at all.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading;

public class StudioWatcher {
	private readonly string root;
	private readonly object gate = new object();
	private readonly List<FileSystemWatcher> watchers = new List<FileSystemWatcher>();
	private readonly List<KeyValuePair<long, string>> log = new List<KeyValuePair<long, string>>();
	private readonly Dictionary<string, DateTime> ours = new Dictionary<string, DateTime>(StringComparer.OrdinalIgnoreCase);
	private readonly Dictionary<string, bool> pending = new Dictionary<string, bool>(StringComparer.OrdinalIgnoreCase);
	private readonly Timer timer;
	private long seq;
	private static readonly string[] Kinds = { ".lua", ".ini", ".png", ".bmp", ".wav", ".ogg", ".flac" };

	public StudioWatcher(string gameDir) {
		root = gameDir;
		timer = new Timer(Flush);
		foreach (string top in new[] { "Data", "Mods" }) {
			string folder = Path.Combine(root, top);
			if (!Directory.Exists(folder)) { continue; }
			FileSystemWatcher watcher = new FileSystemWatcher(folder);
			watcher.IncludeSubdirectories = true;
			watcher.InternalBufferSize = 65536;
			watcher.NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size;
			watcher.Changed += OnChanged;
			watcher.Created += OnChanged;
			watcher.Deleted += OnChanged;
			watcher.Renamed += OnRenamed;
			watcher.EnableRaisingEvents = true;
			watchers.Add(watcher);
		}
	}

	// Studio's own saves: the page already asks the game to reload those, so the bridge leaves them alone for a few seconds.
	public void Wrote(string full) {
		lock (gate) { ours[full] = DateTime.UtcNow; }
	}

	// The changes after number `since`, one "disk path<TAB>changed|deleted" line each, under a first line with the latest number.
	// A negative `since` gives just the number, for a page that has only now opened.
	public string Since(long since) {
		StringBuilder text = new StringBuilder();
		lock (gate) {
			text.Append(seq).Append('\n');
			if (since < 0) { return text.ToString(); }
			foreach (KeyValuePair<long, string> entry in log) {
				if (entry.Key > since) { text.Append(entry.Value).Append('\n'); }
			}
		}
		return text.ToString();
	}

	private void OnRenamed(object sender, RenamedEventArgs e) {
		Note(e.OldFullPath, true);
		Note(e.FullPath, false);
	}

	private void OnChanged(object sender, FileSystemEventArgs e) {
		Note(e.FullPath, e.ChangeType == WatcherChangeTypes.Deleted);
	}

	private void Note(string full, bool deleted) {
		string extension = Path.GetExtension(full).ToLowerInvariant();
		if (Array.IndexOf(Kinds, extension) < 0) { return; }
		if (!deleted && !File.Exists(full)) { return; }
		lock (gate) {
			DateTime when;
			if (ours.TryGetValue(full, out when)) {
				if ((DateTime.UtcNow - when).TotalSeconds < 3) { return; }
				ours.Remove(full);
			}
			pending[full] = deleted;
			timer.Change(300, Timeout.Infinite);
		}
	}

	// A save fires several events (and editors save through temporary files), so changes are gathered for 300 ms and sent as one.
	private void Flush(object state) {
		List<string> scripts = new List<string>();
		List<string> inis = new List<string>();
		lock (gate) {
			string request = Path.Combine(root, "ModStudio\\Scripts.ini");
			if (File.Exists(request) && (DateTime.UtcNow - File.GetLastWriteTimeUtc(request)).TotalMilliseconds < 400) {
				// The page just wrote a request; the game looks a few times a second, so this one waits rather than replacing it.
				timer.Change(400, Timeout.Infinite);
				return;
			}
			foreach (KeyValuePair<string, bool> change in pending) {
				string disk = change.Key.Substring(root.Length + 1).Replace('\\', '/');
				log.Add(new KeyValuePair<long, string>(++seq, disk + "\t" + (change.Value ? "deleted" : "changed")));
				Console.WriteLine(DateTime.Now.ToString("HH:mm:ss") + "  " + (change.Value ? "deleted " : "saved   ") + disk);
				if (change.Value) { continue; }
				// The game's paths start at the module: "Mods/My.rte/Unit.ini" is "My.rte/Unit.ini".
				string modulePath = disk.Substring(disk.IndexOf('/') + 1);
				string extension = Path.GetExtension(disk).ToLowerInvariant();
				if (extension == ".lua") {
					scripts.Add(modulePath);
				} else if (extension == ".ini" && !modulePath.EndsWith("/Index.ini", StringComparison.OrdinalIgnoreCase)) {
					inis.Add(modulePath);
				}
			}
			pending.Clear();
			if (log.Count > 2000) { log.RemoveRange(0, log.Count - 2000); }
			if (scripts.Count == 0 && inis.Count == 0) { return; }
			StringBuilder text = new StringBuilder();
			text.Append("// Written by Cortex Mod Studio's file bridge for a game started with -modstudio.\r\n");
			text.Append("Stamp = ").Append(DateTime.UtcNow.Ticks).Append("\r\n");
			foreach (string file in inis) { text.Append("ReloadFile = ").Append(file).Append("\r\n"); }
			foreach (string file in scripts) { text.Append("ReloadScript = ").Append(file).Append("\r\n"); }
			try {
				Directory.CreateDirectory(Path.GetDirectoryName(request));
				File.WriteAllText(request, text.ToString());
				Console.WriteLine("          asked the game to reload " + (inis.Count + scripts.Count) + " file" + (inis.Count + scripts.Count == 1 ? "" : "s") + ".");
			} catch (Exception e) {
				Console.WriteLine("          couldn't ask the game to reload: " + e.Message);
			}
		}
	}
}
'@
$watcher = $null
if (-not $NoBridge) { $watcher = New-Object StudioWatcher $GameDir }

$host.UI.RawUI.WindowTitle = 'Cortex Mod Studio'
Write-Host "Cortex Mod Studio is running for $GameDir"
Write-Host "Open: $url"
if ($watcher) { Write-Host 'Watching Data and Mods: scripts and INI files you save in any editor reload in a game started from Studio.' }
Write-Host 'Close this window to stop it.'
if (-not $NoBrowser) { Start-Process $url }

# The page as Vite builds it (npm run build in this folder makes dist).
$pageRoot = Join-Path $here 'dist'
if (-not (Test-Path -LiteralPath (Join-Path $pageRoot 'ModStudio.html'))) {
	Write-Host "Mod Studio's page isn't built yet. With Node.js installed (nodejs.org), run  npm ci  then  npm run build  in $here." -ForegroundColor Yellow
}

$contentTypes = @{
	'.html' = 'text/html; charset=utf-8'; '.js' = 'text/javascript; charset=utf-8'; '.css' = 'text/css; charset=utf-8'
	'.png' = 'image/png'; '.svg' = 'image/svg+xml'; '.ico' = 'image/x-icon'; '.json' = 'application/json'
	'.ttf' = 'font/ttf'; '.lua' = 'text/plain; charset=utf-8'
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

# Studio's file history in a browser. (Cortex Studio, the app, keeps each mod's history with git instead: history.rs.) A copy of a
# file is kept every time Studio is about to overwrite or delete it, in %APPDATA%\CortexStudio\History\Copies\<path in Mods>: the
# first copy ever (the file before Studio first changed it) and the newest 5 others. The requests answer as the app's do.
$historyRoot = Join-Path $env:APPDATA 'CortexStudio\History\Copies'

# "Mods/X.rte/a.ini" -> the folder its copies go in; $null for anything outside Mods (the game's own Data is never written).
function Get-HistoryFolder([string]$key) {
	$parts = @($key -split '[\\/]')
	if ($parts.Count -lt 3 -or $parts[0] -ne 'Mods' -or ($parts | Where-Object { $_ -eq '' -or $_ -eq '.' -or $_ -eq '..' })) { return $null }
	return Join-Path $historyRoot (($parts | Select-Object -Skip 1) -join '\')
}

function Get-HistoryCopies([string]$folder) {
	if (-not [System.IO.Directory]::Exists($folder)) { return @() }
	return @([System.IO.Directory]::GetFiles($folder) | Sort-Object { [System.IO.Path]::GetFileName($_) } -Descending)
}

function Save-HistoryCopy([string]$diskPath, [string]$full) {
	try {
		$folder = Get-HistoryFolder ($diskPath -replace '\\', '/')
		if (-not $folder -or -not [System.IO.File]::Exists($full)) { return }
		$content = [System.IO.File]::ReadAllBytes($full)
		$existing = @(Get-HistoryCopies $folder)
		if ($existing.Count -gt 0) {
			$newest = [System.IO.File]::ReadAllBytes($existing[0])
			$sha = [System.Security.Cryptography.SHA1]::Create()
			if ([Convert]::ToBase64String($sha.ComputeHash($newest)) -eq [Convert]::ToBase64String($sha.ComputeHash($content))) { return }
		}
		[void][System.IO.Directory]::CreateDirectory($folder)
		$id = ([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()).ToString('D15') + $(if ($existing.Count -eq 0) { '-original' } else { '' })
		[System.IO.File]::WriteAllBytes((Join-Path $folder $id), $content)
		Get-HistoryCopies $folder | Where-Object { -not $_.EndsWith('-original') } | Select-Object -Skip 5 | ForEach-Object { [System.IO.File]::Delete($_) }
	} catch {}
}

function Get-HistoryJson([string]$key) {
	if ($key) {
		$folder = Get-HistoryFolder $key
		if (-not $folder) { return '[]' }
		$items = foreach ($file in @(Get-HistoryCopies $folder)) {
			$id = [System.IO.Path]::GetFileName($file)
			$original = $id.EndsWith('-original')
			$message = if ($original) { 'The file before Studio first changed it' } else { 'The file before a save' }
			'{"id":' + (ConvertTo-JsonString $id) + ',"time":' + [long]([long]($id -replace '-original$', '') / 1000) + ',"message":' + (ConvertTo-JsonString $message) + ',"original":' + $(if ($original) { 'true' } else { 'false' }) + ',"deleted":false}'
		}
		return '[' + ($items -join ',') + ']'
	}
	if (-not [System.IO.Directory]::Exists($historyRoot)) { return '[]' }
	$rows = foreach ($dir in [System.IO.Directory]::GetDirectories($historyRoot, '*', 'AllDirectories')) {
		$copies = @(Get-HistoryCopies $dir)
		if ($copies.Count -eq 0) { continue }
		[pscustomobject]@{
			key = 'Mods/' + ($dir.Substring($historyRoot.Length + 1) -replace '\\', '/')
			time = [long]([long]([System.IO.Path]::GetFileName($copies[0]) -replace '-original$', '') / 1000)
			count = $copies.Count
		}
	}
	$items = foreach ($row in @($rows | Sort-Object time -Descending)) { '{"key":' + (ConvertTo-JsonString $row.key) + ',"time":' + $row.time + ',"count":' + $row.count + '}' }
	return '[' + ($items -join ',') + ']'
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
		{ $_ -in 'inis', 'luas' } {
			# Every .ini (or .lua) in Data and Mods in one answer, so the page doesn't ask for two thousand files one by one. Each file is a
			# header line "path<TAB>byte count<LF>" followed by its bytes as they are on disk: no text handling here, which keeps it fast.
			$pattern = if ($action -eq 'luas') { '*.lua' } else { '*.ini' }
			$response.ContentType = 'application/octet-stream'
			$response.SendChunked = $true
			$out = $response.OutputStream
			$utf8 = New-Object System.Text.UTF8Encoding $false
			foreach ($top in 'Data', 'Mods') {
				$folder = Join-Path $GameDir $top
				if (-not [System.IO.Directory]::Exists($folder)) { continue }
				foreach ($file in [System.IO.Directory]::EnumerateFiles($folder, $pattern, [System.IO.SearchOption]::AllDirectories)) {
					$bytes = [System.IO.File]::ReadAllBytes($file)
					$header = $utf8.GetBytes(($file.Substring($GameDir.Length + 1) -replace '\\', '/') + "`t" + $bytes.Length + "`n")
					$out.Write($header, 0, $header.Length)
					$out.Write($bytes, 0, $bytes.Length)
				}
			}
		}
		'files' {
			# Every file in Data and Mods, one "path<TAB>size<LF>" line each: the Files list's type filter reads this once.
			$text = New-Object System.Text.StringBuilder
			foreach ($top in 'Data', 'Mods') {
				$folder = New-Object System.IO.DirectoryInfo (Join-Path $GameDir $top)
				if (-not $folder.Exists) { continue }
				foreach ($file in $folder.EnumerateFiles('*', [System.IO.SearchOption]::AllDirectories)) {
					[void]$text.Append($file.FullName.Substring($GameDir.Length + 1).Replace('\', '/')).Append("`t").Append($file.Length).Append("`n")
				}
			}
			Send-Text $response 200 $text.ToString()
		}
		'list' {
			if (-not [System.IO.Directory]::Exists($full)) { Send-Text $response 200 '[]' 'application/json'; return }
			$items = foreach ($entry in [System.IO.Directory]::EnumerateFileSystemEntries($full)) {
				$kind = if ([System.IO.Directory]::Exists($entry)) { 'directory' } else { 'file' }
				'{"name":' + (ConvertTo-JsonString ([System.IO.Path]::GetFileName($entry))) + ',"kind":"' + $kind + '"}'
			}
			Send-Text $response 200 ('[' + (@($items) -join ',') + ']') 'application/json'
		}
		'tree' {
			# Every file under a folder, as paths from that folder with their sizes: for packaging a mod and the project tree's search.
			if (-not [System.IO.Directory]::Exists($full)) { Send-Text $response 200 '[]' 'application/json'; return }
			$items = foreach ($file in [System.IO.Directory]::EnumerateFiles($full, '*', [System.IO.SearchOption]::AllDirectories)) {
				'{"path":' + (ConvertTo-JsonString ($file.Substring($full.Length + 1) -replace '\\', '/')) + ',"size":' + (New-Object System.IO.FileInfo $file).Length + '}'
			}
			Send-Text $response 200 ('[' + (@($items) -join ',') + ']') 'application/json'
		}
		'tail' {
			# The end of a file the game keeps adding to (ModStudio/Console.log), from a byte offset. X-Size is the file's whole length, so the
			# page can tell when the game started over and emptied it.
			$from = [long]0
			[void][long]::TryParse([string]$request.QueryString['from'], [ref]$from)
			if (-not [System.IO.File]::Exists($full)) { $response.AddHeader('X-Size', '0'); Send-Bytes $response ([byte[]]@()) 'application/octet-stream'; return }
			$stream = New-Object System.IO.FileStream($full, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, ([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete))
			try {
				$size = $stream.Length
				if ($from -gt $size -or $from -lt 0) { $from = 0 }
				$count = [int][Math]::Min($size - $from, 4MB)
				$bytes = New-Object byte[] $count
				[void]$stream.Seek($from, [System.IO.SeekOrigin]::Begin)
				$read = 0
				while ($read -lt $count) {
					$n = $stream.Read($bytes, $read, $count - $read)
					if ($n -le 0) { break }
					$read += $n
				}
				$response.AddHeader('X-Size', [string]$size)
				$response.AddHeader('X-From', [string]$from)
				Send-Bytes $response $bytes 'application/octet-stream'
			} finally {
				$stream.Close()
			}
		}
		'delete' {
			# Only files in Mods: the game's own files stay as they are.
			if ($request.HttpMethod -ne 'POST') { Send-Text $response 405 'Use POST.'; return }
			$mods = Join-Path $GameDir 'Mods'
			if (-not $full.StartsWith($mods + '\', [StringComparison]::OrdinalIgnoreCase)) { Send-Text $response 403 'Only files in Mods can be deleted.'; return }
			if ($watcher) { $watcher.Wrote($full) }
			Save-HistoryCopy $path $full
			if ([System.IO.File]::Exists($full)) { [System.IO.File]::Delete($full) }
			Send-Text $response 200 'ok'
		}
		'history' {
			# Without a key: every file Studio kept copies of. With one: that file's copies, newest first. With mods: the mods you edit
			# (a folder in History\Copies each).
			if ($request.QueryString['mods']) {
				$names = if ([System.IO.Directory]::Exists($historyRoot)) { @([System.IO.Directory]::GetDirectories($historyRoot) | ForEach-Object { ConvertTo-JsonString ([System.IO.Path]::GetFileName($_)) }) } else { @() }
				Send-Text $response 200 ('[' + ($names -join ',') + ']') 'application/json'
				return
			}
			Send-Text $response 200 (Get-HistoryJson ([string]$request.QueryString['key'])) 'application/json'
		}
		'history-start' {
			# "Edit this mod": lets Studio's designer change a mod it didn't make. Copies of each file are kept as it is changed.
			if ($path -notmatch '^Mods/([^/\\]+\.rte)$') { Send-Text $response 400 'Only a mod in Mods can be edited.'; return }
			[void][System.IO.Directory]::CreateDirectory((Join-Path $historyRoot $Matches[1]))
			Send-Text $response 200 'ok'
		}
		'history-file' {
			$id = [string]$request.QueryString['id']
			$folder = Get-HistoryFolder ([string]$request.QueryString['key'])
			if (-not $folder -or $id -match '[\\/]|\.\.' -or -not [System.IO.File]::Exists((Join-Path $folder $id))) { Send-Text $response 404 'No such copy.'; return }
			Send-Bytes $response ([System.IO.File]::ReadAllBytes((Join-Path $folder $id))) 'application/octet-stream'
		}
		'changes' {
			# Files saved outside Studio since the page last asked (see StudioWatcher). 404 when the bridge is off, so the page stops asking.
			if (-not $watcher) { Send-Text $response 404 'The file bridge is off.'; return }
			$since = [long]-1
			[void][long]::TryParse([string]$request.QueryString['since'], [ref]$since)
			Send-Text $response 200 $watcher.Since($since)
		}
		'file' {
			if ($request.HttpMethod -eq 'PUT') {
				$folder = [System.IO.Path]::GetDirectoryName($full)
				[void][System.IO.Directory]::CreateDirectory($folder)
				$memory = New-Object System.IO.MemoryStream
				$request.InputStream.CopyTo($memory)
				if ($watcher) { $watcher.Wrote($full) }
				Save-HistoryCopy $path $full
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
	$full = Resolve-Inside $pageRoot $relative
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
