using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace CortexLauncher;

// The mod check: runs Tools/Mods/Check-Mod.py (Python) on the mods folder against a checkout of the game, shows what it found,
// and applies its fixes to the mods folder when asked. The check itself lives in the repo, so the launcher never needs updating when it learns more.

record ModFinding(string Severity, string File, int Line, string Message, string? Fix);
record ModResult(string Name, string Path, int Errors, int Warnings, int Notes, int Fixable, List<ModFinding> Findings);
record ModCheckRun(string Version, string Game, List<ModResult> Mods)
{
	public int Errors => Mods.Sum(m => m.Errors);
	public int Warnings => Mods.Sum(m => m.Warnings);
	public int Fixable => Mods.Sum(m => m.Fixable);
}

partial class MainForm
{
	readonly Button checkModsBtn = new() { Text = "Check mods", AutoSize = true };
	readonly CheckBox checkBeforeLaunch = new() { Text = "Check before launch", AutoSize = true, Padding = new Padding(8, 4, 0, 0) };
	(string exe, string args)? python;

	internal static string ModChecksDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "CortexLauncher", "ModChecks");
	static string ModChecksJson => Path.Combine(ModChecksDir, "last.json");

	void SetUpModCheck()
	{
		checkBeforeLaunch.Checked = settings.CheckModsBeforeLaunch;
		checkBeforeLaunch.CheckedChanged += (_, _) => { settings.CheckModsBeforeLaunch = checkBeforeLaunch.Checked; };
		checkModsBtn.Click += async (_, _) => await CheckModsClicked();
	}

	// The checkout the check reads the game from: the build slot when it is at this commit (what you're about to run), otherwise your repo.
	async Task<string> CheckGameDir(CommitInfo? c)
	{
		if (c != null && File.Exists(Path.Combine(SlotPath, "RTEA.sln")))
		{
			var (code, head) = await Git("rev-parse HEAD", SlotPath);
			if (code == 0 && head.Trim() == c.Sha) return SlotPath;
		}
		return Repo;
	}

	// Check-Mod.py from the game checkout, or from your repo when that version predates the launcher's options (--json and --game).
	string? FindChecker(string gameDir)
	{
		foreach (var dir in new[] { gameDir, Repo }.Distinct(StringComparer.OrdinalIgnoreCase))
		{
			var script = Path.Combine(dir, "Tools", "Mods", "Check-Mod.py");
			try { if (File.Exists(script) && File.ReadAllText(script).Contains("'--json'")) return script; } catch { }
		}
		return null;
	}

	// The checker lives in the repo, so a version older than 8.5.228 (or a checkout on an old branch) simply doesn't have it. Take Tools/Mods from the newest
	// maindev on the remote instead, into the launcher's own folder. The checker only reads the game folder it is pointed at, so any version can use it.
	async Task<string?> ExtractCheckerFromRemote()
	{
		var dest = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "CortexLauncher", "ModTools");
		foreach (var branch in new[] { "maindev", "dev-8.2" })
		{
			var rf = $"{settings.Remote}/{branch}";
			var (c0, _) = await Git($"rev-parse --verify --quiet {rf}");
			if (c0 != 0) continue;
			var zip = Path.Combine(Path.GetTempPath(), "cortex-modtools.zip");
			try { File.Delete(zip); } catch { }
			var (c1, o1) = await Git($"archive --format=zip -o \"{zip}\" {rf} Tools/Mods");
			if (c1 != 0 || !File.Exists(zip)) { Append($"Could not read Tools/Mods from {rf}: {o1.Trim()}"); continue; }
			try
			{
				if (Directory.Exists(dest)) Directory.Delete(dest, true);
				System.IO.Compression.ZipFile.ExtractToDirectory(zip, dest);
			}
			catch (Exception ex) { Append("Could not unpack the mod checker: " + ex.Message); continue; }
			var script = Path.Combine(dest, "Tools", "Mods", "Check-Mod.py");
			if (File.Exists(script) && File.ReadAllText(script).Contains("'--json'")) { Append($"Using the mod checker from {rf}"); return script; }
		}
		return null;
	}

	// py -3 (the Windows Python launcher), then python and python3 on PATH. The Microsoft Store's "python" stand-in fails --version, so it is skipped.
	async Task<(string exe, string args)?> FindPython()
	{
		if (python != null) return python;
		foreach (var (exe, args) in new[] { ("py", "-3"), ("python", ""), ("python3", "") })
		{
			try
			{
				var psi = new ProcessStartInfo(exe, (args + " --version").Trim()) { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true };
				using var p = Process.Start(psi)!;
				var o = await p.StandardOutput.ReadToEndAsync() + await p.StandardError.ReadToEndAsync();
				await p.WaitForExitAsync();
				if (p.ExitCode == 0 && o.Contains("Python 3")) return python = (exe, args);
			}
			catch { }
		}
		return null;
	}

	// Runs the checker; fix=true makes its fixes first (it keeps a .bak of each file it changes). Returns null when it couldn't run.
	async Task<ModCheckRun?> RunModCheck(string gameDir, bool fix, CancellationToken ct, IReadOnlyList<string>? only = null)
	{
		var mods = modsBox.Text.Trim();
		if (mods == "" || !Directory.Exists(mods)) { Append(mods == "" ? "Set a mods folder to check." : $"Mods folder not found: {mods}"); return null; }
		var py = await FindPython();
		if (py == null) { Append("Python 3 wasn't found, so mods can't be checked. Install it from python.org (tick \"Add to PATH\")."); return null; }
		var script = FindChecker(gameDir);
		script ??= await ExtractCheckerFromRemote();
		if (script == null) { Append("No mod checker found: this version predates Tools/Mods/Check-Mod.py (8.5.228) and the latest maindev couldn't be fetched. Press Refresh and try again."); return null; }
		// A single .rte picked as the mods folder is checked on its own, with its folder as the place other mods live.
		var target = only is { Count: > 0 }
			? string.Join(" ", only.Select(m => "\"" + m + "\"")) + " --mods \"" + mods + "\"" // just these mods; the rest of the folder is where other mods live
			: mods.EndsWith(".rte", StringComparison.OrdinalIgnoreCase) && Directory.GetDirectories(mods, "*.rte").Length == 0
			? $"\"{mods}\" --mods \"{Path.GetDirectoryName(mods)}\""
			: $"--all --mods \"{mods}\"";
		Directory.CreateDirectory(ModChecksDir);
		try { File.Delete(ModChecksJson); } catch { }
		var args = $"{py.Value.args} \"{script}\" {target} --game \"{gameDir}\" --report-dir \"{ModChecksDir}\" --json \"{ModChecksJson}\" --quiet" + (fix ? " --fix --yes" : "");
		var env = new Dictionary<string, string> { ["PYTHONIOENCODING"] = "utf-8", ["PYTHONDONTWRITEBYTECODE"] = "1" };
		await Exec(py.Value.exe, args.Trim(), Path.GetDirectoryName(script), ct, true, env);
		if (ct.IsCancellationRequested) return null;
		try { return ParseModCheck(File.ReadAllText(ModChecksJson)); }
		catch (Exception ex) { Append("The mod check didn't finish: " + ex.Message); return null; }
	}

	static ModCheckRun ParseModCheck(string json)
	{
		using var doc = JsonDocument.Parse(json);
		var root = doc.RootElement;
		var mods = new List<ModResult>();
		foreach (var m in root.GetProperty("mods").EnumerateArray())
		{
			var counts = m.GetProperty("counts");
			var findings = m.GetProperty("findings").EnumerateArray().Select(f => new ModFinding(
				f.GetProperty("severity").GetString()!, f.GetProperty("file").GetString()!, f.GetProperty("line").GetInt32(),
				f.GetProperty("message").GetString()!, f.GetProperty("fix").ValueKind == JsonValueKind.String ? f.GetProperty("fix").GetString() : null)).ToList();
			mods.Add(new ModResult(m.GetProperty("name").GetString()!, m.GetProperty("path").GetString()!,
				counts.GetProperty("ERROR").GetInt32(), counts.GetProperty("WARNING").GetInt32(), counts.GetProperty("NOTE").GetInt32(),
				m.GetProperty("fixable").GetInt32(), findings));
		}
		return new ModCheckRun(root.GetProperty("version").GetString()!, root.GetProperty("game").GetString()!, mods);
	}

	async Task CheckModsClicked()
	{
		if (busy) return;
		SaveSettings();
		cts = new CancellationTokenSource();
		SetBusy(true, "Checking mods...");
		string gameDir;
		ModCheckRun? result;
		try
		{
			gameDir = await CheckGameDir(commitList.SelectedItems.Count > 0 ? (CommitInfo)commitList.SelectedItems[0].Tag! : null);
			result = await RunModCheck(gameDir, false, cts.Token);
		}
		finally { SetBusy(false, currentRef); }
		if (result == null) return;
		Append(Summary(result));
		using var form = new ModCheckForm(this, result, gameDir, false);
		form.ShowDialog(this);
	}

	// Before a launch: check the mods against the version being launched. True means go ahead.
	// The check is skipped when nothing changed since this commit and mods folder last checked clean, or you chose Launch anyway for them.
	async Task<bool> CheckModsBeforeLaunch(CommitInfo c, CancellationToken ct)
	{
		if (!checkBeforeLaunch.Checked || modsBox.Text.Trim() == "" || !Directory.Exists(modsBox.Text.Trim())) return true;
		var stamp = $"{c.Sha}|{modsBox.Text.Trim()}|{ModsStamp(modsBox.Text.Trim())}";
		if (stamp == settings.LastModCheckPassed) { Append("Mods unchanged since the last check against this version; not checking again."); return true; }
		status.Text = "Checking mods...";
		var gameDir = WorktreePath(c);
		var result = await RunModCheck(gameDir, false, ct);
		if (result == null) return !ct.IsCancellationRequested; // a check that couldn't run never stops a launch
		Append(Summary(result));
		if (result.Errors == 0) { settings.LastModCheckPassed = stamp; return true; }
		using var form = new ModCheckForm(this, result, gameDir, true);
		if (form.ShowDialog(this) != DialogResult.OK) return false;
		settings.LastModCheckPassed = $"{c.Sha}|{modsBox.Text.Trim()}|{ModsStamp(modsBox.Text.Trim())}"; // after any fixes made in the window
		return true;
	}

	// Newest write time and file count of the mods folder: changes whenever a mod is added, removed or edited.
	static string ModsStamp(string dir)
	{
		try
		{
			long newest = 0; int n = 0;
			foreach (var f in new DirectoryInfo(dir).EnumerateFiles("*", SearchOption.AllDirectories)) { n++; newest = Math.Max(newest, f.LastWriteTimeUtc.Ticks); }
			foreach (var d in new DirectoryInfo(dir).EnumerateDirectories("*", SearchOption.AllDirectories)) newest = Math.Max(newest, d.LastWriteTimeUtc.Ticks);
			return $"{n}|{newest}";
		}
		catch { return Guid.NewGuid().ToString(); }
	}

	static string Summary(ModCheckRun r) =>
		$"Mods checked against {r.Version}: {r.Mods.Count} mod(s), {r.Errors} error(s), {r.Warnings} warning(s)" + (r.Fixable > 0 ? $", {r.Fixable} fixable" : "");

	// Used by the results window: makes the fixes, then checks again so the window shows what's left.
	internal async Task<ModCheckRun?> FixMods(string gameDir, Action<string>? step = null)
	{
		cts = new CancellationTokenSource();
		var wasBusy = busy;
		if (!wasBusy) SetBusy(true, "Fixing mods...");
		try
		{
			step?.Invoke("Making the fixes in your mods...");
			if (await RunModCheck(gameDir, true, cts.Token) == null) return null;
			settings.LastModCheckPassed = "";
			step?.Invoke("Checking the mods again...");
			var after = await RunModCheck(gameDir, false, cts.Token);
			if (after != null) Append(Summary(after));
			return after;
		}
		finally { if (!wasBusy) SetBusy(false, currentRef); }
	}
}

// What the check found: the mods on top with their counts, the selected mod's findings below.
// Opened after Check mods, or before a launch when a mod has an error (then its buttons are Launch anyway / Don't launch).
class ModCheckForm : Form
{
	readonly MainForm owner;
	readonly string gameDir;
	ModCheckRun run;
	readonly ListView modList = new() { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, HideSelection = false, MultiSelect = false };
	readonly ListView findingList = new() { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, HideSelection = false, MultiSelect = false };
	readonly Label summary = new() { Dock = DockStyle.Top, AutoSize = false, Height = 44, Padding = new Padding(8, 8, 8, 0) };
	readonly ProgressBar progress = new() { Style = ProgressBarStyle.Marquee, MarqueeAnimationSpeed = 30, Dock = DockStyle.Top, Height = 14 };
	readonly Label progressText = new() { Dock = DockStyle.Top, AutoSize = false, Height = 22, Padding = new Padding(8, 4, 0, 0), Font = new Font(SystemFonts.DefaultFont, FontStyle.Bold) };
	readonly Panel progressPanel = new() { Dock = DockStyle.Top, Height = 40, Visible = false };
	readonly System.Windows.Forms.Timer tick = new() { Interval = 500 };
	readonly Button fixBtn = new() { Text = "Fix", AutoSize = true };
	readonly Button reportBtn = new() { Text = "Open report", AutoSize = true };
	readonly Button folderBtn = new() { Text = "Open mod folder", AutoSize = true };

	public ModCheckForm(MainForm owner, ModCheckRun run, string gameDir, bool beforeLaunch)
	{
		this.owner = owner; this.run = run; this.gameDir = gameDir;
		Text = beforeLaunch ? "Mod check: some mods have errors" : "Mod check";
		Width = 1100; Height = 640;
		StartPosition = FormStartPosition.CenterParent;

		modList.Columns.Add("Mod", 260);
		modList.Columns.Add("Errors", 60);
		modList.Columns.Add("Warnings", 70);
		modList.Columns.Add("Notes", 60);
		modList.Columns.Add("Fixable", 60);
		findingList.Columns.Add("", 70);
		findingList.Columns.Add("Where", 280);
		findingList.Columns.Add("Problem", 560);
		findingList.Columns.Add("Fix does", 260);
		findingList.ShowItemToolTips = true;

		var split = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, SplitterDistance = 200 };
		split.Panel1.Controls.Add(modList);
		split.Panel2.Controls.Add(findingList);

		var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, AutoSize = true, FlowDirection = FlowDirection.LeftToRight, Padding = new Padding(4) };
		buttons.Controls.AddRange(new Control[] { fixBtn, reportBtn, folderBtn });
		if (beforeLaunch)
		{
			var launch = new Button { Text = "Launch anyway", AutoSize = true, DialogResult = DialogResult.OK };
			var stop = new Button { Text = "Don't launch", AutoSize = true, DialogResult = DialogResult.Cancel };
			buttons.Controls.AddRange(new Control[] { new Label { Width = 40 }, launch, stop });
			AcceptButton = stop; CancelButton = stop;
		}
		else
		{
			var close = new Button { Text = "Close", AutoSize = true, DialogResult = DialogResult.Cancel };
			buttons.Controls.AddRange(new Control[] { new Label { Width = 40 }, close });
			CancelButton = close;
		}

		progressPanel.Controls.Add(progressText);
		progressPanel.Controls.Add(progress);
		Controls.Add(split);
		Controls.Add(buttons);
		Controls.Add(progressPanel);
		Controls.Add(summary);

		modList.SelectedIndexChanged += (_, _) => ShowFindings();
		findingList.DoubleClick += (_, _) => OpenFinding();
		reportBtn.Click += (_, _) => { if (SelectedMod() is { } m) Open(Path.Combine(MainForm.ModChecksDir, m.Name + ".txt")); };
		folderBtn.Click += (_, _) => { if (SelectedMod() is { } m) Open(m.Path); };
		fixBtn.Click += async (_, _) => await Fix();
		Fill();
	}

	ModResult? SelectedMod() => modList.SelectedItems.Count > 0 ? (ModResult)modList.SelectedItems[0].Tag! : null;

	void Fill()
	{
		summary.Text = $"Checked {run.Mods.Count} mod(s) against version {run.Version} ({run.Game}).\n"
			+ $"{run.Errors} error(s) (it won't load, or a script stops), {run.Warnings} warning(s) (part of it won't work). Double-click a finding to open its file.";
		var keep = SelectedMod()?.Name;
		modList.BeginUpdate();
		modList.Items.Clear();
		foreach (var m in run.Mods.OrderByDescending(m => m.Errors).ThenByDescending(m => m.Warnings).ThenBy(m => m.Name))
		{
			var it = new ListViewItem(m.Name) { Tag = m };
			it.SubItems.Add(m.Errors.ToString());
			it.SubItems.Add(m.Warnings.ToString());
			it.SubItems.Add(m.Notes.ToString());
			it.SubItems.Add(m.Fixable > 0 ? m.Fixable.ToString() : "");
			it.ForeColor = m.Errors > 0 ? Color.Firebrick : m.Warnings > 0 ? Color.DarkGoldenrod : Color.ForestGreen;
			modList.Items.Add(it);
		}
		modList.EndUpdate();
		var select = modList.Items.Cast<ListViewItem>().FirstOrDefault(i => ((ModResult)i.Tag!).Name == keep) ?? (modList.Items.Count > 0 ? modList.Items[0] : null);
		if (select != null) select.Selected = true;
		fixBtn.Text = run.Fixable > 0 ? $"Fix {run.Fixable} problem(s)" : "Nothing to fix";
		fixBtn.Enabled = run.Fixable > 0;
		ShowFindings();
	}

	void ShowFindings()
	{
		findingList.BeginUpdate();
		findingList.Items.Clear();
		var m = SelectedMod();
		reportBtn.Enabled = folderBtn.Enabled = m != null;
		if (m != null)
			foreach (var f in m.Findings)
			{
				var it = new ListViewItem(f.Severity) { Tag = f, ToolTipText = f.Message };
				it.SubItems.Add(f.Line > 0 ? $"{f.File}:{f.Line}" : f.File);
				it.SubItems.Add(f.Message);
				it.SubItems.Add(f.Fix ?? "");
				it.ForeColor = f.Severity == "ERROR" ? Color.Firebrick : f.Severity == "WARNING" ? Color.DarkGoldenrod : SystemColors.GrayText;
				findingList.Items.Add(it);
			}
		findingList.EndUpdate();
	}

	void OpenFinding()
	{
		if (findingList.SelectedItems.Count == 0 || SelectedMod() is not { } m) return;
		var f = (ModFinding)findingList.SelectedItems[0].Tag!;
		// Files are named from the folder the mod is in (Some.rte/...).
		var path = Path.Combine(Path.GetDirectoryName(m.Path)!, f.File.Replace('/', Path.DirectorySeparatorChar));
		if (File.Exists(path)) Open(path);
	}

	static void Open(string path)
	{
		try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
		catch (Exception ex) { MessageBox.Show(ex.Message, "Open"); }
	}

	async Task Fix()
	{
		var changes = run.Mods.SelectMany(m => m.Findings.Where(f => f.Fix != null).Select(f => $"{f.File}:{f.Line}   {f.Fix}")).ToList();
		var list = string.Join("\n", changes.Take(25)) + (changes.Count > 25 ? $"\n...and {changes.Count - 25} more" : "");
		if (MessageBox.Show(this, $"Make these {changes.Count} change(s) in your mods folder?\n\n{list}\n\nA .bak copy of each changed file is kept next to it.",
			"Fix mods", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
		// The window stays alive (not greyed out) with a moving bar, the step it is on and a running clock, so a long fix doesn't look like a crash.
		var started = DateTime.Now;
		var stepText = "Working...";
		void Show() => progressText.Text = $"{stepText}   {(DateTime.Now - started):mm\\:ss} elapsed. Please wait, the window will update when it's done.";
		tick.Tick += (_, _) => Show();
		progressPanel.Visible = true; Show(); tick.Start();
		UseWaitCursor = true;
		foreach (Control c in new Control[] { fixBtn, reportBtn, folderBtn, modList, findingList }) c.Enabled = false;
		ControlBox = false; // no closing the window half-way through a fix
		try
		{
			var after = await owner.FixMods(gameDir, t => { stepText = t; Show(); });
			if (after != null) { run = after; Fill(); }
		}
		finally
		{
			tick.Stop(); progressPanel.Visible = false; UseWaitCursor = false; ControlBox = true;
			foreach (Control c in new Control[] { reportBtn, folderBtn, modList, findingList }) c.Enabled = true;
			ShowFindings();
			fixBtn.Enabled = run.Fixable > 0;
		}
	}
}
