// Studio's local server, the same requests Start-ModStudio.ps1 answers, so Mod Studio's page works unchanged in the app and in a browser.
// It only listens on this computer (127.0.0.1) and only answers API requests carrying the token it puts in the page's address.

use crate::watcher::StudioWatcher;
use include_dir::{include_dir, Dir};
use std::fs;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::UNIX_EPOCH;
use tiny_http::{Header, Method, Request, Response, Server};

// Mod Studio's page, built into the app so it is one file. CORTEX_STUDIO_PAGE (a folder) serves it from disk instead, for working on it.
static PAGE: Dir<'static> = include_dir!("$CARGO_MANIFEST_DIR/../../ModStudio");

pub struct Studio {
	pub game_dir: PathBuf,
	pub token: String,
	pub watcher: Option<StudioWatcher>,
	page_dir: Option<PathBuf>,
}

/// Starts the server on a free port and returns the page's address.
pub fn start(game_dir: PathBuf, watch: bool, port: u16) -> Result<String, String> {
	let server = Server::http(("127.0.0.1", port)).map_err(|e| format!("Couldn't start Studio's server: {e}"))?;
	let port = server.server_addr().to_ip().map(|a| a.port()).unwrap_or(port);
	let token = new_token();
	let studio = Arc::new(Studio {
		watcher: if watch { Some(StudioWatcher::start(&game_dir)) } else { None },
		game_dir,
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
	let root = &studio.game_dir;
	let relative = query_value(query, "path").unwrap_or_default();
	let Some(full) = resolve(root, &relative) else {
		return text(400, "That path is outside the game folder.");
	};

	match action {
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
			if let Some(w) = &studio.watcher {
				w.wrote(&full);
			}
			if full.is_file() {
				if let Err(e) = fs::remove_file(&full) {
					return text(500, e.to_string());
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
				if let Some(w) = &studio.watcher {
					w.wrote(&full);
				}
				match fs::write(&full, body) {
					Ok(()) => text(200, "ok"),
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
		"changes" => {
			// Files saved outside Studio since the page last asked. 404 when the bridge is off, so the page stops asking.
			let Some(w) = &studio.watcher else { return text(404, "The file bridge is off.") };
			let since: i64 = query_value(query, "since").and_then(|v| v.parse().ok()).unwrap_or(-1);
			text(200, w.since(since))
		}
		_ => text(404, "Unknown request."),
	}
}
