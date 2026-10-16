// The file bridge: watches Data and Mods, and when a .lua or .ini is saved by anything but Studio, asks a game started with -modstudio
// to reload it (through ModStudio/Scripts.ini) and keeps a numbered list of changes for an open Studio page (the "changes" request).
// The same rules as Start-ModStudio.ps1's StudioWatcher. When the game folder is a version Studio launched, your own mods folder (whose
// mods are hard-linked into the version's Mods) is watched too: a save there is linked into the version again and reloaded from there.

use notify::{EventKind, RecursiveMode, Watcher};
use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{channel, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime};

const KINDS: [&str; 7] = ["lua", "ini", "png", "bmp", "wav", "ogg", "flac"];
const LOG_LIMIT: usize = 2000;

#[derive(Default)]
struct State {
	seq: u64,
	log: Vec<(u64, String)>,
	ours: HashMap<PathBuf, Instant>,
}

pub struct StudioWatcher {
	state: Arc<Mutex<State>>,
	_watchers: Vec<notify::RecommendedWatcher>,
}

impl StudioWatcher {
	pub fn start(root: &Path, mods_source: Option<PathBuf>) -> StudioWatcher {
		let state = Arc::new(Mutex::new(State::default()));
		let (notes, inbox) = channel::<(PathBuf, bool)>();
		let mut watchers = Vec::new();
		let folders = [root.join("Data"), root.join("Mods")].into_iter().chain(mods_source.clone());
		for folder in folders {
			if !folder.is_dir() {
				continue;
			}
			let sender: Sender<(PathBuf, bool)> = notes.clone();
			let made = notify::recommended_watcher(move |result: notify::Result<notify::Event>| {
				if let Ok(event) = result {
					let deleted = matches!(event.kind, EventKind::Remove(_));
					if matches!(event.kind, EventKind::Access(_)) {
						return;
					}
					for path in event.paths {
						let _ = sender.send((path, deleted));
					}
				}
			});
			match made {
				Ok(mut watcher) => {
					if let Err(e) = watcher.watch(&folder, RecursiveMode::Recursive) {
						eprintln!("Couldn't watch {}: {e}", folder.display());
						continue;
					}
					watchers.push(watcher);
				}
				Err(e) => eprintln!("Couldn't watch {}: {e}", folder.display()),
			}
		}

		let thread_root = root.to_path_buf();
		let thread_state = state.clone();
		std::thread::spawn(move || gather(thread_root, mods_source, thread_state, inbox));
		StudioWatcher { state, _watchers: watchers }
	}

	/// Studio's own saves: the page already asks the game to reload those, so the bridge leaves them alone for a few seconds.
	pub fn wrote(&self, full: &Path) {
		self.state.lock().unwrap().ours.insert(full.to_path_buf(), Instant::now());
	}

	/// The changes after number `since`, one "disk path<TAB>changed|deleted" line each, under a first line with the latest number.
	/// A negative `since` gives just the number, for a page that has only now opened.
	pub fn since(&self, since: i64) -> String {
		let state = self.state.lock().unwrap();
		let mut text = format!("{}\n", state.seq);
		if since < 0 {
			return text;
		}
		for (n, line) in &state.log {
			if *n as i64 > since {
				text.push_str(line);
				text.push('\n');
			}
		}
		text
	}
}

fn wanted(path: &Path) -> bool {
	path.extension().and_then(|e| e.to_str()).map(|e| KINDS.contains(&e.to_ascii_lowercase().as_str())).unwrap_or(false)
}

// A save fires several events (and editors save through temporary files), so changes are gathered for 300 ms and sent as one.
fn gather(root: PathBuf, mods_source: Option<PathBuf>, state: Arc<Mutex<State>>, inbox: std::sync::mpsc::Receiver<(PathBuf, bool)>) {
	let mut pending: BTreeMap<PathBuf, bool> = BTreeMap::new();
	let mut due: Option<Instant> = None;
	loop {
		let wait = match due {
			Some(when) => when.saturating_duration_since(Instant::now()),
			None => Duration::from_secs(3600),
		};
		match inbox.recv_timeout(wait) {
			Ok((path, deleted)) => {
				if !wanted(&path) {
					continue;
				}
				// A rename or a write reports the file as it is now; a path that is gone was deleted.
				let deleted = deleted || !path.exists();
				let from_mods_folder = mods_source.as_deref().and_then(|source| path.strip_prefix(source).ok().map(Path::to_path_buf));
				if from_mods_folder.is_some() {
					// Studio's own save passed on to your mods folder (launcher::mirror_to_mods): the version already has it.
					let mut s = state.lock().unwrap();
					if s.ours.get(&path).is_some_and(|when| when.elapsed() < Duration::from_secs(3)) {
						continue;
					}
					s.ours.remove(&path);
				}
				let path = match from_mods_folder {
					Some(relative) => match relink(&path, &root, &relative, deleted) {
						Some(linked) => {
							// Relinking it changes the version's copy too; that event is ours, not another save.
							state.lock().unwrap().ours.insert(linked.clone(), Instant::now());
							linked
						}
						None => continue,
					},
					None => {
						let mut s = state.lock().unwrap();
						if let Some(when) = s.ours.get(&path) {
							if when.elapsed() < Duration::from_secs(3) {
								continue;
							}
							s.ours.remove(&path);
						}
						path
					}
				};
				pending.insert(path, deleted);
				due = Some(Instant::now() + Duration::from_millis(300));
			}
			Err(RecvTimeoutError::Timeout) => {
				if pending.is_empty() {
					due = None;
					continue;
				}
				if flush(&root, &state, &pending) {
					pending.clear();
					due = None;
				} else {
					due = Some(Instant::now() + Duration::from_millis(400));
				}
			}
			Err(RecvTimeoutError::Disconnected) => return,
		}
	}
}

/// A file saved (or deleted) in your mods folder: makes the version's copy match, a hard link again where it can be, and returns the
/// version's path. None when that mod isn't linked into the version (it has its own) or the copy couldn't be made.
fn relink(from: &Path, root: &Path, relative: &Path, deleted: bool) -> Option<PathBuf> {
	let module = relative.components().next()?;
	if !root.join("Mods").join(module).join(crate::launcher::COPY_MARKER).exists() {
		return None;
	}
	let to = root.join("Mods").join(relative);
	if to.exists() {
		std::fs::remove_file(&to).ok()?;
	}
	if !deleted {
		std::fs::create_dir_all(to.parent()?).ok()?;
		if std::fs::hard_link(from, &to).is_err() {
			std::fs::copy(from, &to).ok()?;
		}
	}
	Some(to)
}

/// Logs the changes and writes the game's request. False when the page wrote a request moments ago: the game looks a few times a second,
/// so this one waits rather than replacing it.
fn flush(root: &Path, state: &Arc<Mutex<State>>, pending: &BTreeMap<PathBuf, bool>) -> bool {
	let request = root.join("ModStudio").join("Scripts.ini");
	if let Ok(modified) = std::fs::metadata(&request).and_then(|m| m.modified()) {
		if SystemTime::now().duration_since(modified).map(|d| d < Duration::from_millis(400)).unwrap_or(false) {
			return false;
		}
	}
	let mut scripts = Vec::new();
	let mut inis = Vec::new();
	{
		let mut s = state.lock().unwrap();
		for (path, deleted) in pending {
			let Ok(relative) = path.strip_prefix(root) else { continue };
			let disk = relative.to_string_lossy().replace('\\', "/");
			s.seq += 1;
			let n = s.seq;
			s.log.push((n, format!("{disk}\t{}", if *deleted { "deleted" } else { "changed" })));
			println!("{}  {}{disk}", chrono::Local::now().format("%H:%M:%S"), if *deleted { "deleted " } else { "saved   " });
			if *deleted {
				continue;
			}
			// The game's paths start at the module: "Mods/My.rte/Unit.ini" is "My.rte/Unit.ini".
			let module_path = disk.split_once('/').map(|(_, rest)| rest.to_string()).unwrap_or(disk.clone());
			let extension = disk.rsplit('.').next().unwrap_or("").to_ascii_lowercase();
			if extension == "lua" {
				scripts.push(module_path);
			} else if extension == "ini" && !module_path.to_ascii_lowercase().ends_with("/index.ini") {
				inis.push(module_path);
			}
		}
		if s.log.len() > LOG_LIMIT {
			let extra = s.log.len() - LOG_LIMIT;
			s.log.drain(0..extra);
		}
	}
	if scripts.is_empty() && inis.is_empty() {
		return true;
	}
	let mut text = String::from("// Written by Cortex Studio's file bridge for a game started with -modstudio.\r\n");
	text.push_str(&format!("Stamp = {}\r\n", SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).map(|d| d.as_millis()).unwrap_or(0)));
	for file in &inis {
		text.push_str(&format!("ReloadFile = {file}\r\n"));
	}
	for file in &scripts {
		text.push_str(&format!("ReloadScript = {file}\r\n"));
	}
	let count = inis.len() + scripts.len();
	let written = std::fs::create_dir_all(request.parent().unwrap()).and_then(|_| std::fs::write(&request, text));
	match written {
		Ok(()) => println!("          asked the game to reload {count} file{}.", if count == 1 { "" } else { "s" }),
		Err(e) => println!("          couldn't ask the game to reload: {e}"),
	}
	true
}
