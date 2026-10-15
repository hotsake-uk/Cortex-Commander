using System.IO.Compression;
using System.Net.Http;
using System.Text.RegularExpressions;

namespace CortexLauncher;

// Paste mod download links, and the launcher downloads each, unpacks it, puts every .rte folder in the mods folder (replacing an older copy of the
// same mod) and runs the mod check on just the mods it added, showing the same results window as Check mods.
// mod.io's download links look like https://g-508.modapi.io/v1/games/508/mods/<id>/files/<id>/download (the mod page's Download button, "copy link address").
// A mod's web page isn't a file, so those are turned away with a hint rather than guessed at.

partial class MainForm
{
	readonly TextBox modLinksBox = new() { Width = 420, PlaceholderText = "paste mod.io download link(s) here" };
	readonly Button addModsBtn = new() { Text = "Download && check", AutoSize = true };
	static readonly HttpClient http = new(new HttpClientHandler { AllowAutoRedirect = true }) { Timeout = TimeSpan.FromMinutes(15) };

	void SetUpModDownload()
	{
		addModsBtn.Click += async (_, _) => await AddModsFromLinks();
		modLinksBox.KeyDown += async (_, e) => { if (e.KeyCode == Keys.Enter) { e.SuppressKeyPress = true; await AddModsFromLinks(); } };
	}

	// The folder new mods go into: the Mods folder box (its parent if it names a single .rte), or a folder of the launcher's own when none is set.
	string ModsTargetDir()
	{
		var m = modsBox.Text.Trim();
		if (m == "")
		{
			m = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "CortexLauncher", "Mods");
			modsBox.Text = m;
		}
		else if (m.EndsWith(".rte", StringComparison.OrdinalIgnoreCase) && Directory.Exists(m) && Directory.GetDirectories(m, "*.rte").Length == 0)
			m = Path.GetDirectoryName(m) ?? m;
		Directory.CreateDirectory(m);
		return m;
	}

	async Task AddModsFromLinks()
	{
		if (busy) return;
		var links = Regex.Matches(modLinksBox.Text, "https?://[^\\s\"'<>]+").Select(m => m.Value).Distinct().ToList();
		if (links.Count == 0) { Append("Paste one or more mod download links first."); return; }
		SaveSettings();
		cts = new CancellationTokenSource();
		var ct = cts.Token;
		var added = new List<string>();
		SetBusy(true, "Downloading mods...");
		try
		{
			var modsDir = ModsTargetDir();
			var scratchRoot = Path.Combine(modsDir, "_incoming");
			foreach (var url in links)
			{
				if (ct.IsCancellationRequested) break;
				if (Uri.TryCreate(url, UriKind.Absolute, out var u) && u.Host.EndsWith("mod.io", StringComparison.OrdinalIgnoreCase) && !u.Host.Contains("modapi", StringComparison.OrdinalIgnoreCase))
				{
					Append($"{url}\n  That is a mod's web page, not a download. On the page, right-click Download and copy the link address (https://g-508.modapi.io/.../download), then paste that.");
					continue;
				}
				var work = Path.Combine(scratchRoot, Guid.NewGuid().ToString("N"));
				Directory.CreateDirectory(work);
				try { added.AddRange(await DownloadOneMod(url, work, modsDir, ct)); }
				catch (OperationCanceledException) { Append("Cancelled."); break; }
				catch (Exception ex) { Append($"{url}\n  Failed: {ex.Message}"); }
				finally { try { Directory.Delete(work, true); } catch { } }
			}
			try { if (Directory.Exists(scratchRoot) && !Directory.EnumerateFileSystemEntries(scratchRoot).Any()) Directory.Delete(scratchRoot); } catch { }
			if (added.Count > 0) modLinksBox.Text = "";
		}
		finally { SetBusy(false, currentRef); }
		if (added.Count == 0) { Append("No mods were added."); return; }
		Append($"Added {added.Count} mod(s): " + string.Join(", ", added.Select(Path.GetFileName)));
		settings.LastModCheckPassed = ""; // the mods folder changed
		await CheckAddedMods(added);
	}

	// Downloads one link, checks it is a zip, and moves every .rte folder in it (not ones nested inside another .rte) into the mods folder.
	async Task<List<string>> DownloadOneMod(string url, string work, string modsDir, CancellationToken ct)
	{
		var zip = Path.Combine(work, "download.zip");
		status.Text = "Downloading...";
		Append($"Downloading {url}");
		using (var resp = await http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, ct))
		{
			if (!resp.IsSuccessStatusCode) throw new Exception($"the server answered {(int)resp.StatusCode} {resp.ReasonPhrase} (links from mod.io can expire; copy a fresh one)");
			await using var src = await resp.Content.ReadAsStreamAsync(ct);
			await using var dst = File.Create(zip);
			await src.CopyToAsync(dst, ct);
		}
		Append($"  {new FileInfo(zip).Length / 1048576.0:0.0} MB downloaded, unpacking");
		var unpacked = Path.Combine(work, "files");
		try { ZipFile.ExtractToDirectory(zip, unpacked); }
		catch (InvalidDataException) { throw new Exception("that isn't a zip file (an expired or page link gives a web page instead)"); }
		var found = Directory.GetDirectories(unpacked, "*.rte", SearchOption.AllDirectories)
			.Where(d => !Path.GetRelativePath(unpacked, d).Split(Path.DirectorySeparatorChar).SkipLast(1).Any(p => p.EndsWith(".rte", StringComparison.OrdinalIgnoreCase)))
			.ToList();
		if (found.Count == 0) throw new Exception("no .rte mod folder inside the download");
		var installed = new List<string>();
		foreach (var d in found)
		{
			var target = Path.Combine(modsDir, Path.GetFileName(d));
			bool replaced = Directory.Exists(target);
			if (replaced) Directory.Delete(target, true);
			Directory.Move(d, target); // scratch sits inside the mods folder, so this is a rename, not a copy
			Append(replaced ? $"  Replaced {Path.GetFileName(d)}" : $"  Installed {Path.GetFileName(d)}");
			installed.Add(target);
		}
		return installed;
	}

	// The mod check on just the added mods, against the selected version, shown in the usual results window.
	async Task CheckAddedMods(List<string> mods)
	{
		cts = new CancellationTokenSource();
		SetBusy(true, "Checking mods...");
		string gameDir;
		ModCheckRun? result;
		try
		{
			gameDir = await CheckGameDir(commitList.SelectedItems.Count > 0 ? (CommitInfo)commitList.SelectedItems[0].Tag! : null);
			result = await RunModCheck(gameDir, false, cts.Token, mods);
		}
		finally { SetBusy(false, currentRef); }
		if (result == null) return;
		Append(Summary(result));
		using var form = new ModCheckForm(this, result, gameDir, false);
		form.ShowDialog(this);
	}
}
