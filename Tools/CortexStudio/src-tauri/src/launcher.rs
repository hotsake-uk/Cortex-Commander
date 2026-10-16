// The launcher's jobs inside Cortex Studio (phase 2 of the plan): what the setup screen checks, the versions of the game in the repo,
// building one, and launching it with your Settings.ini, presets, mods and world. Ported from Tools/Launcher (the Windows launcher), so
// it keeps the same model: the repo is only fetched from, and the version you build is a git worktree under CortexVersions. Cortex
// Studio's worktree is CortexVersions/Studio, so it never moves the launcher's own CortexVersions/Current.
//
// The page asks for all of this through the server's "launcher/..." requests (see handle at the bottom). A build or a fetch is a job:
// it runs in the background and the page reads its output with "launcher/job".

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::fs;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::{Arc, Mutex};
use std::time::Instant;

// ---- settings ----

#[derive(Clone, Serialize, Deserialize, Default)]
#[serde(default, rename_all = "camelCase")]
pub struct Config {
	/// "" until the setup screen is done, then "simple" or "advanced".
	pub mode: String,
	pub repo: String,
	pub remote: String,
	/// Where worktrees go; "" is CortexVersions next to the repo.
	pub versions_dir: String,
	pub slot: String,
	/// The branch Simple mode keeps up to date.
	pub branch: String,
	/// Your own checkout of the game (a git worktree of your work). When set, it is the slot: built and launched in place, never
	/// fetched into, moved to another commit or deleted. "" keeps the branch builds in `versions_dir`.
	pub work_dir: String,
	pub last_ref: String,
	pub configuration: String,
	pub settings_ini: String,
	pub presets_dir: String,
	pub mods_dir: String,
	pub world: String,
	pub map: String,
}

pub fn config_dir() -> Option<PathBuf> {
	let base = if cfg!(windows) {
		std::env::var_os("APPDATA").map(PathBuf::from)
	} else if cfg!(target_os = "macos") {
		std::env::var_os("HOME").map(|h| PathBuf::from(h).join("Library").join("Application Support"))
	} else {
		std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))
	};
	base.map(|b| b.join("CortexStudio"))
}

fn launcher_dir() -> Option<PathBuf> {
	// The Windows launcher's own folder: its settings are the starting values here, and its Worlds folder is shared.
	std::env::var_os("APPDATA").map(|a| PathBuf::from(a).join("CortexLauncher"))
}

fn config_file() -> Option<PathBuf> {
	config_dir().map(|d| d.join("launcher.json"))
}

impl Config {
	pub fn load(game_dir: &Path) -> Config {
		let saved = config_file().and_then(|f| fs::read_to_string(f).ok()).and_then(|t| serde_json::from_str::<Config>(&t).ok());
		let mut config = saved.unwrap_or_else(|| Config::from_launcher().unwrap_or_default());
		if config.repo.is_empty() {
			// Studio started in a checkout: that is the repo.
			if game_dir.join(".git").exists() {
				config.repo = game_dir.to_string_lossy().into_owned();
			}
		}
		config.fill_defaults();
		config
	}

	/// The Windows launcher's settings.json, so moving to Cortex Studio keeps your repo, Settings.ini, presets, mods and world.
	fn from_launcher() -> Option<Config> {
		let text = fs::read_to_string(launcher_dir()?.join("settings.json")).ok()?;
		let v: Value = serde_json::from_str(&text).ok()?;
		let s = |k: &str| v.get(k).and_then(|x| x.as_str()).unwrap_or("").to_string();
		Some(Config {
			repo: s("RepoPath"),
			remote: s("Remote"),
			versions_dir: s("VersionsDir"),
			configuration: s("Configuration"),
			settings_ini: s("SettingsIni"),
			presets_dir: s("PresetsDir"),
			mods_dir: s("ModsDir"),
			world: s("World"),
			last_ref: s("LastRef"),
			..Config::default()
		})
	}

	fn fill_defaults(&mut self) {
		if self.remote.is_empty() {
			self.remote = "origin".into();
		}
		if self.slot.is_empty() {
			self.slot = "Studio".into();
		}
		if self.branch.is_empty() {
			self.branch = "maindev".into();
		}
		let valid = configurations();
		if !valid.contains(&self.configuration.as_str()) {
			self.configuration = valid[0].into();
		}
	}

	pub fn save(&self) {
		if let Some(file) = config_file() {
			let _ = fs::create_dir_all(file.parent().unwrap());
			let _ = fs::write(file, serde_json::to_string_pretty(self).unwrap());
		}
	}

	pub fn versions_dir(&self) -> PathBuf {
		if !self.versions_dir.is_empty() {
			return PathBuf::from(&self.versions_dir);
		}
		let repo = PathBuf::from(&self.repo);
		repo.parent().map(|p| p.join("CortexVersions")).unwrap_or_else(|| repo.join("CortexVersions"))
	}

	pub fn slot_path(&self) -> PathBuf {
		if !self.work_dir.is_empty() {
			return PathBuf::from(&self.work_dir);
		}
		self.versions_dir().join(&self.slot)
	}

	fn worlds_dir(&self) -> PathBuf {
		match launcher_dir() {
			Some(d) if d.join("Worlds").is_dir() => d.join("Worlds"),
			_ => config_dir().unwrap_or_default().join("Worlds"),
		}
	}
}

fn configurations() -> Vec<&'static str> {
	if cfg!(windows) {
		vec!["Final", "Debug Release", "Debug Minimal", "Debug Full"]
	} else {
		vec!["release", "debug"]
	}
}

/// Where a build of `configuration` puts the game in a checkout.
fn exe_path(slot: &Path, configuration: &str) -> PathBuf {
	if cfg!(windows) {
		slot.join(match configuration {
			"Debug Release" => "Cortex Command.debug.release.exe",
			"Debug Minimal" => "Cortex Command.debug.minimal.exe",
			"Debug Full" => "Cortex Command.debug.full.exe",
			_ => "Cortex Command.exe",
		})
	} else {
		// meson names the program CortexCommand for every build type; the build folder tells them apart.
		slot.join(format!("build-{configuration}")).join("CortexCommand")
	}
}

const BUILT_MARKER: &str = ".cortex-studio-built";
const COPY_MARKER: &str = ".launcher-copy";
const WORLD_MARKER: &str = ".launcher-world";

/// "sha configuration" of the last good build in the slot.
fn built_info(slot: &Path) -> String {
	fs::read_to_string(slot.join(BUILT_MARKER)).map(|s| s.trim().to_string()).unwrap_or_default()
}

// ---- running programs ----

#[cfg(windows)]
fn quiet(command: &mut Command) -> &mut Command {
	use std::os::windows::process::CommandExt;
	command.creation_flags(0x0800_0000) // CREATE_NO_WINDOW
}

#[cfg(not(windows))]
fn quiet(command: &mut Command) -> &mut Command {
	command
}

/// Runs a program and returns its output, or what went wrong.
fn capture(program: &str, args: &[&str], cwd: Option<&Path>) -> Result<String, String> {
	let mut command = Command::new(program);
	command.args(args).stdin(Stdio::null());
	if let Some(dir) = cwd {
		command.current_dir(dir);
	}
	let out = quiet(&mut command).output().map_err(|e| format!("{program}: {e}"))?;
	if out.status.success() {
		Ok(String::from_utf8_lossy(&out.stdout).into_owned())
	} else {
		Err(format!("{}{}", String::from_utf8_lossy(&out.stdout), String::from_utf8_lossy(&out.stderr)).trim().to_string())
	}
}

fn git(repo: &Path, args: &[&str]) -> Result<String, String> {
	capture("git", args, Some(repo))
}

#[derive(Default)]
pub struct Job {
	pub id: u64,
	pub title: String,
	pub lines: Vec<String>,
	/// "idle", "running", "ok", "failed" or "cancelled".
	pub state: String,
	pid: Option<u32>,
	cancel: bool,
}

#[derive(Clone)]
struct JobCtx(Arc<Mutex<Job>>);

impl JobCtx {
	fn log(&self, line: impl Into<String>) {
		let mut job = self.0.lock().unwrap();
		job.lines.push(line.into());
		if job.lines.len() > 20000 {
			job.lines.drain(0..5000);
		}
	}

	fn cancelled(&self) -> bool {
		self.0.lock().unwrap().cancel
	}

	/// Runs a program, its output going to the job's log. Ok(()) only when it exits with 0.
	fn run(&self, program: &str, args: &[&str], cwd: &Path, env: &[(&str, String)]) -> Result<(), String> {
		if self.cancelled() {
			return Err("Cancelled.".into());
		}
		self.log(format!("> {} {}", Path::new(program).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(), args.join(" ")));
		let mut command = Command::new(program);
		command.args(args).current_dir(cwd).stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped());
		for (k, v) in env {
			command.env(k, v);
		}
		#[cfg(unix)]
		{
			// Its own process group, so Cancel stops the compilers it starts as well.
			use std::os::unix::process::CommandExt;
			command.process_group(0);
		}
		let mut child = quiet(&mut command).spawn().map_err(|e| format!("Couldn't start {program}: {e}"))?;
		self.0.lock().unwrap().pid = Some(child.id());
		let stderr = child.stderr.take().unwrap();
		let errors = self.clone();
		let reader = std::thread::spawn(move || {
			for line in BufReader::new(stderr).lines().map_while(Result::ok) {
				errors.log(line);
			}
		});
		for line in BufReader::new(child.stdout.take().unwrap()).lines().map_while(Result::ok) {
			self.log(line);
		}
		let _ = reader.join();
		let status = child.wait().map_err(|e| e.to_string())?;
		self.0.lock().unwrap().pid = None;
		if self.cancelled() {
			return Err("Cancelled.".into());
		}
		if status.success() {
			Ok(())
		} else {
			Err(format!("{} stopped with {}", Path::new(program).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(), status))
		}
	}
}

pub struct Launcher {
	pub config: Mutex<Config>,
	job: Arc<Mutex<Job>>,
}

impl Launcher {
	pub fn new(game_dir: &Path) -> Launcher {
		Launcher { config: Mutex::new(Config::load(game_dir)), job: Arc::new(Mutex::new(Job { state: "idle".into(), ..Job::default() })) }
	}

	fn start_job<F>(&self, title: &str, work: F) -> Result<u64, String>
	where
		F: FnOnce(&JobCtx) -> Result<(), String> + Send + 'static,
	{
		let id = {
			let mut job = self.job.lock().unwrap();
			if job.state == "running" {
				return Err(format!("Busy: {}.", job.title));
			}
			*job = Job { id: job.id + 1, title: title.into(), state: "running".into(), ..Job::default() };
			job.id
		};
		let ctx = JobCtx(self.job.clone());
		std::thread::spawn(move || {
			let started = Instant::now();
			let result = work(&ctx);
			let seconds = started.elapsed().as_secs();
			match &result {
				Ok(()) => ctx.log(format!("Done in {}:{:02}.", seconds / 60, seconds % 60)),
				Err(e) => ctx.log(e.clone()),
			}
			let mut job = ctx.0.lock().unwrap();
			job.state = if job.cancel { "cancelled" } else if result.is_ok() { "ok" } else { "failed" }.into();
		});
		Ok(id)
	}

	fn cancel(&self) {
		let pid = {
			let mut job = self.job.lock().unwrap();
			job.cancel = true;
			job.pid
		};
		if let Some(pid) = pid {
			// The build's own children (compilers) go too.
			let _ = if cfg!(windows) {
				quiet(Command::new("taskkill").args(["/T", "/F", "/PID", &pid.to_string()])).status()
			} else {
				Command::new("kill").args(["-TERM", "--", &format!("-{pid}")]).status()
			};
		}
	}
}

// ---- dependencies ----

fn found_version(program: &str, args: &[&str]) -> Option<String> {
	capture(program, args, None).ok().map(|o| o.lines().next().unwrap_or("").trim().to_string())
}

fn find_msbuild() -> Option<PathBuf> {
	let pf86 = std::env::var_os("ProgramFiles(x86)").map(PathBuf::from).unwrap_or_else(|| PathBuf::from(r"C:\Program Files (x86)"));
	let vswhere = pf86.join(r"Microsoft Visual Studio\Installer\vswhere.exe");
	let out = capture(&vswhere.to_string_lossy(), &["-latest", "-products", "*", "-requires", "Microsoft.Component.MSBuild", "-find", r"MSBuild\**\Bin\MSBuild.exe"], None).ok()?;
	out.lines().map(|l| PathBuf::from(l.trim())).find(|p| p.is_file())
}

fn linux_distro() -> String {
	let text = fs::read_to_string("/etc/os-release").unwrap_or_default();
	let get = |key: &str| text.lines().find_map(|l| l.strip_prefix(&format!("{key}="))).unwrap_or("").trim_matches('"').to_lowercase();
	format!("{} {}", get("ID"), get("ID_LIKE"))
}

/// What building and running the game needs on this computer: each found or missing, with the way to get it on this platform.
fn dependencies() -> Value {
	let mut rows = Vec::new();
	let mut row = |name: &str, needed: &str, found: Option<String>, fix: &str| {
		rows.push(json!({ "name": name, "neededFor": needed, "ok": found.is_some(), "found": found.unwrap_or_default(), "fix": fix }));
	};
	let platform;
	if cfg!(windows) {
		platform = "Windows";
		row("Git", "Getting the game's code", found_version("git", &["--version"]), "winget install Git.Git   (or https://git-scm.com/download/win)");
		row(
			"Visual Studio 2022 C++ build tools",
			"Building the game",
			find_msbuild().map(|p| p.to_string_lossy().into_owned()),
			"winget install Microsoft.VisualStudio.2022.BuildTools --override \"--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive\"",
		);
		row("Python 3", "The mod check (optional)", found_version("py", &["-3", "--version"]).or_else(|| found_version("python", &["--version"])), "winget install Python.Python.3.12   (or python.org, with Add to PATH ticked)");
	} else if cfg!(target_os = "macos") {
		platform = "macOS";
		row("Xcode command line tools", "Git and building", found_version("xcode-select", &["-p"]), "xcode-select --install");
		row("Homebrew", "Installing the libraries", found_version("brew", &["--version"]), "/bin/bash -c \"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\"");
		let libs = "brew install pkg-config sdl3 minizip lz4 flac luajit lua libpng tbb gcc@13 ninja meson dylibbundler";
		row("meson 1.6 or newer", "Building the game", found_version("meson", &["--version"]), libs);
		row("ninja", "Building the game", found_version("ninja", &["--version"]), libs);
		row("gcc 13", "Building the game", found_version("gcc-13", &["--version"]), libs);
		row("Python 3", "The mod check (optional)", found_version("python3", &["--version"]), "brew install python");
	} else {
		platform = "Linux";
		let distro = linux_distro();
		let libs = if distro.contains("arch") {
			"sudo pacman -S tbb flac minizip lz4 libpng mesa meson ninja base-devel"
		} else if distro.contains("fedora") {
			"sudo dnf install gcc-c++ flac-devel minizip-devel lz4-devel libpng-devel tbb-devel mesa-libGL-devel ninja-build meson"
		} else {
			"sudo apt-get install build-essential libflac++-dev libminizip-dev liblz4-dev libpng-dev libtbb-dev libgl-dev ninja-build python3-pip && pip install meson"
		};
		row("Git", "Getting the game's code", found_version("git", &["--version"]), if distro.contains("arch") { "sudo pacman -S git" } else if distro.contains("fedora") { "sudo dnf install git" } else { "sudo apt-get install git" });
		row("meson 1.6 or newer", "Building the game", found_version("meson", &["--version"]), libs);
		row("ninja", "Building the game", found_version("ninja", &["--version"]), libs);
		row("g++ 13 or newer", "Building the game", found_version("g++", &["--version"]), libs);
		let pkg = |name: &str| capture("pkg-config", &["--modversion", name], None).ok().map(|v| v.trim().to_string());
		// What meson.build asks the system for (LuaJIT, SDL and the rest come with the code).
		let wanted = ["flac", "minizip", "liblz4", "libpng", "tbb", "gl"];
		let missing: Vec<&str> = wanted.iter().copied().filter(|n| pkg(n).is_none()).collect();
		row("Game libraries", "Building the game", missing.is_empty().then(|| wanted.join(", ")), &format!("{libs}   (missing: {})", missing.join(", ")));
		row("Python 3", "The mod check (optional)", found_version("python3", &["--version"]), "sudo apt-get install python3");
	}
	json!({ "platform": platform, "rows": rows })
}

// ---- versions ----

/// "8.5.230" from a commit subject "[8.5.230] ..." or a body line "Version 8.5.230".
fn version_in(subject: &str, body: &str) -> Option<String> {
	let is_version = |s: &str| s.split('.').count() == 3 && s.split('.').all(|p| !p.is_empty() && p.chars().all(|c| c.is_ascii_digit()));
	if let Some(rest) = subject.strip_prefix('[') {
		if let Some((v, _)) = rest.split_once(']') {
			if is_version(v) {
				return Some(v.to_string());
			}
		}
	}
	// Squash merges: "8.5.248 - Vehicles: ...".
	if let Some((v, _)) = subject.split_once(" - ") {
		if is_version(v.trim()) {
			return Some(v.trim().to_string());
		}
	}
	body.lines().filter_map(|l| l.trim().strip_prefix("Version ")).map(str::trim).find(|v| is_version(v)).map(str::to_string)
}

fn refs(config: &Config) -> Result<Value, String> {
	let repo = Path::new(&config.repo);
	let tags = git(repo, &["for-each-ref", "--sort=-version:refname", "--format=%(refname:short)", "refs/tags"])?;
	let branches = git(repo, &["for-each-ref", "--sort=-committerdate", "--format=%(refname:short)", &format!("refs/remotes/{}", config.remote)])?;
	// Versions from the integration branch's merge history, as the launcher reads them (cloud threads can't push tags).
	let mut versions: Vec<(String, String)> = Vec::new();
	if let Ok(log) = git(repo, &["log", &format!("{}/maindev", config.remote), "--first-parent", "--format=%H%x1f%s%x1f%b%x1e", "--"]) {
		let mut seen = std::collections::HashSet::new();
		for record in log.split('\x1e') {
			let fields: Vec<&str> = record.trim_matches(['\n', '\r']).split('\x1f').collect();
			if fields.len() < 2 {
				continue;
			}
			if let Some(v) = version_in(fields[1], fields.get(2).copied().unwrap_or("")) {
				if seen.insert(v.clone()) {
					versions.push((v, fields[0].to_string()));
				}
			}
		}
	}
	let mut list: Vec<Value> = versions.iter().map(|(v, sha)| json!({ "name": format!("ver: {v}"), "ref": sha })).collect();
	list.extend(tags.lines().filter(|t| !t.trim().is_empty()).map(|t| json!({ "name": format!("tag: {}", t.trim()), "ref": t.trim() })));
	list.extend(
		branches
			.lines()
			.map(str::trim)
			.filter(|b| !b.is_empty() && !b.ends_with("/HEAD") && *b != config.remote)
			.map(|b| json!({ "name": b, "ref": b })),
	);
	Ok(json!({ "refs": list, "default": format!("{}/{}", config.remote, config.branch) }))
}

fn commits(config: &Config, reference: &str) -> Result<Value, String> {
	let repo = Path::new(&config.repo);
	let log = git(repo, &["log", reference, "-n", "60", "--date-order", "--date=iso", "--format=%H%x09%cd%x09%an%x09%s", "--"])?;
	let built = built_info(&config.slot_path());
	let rows: Vec<Value> = log
		.lines()
		.filter_map(|l| {
			let p: Vec<&str> = l.splitn(4, '\t').collect();
			(p.len() == 4).then(|| {
				json!({ "sha": p[0], "date": p[1].chars().take(16).collect::<String>(), "author": p[2], "subject": p[3],
					"built": built == format!("{} {}", p[0], config.configuration) })
			})
		})
		.collect();
	Ok(json!({ "commits": rows }))
}

/// The game version at each commit: the commit that brought a version in reads "8.5.230", later ones "8.5.230+N".
fn versions(config: &Config, shas: &[String]) -> Value {
	let repo = Path::new(&config.repo);
	let mut adders: HashMap<String, String> = HashMap::new();
	let mut out = serde_json::Map::new();
	for sha in shas {
		let text = git(repo, &["show", &format!("{sha}:Source/System/GameVersion.h")]).unwrap_or_default();
		let version = text.split("c_VersionString").nth(1).and_then(|r| r.split('"').nth(1)).map(str::to_string);
		let Some(v) = version else {
			out.insert(sha.clone(), json!("?"));
			continue;
		};
		let adder = adders
			.entry(v.clone())
			.or_insert_with(|| git(repo, &["log", sha, "--format=%H", "-n", "1", "--", "Source/System/GameVersion.h"]).map(|o| o.trim().to_string()).unwrap_or_default())
			.clone();
		let label = if adder.is_empty() || &adder == sha {
			v
		} else {
			match git(repo, &["rev-list", "--count", sha, &format!("^{adder}")]).ok().and_then(|o| o.trim().parse::<u32>().ok()) {
				Some(n) if n > 0 => format!("{v}+{n}"),
				_ => v,
			}
		};
		out.insert(sha.clone(), json!(label));
	}
	Value::Object(out)
}

// ---- building ----

fn ensure_worktree(ctx: &JobCtx, config: &Config, sha: &str) -> Result<(), String> {
	if !config.work_dir.is_empty() {
		// Your own copy is built where it is, at whatever you have checked out.
		return Ok(());
	}
	let repo = PathBuf::from(&config.repo);
	let slot = config.slot_path();
	if slot.join(".git").exists() {
		if git(&slot, &["rev-parse", "HEAD"]).map(|h| h.trim() == sha).unwrap_or(false) {
			return Ok(());
		}
		// Move the one checkout to the chosen commit; build outputs are untracked, so the next build is incremental.
		ctx.log(format!("Moving {} to {}", slot.display(), &sha[..sha.len().min(10)]));
		return ctx.run("git", &["checkout", "-f", "--detach", sha], &slot, &[]);
	}
	fs::create_dir_all(config.versions_dir()).map_err(|e| e.to_string())?;
	let _ = git(&repo, &["worktree", "prune"]);
	ctx.run("git", &["worktree", "add", "--detach", &slot.to_string_lossy(), sha], &repo, &[])
}

fn build(ctx: &JobCtx, config: &Config, sha: &str) -> Result<(), String> {
	ensure_worktree(ctx, config, sha)?;
	let slot = config.slot_path();
	let configuration = config.configuration.as_str();
	let _ = fs::remove_file(slot.join(BUILT_MARKER));
	if cfg!(windows) {
		let msbuild = find_msbuild().ok_or("MSBuild not found. Install Visual Studio 2022 with the Desktop development with C++ workload (see Setup).")?;
		// fmod.dll sits next to the exe.
		let fmod = slot.join("external").join("lib").join("win").join("fmod.dll");
		if fmod.is_file() {
			let _ = fs::copy(&fmod, slot.join("fmod.dll"));
		}
		ctx.log(format!("Building {} ({configuration})", &sha[..10.min(sha.len())]));
		// Same command as .github/workflows/msbuild.yml, with x64 made explicit.
		ctx.run(
			&msbuild.to_string_lossy(),
			&["/m", "/nologo", "/v:m", &format!("/p:Configuration={configuration}"), "/p:Platform=x64", "RTEA.sln"],
			&slot,
			&[],
		)?;
	} else {
		let dir = format!("build-{configuration}");
		let mut env: Vec<(&str, String)> = if cfg!(target_os = "macos") { vec![("CC", "gcc-13".into()), ("CXX", "g++-13".into())] } else { vec![] };
		// Homebrew's gcc looks for the SDK it was built against (CommandLineTools/SDKs/MacOSX15.sdk, say), which older or
		// newer command line tools don't have ("ld: library 'System' not found"). Point it at the SDK this Mac has.
		if cfg!(target_os = "macos") && std::env::var_os("SDKROOT").is_none() {
			if let Ok(sdk) = capture("xcrun", &["--show-sdk-path"], None) {
				env.push(("SDKROOT", sdk.trim().to_string()));
			}
		}
		if !slot.join(&dir).join("build.ninja").is_file() {
			let mut args = vec!["setup", dir.as_str(), "--buildtype", configuration];
			if slot.join(&dir).is_dir() {
				args.push("--wipe"); // a setup that was stopped part way
			}
			if cfg!(target_os = "macos") {
				args.push("-Db_lto=false");
			}
			ctx.run("meson", &args, &slot, &env)?;
		}
		ctx.run("ninja", &["-C", &dir], &slot, &env)?;
	}
	if !exe_path(&slot, configuration).is_file() {
		return Err(format!("The build finished but {} isn't there.", exe_path(&slot, configuration).display()));
	}
	fs::write(slot.join(BUILT_MARKER), format!("{sha} {configuration}")).map_err(|e| e.to_string())?;
	ctx.log(format!("Built {} ({configuration}).", &sha[..10.min(sha.len())]));
	Ok(())
}

/// Simple mode: fetch, and build the newest commit of the chosen branch if it isn't already the one built.
fn update(ctx: &JobCtx, config: &Config) -> Result<(), String> {
	if !config.work_dir.is_empty() {
		// Your own copy: no fetch, just build what is there (the build tools only redo what changed).
		let work = config.slot_path();
		if !work.join("RTEA.sln").is_file() {
			return Err(format!("{} doesn't look like a checkout of the game.", work.display()));
		}
		let sha = git(&work, &["rev-parse", "HEAD"]).map(|h| h.trim().to_string()).unwrap_or_else(|_| "working-copy".into());
		ctx.log(format!("Building your copy in {}.", work.display()));
		return build(ctx, config, &sha);
	}
	let repo = PathBuf::from(&config.repo);
	ctx.run("git", &["fetch", &config.remote, "--prune"], &repo, &[])?;
	let reference = format!("{}/{}", config.remote, config.branch);
	let sha = git(&repo, &["rev-parse", &reference])?.trim().to_string();
	if built_info(&config.slot_path()) == format!("{sha} {}", config.configuration) && exe_path(&config.slot_path(), &config.configuration).is_file() {
		ctx.log(format!("{reference} is at {}, which is already built. Nothing to do.", &sha[..10]));
		return Ok(());
	}
	ctx.log(format!("{reference} is at {}. Building it.", &sha[..10]));
	build(ctx, config, &sha)
}

// ---- launching ----

fn sync_dir(src: &Path, dest: &Path) -> std::io::Result<u32> {
	// Changed files only, each a hard link to your file where it can be (the same file, so nothing is read or virus-scanned afresh), else a copy.
	let mut n = 0;
	fs::create_dir_all(dest)?;
	for entry in fs::read_dir(src)? {
		let entry = entry?;
		let from = entry.path();
		let to = dest.join(entry.file_name());
		if entry.file_type()?.is_dir() {
			n += sync_dir(&from, &to)?;
			continue;
		}
		let a = entry.metadata()?;
		if let Ok(b) = fs::metadata(&to) {
			if a.len() == b.len() && a.modified().ok() == b.modified().ok() {
				continue;
			}
			fs::remove_file(&to)?;
		}
		if fs::hard_link(&from, &to).is_err() {
			fs::copy(&from, &to)?;
		}
		n += 1;
	}
	for entry in fs::read_dir(dest)? {
		let entry = entry?;
		let name = entry.file_name();
		if name == COPY_MARKER || src.join(&name).exists() {
			continue;
		}
		if entry.file_type()?.is_dir() {
			fs::remove_dir_all(entry.path())?;
		} else {
			fs::remove_file(entry.path())?;
		}
		n += 1;
	}
	Ok(n)
}

fn link_mods(config: &Config, slot: &Path, log: &mut Vec<String>) {
	if config.mods_dir.is_empty() {
		return;
	}
	let src = PathBuf::from(&config.mods_dir);
	let Ok(entries) = fs::read_dir(&src) else {
		log.push(format!("Mods folder not found: {}", src.display()));
		return;
	};
	let mods = slot.join("Mods");
	let _ = fs::create_dir_all(&mods);
	for entry in entries.filter_map(|e| e.ok()) {
		let name = entry.file_name().to_string_lossy().into_owned();
		if !entry.path().is_dir() || !name.to_lowercase().ends_with(".rte") {
			continue;
		}
		let dest = mods.join(&name);
		if dest.is_dir() && !dest.join(COPY_MARKER).exists() {
			log.push(format!("Skipping {name}: this version has its own."));
			continue;
		}
		match sync_dir(&entry.path(), &dest).and_then(|n| fs::write(dest.join(COPY_MARKER), "").map(|_| n)) {
			Ok(0) => log.push(format!("Mod {name} up to date.")),
			Ok(n) => log.push(format!("Linked mod {name} ({n} files changed).")),
			Err(e) => log.push(format!("Couldn't link mod {name}: {e}")),
		}
	}
}

fn link_presets(config: &Config, slot: &Path, log: &mut Vec<String>) {
	if config.presets_dir.is_empty() {
		return;
	}
	let shared = PathBuf::from(&config.presets_dir);
	let _ = fs::create_dir_all(&shared);
	let link = slot.join("Userdata").join("Presets");
	let _ = fs::create_dir_all(link.parent().unwrap());
	if let Ok(meta) = fs::symlink_metadata(&link) {
		let is_link = meta.file_type().is_symlink() || fs::read_link(&link).is_ok();
		if is_link {
			if fs::canonicalize(&link).ok() == fs::canonicalize(&shared).ok() {
				return;
			}
			let _ = fs::remove_dir(&link).or_else(|_| fs::remove_file(&link));
		} else {
			// The version's own presets move into the shared folder first, never lost.
			let _ = merge_missing(&link, &shared);
			let _ = fs::remove_dir_all(&link);
		}
	}
	let made = if cfg!(windows) {
		quiet(Command::new("cmd").args(["/c", "mklink", "/J", &link.to_string_lossy(), &shared.to_string_lossy()])).status().map(|s| s.success()).unwrap_or(false)
	} else {
		#[cfg(unix)]
		{
			std::os::unix::fs::symlink(&shared, &link).is_ok()
		}
		#[cfg(not(unix))]
		{
			false
		}
	};
	log.push(if made { format!("Presets linked to {}.", shared.display()) } else { "Couldn't link the presets folder.".into() });
}

fn merge_missing(from: &Path, to: &Path) -> std::io::Result<()> {
	fs::create_dir_all(to)?;
	for entry in fs::read_dir(from)? {
		let entry = entry?;
		let target = to.join(entry.file_name());
		if entry.file_type()?.is_dir() {
			merge_missing(&entry.path(), &target)?;
		} else if !target.exists() {
			fs::copy(entry.path(), &target)?;
		}
	}
	Ok(())
}

/// Copies a planet skin over the version's Data/Base.rte (files it still has only); going back to stock is a git checkout of that folder.
fn apply_world(config: &Config, slot: &Path, log: &mut Vec<String>) -> Result<(), String> {
	if !config.work_dir.is_empty() {
		// Skins are undone with a git checkout of Data/Base.rte, which would throw away edits in your own copy.
		if !config.world.is_empty() {
			log.push("World skins aren't applied to your own copy of the game.".into());
		}
		return Ok(());
	}
	let marker = slot.join(WORLD_MARKER);
	if marker.exists() {
		git(slot, &["checkout", "-f", "--", "Data/Base.rte"]).map_err(|e| format!("Couldn't restore stock Data/Base.rte: {e}"))?;
		let _ = fs::remove_file(&marker);
	}
	if config.world.is_empty() {
		return Ok(());
	}
	let mut src = config.worlds_dir().join(&config.world);
	if src.join("base.rte").is_dir() {
		src = src.join("base.rte");
	}
	if !src.is_dir() {
		return Err(format!("World folder not found: {}", src.display()));
	}
	fs::write(&marker, &config.world).map_err(|e| e.to_string())?;
	let base = slot.join("Data").join("Base.rte");
	let mut files = Vec::new();
	fn walk(dir: &Path, out: &mut Vec<PathBuf>) {
		for e in fs::read_dir(dir).into_iter().flatten().flatten() {
			if e.path().is_dir() {
				walk(&e.path(), out)
			} else {
				out.push(e.path())
			}
		}
	}
	walk(&src, &mut files);
	let (mut copied, mut skipped) = (0, 0);
	for f in files {
		let target = base.join(f.strip_prefix(&src).unwrap());
		if !target.is_file() {
			skipped += 1;
			continue;
		}
		fs::copy(&f, &target).map_err(|e| e.to_string())?;
		copied += 1;
	}
	log.push(format!("World {}: {copied} files over Data/Base.rte{}.", config.world, if skipped > 0 { format!(" ({skipped} the game no longer uses skipped)") } else { String::new() }));
	Ok(())
}

// ---- what the Play and Launch settings screens show ----

fn modified_secs(path: &Path) -> Option<u64> {
	fs::metadata(path).and_then(|m| m.modified()).ok().and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok()).map(|d| d.as_secs())
}

/// Files, bytes and the newest change in a folder, all the way down.
fn folder_stats(dir: &Path) -> (u64, u64, u64) {
	let (mut files, mut bytes, mut newest) = (0u64, 0u64, 0u64);
	let mut stack = vec![dir.to_path_buf()];
	while let Some(d) = stack.pop() {
		for entry in fs::read_dir(&d).into_iter().flatten().flatten() {
			let Ok(meta) = entry.metadata() else { continue };
			if meta.is_dir() {
				stack.push(entry.path());
			} else {
				files += 1;
				bytes += meta.len();
				let when = meta.modified().ok().and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok()).map(|d| d.as_secs()).unwrap_or(0);
				newest = newest.max(when);
			}
		}
	}
	(files, bytes, newest)
}

/// The "Key = Value" lines of a Settings.ini, in order (nested blocks' lines too, with their indent dropped).
fn settings_values(path: &Path) -> Vec<(String, String)> {
	fs::read_to_string(path)
		.unwrap_or_default()
		.lines()
		.filter(|l| l.starts_with('\t') && !l.trim_start().starts_with("//"))
		.filter_map(|l| l.trim().split_once('=').map(|(k, v)| (k.trim().to_string(), v.trim().to_string())))
		.filter(|(k, _)| !k.is_empty())
		.take(1000)
		.collect()
}

fn overview(config: &Config) -> Value {
	let slot = config.slot_path();
	let built = built_info(&slot);
	let sha = built.split(' ').next().unwrap_or("").to_string();
	let exe = exe_path(&slot, &config.configuration);
	let mut build = json!({ "sha": sha, "exe": exe, "exeModified": modified_secs(&exe) });
	if !sha.is_empty() && Path::new(&config.repo).join(".git").exists() {
		if let Ok(text) = git(Path::new(&config.repo), &["show", "-s", "--format=%s%x00%ci%x00%an%x00%b", &sha]) {
			let mut parts = text.splitn(4, '\0');
			let (subject, date, author, body) = (parts.next().unwrap_or(""), parts.next().unwrap_or(""), parts.next().unwrap_or(""), parts.next().unwrap_or(""));
			build["subject"] = json!(subject.trim());
			build["date"] = json!(date.trim());
			build["author"] = json!(author.trim());
			build["version"] = json!(version_in(subject.trim(), body));
		}
	}

	let settings_path = PathBuf::from(&config.settings_ini);
	let settings = json!({
		"exists": !config.settings_ini.is_empty() && settings_path.is_file(),
		"modified": modified_secs(&settings_path),
		"values": settings_values(&settings_path),
	});

	let presets_dir = PathBuf::from(&config.presets_dir);
	let presets: Vec<String> = fs::read_dir(&presets_dir).into_iter().flatten().flatten().map(|e| e.file_name().to_string_lossy().into_owned()).collect();

	let mut mods = Vec::new();
	let mods_dir = PathBuf::from(&config.mods_dir);
	if !config.mods_dir.is_empty() {
		for entry in fs::read_dir(&mods_dir).into_iter().flatten().flatten() {
			let name = entry.file_name().to_string_lossy().into_owned();
			if !entry.path().is_dir() || !name.to_lowercase().ends_with(".rte") {
				continue;
			}
			let (files, bytes, newest) = folder_stats(&entry.path());
			let in_version = slot.join("Mods").join(&name);
			let state = if !in_version.is_dir() {
				"new"
			} else if in_version.join(COPY_MARKER).exists() {
				"linked"
			} else {
				"own"
			};
			mods.push(json!({ "name": name, "files": files, "bytes": bytes, "modified": newest, "state": state }));
		}
	}
	mods.sort_by(|a, b| a["name"].as_str().unwrap_or("").to_lowercase().cmp(&b["name"].as_str().unwrap_or("").to_lowercase()));

	json!({
		"build": build,
		"settings": settings,
		"presets": { "exists": !config.presets_dir.is_empty() && presets_dir.is_dir(), "count": presets.len() },
		"mods": { "exists": !config.mods_dir.is_empty() && mods_dir.is_dir(), "list": mods },
		"worldsDir": config.worlds_dir(),
	})
}

fn worlds(config: &Config) -> Vec<String> {
	let mut names: Vec<String> =
		fs::read_dir(config.worlds_dir()).into_iter().flatten().flatten().filter(|e| e.path().is_dir()).map(|e| e.file_name().to_string_lossy().into_owned()).collect();
	names.sort();
	names
}

/// Starts the built version: "play", "test" (the Maginot map with Studio's unit), "sandbox" or "editor" (the Actor Editor).
fn launch(config: &Config, mode: &str, map: &str) -> Result<Vec<String>, String> {
	let slot = config.slot_path();
	let exe = exe_path(&slot, &config.configuration);
	if !exe.is_file() {
		return Err("Nothing is built yet. Build a version first.".into());
	}
	let mut log = Vec::new();
	link_mods(config, &slot, &mut log);
	link_presets(config, &slot, &mut log);
	apply_world(config, &slot, &mut log)?;
	if !config.settings_ini.is_empty() {
		// The game reads (and rewrites) Settings.ini, so each launch gets a fresh copy of yours.
		let userdata = slot.join("Userdata");
		let _ = fs::create_dir_all(&userdata);
		fs::copy(&config.settings_ini, userdata.join("Settings.ini")).map_err(|e| format!("Couldn't copy Settings.ini: {e}"))?;
		log.push(format!("Using Settings.ini from {}.", config.settings_ini));
	}
	let mut args: Vec<String> = match mode {
		"test" => vec!["-modstudio".into(), "-test".into()],
		"sandbox" => vec!["-modstudio".into(), "-sandbox".into()],
		"editor" => vec!["-modstudio".into(), "-editor".into(), "ActorEditor".into()],
		_ => vec![],
	};
	if (mode == "test" || mode == "sandbox") && !map.is_empty() {
		args.push(map.into());
	}
	let mut command = Command::new(&exe);
	command.args(&args).current_dir(&slot);
	let fmod = if cfg!(target_os = "macos") { slot.join("external/lib/macOS") } else { slot.join("external/lib/linux/x86_64") };
	if !cfg!(windows) {
		command.env(if cfg!(target_os = "macos") { "DYLD_LIBRARY_PATH" } else { "LD_LIBRARY_PATH" }, fmod);
	}
	command.spawn().map_err(|e| format!("Couldn't start the game: {e}"))?;
	log.push(format!("Launched {}{}.", exe.file_name().unwrap().to_string_lossy(), if args.is_empty() { String::new() } else { format!(" {}", args.join(" ")) }));
	Ok(log)
}

// ---- folder and file pickers (native dialogs, so the page can offer Browse buttons) ----

type MainThread = Box<dyn Fn(Box<dyn FnOnce() + Send>) + Send + Sync>;
static MAIN_THREAD: std::sync::OnceLock<MainThread> = std::sync::OnceLock::new();

/// The window's main thread, where macOS insists dialogs open. Set by main.rs once the window exists. (Windows opens them on any
/// thread, and on Linux the desktop's portal draws them, so there they open from the request's own thread and the window keeps running.)
pub fn set_main_thread(run: MainThread) {
	let _ = MAIN_THREAD.set(run);
}

fn pick(kind: &str, title: &str) -> Option<String> {
	let (kind, title) = (kind.to_string(), title.to_string());
	let ask = move || {
		let dialog = rfd::FileDialog::new().set_title(&title);
		let picked = if kind == "file" { dialog.pick_file() } else { dialog.pick_folder() };
		picked.map(|p| p.to_string_lossy().into_owned())
	};
	match MAIN_THREAD.get().filter(|_| cfg!(target_os = "macos")) {
		Some(run) => {
			let (tx, rx) = std::sync::mpsc::channel();
			run(Box::new(move || {
				let _ = tx.send(ask());
			}));
			rx.recv().ok().flatten()
		}
		None => ask(),
	}
}

// ---- requests ----

pub struct Answer {
	pub status: u16,
	pub body: String,
	/// Set when the game folder Studio edits should change (after a launch, or Edit this version).
	pub switch_to: Option<PathBuf>,
}

fn ok(v: Value) -> Answer {
	Answer { status: 200, body: v.to_string(), switch_to: None }
}

fn fail(status: u16, message: impl Into<String>) -> Answer {
	Answer { status, body: json!({ "error": message.into() }).to_string(), switch_to: None }
}

fn arg(body: &Value, key: &str) -> String {
	body.get(key).and_then(|v| v.as_str()).unwrap_or("").to_string()
}

/// The "launcher/<action>" requests. `body` is the request's JSON (POST and PUT).
pub fn handle(launcher: &Launcher, action: &str, query: &dyn Fn(&str) -> Option<String>, body: &Value, game_dir: &Path) -> Answer {
	let config = launcher.config.lock().unwrap().clone();
	let repo_ok = Path::new(&config.repo).join(".git").exists();
	let own_copy = !config.work_dir.is_empty();
	let needs_repo = matches!(action, "refs" | "commits" | "versions" | "fetch" | "build" | "update") && !(own_copy && action == "update");
	if needs_repo && !repo_ok {
		return fail(400, format!("'{}' isn't a git checkout of the game. Set the repo folder in Settings.", config.repo));
	}
	match action {
		"config" if !body.is_null() => {
			// PUT: the page's whole config back, merged over what's saved.
			let mut merged = serde_json::to_value(&config).unwrap();
			if let (Some(m), Some(b)) = (merged.as_object_mut(), body.as_object()) {
				for (k, v) in b {
					m.insert(k.clone(), v.clone());
				}
			}
			let mut next: Config = serde_json::from_value(merged).unwrap_or(config);
			next.fill_defaults();
			next.save();
			*launcher.config.lock().unwrap() = next.clone();
			ok(json!({ "config": next }))
		}
		"config" => {
			let slot = config.slot_path();
			let built = built_info(&slot);
			ok(json!({
				"config": config,
				"configurations": configurations(),
				"platform": std::env::consts::OS,
				"repoOk": repo_ok,
				"slot": slot,
				"built": built,
				"builtExe": exe_path(&slot, &config.configuration).is_file(),
				"worlds": worlds(&config),
				"gameDir": game_dir,
				"editingSlot": fs::canonicalize(game_dir).ok() == fs::canonicalize(&slot).ok(),
			}))
		}
		"deps" => ok(dependencies()),
		"overview" => ok(overview(&config)),
		"refs" => match refs(&config) {
			Ok(v) => ok(v),
			Err(e) => fail(500, e),
		},
		"commits" => match commits(&config, &query("ref").unwrap_or_else(|| format!("{}/{}", config.remote, config.branch))) {
			Ok(v) => ok(v),
			Err(e) => fail(500, e),
		},
		"versions" => {
			let shas: Vec<String> = body.get("shas").and_then(|s| s.as_array()).map(|a| a.iter().filter_map(|x| x.as_str().map(str::to_string)).collect()).unwrap_or_default();
			ok(versions(&config, &shas))
		}
		"fetch" => {
			let c = config.clone();
			job_answer(launcher.start_job("Fetching", move |ctx| ctx.run("git", &["fetch", &c.remote, "--tags", "--prune"], Path::new(&c.repo), &[])))
		}
		"build" => {
			if own_copy {
				return fail(400, "Studio is set to build your own copy of the game. Clear that in Launch settings to build a commit.");
			}
			let sha = arg(body, "sha");
			if sha.len() < 7 {
				return fail(400, "Pick a commit to build.");
			}
			let c = config.clone();
			job_answer(launcher.start_job(&format!("Building {}", &sha[..10.min(sha.len())]), move |ctx| build(ctx, &c, &sha)))
		}
		"update" => {
			let c = config.clone();
			job_answer(launcher.start_job(&format!("Updating {}", config.branch), move |ctx| update(ctx, &c)))
		}
		"job" => {
			let from: usize = query("from").and_then(|v| v.parse().ok()).unwrap_or(0);
			let job = launcher.job.lock().unwrap();
			let from = from.min(job.lines.len());
			ok(json!({ "id": job.id, "title": job.title, "state": job.state, "lines": job.lines[from..], "next": job.lines.len() }))
		}
		"cancel" => {
			launcher.cancel();
			ok(json!({ "ok": true }))
		}
		"launch" => match launch(&config, &arg(body, "mode"), &arg(body, "map")) {
			Ok(log) => {
				// Studio follows the launch, so Preview in game and the console talk to the game that's running.
				let slot = config.slot_path();
				let switch = fs::canonicalize(game_dir).ok() != fs::canonicalize(&slot).ok();
				Answer { status: 200, body: json!({ "log": log, "switched": switch }).to_string(), switch_to: switch.then_some(slot) }
			}
			Err(e) => fail(400, e),
		},
		"edit" => {
			// Edit this version (the built slot) or the repo checkout in Studio.
			let target = if arg(body, "which") == "repo" { PathBuf::from(&config.repo) } else { config.slot_path() };
			if !target.join("Data").join("Base.rte").is_dir() {
				return fail(400, format!("{} has no game in it yet.", target.display()));
			}
			Answer { status: 200, body: json!({ "ok": true }).to_string(), switch_to: Some(target) }
		}
		"delete-build" => {
			if own_copy {
				return fail(400, "That is your own copy of the game, so Studio won't delete it.");
			}
			let slot = config.slot_path();
			if !slot.exists() {
				return ok(json!({ "ok": true }));
			}
			if fs::canonicalize(game_dir).ok() == fs::canonicalize(&slot).ok() {
				return fail(400, "Studio is editing this version. Switch Studio to the repo first.");
			}
			// The presets link goes first, on its own, so nothing deleting the folder can follow it into your shared presets.
			let presets = slot.join("Userdata").join("Presets");
			if fs::symlink_metadata(&presets).map(|m| m.file_type().is_symlink()).unwrap_or(false) || fs::read_link(&presets).is_ok() {
				let _ = fs::remove_dir(&presets).or_else(|_| fs::remove_file(&presets));
			}
			let _ = fs::remove_dir_all(&slot);
			let _ = git(Path::new(&config.repo), &["worktree", "prune"]);
			ok(json!({ "ok": !slot.exists() }))
		}
		"pick" => ok(json!({ "path": pick(&query("kind").unwrap_or_default(), &query("title").unwrap_or_else(|| "Pick a folder".into())) })),
		_ => fail(404, "Unknown request."),
	}
}

fn job_answer(started: Result<u64, String>) -> Answer {
	match started {
		Ok(id) => ok(json!({ "job": id })),
		Err(e) => fail(409, e),
	}
}
