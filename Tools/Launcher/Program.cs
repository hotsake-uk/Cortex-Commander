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
	readonly TextBox repoBox = new() { Dock = DockStyle.Fill };
	readonly ComboBox configBox = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 130 };
	readonly TextBox filterBox = new() { Dock = DockStyle.Top, PlaceholderText = "filter branches / tags" };
	readonly ListBox refList = new() { Dock = DockStyle.Fill, IntegralHeight = false };
	readonly ListView commitList = new() { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, HideSelection = false, MultiSelect = false };
	readonly TextBox customRef = new() { Width = 220, PlaceholderText = "or any branch / tag (v8.2.3) / sha" };
	readonly TextBox log = new() { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false, Font = new Font("Consolas", 9f), BackColor = Color.FromArgb(24, 24, 24), ForeColor = Color.Gainsboro };
	readonly Button fetchBtn = new() { Text = "Fetch", AutoSize = true };
	readonly Button buildBtn = new() { Text = "Build", AutoSize = true };
	readonly Button runBtn = new() { Text = "Run", AutoSize = true };
	readonly Button buildRunBtn = new() { Text = "Build && Run", AutoSize = true };
	readonly Button deleteBtn = new() { Text = "Delete cached", AutoSize = true };
	readonly Button openBtn = new() { Text = "Open folder", AutoSize = true };
	readonly Button cancelBtn = new() { Text = "Cancel", AutoSize = true, Enabled = false };
	readonly ListView feedList = new() { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, HideSelection = false, MultiSelect = false };
	readonly CheckBox liveBox = new() { Text = "Live", Checked = true, AutoSize = true, Padding = new Padding(8, 3, 0, 0) };
	readonly NumericUpDown intervalBox = new() { Minimum = 3, Maximum = 600, Value = 10, Width = 50 };
	readonly Label liveStatus = new() { AutoSize = true, Padding = new Padding(8, 6, 0, 0) };
	readonly TabPage feedTab = new("Live feed");
	readonly TabControl bottomTabs = new() { Dock = DockStyle.Fill };
	Dictionary<string, string>? lastRemote;
	int unseen;
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

		var top = new TableLayoutPanel { Dock = DockStyle.Top, Height = 34, ColumnCount = 5, RowCount = 1 };
		top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
		top.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
		top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
		top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
		top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
		top.Controls.Add(new Label { Text = "Repo:", AutoSize = true, Padding = new Padding(4, 7, 0, 0) }, 0, 0);
		top.Controls.Add(repoBox, 1, 0);
		var browse = new Button { Text = "...", AutoSize = true };
		browse.Click += (_, _) => { using var d = new FolderBrowserDialog { SelectedPath = repoBox.Text }; if (d.ShowDialog() == DialogResult.OK) { repoBox.Text = d.SelectedPath; } };
		top.Controls.Add(browse, 2, 0);
		top.Controls.Add(configBox, 3, 0);
		top.Controls.Add(fetchBtn, 4, 0);

		var refsPanel = new Panel { Dock = DockStyle.Fill };
		refsPanel.Controls.Add(refList);
		refsPanel.Controls.Add(filterBox);

		commitList.Columns.Add("Commit", 90);
		commitList.Columns.Add("Version", 70);
		commitList.Columns.Add("Date", 90);
		commitList.Columns.Add("Author", 110);
		commitList.Columns.Add("Message", 500);
		commitList.Columns.Add("Cached", 60);

		var buttons = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, FlowDirection = FlowDirection.LeftToRight };
		buttons.Controls.AddRange(new Control[] { customRef, buildBtn, runBtn, buildRunBtn, deleteBtn, openBtn, cancelBtn, status });
		var customGo = new Button { Text = "Go", AutoSize = true };
		customGo.Click += async (_, _) => { if (customRef.Text.Trim() != "") await LoadCommits(customRef.Text.Trim()); };
		buttons.Controls.Add(customGo);
		buttons.Controls.SetChildIndex(customGo, 1);

		var right = new Panel { Dock = DockStyle.Fill };
		right.Controls.Add(commitList);
		right.Controls.Add(buttons);

		var split = new SplitContainer { Dock = DockStyle.Fill, SplitterDistance = 300 };
		split.Panel1.Controls.Add(refsPanel);
		split.Panel2.Controls.Add(right);

		var vsplit = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, SplitterDistance = 430 };
		vsplit.Panel1.Controls.Add(split);
		var logTab = new TabPage("Log");
		logTab.Controls.Add(log);
		var feedTop = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true };
		feedTop.Controls.AddRange(new Control[] { liveBox, new Label { Text = "poll every", AutoSize = true, Padding = new Padding(8, 6, 0, 0) }, intervalBox, new Label { Text = "s", AutoSize = true, Padding = new Padding(0, 6, 0, 0) }, liveStatus });
		feedList.Columns.Add("Time", 70);
		feedList.Columns.Add("Branch / tag", 200);
		feedList.Columns.Add("Commit", 90);
		feedList.Columns.Add("Version", 70);
		feedList.Columns.Add("Author", 110);
		feedList.Columns.Add("Message", 600);
		feedTab.Controls.Add(feedList);
		feedTab.Controls.Add(feedTop);
		bottomTabs.TabPages.Add(feedTab);
		bottomTabs.TabPages.Add(logTab);
		bottomTabs.SelectedIndexChanged += (_, _) => { if (bottomTabs.SelectedTab == feedTab) { unseen = 0; feedTab.Text = "Live feed"; } };
		vsplit.Panel2.Controls.Add(bottomTabs);

		Controls.Add(vsplit);
		Controls.Add(top);

		fetchBtn.Click += async (_, _) => await FetchAsync();
		filterBox.TextChanged += (_, _) => ApplyFilter();
		refList.SelectedIndexChanged += async (_, _) => { if (refList.SelectedItem is string r) await LoadCommits(r); };
		commitList.DoubleClick += async (_, _) => await BuildAndRun(true);
		buildBtn.Click += async (_, _) => await BuildAndRun(false, false);
		runBtn.Click += (_, _) => RunSelected();
		buildRunBtn.Click += async (_, _) => await BuildAndRun(true);
		deleteBtn.Click += async (_, _) => await DeleteSelected();
		openBtn.Click += (_, _) => { var c = Selected(); if (c != null) Process.Start("explorer.exe", WorktreePath(c)); };
		cancelBtn.Click += (_, _) => { cts?.Cancel(); try { running?.Kill(true); } catch { } };
		FormClosing += (_, _) => { SaveSettings(); };
		feedList.DoubleClick += async (_, _) => await OpenFeedItem();
		Shown += async (_, _) => { await FetchAsync(); _ = PollLoop(); };
	}

	void SaveSettings()
	{
		settings.RepoPath = repoBox.Text.Trim();
		settings.Configuration = (string)configBox.SelectedItem!;
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
		lastRemote = await LsRemote();
		ApplyFilter();
		SetBusy(false, $"{allRefs.Count} refs");
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

	async Task<Dictionary<string, string>?> LsRemote()
	{
		var (code, o) = await Git($"ls-remote --heads --tags {settings.Remote}");
		if (code != 0) return null;
		var d = new Dictionary<string, string>();
		foreach (var line in o.Split('\n', StringSplitOptions.RemoveEmptyEntries))
		{
			var p = line.Split('\t');
			if (p.Length != 2) continue;
			var name = p[1].Trim();
			if (name.EndsWith("^{}")) d[name[..^3]] = p[0]; // annotated tag: use the commit it points at
			else if (!d.ContainsKey(name) || !name.StartsWith("refs/tags/")) d[name] = p[0];
		}
		return d;
	}

	async Task PollLoop()
	{
		while (!IsDisposed)
		{
			await Task.Delay((int)intervalBox.Value * 1000);
			if (IsDisposed || !liveBox.Checked || lastRemote == null) { if (lastRemote == null && !busy && liveBox.Checked) lastRemote = await LsRemote(); continue; }
			try
			{
				var now = await LsRemote();
				if (now == null) { liveStatus.Text = "remote unreachable"; continue; }
				var changed = now.Where(kv => !lastRemote.TryGetValue(kv.Key, out var old) || old != kv.Value).ToList();
				liveStatus.Text = $"checked {DateTime.Now:HH:mm:ss}";
				if (changed.Count == 0) continue;
				var before = lastRemote;
				var (fcode, fout) = await Git($"fetch {settings.Remote} --tags --prune --force");
				if (fcode != 0) { Append("live fetch failed: " + fout.Trim()); continue; }
				lastRemote = now;
				await AnnounceChanges(before, changed);
				await RefreshRefs();
				ApplyFilter();
				if (!string.IsNullOrEmpty(currentRef) && changed.Any(c => c.Key.EndsWith("/" + currentRef.Replace(settings.Remote + "/", "")))) await LoadCommits(currentRef);
			}
			catch (Exception ex) { Append("live poll error: " + ex.Message); }
		}
	}

	async Task AnnounceChanges(Dictionary<string, string> before, List<KeyValuePair<string, string>> changed)
	{
		var re = new Regex("c_VersionString\\s*=\\s*\"([^\"]+)\"");
		foreach (var (refName, sha) in changed.OrderBy(c => c.Key))
		{
			bool isTag = refName.StartsWith("refs/tags/");
			var label = isTag ? "tag " + refName["refs/tags/".Length..] : refName["refs/heads/".Length..];
			var tip = isTag ? sha : $"{settings.Remote}/{label}";
			string range;
			if (isTag) range = $"-n 1 {sha}";
			else if (before.TryGetValue(refName, out var old))
			{
				var (anc, _) = await Git($"merge-base --is-ancestor {old} {sha}");
				range = anc == 0 ? $"{old}..{sha} -n 30" : $"-n 5 {sha}"; // force-push: show the new tip
				if (anc != 0) label += " (force-pushed)";
			}
			else
			{
				// brand new branch: only show commits not already on another known branch
				var others = string.Join(" ", before.Where(kv => kv.Key.StartsWith("refs/heads/")).Select(kv => "^" + kv.Value).Distinct());
				range = $"{sha} {others} -n 10";
				label += " (new branch)";
			}
			var (code, o) = await Git($"log {range} --date=short --format=%H%x09%ad%x09%an%x09%s --");
			if (code != 0) continue;
			var commits = o.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(l => l.Split('\t', 4)).Where(x => x.Length == 4)
				.Select(x => new CommitInfo(x[0], x[1], x[2], x[3])).Reverse().ToList(); // oldest first, so the newest ends on top
			foreach (var c in commits)
			{
				var (vc, vt) = await Git($"show {c.Sha}:Source/System/GameVersion.h");
				var m = vc == 0 ? re.Match(vt) : null;
				var item = new ListViewItem(DateTime.Now.ToString("HH:mm:ss")) { Tag = (isTag ? refName["refs/tags/".Length..] : $"{settings.Remote}/{refName["refs/heads/".Length..]}", c) };
				item.SubItems.Add(label);
				item.SubItems.Add(c.Short);
				item.SubItems.Add(m is { Success: true } ? m.Groups[1].Value : "?");
				item.SubItems.Add(c.Author);
				item.SubItems.Add(c.Subject);
				feedList.Items.Insert(0, item);
				Append($"[live] {label}: {c.Short} {c.Author}: {c.Subject}");
				unseen++;
			}
		}
		if (bottomTabs.SelectedTab != feedTab) feedTab.Text = $"Live feed ({unseen} new)";
		else unseen = 0;
		System.Media.SystemSounds.Asterisk.Play();
	}

	async Task OpenFeedItem()
	{
		if (feedList.SelectedItems.Count == 0) return;
		var (r, c) = ((string, CommitInfo))feedList.SelectedItems[0].Tag!;
		await LoadCommits(r);
		foreach (ListViewItem it in commitList.Items)
			if (((CommitInfo)it.Tag!).Sha == c.Sha) { it.Selected = true; it.EnsureVisible(); break; }
	}

	void ApplyFilter()
	{
		var f = filterBox.Text.Trim();
		refList.Items.Clear();
		foreach (var r in allRefs)
			if (f == "" || r.Contains(f, StringComparison.OrdinalIgnoreCase)) refList.Items.Add(r);
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

	void MarkCached(CommitInfo c)
	{
		foreach (ListViewItem it in commitList.Items)
			if (((CommitInfo)it.Tag!).Sha == c.Sha) it.SubItems[5].Text = File.Exists(ExePath(c)) ? "yes" : "";
	}

	void RunSelected()
	{
		var c = Selected(); if (c == null) return;
		Launch(c);
	}

	void Launch(CommitInfo c)
	{
		var exe = ExePath(c);
		if (!File.Exists(exe)) { Append($"Not built yet for this configuration: {exe}"); return; }
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
