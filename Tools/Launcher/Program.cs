using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace CortexLauncher;

static class Program
{
	[STAThread]
	static void Main()
	{
		Application.EnableVisualStyles();
		Application.SetCompatibleTextRenderingDefault(false);
		Application.SetHighDpiMode(HighDpiMode.SystemAware);
		Application.Run(new MainForm());
	}
}

class Settings
{
	public string RepoPath { get; set; } = @"C:\Users\Liamn\Desktop\cortex\Cortex-Command-Community-Project";
	public string VersionsDir { get; set; } = "";
	public string Configuration { get; set; } = "Final";
	public string Remote { get; set; } = "origin";
	public string SettingsIni { get; set; } = "";
	public string ModsDir { get; set; } = "";
	public string LastRef { get; set; } = "";

	static string FilePath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "CortexLauncher", "settings.json");

	public static Settings Load()
	{
		try { return JsonSerializer.Deserialize<Settings>(File.ReadAllText(FilePath)) ?? new Settings(); }
		catch { return new Settings(); }
	}

	public void Save()
	{
		Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
		File.WriteAllText(FilePath, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
	}

	public string EffectiveVersionsDir => string.IsNullOrWhiteSpace(VersionsDir)
		? Path.Combine(Path.GetDirectoryName(RepoPath.TrimEnd('\\', '/')) ?? RepoPath, "CortexVersions")
		: VersionsDir;
}

record CommitInfo(string Sha, string Date, string Author, string Subject)
{
	public string Short => Sha[..10];
}

class MainForm : Form
{
	readonly Settings settings = Settings.Load();
	readonly TextBox repoBox = new() { Width = 360 };
	readonly ComboBox configBox = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 130 };
	readonly ComboBox branchBox = new() { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDown, AutoCompleteMode = AutoCompleteMode.SuggestAppend, AutoCompleteSource = AutoCompleteSource.ListItems, MaxDropDownItems = 25 };
	readonly Button repoBrowse = new() { Text = "...", AutoSize = true };
	readonly ListView commitList = new() { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, HideSelection = false, MultiSelect = false };
	readonly TextBox log = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false, Font = new Font("Consolas", 9f), BackColor = Color.FromArgb(24, 24, 24), ForeColor = Color.Gainsboro };
	readonly Button fetchBtn = new() { Text = "Fetch", AutoSize = true };
	readonly Button buildBtn = new() { Text = "Build", AutoSize = true };
	readonly TextBox iniBox = new() { Width = 420, PlaceholderText = "optional Settings.ini to copy into the version before it runs" };
	readonly TextBox modsBox = new() { Width = 420, PlaceholderText = "optional folder of .rte mods to copy into each version's Mods folder" };
	readonly Button runBtn = new() { Text = "Run", AutoSize = true };
	readonly Button buildRunBtn = new() { Text = "Build && Run", AutoSize = true };
	readonly Button deleteBtn = new() { Text = "Delete cached", AutoSize = true };
	readonly Button openBtn = new() { Text = "Open folder", AutoSize = true };
	readonly Button cancelBtn = new() { Text = "Cancel", AutoSize = true, Enabled = false };
	readonly Label status = new() { AutoSize = true, Padding = new Padding(8, 6, 0, 0) };

	List<string> allRefs = new();
	readonly Dictionary<string, string> versionShas = new(); // "ver: 8.2.N" -> merge commit on the integration branch
	const string IntegrationBranch = "dev-8.2";
	string currentRef = "";
	Process? running;
	bool busy;
	CancellationTokenSource? cts;

	public MainForm()
	{
		Text = "Cortex Command Launcher";
		Width = 1200; Height = 800;
		StartPosition = FormStartPosition.CenterScreen;

		configBox.Items.AddRange(new object[] { "Final", "Debug Release", "Debug Minimal", "Debug Full" });
		configBox.SelectedItem = settings.Configuration;
		if (configBox.SelectedIndex < 0) configBox.SelectedIndex = 0;
		repoBox.Text = settings.RepoPath;
		iniBox.Text = settings.SettingsIni;
		modsBox.Text = settings.ModsDir;

		// Dead simple: branch, commit, settings, mods, then Build & Launch.
		static Control Row(string label, Control field, params Control[] extra)
		{
			var r = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, WrapContents = false, FlowDirection = FlowDirection.LeftToRight };
			r.Controls.Add(new Label { Text = label, Width = 80, Padding = new Padding(4, 6, 0, 0) });
			r.Controls.Add(field);
			r.Controls.AddRange(extra);
			return r;
		}
		branchBox.Dock = DockStyle.None; branchBox.Width = 420;
		iniBox.Width = 420; modsBox.Width = 420;
		var iniBrowse = new Button { Text = "...", AutoSize = true };
		iniBrowse.Click += (_, _) => { using var d = new OpenFileDialog { Filter = "Settings.ini|*.ini|All files|*.*", FileName = iniBox.Text }; if (d.ShowDialog() == DialogResult.OK) iniBox.Text = d.FileName; };
		var modsBrowse = new Button { Text = "...", AutoSize = true };
		modsBrowse.Click += (_, _) => { using var d = new FolderBrowserDialog { SelectedPath = modsBox.Text }; if (d.ShowDialog() == DialogResult.OK) modsBox.Text = d.SelectedPath; };
		repoBrowse.Click += (_, _) => { using var d = new FolderBrowserDialog { SelectedPath = repoBox.Text }; if (d.ShowDialog() == DialogResult.OK) repoBox.Text = d.SelectedPath; };

		commitList.Columns.Add("Commit", 90);
		commitList.Columns.Add("Version", 70);
		commitList.Columns.Add("Date", 90);
		commitList.Columns.Add("Author", 110);
		commitList.Columns.Add("Message", 600);
		commitList.Columns.Add("Built", 50);

		buildRunBtn.Text = "Build && Launch";
		buildRunBtn.Font = new Font(Font.FontFamily, 12f, FontStyle.Bold);
		buildRunBtn.Padding = new Padding(20, 6, 20, 6);
		var actions = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, FlowDirection = FlowDirection.LeftToRight, Padding = new Padding(0, 6, 0, 6) };
		actions.Controls.AddRange(new Control[] { buildRunBtn, cancelBtn, new Label { Text = "Build type:", AutoSize = true, Padding = new Padding(16, 8, 0, 0) }, configBox, status });

		var logPanel = new Panel { Dock = DockStyle.Bottom, Height = 170 };
		logPanel.Controls.Add(log);

		Controls.Add(commitList);
		Controls.Add(logPanel);
		Controls.Add(actions);
		Controls.Add(Row("Mods:", modsBox, modsBrowse));
		Controls.Add(Row("Settings:", iniBox, iniBrowse));
		Controls.Add(new Label { Text = "Commit (latest is selected; pick an older one if you want):", AutoSize = true, Dock = DockStyle.Top, Padding = new Padding(4, 8, 0, 2) });
		Controls.Add(Row("Branch:", branchBox, fetchBtn));
		Controls.Add(Row("Repo:", repoBox, repoBrowse));

		fetchBtn.Click += async (_, _) => await FetchAsync();
		branchBox.SelectionChangeCommitted += async (_, _) => { if (branchBox.SelectedItem is string r) await LoadCommits(r); };
		branchBox.KeyDown += async (_, e) => { if (e.KeyCode == Keys.Enter && branchBox.Text.Trim() != "") { e.SuppressKeyPress = true; await LoadCommits(branchBox.Text.Trim()); } };
		commitList.DoubleClick += async (_, _) => await BuildAndRun(true);
		buildRunBtn.Click += async (_, _) => await BuildAndRun(true);
		cancelBtn.Click += (_, _) => { cts?.Cancel(); try { running?.Kill(true); } catch { } };
		FormClosing += (_, _) => { SaveSettings(); };
		Shown += async (_, _) => await FetchAsync();
	}

	void SaveSettings()
	{
		settings.RepoPath = repoBox.Text.Trim();
		settings.Configuration = (string)configBox.SelectedItem!;
		settings.SettingsIni = iniBox.Text.Trim();
		settings.ModsDir = modsBox.Text.Trim();
		settings.LastRef = branchBox.Text.Trim();
		settings.Save();
	}

	string Repo => repoBox.Text.Trim();
	string WorktreePath(CommitInfo c) => Path.Combine(settings.EffectiveVersionsDir, c.Short);
	string ExeName => settings.Configuration switch
	{
		"Debug Release" => "Cortex Command.debug.release.exe",
		"Debug Minimal" => "Cortex Command.debug.minimal.exe",
		"Debug Full" => "Cortex Command.debug.full.exe",
		_ => "Cortex Command.exe",
	};
	// Each configuration has its own exe name, so one worktree can hold several builds.
	string ExePath(CommitInfo c) => Path.Combine(WorktreePath(c), ExeName);

	CommitInfo? Selected()
	{
		if (commitList.SelectedItems.Count == 0) { Append("Select a commit first."); return null; }
		return (CommitInfo)commitList.SelectedItems[0].Tag!;
	}

	void Append(string line)
	{
		if (InvokeRequired) { BeginInvoke(() => Append(line)); return; }
		log.AppendText(line + Environment.NewLine);
	}

	void SetBusy(bool b, string text = "")
	{
		busy = b;
		foreach (var x in new Control[] { fetchBtn, buildBtn, runBtn, buildRunBtn, deleteBtn, openBtn, configBox, repoBox })
			x.Enabled = !b;
		cancelBtn.Enabled = b;
		status.Text = text;
	}

	// Runs a process, streaming output to the log. Returns the exit code.
	async Task<int> Exec(string file, string args, string? cwd, CancellationToken ct = default, bool echo = true)
	{
		if (echo) Append($"> {Path.GetFileName(file)} {args}");
		var psi = new ProcessStartInfo(file, args)
		{
			WorkingDirectory = cwd ?? Environment.CurrentDirectory,
			RedirectStandardOutput = true,
			RedirectStandardError = true,
			UseShellExecute = false,
			CreateNoWindow = true,
			StandardOutputEncoding = Encoding.UTF8,
			StandardErrorEncoding = Encoding.UTF8,
		};
		using var p = new Process { StartInfo = psi, EnableRaisingEvents = true };
		p.OutputDataReceived += (_, e) => { if (e.Data != null) Append(e.Data); };
		p.ErrorDataReceived += (_, e) => { if (e.Data != null) Append(e.Data); };
		try { p.Start(); }
		catch (Exception ex) { Append($"Failed to start {file}: {ex.Message}"); return -1; }
		running = p;
		p.BeginOutputReadLine(); p.BeginErrorReadLine();
		using var reg = ct.Register(() => { try { p.Kill(true); } catch { } });
		await p.WaitForExitAsync();
		running = null;
		return p.ExitCode;
	}

	// Runs git quietly and captures stdout.
	async Task<(int code, string output)> Git(string args, string? cwd = null)
	{
		var psi = new ProcessStartInfo("git", args)
		{
			WorkingDirectory = cwd ?? Repo,
			RedirectStandardOutput = true,
			RedirectStandardError = true,
			UseShellExecute = false,
			CreateNoWindow = true,
			StandardOutputEncoding = Encoding.UTF8,
		};
		try
		{
			using var p = Process.Start(psi)!;
			var outTask = p.StandardOutput.ReadToEndAsync();
			var errTask = p.StandardError.ReadToEndAsync();
			await p.WaitForExitAsync();
			return (p.ExitCode, await outTask + (p.ExitCode != 0 ? await errTask : ""));
		}
		catch (Exception ex) { return (-1, ex.Message); }
	}

	async Task FetchAsync()
	{
		SaveSettings();
		if (!Directory.Exists(Path.Combine(Repo, ".git")) && !File.Exists(Path.Combine(Repo, ".git")))
		{
			Append($"'{Repo}' is not a git checkout. Set the repo path at the top.");
			return;
		}
		SetBusy(true, "Fetching...");
		cts = new CancellationTokenSource();
		await Exec("git", $"fetch {settings.Remote} --tags --prune", Repo, cts.Token);
		await RefreshRefs();
		ApplyFilter();
		SetBusy(false, $"{allRefs.Count} refs");
		if (branchBox.Text == "" && currentRef == "")
		{
			branchBox.Text = settings.LastRef != "" ? settings.LastRef : allRefs.FirstOrDefault(r => r.EndsWith("/" + IntegrationBranch)) ?? "";
			if (branchBox.Text != "") await LoadCommits(branchBox.Text);
		}
	}

	async Task RefreshRefs()
	{
		// Tags first (newest version first, so v8.2.N lands at the top), then remote branches by recent activity.
		var (_, tags) = await Git("for-each-ref --sort=-version:refname --format=%(refname:short) refs/tags");
		var (_, branches) = await Git($"for-each-ref --sort=-committerdate --format=%(refname:short) refs/remotes/{settings.Remote}");
		await LoadVersions();
		allRefs = versionShas.Keys.Concat(tags.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(t => "tag: " + t.Trim()))
			.Concat(branches.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(b => b.Trim()).Where(b => !b.EndsWith("/HEAD") && b != settings.Remote))
			.ToList();
	}

	// Cloud threads can't push tags, so versions are also read from the merge history of the integration branch:
	// a commit whose title starts "[8.2.N]" or whose message has a "Version 8.2.N" line is version 8.2.N.
	async Task LoadVersions()
	{
		versionShas.Clear();
		var (code, o) = await Git($"log {settings.Remote}/{IntegrationBranch} --first-parent --format=%H%x1f%s%x1f%b%x1e --");
		if (code != 0) return;
		var subj = new Regex(@"^\[(\d+\.\d+\.\d+)\]");
		var body = new Regex(@"^Version (\d+\.\d+\.\d+)\s*$", RegexOptions.Multiline);
		var found = new Dictionary<string, string>();
		foreach (var rec in o.Split('\x1e', StringSplitOptions.RemoveEmptyEntries))
		{
			var f = rec.Trim('\n', '\r').Split('\x1f');
			if (f.Length < 2) continue;
			var m = subj.Match(f[1]);
			var v = m.Success ? m.Groups[1].Value : f.Length > 2 && body.Match(f[2]) is { Success: true } b ? b.Groups[1].Value : null;
			if (v != null && !found.ContainsKey(v)) found[v] = f[0]; // newest commit wins
		}
		foreach (var kv in found.OrderByDescending(k => Version.TryParse(k.Key, out var ver) ? ver : new Version(0, 0)))
			versionShas["ver: " + kv.Key] = kv.Value;
	}

	// ---- Live feed: git has no push notifications for a plain remote, so poll ls-remote (cheap) and fetch only when refs moved.

	void ApplyFilter()
	{
		var t = branchBox.Text;
		branchBox.BeginUpdate();
		branchBox.Items.Clear();
		branchBox.Items.AddRange(allRefs.Cast<object>().ToArray());
		branchBox.EndUpdate();
		branchBox.Text = t;
	}

	async Task LoadCommits(string refName)
	{
		var shown = refName;
		if (refName.StartsWith("tag: ")) refName = refName[5..];
		else if (versionShas.TryGetValue(refName, out var vsha)) refName = vsha;
		currentRef = refName;
		var (code, output) = await Git($"log {refName} -n 60 --date=short --format=%H%x09%ad%x09%an%x09%s --");
		commitList.Items.Clear();
		if (code != 0) { Append($"git log failed for '{refName}': {output.Trim()}"); return; }
		var commits = output.Split('\n', StringSplitOptions.RemoveEmptyEntries)
			.Select(l => l.Split('\t', 4))
			.Where(p => p.Length == 4)
			.Select(p => new CommitInfo(p[0], p[1], p[2], p[3]))
			.ToList();
		foreach (var c in commits)
		{
			var item = new ListViewItem(c.Short) { Tag = c };
			item.SubItems.Add("...");
			item.SubItems.Add(c.Date);
			item.SubItems.Add(c.Author);
			item.SubItems.Add(c.Subject);
			item.SubItems.Add(File.Exists(ExePath(c)) ? "yes" : "");
			commitList.Items.Add(item);
		}
		if (commitList.Items.Count > 0) commitList.Items[0].Selected = true;
		status.Text = shown;
		_ = FillVersions(commits, refName);
	}

	// Reads the game version at each commit so the list shows 8.2.x numbers.
	async Task FillVersions(List<CommitInfo> commits, string forRef)
	{
		var re = new Regex("c_VersionString\\s*=\\s*\"([^\"]+)\"");
		for (int i = 0; i < commits.Count; i++)
		{
			if (forRef != currentRef) return;
			var (code, text) = await Git($"show {commits[i].Sha}:Source/System/GameVersion.h");
			var m = code == 0 ? re.Match(text) : null;
			if (forRef != currentRef || i >= commitList.Items.Count) return;
			commitList.Items[i].SubItems[1].Text = m is { Success: true } ? m.Groups[1].Value : "?";
		}
	}

	async Task<string?> FindMsBuild()
	{
		var pf86 = Environment.GetEnvironmentVariable("ProgramFiles(x86)") ?? @"C:\Program Files (x86)";
		var vswhere = Path.Combine(pf86, @"Microsoft Visual Studio\Installer\vswhere.exe");
		if (!File.Exists(vswhere)) return null;
		var psi = new ProcessStartInfo(vswhere, "-latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\\**\\Bin\\MSBuild.exe")
		{ RedirectStandardOutput = true, UseShellExecute = false, CreateNoWindow = true };
		using var p = Process.Start(psi)!;
		var o = await p.StandardOutput.ReadToEndAsync();
		await p.WaitForExitAsync();
		return o.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(s => s.Trim()).FirstOrDefault(File.Exists);
	}

	async Task<bool> EnsureWorktree(CommitInfo c, CancellationToken ct)
	{
		var path = WorktreePath(c);
		if (Directory.Exists(path) && File.Exists(Path.Combine(path, "RTEA.sln"))) return true;
		Directory.CreateDirectory(settings.EffectiveVersionsDir);
		await Exec("git", "worktree prune", Repo, ct, false);
		return await Exec("git", $"worktree add --detach \"{path}\" {c.Sha}", Repo, ct) == 0;
	}

	async Task BuildAndRun(bool run, bool build = true)
	{
		if (busy) return;
		var c = Selected(); if (c == null) return;
		SaveSettings();
		settings.Configuration = (string)configBox.SelectedItem!;
		cts = new CancellationTokenSource();
		var ct = cts.Token;
		SetBusy(true, $"Preparing {c.Short}...");
		try
		{
			if (!await EnsureWorktree(c, ct)) { Append("Checkout failed."); return; }
			if (build)
			{
				var msbuild = await FindMsBuild();
				if (msbuild == null) { Append("MSBuild not found. Install Visual Studio 2022 with the 'Desktop development with C++' workload."); return; }
				var dir = WorktreePath(c);
				// README step: fmod.dll must sit next to the exe.
				var fmod = Path.Combine(dir, "external", "lib", "win", "fmod.dll");
				if (File.Exists(fmod)) File.Copy(fmod, Path.Combine(dir, "fmod.dll"), true);
				status.Text = $"Building {c.Short} ({settings.Configuration})...";
				var sw = Stopwatch.StartNew();
				// Same command as .github/workflows/msbuild.yml, with x64 made explicit.
				var code = await Exec(msbuild, $"/m /nologo /v:m /p:Configuration=\"{settings.Configuration}\" /p:Platform=x64 RTEA.sln", dir, ct);
				if (code != 0) { Append($"BUILD FAILED (exit {code}) after {sw.Elapsed:mm\\:ss}"); return; }
				Append($"Build succeeded in {sw.Elapsed:mm\\:ss}");
				MarkCached(c);
			}
			if (run) Launch(c);
		}
		finally { SetBusy(false, currentRef); }
	}

	// Copies every *.rte folder of the mods folder into the version's Mods folder (only files that changed), so each version runs with its own copy.
	// A folder the version ships itself is left alone; ones the launcher copied are marked and kept in sync with the source.
	const string CopyMarker = ".launcher-copy";
	void LinkMods(CommitInfo c)
	{
		var src = modsBox.Text.Trim();
		if (src == "") return;
		if (!Directory.Exists(src)) { Append($"Mods folder not found: {src}"); return; }
		var data = Path.Combine(WorktreePath(c), "Mods"); // the game loads user mods from Mods/, official ones from Data/
		Directory.CreateDirectory(data);
		// An earlier launcher build copied mods into Data/; tidy those marked copies away.
		var oldData = Path.Combine(WorktreePath(c), "Data");
		if (Directory.Exists(oldData))
			foreach (var od in Directory.GetDirectories(oldData))
				if (File.Exists(Path.Combine(od, CopyMarker))) try { Directory.Delete(od, true); } catch { }
		var dirs = Directory.GetDirectories(src, "*.rte");
		if (dirs.Length == 0 && src.EndsWith(".rte", StringComparison.OrdinalIgnoreCase)) dirs = new[] { src };
		foreach (var d in dirs)
		{
			var dest = Path.Combine(data, Path.GetFileName(d));
			if (Directory.Exists(dest) && !File.Exists(Path.Combine(dest, CopyMarker))) { Append($"Skipping {Path.GetFileName(d)}: the version has its own"); continue; }
			try
			{
				int n = SyncDir(d, dest);
				File.WriteAllText(Path.Combine(dest, CopyMarker), "");
				Append(n > 0 ? $"Copied mod {Path.GetFileName(d)} ({n} files)" : $"Mod {Path.GetFileName(d)} up to date");
			}
			catch (Exception ex) { Append($"Mod copy failed for {Path.GetFileName(d)}: " + ex.Message); }
		}
	}

	static int SyncDir(string src, string dest)
	{
		int n = 0;
		Directory.CreateDirectory(dest);
		foreach (var f in Directory.GetFiles(src))
		{
			var t = Path.Combine(dest, Path.GetFileName(f));
			var fi = new FileInfo(f);
			if (File.Exists(t) && new FileInfo(t).Length == fi.Length && File.GetLastWriteTimeUtc(t) == fi.LastWriteTimeUtc) continue;
			File.Copy(f, t, true); File.SetLastWriteTimeUtc(t, fi.LastWriteTimeUtc); n++;
		}
		foreach (var d in Directory.GetDirectories(src)) n += SyncDir(d, Path.Combine(dest, Path.GetFileName(d)));
		foreach (var f in Directory.GetFiles(dest))
			if (Path.GetFileName(f) != CopyMarker && !File.Exists(Path.Combine(src, Path.GetFileName(f)))) File.Delete(f);
		foreach (var d in Directory.GetDirectories(dest))
			if (!Directory.Exists(Path.Combine(src, Path.GetFileName(d)))) Directory.Delete(d, true);
		return n;
	}

	void MarkCached(CommitInfo c)
	{
		foreach (ListViewItem it in commitList.Items)
			if (((CommitInfo)it.Tag!).Sha == c.Sha) it.SubItems[5].Text = File.Exists(ExePath(c)) ? "yes" : "";
	}

	void Launch(CommitInfo c)
	{
		var exe = ExePath(c);
		if (!File.Exists(exe)) { Append($"Not built yet for this configuration: {exe}"); return; }
		LinkMods(c);
		var ini = iniBox.Text.Trim();
		if (ini != "")
		{
			// The game reads (and rewrites) Settings.ini from its working directory, so give each launch a fresh copy of the chosen file.
			if (!File.Exists(ini)) { Append($"Settings.ini not found: {ini}"); return; }
			try { Directory.CreateDirectory(Path.Combine(WorktreePath(c), "Userdata")); File.Copy(ini, Path.Combine(WorktreePath(c), "Userdata", "Settings.ini"), true); Append($"Using Settings.ini from {ini}"); }
			catch (Exception ex) { Append("Could not copy Settings.ini: " + ex.Message); return; }
		}
		Append($"Launching {c.Short}");
		Process.Start(new ProcessStartInfo(exe) { WorkingDirectory = WorktreePath(c), UseShellExecute = true });
	}

	async Task DeleteSelected()
	{
		var c = Selected(); if (c == null) return;
		var path = WorktreePath(c);
		if (!Directory.Exists(path)) { Append("Nothing cached for that commit."); return; }
		if (MessageBox.Show($"Delete the cached build and checkout for {c.Short}?", "Delete", MessageBoxButtons.YesNo) != DialogResult.Yes) return;
		SetBusy(true, "Deleting...");
		await Exec("git", $"worktree remove --force \"{path}\"", Repo);
		if (Directory.Exists(path))
		{
			try { Directory.Delete(path, true); } catch (Exception ex) { Append($"Could not fully delete: {ex.Message}"); }
			await Exec("git", "worktree prune", Repo, default, false);
		}
		MarkCached(c);
		SetBusy(false, currentRef);
	}
}
