// Cortex Command Community Edition, downloaded for Classic mode: its releases on GitHub are one zip per platform with the program and its
// Data folder, so Studio fetches the one for this computer, unpacks it into a folder of its own and edits it there. Getting a newer build
// over one already there replaces the program and Data and keeps everything else (Mods, Userdata, the game's settings).
//
// The page asks through "launcher/community..." requests (launcher.rs handle): the downloads run as a launcher job.

use crate::launcher::JobCtx;
use serde_json::{json, Value};
use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

const REPO: &str = "cortex-command-community/Cortex-Command-Community-Project";
/// What Studio writes into a folder it unpacked a release into: which one, and the asset it came from (to tell when a newer one is out).
const MARKER: &str = ".cortex-studio-release.json";

pub struct Release {
	pub id: &'static str,
	pub tag: &'static str,
	pub name: &'static str,
	pub folder: &'static str,
}

/// The releases Studio offers. The ids are the page's targets (js/target.js).
pub const RELEASES: [Release; 2] = [
	Release { id: "ce-6.2.2", tag: "v6.2.2", name: "Community Edition 6.2.2", folder: "Community Edition 6.2.2" },
	Release { id: "ce-nightly", tag: "nightly", name: "Community Edition nightly", folder: "Community Edition nightly" },
];

/// One way out to GitHub: through the proxy the computer is set up with (HTTPS_PROXY), trusting the computer's own certificates (so an
/// antivirus or company proxy that checks HTTPS works too), and giving up on a connection that stalls.
fn agent() -> ureq::Agent {
	ureq::AgentBuilder::new().try_proxy_from_env(true).timeout_connect(std::time::Duration::from_secs(20)).timeout_read(std::time::Duration::from_secs(60)).build()
}

/// What went wrong talking to GitHub, said plainly.
fn say(e: ureq::Error, what: &str) -> String {
	match e {
		ureq::Error::Status(403 | 429, _) => format!("GitHub is turning away requests from this computer for now (it allows about 60 an hour without signing in), so {what} has to wait. Try again later."),
		ureq::Error::Status(404, _) => format!("GitHub has no {what}."),
		other => format!("Couldn't reach GitHub for {what}: {other}"),
	}
}

fn release(id: &str) -> Option<&'static Release> {
	RELEASES.iter().find(|r| r.id == id)
}

/// The release's zip for this computer, as the Community Edition's release workflow names them.
fn asset_name() -> Option<&'static str> {
	if cfg!(windows) {
		Some("CortexCommand.windows.zip")
	} else if cfg!(target_os = "macos") {
		Some("CortexCommand.macos.zip")
	} else if cfg!(all(target_os = "linux", target_arch = "x86_64")) {
		Some("CortexCommand.linux.zip")
	} else {
		None
	}
}

/// Where downloaded games go unless you pick somewhere else: Documents/Cortex Studio.
pub fn default_base() -> PathBuf {
	let home = std::env::var_os(if cfg!(windows) { "USERPROFILE" } else { "HOME" }).map(PathBuf::from).unwrap_or_else(std::env::temp_dir);
	let documents = home.join("Documents");
	if documents.is_dir() { documents } else { home }.join("Cortex Studio")
}

/// The folder a release goes in: `base` (empty for the default) / its folder name.
pub fn folder(id: &str, base: &str) -> Option<PathBuf> {
	let r = release(id)?;
	let base = if base.trim().is_empty() { default_base() } else { PathBuf::from(base.trim()) };
	Some(base.join(r.folder))
}

/// A folder Studio made for a download that hasn't happened yet (the first start, with no game): Studio opens on it and the page gets the game.
pub fn waiting_for_download(dir: &Path) -> bool {
	!dir.join("Data").join("Base.rte").is_dir() && RELEASES.iter().any(|r| dir.file_name().is_some_and(|n| n == r.folder)) && dir.is_dir()
}

/// Which release a folder Studio downloaded into is for, from its name.
pub fn release_for(dir: &Path) -> Option<&'static str> {
	RELEASES.iter().find(|r| dir.file_name().is_some_and(|n| n == r.folder)).map(|r| r.id)
}

fn marker(dir: &Path) -> Value {
	fs::read_to_string(dir.join(MARKER)).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or(Value::Null)
}

/// What the page shows: each release, where it goes, and what is there now.
pub fn overview(base: &str, game_dir: &Path) -> Value {
	let current = fs::canonicalize(game_dir).ok();
	let list: Vec<Value> = RELEASES
		.iter()
		.map(|r| {
			let dir = folder(r.id, base).unwrap();
			json!({
				"id": r.id,
				"name": r.name,
				"tag": r.tag,
				"dir": dir,
				"installed": dir.join("Data").join("Base.rte").is_dir(),
				"download": marker(&dir),
				"editing": current.is_some() && fs::canonicalize(&dir).ok() == current,
			})
		})
		.collect();
	json!({ "releases": list, "base": if base.trim().is_empty() { default_base() } else { PathBuf::from(base.trim()) }, "defaultBase": default_base(), "supported": asset_name().is_some(), "platform": std::env::consts::OS })
}

/// The release's asset on GitHub: its address, size and when it was uploaded (the nightly's changes with each build).
fn latest_asset(r: &Release) -> Result<Value, String> {
	let asset = asset_name().ok_or("Community Edition has no release for this computer.")?;
	let url = format!("https://api.github.com/repos/{REPO}/releases/tags/{}", r.tag);
	let answer: Value = agent()
		.get(&url)
		.set("User-Agent", "Cortex-Studio")
		.set("Accept", "application/vnd.github+json")
		.call()
		.map_err(|e| say(e, &format!("the newest build of {}", r.name)))?
		.into_json()
		.map_err(|e| format!("GitHub's answer about {} couldn't be read: {e}", r.name))?;
	let found = answer.get("assets").and_then(|a| a.as_array()).and_then(|a| a.iter().find(|x| x.get("name").and_then(|n| n.as_str()) == Some(asset)));
	let found = found.ok_or_else(|| format!("{} has no {asset}.", r.name))?;
	Ok(json!({
		"tag": r.tag,
		"asset": asset,
		"url": found.get("browser_download_url").cloned().unwrap_or(Value::Null),
		"size": found.get("size").cloned().unwrap_or(Value::Null),
		"updatedAt": found.get("updated_at").cloned().unwrap_or(Value::Null),
	}))
}

/// For each release, the newest build on GitHub, and whether the one downloaded is older.
pub fn check(base: &str) -> Value {
	let mut out = serde_json::Map::new();
	for r in RELEASES.iter() {
		let dir = folder(r.id, base).unwrap();
		let entry = match latest_asset(r) {
			Ok(latest) => {
				let have = marker(&dir);
				let newer = dir.join("Data").join("Base.rte").is_dir() && have.get("updatedAt").is_some() && have.get("updatedAt") != latest.get("updatedAt");
				json!({ "latest": latest, "newer": newer })
			}
			Err(e) => json!({ "error": e }),
		};
		out.insert(r.id.into(), entry);
	}
	Value::Object(out)
}

fn megabytes(bytes: u64) -> String {
	format!("{:.0} MB", bytes as f64 / 1_048_576.0)
}

/// The download job: the zip into the folder, then unpacked there.
pub fn get(ctx: &JobCtx, id: &str, dir: &Path) -> Result<(), String> {
	let r = release(id).ok_or("Studio doesn't know that release.")?;
	fs::create_dir_all(dir).map_err(|e| format!("Couldn't make {}: {e}", dir.display()))?;
	ctx.log(format!("Asking GitHub for {}…", r.name));
	let (url, size, updated) = match latest_asset(r) {
		Ok(a) => (a["url"].as_str().unwrap_or_default().to_string(), a["size"].as_u64(), a["updatedAt"].clone()),
		// GitHub's API allows a few requests an hour without signing in: the release's own address works without it.
		Err(e) => {
			ctx.log(format!("{e} Trying the release's download address."));
			(format!("https://github.com/{REPO}/releases/download/{}/{}", r.tag, asset_name().ok_or("Community Edition has no release for this computer.")?), None, Value::Null)
		}
	};
	let zip_path = dir.join(".download.zip");
	download(ctx, &url, &zip_path, size)?;
	ctx.log("Unpacking…");
	let result = unpack(ctx, &zip_path, dir);
	let _ = fs::remove_file(&zip_path);
	result?;
	let note = json!({ "id": r.id, "tag": r.tag, "asset": asset_name(), "updatedAt": updated, "downloaded": chrono::Local::now().to_rfc3339() });
	let _ = fs::write(dir.join(MARKER), serde_json::to_string_pretty(&note).unwrap());
	ctx.log(format!("{} is in {}.", r.name, dir.display()));
	Ok(())
}

fn download(ctx: &JobCtx, url: &str, to: &Path, size: Option<u64>) -> Result<(), String> {
	ctx.log(format!("Downloading {url}"));
	let response = agent().get(url).set("User-Agent", "Cortex-Studio").call().map_err(|e| say(e, "the download"))?;
	let total = size.or_else(|| response.header("Content-Length").and_then(|v| v.parse().ok()));
	let mut reader = response.into_reader();
	let part = to.with_extension("zip.part");
	let mut file = fs::File::create(&part).map_err(|e| format!("Couldn't write {}: {e}", part.display()))?;
	let mut buffer = vec![0u8; 1 << 16];
	let (mut done, mut shown) = (0u64, 0u64);
	loop {
		if ctx.cancelled() {
			drop(file);
			let _ = fs::remove_file(&part);
			return Err("Cancelled.".into());
		}
		let n = reader.read(&mut buffer).map_err(|e| format!("The download stopped: {e}"))?;
		if n == 0 {
			break;
		}
		file.write_all(&buffer[..n]).map_err(|e| format!("Couldn't write {}: {e}", part.display()))?;
		done += n as u64;
		// a line every 5%, or every 20 MB when the size isn't known
		let step = total.map(|t| (t / 20).max(1)).unwrap_or(20 << 20);
		if done - shown >= step {
			shown = done;
			ctx.log(match total {
				Some(t) => format!("Downloaded {} of {} ({}%)", megabytes(done), megabytes(t), done * 100 / t.max(1)),
				None => format!("Downloaded {}", megabytes(done)),
			});
		}
	}
	file.flush().map_err(|e| e.to_string())?;
	drop(file);
	if let Some(t) = total {
		if done != t {
			let _ = fs::remove_file(&part);
			return Err(format!("The download stopped at {} of {}.", megabytes(done), megabytes(t)));
		}
	}
	ctx.log(format!("Downloaded {}.", megabytes(done)));
	fs::rename(&part, to).map_err(|e| e.to_string())
}

/// Unpacks the release over `dir`: what the zip has at the top (the program, its libraries, Data) replaces what is there; the rest stays.
fn unpack(ctx: &JobCtx, zip_path: &Path, dir: &Path) -> Result<(), String> {
	let file = fs::File::open(zip_path).map_err(|e| e.to_string())?;
	let mut archive = zip::ZipArchive::new(file).map_err(|e| format!("The download isn't a zip Studio can read: {e}"))?;
	let tops: std::collections::BTreeSet<String> = (0..archive.len())
		.filter_map(|i| archive.by_index(i).ok().and_then(|f| f.enclosed_name()).and_then(|p| p.components().next().map(|c| c.as_os_str().to_string_lossy().into_owned())))
		.collect();
	if !tops.contains("Data") {
		return Err("The download has no Data folder, so it isn't a Cortex Command release.".into());
	}
	// The old official modules go, so files a newer build dropped don't linger. Mods and Userdata aren't in the zip, so they stay.
	for top in &tops {
		let old = dir.join(top);
		if old.is_dir() {
			fs::remove_dir_all(&old).map_err(|e| format!("Couldn't replace {}: {e}", old.display()))?;
		} else if old.exists() {
			let _ = fs::remove_file(&old);
		}
	}
	let count = archive.len();
	for i in 0..count {
		if ctx.cancelled() {
			return Err("Cancelled. The game folder is only partly unpacked: get it again to finish.".into());
		}
		let mut entry = archive.by_index(i).map_err(|e| e.to_string())?;
		let Some(relative) = entry.enclosed_name() else { continue };
		let out = dir.join(relative);
		if entry.is_dir() {
			fs::create_dir_all(&out).map_err(|e| e.to_string())?;
			continue;
		}
		if let Some(parent) = out.parent() {
			fs::create_dir_all(parent).map_err(|e| e.to_string())?;
		}
		let mut target = fs::File::create(&out).map_err(|e| format!("Couldn't write {}: {e}", out.display()))?;
		std::io::copy(&mut entry, &mut target).map_err(|e| format!("Couldn't unpack {}: {e}", out.display()))?;
		#[cfg(unix)]
		if let Some(mode) = entry.unix_mode() {
			use std::os::unix::fs::PermissionsExt;
			let _ = fs::set_permissions(&out, fs::Permissions::from_mode(mode & 0o777));
		}
		if count > 20 && (i + 1) % (count / 10) == 0 {
			ctx.log(format!("Unpacked {} of {count} files", i + 1));
		}
	}
	finish_for_platform(ctx, dir)
}

/// macOS: the app comes as a tar inside the zip (to keep its links and permissions). Linux: the AppImage must be runnable.
fn finish_for_platform(ctx: &JobCtx, dir: &Path) -> Result<(), String> {
	if cfg!(target_os = "macos") {
		let tar = dir.join("CortexCommand.tar");
		if tar.is_file() {
			for entry in fs::read_dir(dir).map_err(|e| e.to_string())?.filter_map(|e| e.ok()) {
				if entry.path().extension().is_some_and(|x| x == "app") {
					let _ = fs::remove_dir_all(entry.path());
				}
			}
			ctx.run("tar", &["-xf", "CortexCommand.tar"], dir, &[])?;
			let _ = fs::remove_file(&tar);
		}
		// a game downloaded by a browser is quarantined; one Studio downloaded isn't, but a folder copied in may be
		let _ = std::process::Command::new("xattr").args(["-dr", "com.apple.quarantine"]).arg(dir).status();
	}
	#[cfg(unix)]
	if cfg!(target_os = "linux") {
		use std::os::unix::fs::PermissionsExt;
		for entry in fs::read_dir(dir).map_err(|e| e.to_string())?.filter_map(|e| e.ok()) {
			let name = entry.file_name().to_string_lossy().into_owned();
			if name.ends_with(".AppImage") {
				let _ = fs::set_permissions(entry.path(), fs::Permissions::from_mode(0o755));
			}
		}
	}
	if crate::launcher::release_exe(dir).is_none() {
		return Err(format!("Unpacked, but there's no Cortex Command program in {}.", dir.display()));
	}
	Ok(())
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::launcher::Job;
	use std::sync::{Arc, Mutex};

	#[test]
	#[cfg(target_os = "linux")]
	fn a_newer_build_replaces_the_program_and_data_and_keeps_the_rest() {
		let dir = std::env::temp_dir().join(format!("cortex-studio-community-{}", std::process::id()));
		let _ = fs::remove_dir_all(&dir);
		fs::create_dir_all(dir.join("Data/Base.rte")).unwrap();
		fs::create_dir_all(dir.join("Mods/Mine.rte")).unwrap();
		fs::write(dir.join("Data/Base.rte/Dropped.ini"), "old").unwrap();
		fs::write(dir.join("Mods/Mine.rte/Index.ini"), "mine").unwrap();
		fs::write(dir.join("Settings.ini"), "kept").unwrap();

		let zip_path = dir.join(".download.zip");
		let mut writer = zip::ZipWriter::new(fs::File::create(&zip_path).unwrap());
		let options = zip::write::SimpleFileOptions::default();
		writer.start_file("CortexCommand.AppImage", options.unix_permissions(0o644)).unwrap();
		writer.write_all(b"#!/bin/sh\n").unwrap();
		writer.start_file("Data/Base.rte/Index.ini", options).unwrap();
		writer.write_all(b"DataModule").unwrap();
		writer.start_file("../outside.txt", options).unwrap();
		writer.write_all(b"no").unwrap();
		writer.finish().unwrap();

		let ctx = JobCtx(Arc::new(Mutex::new(Job::default())));
		unpack(&ctx, &zip_path, &dir).unwrap();
		assert!(dir.join("Data/Base.rte/Index.ini").is_file());
		assert!(!dir.join("Data/Base.rte/Dropped.ini").exists(), "the old Data goes");
		assert_eq!(fs::read_to_string(dir.join("Mods/Mine.rte/Index.ini")).unwrap(), "mine");
		assert_eq!(fs::read_to_string(dir.join("Settings.ini")).unwrap(), "kept");
		assert!(!dir.parent().unwrap().join("outside.txt").exists(), "nothing is written outside the folder");
		use std::os::unix::fs::PermissionsExt;
		assert_eq!(fs::metadata(dir.join("CortexCommand.AppImage")).unwrap().permissions().mode() & 0o111, 0o111);
		assert!(waiting_for_download(&std::env::temp_dir().join("x").join("Community Edition 6.2.2")) == false, "only a folder that exists waits");
		let _ = fs::remove_dir_all(&dir);
	}
}
