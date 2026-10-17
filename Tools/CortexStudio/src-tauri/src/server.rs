// Studio's local server, the same requests Start-ModStudio.ps1 answers, so Mod Studio's page works unchanged in the app and in a browser.
// It only listens on this computer (127.0.0.1) and only answers API requests carrying the token it puts in the page's address.

use crate::backup;
use crate::launcher::{self, Launcher};
use crate::watcher::StudioWatcher;
use include_dir::{include_dir, Dir};
use std::fs;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, RwLock};
use std::time::UNIX_EPOCH;
use tiny_http::{Header, Method, Request, Response, Server};

// Mod Studio's page as Vite builds it (Tools/ModStudio/dist, made by build.rs), built into the app so it is one file. CORTEX_STUDIO_PAGE
// (a folder) serves it from disk instead, for working on it (dev.sh points it at dist/ and keeps dist/ rebuilt).
static PAGE: Dir<'static> = include_dir!("$CARGO_MANIFEST_DIR/../../ModStudio/dist");

pub struct Studio {
	/// The game folder Studio edits. It changes when you launch a version from Studio (the page then edits the game that's running).
	pub game_dir: RwLock<PathBuf>,
	pub token: String,
	pub watcher: Mutex<Option<StudioWatcher>>,
	watch: bool,
	launcher: Launcher,
	page_dir: Option<PathBuf>,
}

impl Studio {
	fn game_dir(&self) -> PathBuf {
		self.game_dir.read().unwrap().clone()
	}

	fn wrote(&self, full: &Path) {
		if let Some(w) = self.watcher.lock().unwrap().as_ref() {
			w.wrote(full);
		}
	}

	/// Points Studio at another game folder: the file bridge follows, and the app starts there next time.
	fn switch_to(&self, dir: PathBuf) {
		let dir = std::fs::canonicalize(&dir).unwrap_or(dir);
		let dir = PathBuf::from(dir.to_string_lossy().trim_start_matches(r"\\?\"));
		*self.game_dir.write().unwrap() = dir.clone();
		if self.watch {
			*self.watcher.lock().unwrap() = Some(StudioWatcher::start(&dir, launcher::linked_mods_dir(&dir)));
		}
		crate::remember_game_dir(&dir);
	}
}

/// Starts the server on a free port and returns the page's address.
pub fn start(game_dir: PathBuf, watch: bool, port: u16) -> Result<String, String> {
	let server = Server::http(("127.0.0.1", port)).map_err(|e| format!("Couldn't start Studio's server: {e}"))?;
	let port = server.server_addr().to_ip().map(|a| a.port()).unwrap_or(port);
	let token = new_token();
	let studio = Arc::new(Studio {
		watcher: Mutex::new(if watch { Some(StudioWatcher::start(&game_dir, launcher::linked_mods_dir(&game_dir))) } else { None }),
		watch,
		launcher: Launcher::new(&game_dir),
		game_dir: RwLock::new(game_dir),
		token: token.clone(),
		page_dir: std::env::var_os("CORTEX_STUDIO_PAGE").map(PathBuf::from),
	});
	let server = Arc::new(server);
	// A few threads, so a long request (the INI bundle) doesn't hold up the console's polling.
	for _ in 0..4 {
		let server = server.clone();
		let studio = studio.clone();
		std::thread::spawn(move || {
			for request in server.incoming_requests() {
				handle(&studio, request);
			}
		});
	}
	Ok(format!("http://localhost:{port}/?token={token}"))
}

fn new_token() -> String {
	let mut bytes = [0u8; 16];
	getrandom::getrandom(&mut bytes).expect("no random numbers from the system");
	bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn header(name: &str, value: &str) -> Header {
	Header::from_bytes(name.as_bytes(), value.as_bytes()).unwrap()
}

fn text(status: u16, body: impl Into<String>) -> Response<std::io::Cursor<Vec<u8>>> {
	Response::from_data(body.into().into_bytes()).with_status_code(status).with_header(header("Content-Type", "text/plain; charset=utf-8"))
}

fn bytes(body: Vec<u8>, kind: &str) -> Response<std::io::Cursor<Vec<u8>>> {
	Response::from_data(body).with_header(header("Content-Type", kind))
}

fn json_string(s: &str) -> String {
	serde_json::to_string(s).unwrap()
}

fn handle(studio: &Studio, mut request: Request) {
	let url = request.url().to_string();
	let (path, query) = url.split_once('?').unwrap_or((&url, ""));
	let path = decode(path);
	let response = if let Some(action) = path.strip_prefix("/api/") {
		api(studio, &mut request, action, query)
	} else {
		page(studio, &path)
	};
	let _ = request.respond(response);
}

// ---- the page ----

fn content_type(name: &str) -> &'static str {
	match name.rsplit('.').next().unwrap_or("").to_ascii_lowercase().as_str() {
		"html" => "text/html; charset=utf-8",
		"js" => "text/javascript; charset=utf-8",
		"css" => "text/css; charset=utf-8",
		"png" => "image/png",
		"svg" => "image/svg+xml",
		"ico" => "image/x-icon",
		"json" => "application/json",
		"ttf" => "font/ttf",
		"lua" => "text/plain; charset=utf-8",
		_ => "application/octet-stream",
	}
}

fn page(studio: &Studio, path: &str) -> Response<std::io::Cursor<Vec<u8>>> {
	let relative = if path == "/" { "ModStudio.html" } else { path.trim_start_matches('/') };
	if relative.split('/').any(|part| part == "..") {
		return text(404, "Not found.");
	}
	let body = match &studio.page_dir {
		Some(dir) => fs::read(dir.join(relative)).ok(),
		None => PAGE.get_file(relative).map(|f| f.contents().to_vec()),
	};
	match body {
		Some(body) => bytes(body, content_type(relative)).with_header(header("Cache-Control", "no-cache")),
		None => text(404, "Not found."),
	}
}

// ---- the API ----

fn query_value(query: &str, key: &str) -> Option<String> {
	query.split('&').filter_map(|pair| pair.split_once('=').or(Some((pair, "")))).find(|(k, _)| decode(k) == key).map(|(_, v)| decode(v))
}

fn decode(s: &str) -> String {
	let bytes = s.as_bytes();
	let mut out = Vec::with_capacity(bytes.len());
	let mut i = 0;
	while i < bytes.len() {
		match bytes[i] {
			b'%' if i + 2 < bytes.len() => {
				match u8::from_str_radix(std::str::from_utf8(&bytes[i + 1..i + 3]).unwrap_or("zz"), 16) {
					Ok(b) => {
						out.push(b);
						i += 3;
						continue;
					}
					Err(_) => out.push(b'%'),
				}
			}
			b'+' => out.push(b' '),
			b => out.push(b),
		}
		i += 1;
	}
	String::from_utf8_lossy(&out).into_owned()
}

/// A path from the page, made absolute under the game folder; None if it would leave it. On Linux and macOS a path written with other
/// letter case than the files on disk (as Windows-made mods often are) is matched to them.
fn resolve(root: &Path, relative: &str) -> Option<PathBuf> {
	let mut full = root.to_path_buf();
	for part in relative.split(['/', '\\']).filter(|p| !p.is_empty() && *p != ".") {
		if part == ".." || part.contains(':') {
			return None;
		}
		let next = full.join(part);
		if cfg!(windows) || next.exists() {
			full = next;
			continue;
		}
		let matched = fs::read_dir(&full).ok().and_then(|entries| {
			entries.filter_map(|e| e.ok()).map(|e| e.file_name()).find(|name| name.to_string_lossy().eq_ignore_ascii_case(part))
		});
		full = match matched {
			Some(name) => full.join(name),
			None => next,
		};
	}
	Some(full)
}

/// Opens a file in a program: "default" is the system's own choice for the file; anything else must be the full path of a program file that is
/// there. The program runs on its own (Studio does not wait for it). Programs with ways of their own are used that way: Aseprite gets a
/// sprite's frames (Name000.png, Name001.png...) as one animation, through a script, named after the first frame so saving writes the frames
/// back; Visual Studio Code and Notepad++ open a script at its line.
fn open_with(program: &str, file: &Path, frames: &[PathBuf], line: Option<u64>) -> Result<(), String> {
	let exe_name = Path::new(program).file_stem().map(|n| n.to_string_lossy().to_lowercase()).unwrap_or_default();
	if program != "default" && exe_name == "aseprite" && frames.len() > 1 {
		return open_frames_in_aseprite(program, frames);
	}
	if program != "default" && line.is_some() && (exe_name == "code" || exe_name == "notepad++") {
		let exe = PathBuf::from(program);
		if !exe.is_absolute() || !exe.is_file() {
			return Err(format!("{program} is not a program Studio can find. Pick it again in Settings."));
		}
		let line = line.unwrap_or(1);
		let mut command = std::process::Command::new(&exe);
		if exe_name == "code" {
			command.arg("--goto").arg(format!("{}:{line}", file.to_string_lossy()));
		} else {
			command.arg(format!("-n{line}")).arg(file);
		}
		return command.spawn().map(|_| ()).map_err(|e| format!("Couldn't start {program}: {e}"));
	}
	let started = if program == "default" {
		if cfg!(windows) {
			std::process::Command::new("cmd").args(["/C", "start", ""]).arg(file).spawn()
		} else if cfg!(target_os = "macos") {
			std::process::Command::new("open").arg(file).spawn()
		} else {
			std::process::Command::new("xdg-open").arg(file).spawn()
		}
	} else {
		let exe = PathBuf::from(program);
		let app_bundle = cfg!(target_os = "macos") && exe.is_dir() && exe.extension().is_some_and(|e| e == "app");
		if !exe.is_absolute() || !(exe.is_file() || app_bundle) {
			return Err(format!("{program} is not a program Studio can find. Pick it again in Settings."));
		}
		if app_bundle {
			std::process::Command::new("open").arg("-a").arg(&exe).arg(file).spawn()
		} else {
			std::process::Command::new(&exe).arg(file).spawn()
		}
	};
	started.map(|_| ()).map_err(|e| format!("Couldn't start {program}: {e}"))
}

/// A sprite's frames into Aseprite as one animation: a small script (in the temp folder) makes a sprite from the first frame, adds the others
/// as frames, and names it after the first frame, so Aseprite's Save writes Name000.png, Name001.png... back where they were.
fn open_frames_in_aseprite(program: &str, frames: &[PathBuf]) -> Result<(), String> {
	let exe = PathBuf::from(program);
	if !exe.is_absolute() || !exe.is_file() {
		return Err(format!("{program} is not a program Studio can find. Pick it again in Settings."));
	}
	let lua = |p: &PathBuf| format!("\"{}\"", p.to_string_lossy().replace('\\', "/").replace('"', "\\\""));
	let list = frames.iter().map(lua).collect::<Vec<_>>().join(",\n\t");
	let script = format!(
		"-- Written by Cortex Studio: opens a sprite's frames as one animation. Saving writes the frames back.\n\
		 local files = {{\n\t{list}\n}}\n\
		 local spr = Sprite{{ fromFile = files[1] }}\n\
		 -- Aseprite may read the numbered frames itself: only the ones it didn't are added\n\
		 for i = #spr.frames + 1, #files do\n\
		 \tlocal img = Image{{ fromFile = files[i] }}\n\
		 \tlocal frame = spr:newEmptyFrame()\n\
		 \tspr:newCel(spr.layers[1], frame, img, Point(0, 0))\n\
		 end\n\
		 spr.filename = files[1]\n\
		 app.frame = spr.frames[1]\n"
	);
	static COUNT: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
	let path = std::env::temp_dir().join(format!("cortex-studio-aseprite-{}-{}.lua", std::process::id(), COUNT.fetch_add(1, std::sync::atomic::Ordering::SeqCst)));
	fs::write(&path, script).map_err(|e| format!("Couldn't write the script for Aseprite: {e}"))?;
	std::process::Command::new(&exe).arg("--script").arg(&path).spawn().map(|_| ()).map_err(|e| format!("Couldn't start {program}: {e}"))
}

/// Picture, script and sound editors installed in the usual places, as [{ name, kinds, path }].
fn find_programs() -> serde_json::Value {
	let env = |k: &str| std::env::var(k).ok().map(PathBuf::from);
	let mut roots: Vec<PathBuf> = Vec::new();
	for k in ["ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"] {
		if let Some(p) = env(k) {
			if !roots.contains(&p) {
				roots.push(p);
			}
		}
	}
	let local = env("LOCALAPPDATA").map(|p| p.join("Programs"));
	let steam: Vec<PathBuf> = roots.iter().map(|r| r.join("Steam").join("steamapps").join("common")).collect();
	let under = |parts: &[&str]| -> Vec<PathBuf> {
		roots.iter().map(|r| parts.iter().fold(r.clone(), |p, part| p.join(part))).collect()
	};
	let mut candidates: Vec<(&str, Vec<&str>, Vec<PathBuf>)> = vec![
		("Aseprite", vec!["image"], under(&["Aseprite", "Aseprite.exe"]).into_iter().chain(steam.iter().map(|s| s.join("Aseprite").join("Aseprite.exe"))).collect()),
		("Krita", vec!["image"], under(&["Krita (x64)", "bin", "krita.exe"])),
		("Paint.NET", vec!["image"], under(&["paint.net", "paintdotnet.exe"])),
		("Pyxel Edit", vec!["image"], under(&["PyxelEdit", "PyxelEdit.exe"])),
		("Visual Studio Code", vec!["script", "ini"], local.iter().map(|l| l.join("Microsoft VS Code").join("Code.exe")).chain(under(&["Microsoft VS Code", "Code.exe"])).collect()),
		("Notepad++", vec!["script", "ini"], under(&["Notepad++", "notepad++.exe"])),
		("Audacity", vec!["sound"], under(&["Audacity", "Audacity.exe"])),
	];
	// GIMP puts its version in its folder and file names
	let mut gimp = Vec::new();
	for r in &roots {
		let Ok(dirs) = fs::read_dir(r) else { continue };
		for d in dirs.filter_map(|e| e.ok()).filter(|e| e.file_name().to_string_lossy().starts_with("GIMP")) {
			let Ok(bins) = fs::read_dir(d.path().join("bin")) else { continue };
			gimp.extend(bins.filter_map(|e| e.ok()).map(|e| e.path()).filter(|p| {
				let n = p.file_name().map(|n| n.to_string_lossy().to_lowercase()).unwrap_or_default();
				n.starts_with("gimp-") && n.ends_with(".exe") && !n.contains("console")
			}));
		}
	}
	candidates.push(("GIMP", vec!["image"], gimp));
	if cfg!(target_os = "macos") {
		candidates.push(("Aseprite", vec!["image"], vec![PathBuf::from("/Applications/Aseprite.app")]));
		candidates.push(("Visual Studio Code", vec!["script", "ini"], vec![PathBuf::from("/Applications/Visual Studio Code.app")]));
	}
	let mut found = Vec::new();
	for (name, kinds, paths) in candidates {
		if let Some(path) = paths.into_iter().find(|p| p.exists()) {
			found.push(serde_json::json!({ "name": name, "kinds": kinds, "path": path.to_string_lossy() }));
		}
	}
	serde_json::Value::Array(found)
}

fn disk_path(root: &Path, full: &Path) -> String {
	full.strip_prefix(root).unwrap_or(full).to_string_lossy().replace('\\', "/")
}

fn walk(dir: &Path, found: &mut Vec<PathBuf>) {
	let Ok(entries) = fs::read_dir(dir) else { return };
	let mut entries: Vec<_> = entries.filter_map(|e| e.ok()).collect();
	entries.sort_by_key(|e| e.file_name());
	for entry in entries {
		let path = entry.path();
		match entry.file_type() {
			Ok(t) if t.is_dir() => walk(&path, found),
			Ok(t) if t.is_file() => found.push(path),
			_ => {}
		}
	}
}

fn game_files(root: &Path) -> Vec<PathBuf> {
	let mut found = Vec::new();
	for top in ["Data", "Mods"] {
		walk(&root.join(top), &mut found);
	}
	found
}

fn api(studio: &Studio, request: &mut Request, action: &str, query: &str) -> Response<std::io::Cursor<Vec<u8>>> {
	let token_ok = request.headers().iter().any(|h| h.field.equiv("X-ModStudio-Token") && h.value.as_str() == studio.token);
	if !token_ok {
		return text(403, "Wrong or missing token.");
	}
	if let Some(what) = action.strip_prefix("launcher/") {
		return launcher_api(studio, request, what, query);
	}
	let root = &studio.game_dir();
	let relative = query_value(query, "path").unwrap_or_default();
	let Some(full) = resolve(root, &relative) else {
		return text(400, "That path is outside the game folder.");
	};

	match action {
		// A tab into its own window. Only in the app; headless, the page opens a browser tab itself.
		"popout" => {
			let mut raw = String::new();
			let _ = request.as_reader().read_to_string(&mut raw);
			let what = serde_json::from_str::<serde_json::Value>(&raw).ok().and_then(|v| v.get("what").and_then(|w| w.as_str().map(String::from)));
			match what {
				Some(what) if !what.is_empty() && what.len() < 2000 && crate::open_popout(&what) => bytes(br#"{"native":true}"#.to_vec(), "application/json"),
				_ => bytes(br#"{"native":false}"#.to_vec(), "application/json"),
			}
		}
		// A pop-out window whose last tab was closed: it goes too.
		"popout-close" => {
			let mut raw = String::new();
			let _ = request.as_reader().read_to_string(&mut raw);
			let label = serde_json::from_str::<serde_json::Value>(&raw).ok().and_then(|v| v.get("win").and_then(|w| w.as_str().map(String::from))).unwrap_or_default();
			let closed = crate::close_popout(&label);
			bytes(format!("{{\"closed\":{closed}}}").into_bytes(), "application/json")
		}
		// A game file opened in another program (the External programs setting): the program the page names, which must be a program file
		// that is there, or "default" for the system's own choice. The file must be in the game folder (resolve checks that).
		"open-with" => {
			let mut raw = String::new();
			let _ = request.as_reader().read_to_string(&mut raw);
			let body = serde_json::from_str::<serde_json::Value>(&raw).unwrap_or_default();
			let program = body.get("program").and_then(|p| p.as_str()).unwrap_or_default().to_string();
			let line = body.get("line").and_then(|l| l.as_u64());
			// a sprite's other frames (in the game folder too), for a program that takes them as one animation
			let mut frames = Vec::new();
			for f in body.get("frames").and_then(|f| f.as_array()).map(|a| a.as_slice()).unwrap_or(&[]) {
				match f.as_str().and_then(|f| resolve(root, f)) {
					Some(p) if p.is_file() => frames.push(p),
					Some(_) => {} // a frame the sprite says it has but that is not there: left out
					None => return text(400, "A frame is outside the game folder."),
				}
			}
			if !full.is_file() {
				return text(404, "That file is not there.");
			}
			match open_with(&program, &full, &frames, line) {
				Ok(()) => bytes(br#"{"ok":true}"#.to_vec(), "application/json"),
				Err(e) => text(400, e),
			}
		}
		// Editors already on this computer, for the External programs setting to offer.
		"find-programs" => bytes(find_programs().to_string().into_bytes(), "application/json"),
		// The window's zoom, kept between runs. Only in the app (a window to zoom); the page asks, and follows the answer.
		"zoom" => {
			if !crate::has_zoom_hook() {
				return bytes(br#"{"native":false,"factor":1}"#.to_vec(), "application/json");
			}
			if matches!(request.method(), Method::Put | Method::Post) {
				let mut raw = String::new();
				let _ = request.as_reader().read_to_string(&mut raw);
				if let Some(factor) = serde_json::from_str::<serde_json::Value>(&raw).ok().and_then(|v| v.get("factor").and_then(|f| f.as_f64())) {
					crate::set_zoom(factor.clamp(0.5, 3.0));
				}
			}
			bytes(format!("{{\"native\":true,\"factor\":{}}}", crate::zoom()).into_bytes(), "application/json")
		}
		"info" => {
			let name = root.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
			let body = format!("{{\"name\":{},\"path\":{},\"app\":\"Cortex Studio\"}}", json_string(&name), json_string(&root.to_string_lossy()));
			bytes(body.into_bytes(), "application/json")
		}
		"inis" | "luas" => {
			// Every .ini (or .lua) in Data and Mods in one answer: "path<TAB>byte count<LF>" then the file's bytes, as on disk.
			let wanted = if action == "luas" { "lua" } else { "ini" };
			let mut body = Vec::new();
			for file in game_files(root) {
				let ext = file.extension().and_then(|e| e.to_str()).unwrap_or("");
				if !ext.eq_ignore_ascii_case(wanted) {
					continue;
				}
				let Ok(content) = fs::read(&file) else { continue };
				body.extend_from_slice(format!("{}\t{}\n", disk_path(root, &file), content.len()).as_bytes());
				body.extend_from_slice(&content);
			}
			bytes(body, "application/octet-stream")
		}
		"files" => {
			// Every file in Data and Mods, one "path<TAB>size<LF>" line each.
			let mut body = String::new();
			for file in game_files(root) {
				let size = fs::metadata(&file).map(|m| m.len()).unwrap_or(0);
				body.push_str(&format!("{}\t{size}\n", disk_path(root, &file)));
			}
			text(200, body)
		}
		"list" => {
			let Ok(entries) = fs::read_dir(&full) else { return bytes(b"[]".to_vec(), "application/json") };
			let items: Vec<String> = entries
				.filter_map(|e| e.ok())
				.map(|e| {
					let kind = if e.file_type().map(|t| t.is_dir()).unwrap_or(false) { "directory" } else { "file" };
					format!("{{\"name\":{},\"kind\":\"{kind}\"}}", json_string(&e.file_name().to_string_lossy()))
				})
				.collect();
			bytes(format!("[{}]", items.join(",")).into_bytes(), "application/json")
		}
		"tree" => {
			let mut found = Vec::new();
			walk(&full, &mut found);
			let items: Vec<String> = found
				.iter()
				.map(|f| {
					let size = fs::metadata(f).map(|m| m.len()).unwrap_or(0);
					format!("{{\"path\":{},\"size\":{size}}}", json_string(&disk_path(&full, f)))
				})
				.collect();
			bytes(format!("[{}]", items.join(",")).into_bytes(), "application/json")
		}
		"tail" => {
			// The end of a file the game keeps adding to (ModStudio/Console.log), from a byte offset; X-Size is the file's whole length.
			let mut from: u64 = query_value(query, "from").and_then(|v| v.parse().ok()).unwrap_or(0);
			let Ok(mut file) = fs::File::open(&full) else {
				return bytes(Vec::new(), "application/octet-stream").with_header(header("X-Size", "0"));
			};
			let size = file.metadata().map(|m| m.len()).unwrap_or(0);
			if from > size {
				from = 0;
			}
			let count = (size - from).min(4 << 20) as usize;
			let mut body = vec![0u8; count];
			let read = file.seek(SeekFrom::Start(from)).and_then(|_| {
				let mut total = 0;
				while total < count {
					let n = file.read(&mut body[total..])?;
					if n == 0 {
						break;
					}
					total += n;
				}
				Ok(total)
			});
			body.truncate(read.unwrap_or(0));
			bytes(body, "application/octet-stream").with_header(header("X-Size", &size.to_string())).with_header(header("X-From", &from.to_string()))
		}
		"delete" => {
			// Only files in Mods: the game's own files stay as they are.
			if *request.method() != Method::Post {
				return text(405, "Use POST.");
			}
			if !full.starts_with(root.join("Mods")) || full == root.join("Mods") {
				return text(403, "Only files in Mods can be deleted.");
			}
			studio.wrote(&full);
			if full.is_file() {
				if let Err(e) = fs::remove_file(&full) {
					return text(500, e.to_string());
				}
				if let Some(twin) = launcher::mods_twin(root, &full) {
					studio.wrote(&twin);
					launcher::mirror_to_mods(&full, &twin, true);
				}
			}
			text(200, "ok")
		}
		"file" => {
			if *request.method() == Method::Put {
				let mut body = Vec::new();
				if let Err(e) = request.as_reader().read_to_end(&mut body) {
					return text(500, e.to_string());
				}
				if let Some(parent) = full.parent() {
					let _ = fs::create_dir_all(parent);
				}
				studio.wrote(&full);
				match fs::write(&full, body) {
					Ok(()) => {
						if let Some(twin) = launcher::mods_twin(root, &full) {
							studio.wrote(&twin);
							launcher::mirror_to_mods(&full, &twin, false);
						}
						text(200, "ok")
					}
					Err(e) => text(500, e.to_string()),
				}
			} else if full.is_file() {
				let modified = fs::metadata(&full).and_then(|m| m.modified()).ok().and_then(|t| t.duration_since(UNIX_EPOCH).ok()).map(|d| d.as_millis()).unwrap_or(0);
				match fs::read(&full) {
					Ok(body) => bytes(body, "application/octet-stream").with_header(header("X-Last-Modified", &modified.to_string())),
					Err(e) => text(500, e.to_string()),
				}
			} else {
				text(404, "Not found.")
			}
		}
		// Backups of a whole mod (backup.rs): GET lists them (?path=Mods/X.rte for one mod), POST makes one, restore puts one back.
		"backups" => bytes(backup::list(query_value(query, "mod").as_deref()).to_string().into_bytes(), "application/json"),
		"backup" => {
			if *request.method() != Method::Post {
				return text(405, "Use POST.");
			}
			match backup::create(root, &query_value(query, "mod").unwrap_or_default(), "") {
				Ok(name) => text(200, name),
				Err(e) => text(500, e),
			}
		}
		"backup-restore" => {
			if *request.method() != Method::Post {
				return text(405, "Use POST.");
			}
			match backup::restore(root, &query_value(query, "mod").unwrap_or_default(), &query_value(query, "name").unwrap_or_default()) {
				Ok((safety, changes)) => {
					// Each file written or taken out is Studio's own, passed on to the mods folder it is linked to.
					for (path, deleted) in changes {
						studio.wrote(&path);
						if let Some(twin) = launcher::mods_twin(root, &path) {
							studio.wrote(&twin);
							launcher::mirror_to_mods(&path, &twin, deleted);
						}
					}
					text(200, safety)
				}
				Err(e) => text(500, e),
			}
		}
		"changes" => {
			// Files saved outside Studio since the page last asked. 404 when the bridge is off, so the page stops asking.
			let watcher = studio.watcher.lock().unwrap();
			let Some(w) = watcher.as_ref() else { return text(404, "The file bridge is off.") };
			let since: i64 = query_value(query, "since").and_then(|v| v.parse().ok()).unwrap_or(-1);
			text(200, w.since(since))
		}
		_ => text(404, "Unknown request."),
	}
}

/// The launcher's requests (launcher.rs). A launch, or Edit this version, moves Studio to that game folder.
fn launcher_api(studio: &Studio, request: &mut Request, action: &str, query: &str) -> Response<std::io::Cursor<Vec<u8>>> {
	let body = if matches!(request.method(), Method::Post | Method::Put) {
		let mut raw = String::new();
		let _ = request.as_reader().read_to_string(&mut raw);
		if raw.trim().is_empty() { serde_json::Value::Object(Default::default()) } else { serde_json::from_str(&raw).unwrap_or(serde_json::Value::Null) }
	} else {
		serde_json::Value::Null
	};
	let lookup = |key: &str| query_value(query, key);
	let answer = launcher::handle(&studio.launcher, action, &lookup, &body, &studio.game_dir());
	if let Some(dir) = answer.switch_to {
		studio.switch_to(dir);
	}
	bytes(answer.body.into_bytes(), "application/json").with_status_code(answer.status)
}
