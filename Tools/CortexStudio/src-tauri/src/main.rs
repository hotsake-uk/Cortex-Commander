// Cortex Studio: Mod Studio as one desktop app for Windows, macOS and Linux. It serves Mod Studio's page and the game folder from a
// built-in server (server.rs, the requests Start-ModStudio.ps1 answers) and shows the page in its own window, with the file bridge
// (watcher.rs) on, so scripts and INI files saved in any editor reload in a game started from Studio.
//
//   cortex-studio [--game-dir <game folder>] [--bridge] [--no-bridge] [--port <port>]
//
// --bridge runs only the server and the file bridge, with no window, printing Studio's address (open it in a browser if you like).
// Without --game-dir it uses the folder it last used, the game folder it sits in, or asks for one.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod history;
mod launcher;
mod server;
mod watcher;

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

#[derive(Default, Serialize, Deserialize)]
struct Settings {
	#[serde(default)]
	game_dir: Option<PathBuf>,
	/// The window's zoom (Ctrl + and -, Ctrl 0): 1.0 is normal. Kept so the window opens as you left it.
	#[serde(default)]
	zoom: Option<f64>,
}

fn settings_file() -> Option<PathBuf> {
	launcher::config_dir().map(|d| d.join("settings.json"))
}

fn load_settings() -> Settings {
	settings_file().and_then(|f| std::fs::read_to_string(f).ok()).and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

fn save_settings(settings: &Settings) {
	if let Some(file) = settings_file() {
		let _ = std::fs::create_dir_all(file.parent().unwrap());
		let _ = std::fs::write(file, serde_json::to_string_pretty(settings).unwrap());
	}
}

/// Studio moved to another game folder (a launch from Studio): start there next time.
pub fn remember_game_dir(dir: &Path) {
	let mut settings = load_settings();
	settings.game_dir = Some(dir.to_path_buf());
	save_settings(&settings);
}

/// Sets the window to this zoom. Registered once the window exists; headless (--bridge) there is none, and the page leaves zooming to the browser.
static ZOOM_HOOK: std::sync::OnceLock<Box<dyn Fn(f64) + Send + Sync>> = std::sync::OnceLock::new();

pub fn has_zoom_hook() -> bool {
	ZOOM_HOOK.get().is_some()
}

/// The zoom the window was left at (1.0 when never changed).
pub fn zoom() -> f64 {
	load_settings().zoom.filter(|z| z.is_finite() && *z > 0.0).unwrap_or(1.0)
}

/// Zooms the window and remembers it for the next start.
pub fn set_zoom(factor: f64) {
	let mut settings = load_settings();
	settings.zoom = Some(factor);
	save_settings(&settings);
	if let Some(hook) = ZOOM_HOOK.get() {
		hook(factor);
	}
}

fn is_game_dir(dir: &Path) -> bool {
	dir.join("Data").join("Base.rte").is_dir()
}

/// The game folder above `start` (Studio kept in <game>/Tools/CortexStudio, or next to the game).
fn game_dir_above(start: &Path) -> Option<PathBuf> {
	start.ancestors().find(|d| is_game_dir(d)).map(Path::to_path_buf)
}

fn pick_game_dir() -> Option<PathBuf> {
	loop {
		let picked = rfd::FileDialog::new().set_title("Pick the Cortex Command game folder (the one with Data inside)").pick_folder()?;
		if is_game_dir(&picked) {
			return Some(picked);
		}
		rfd::MessageDialog::new()
			.set_title("Cortex Studio")
			.set_description("That folder has no Data/Base.rte, so it isn't the game folder.")
			.set_level(rfd::MessageLevel::Warning)
			.show();
	}
}

fn find_game_dir(given: Option<PathBuf>, settings: &Settings) -> Option<PathBuf> {
	if let Some(dir) = given {
		return if is_game_dir(&dir) { Some(dir) } else { None };
	}
	if let Some(dir) = settings.game_dir.as_ref().filter(|d| is_game_dir(d)) {
		return Some(dir.clone());
	}
	let exe = std::env::current_exe().ok().and_then(|e| e.parent().map(Path::to_path_buf));
	exe.as_deref().and_then(game_dir_above).or_else(|| std::env::current_dir().ok().as_deref().and_then(game_dir_above)).or_else(pick_game_dir)
}

#[cfg(windows)]
fn show_console() {
	// The app is a window program; --bridge prints to the console it was started from, or a new one.
	use windows_sys::Win32::System::Console::{AllocConsole, AttachConsole, ATTACH_PARENT_PROCESS};
	unsafe {
		if AttachConsole(ATTACH_PARENT_PROCESS) == 0 {
			AllocConsole();
		}
	}
}

#[cfg(not(windows))]
fn show_console() {}

/// An app opened from the Finder gets a bare PATH, without Homebrew, where meson, ninja and gcc-13 are. Add its folders so building
/// the game and the setup check find them, as they do when Studio is started from a terminal.
#[cfg(target_os = "macos")]
fn add_tool_paths() {
	let mut paths: Vec<PathBuf> = std::env::var_os("PATH").map(|p| std::env::split_paths(&p).collect()).unwrap_or_default();
	for extra in ["/opt/homebrew/bin", "/opt/homebrew/sbin", "/usr/local/bin", "/usr/local/sbin"] {
		let dir = PathBuf::from(extra);
		if dir.is_dir() && !paths.contains(&dir) {
			paths.push(dir);
		}
	}
	if let Ok(joined) = std::env::join_paths(paths) {
		std::env::set_var("PATH", joined);
	}
}

#[cfg(not(target_os = "macos"))]
fn add_tool_paths() {}

fn main() {
	add_tool_paths();
	let mut given = None;
	let mut headless = false;
	let mut watch = true;
	let mut port = 0u16;
	let mut args = std::env::args().skip(1);
	while let Some(arg) = args.next() {
		match arg.as_str() {
			"--game-dir" => given = args.next().map(PathBuf::from),
			"--bridge" => headless = true,
			"--no-bridge" => watch = false,
			"--port" => port = args.next().and_then(|p| p.parse().ok()).unwrap_or(0),
			_ => {}
		}
	}
	if headless {
		show_console();
	}

	let mut settings = load_settings();
	let Some(game_dir) = find_game_dir(given, &settings) else {
		eprintln!("No game folder: pass --game-dir with the folder that has Data/Base.rte inside.");
		std::process::exit(1);
	};
	let game_dir = std::fs::canonicalize(&game_dir).unwrap_or(game_dir);
	let game_dir = PathBuf::from(game_dir.to_string_lossy().trim_start_matches(r"\\?\"));
	settings.game_dir = Some(game_dir.clone());
	save_settings(&settings);

	let url = match server::start(game_dir.clone(), watch, port) {
		Ok(url) => url,
		Err(e) => {
			eprintln!("{e}");
			std::process::exit(1);
		}
	};
	println!("Cortex Studio is running for {}", game_dir.display());
	println!("Open: {url}");
	if watch {
		println!("Watching Data and Mods: scripts and INI files you save in any editor reload in a game started from Studio.");
	}

	if headless {
		println!("Press Ctrl+C (or close this window) to stop it.");
		loop {
			std::thread::park();
		}
	}

	let external: tauri::Url = url.parse().expect("Studio's own address");
	tauri::Builder::default()
		.setup(move |app| {
			let handle = app.handle().clone();
			launcher::set_main_thread(Box::new(move |work| {
				let _ = handle.run_on_main_thread(work);
			}));
			let zoom_handle = app.handle().clone();
			let _ = ZOOM_HOOK.set(Box::new(move |factor| {
				if let Some(window) = tauri::Manager::get_webview_window(&zoom_handle, "main") {
					let _ = window.set_zoom(factor);
				}
			}));
			tauri::WebviewWindowBuilder::new(app, "main", tauri::WebviewUrl::External(external.clone()))
				.title("Cortex Studio")
				.inner_size(1500.0, 950.0)
				.min_inner_size(900.0, 600.0)
				// Ctrl (Cmd on macOS) + and - zoom and 0 resets, as in a browser, but handled by the page (js/zoom.js), which tells Studio's server, so the
				// zoom can be kept: the webview's own hotkeys can't be read back.
				.zoom_hotkeys_enabled(false)
				.on_page_load(|webview, payload| {
					if matches!(payload.event(), tauri::webview::PageLoadEvent::Finished) {
						let _ = webview.set_zoom(zoom());
					}
				})
				.build()?;
			Ok(())
		})
		.run(tauri::generate_context!())
		.expect("Cortex Studio's window couldn't open");
}
