// Studio's mod history, kept with git (libgit2, built into the app: nobody needs git installed). The first time Studio changes a
// mod, the mod as it was is committed ("before Studio"), and every save or delete after that is a commit, so any version of any file
// can be put back, even after a change that stops the mod loading. Each mod's history is a git folder of its own in
// CortexStudio/History/<mod>.git in the user's config folder, with the mod's folder as its work tree, so nothing is added to the mod
// and a downloaded mod is covered like your own.
//
// Restores are written by Studio in place (a normal save), not checked out by git, because a version's Mods are hard links to your
// mods folder: the same file, which has to stay the same file. Mod Studio in a browser (Start-ModStudio.ps1) keeps plain copies
// instead and answers the same requests.

use git2::{IndexAddOption, Oid, Repository, Signature};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

const BEFORE: &str = "The mod before Studio changed it";
// Marker files the launchers write into a version's Mods, never part of a mod.
const IGNORED: &str = ".launcher-copy\n.launcher-world\n.cortex-studio-built\n";

// One history change at a time: the server answers on several threads, and a git index takes one writer.
static LOCK: Mutex<()> = Mutex::new(());

pub fn root() -> Option<PathBuf> {
	crate::launcher::config_dir().map(|d| d.join("History"))
}

/// "Mods/X.rte/a/b.ini" -> ("X.rte", "a/b.ini"). Only files in Mods: the game's own Data is never written.
fn split(disk_path: &str) -> Option<(String, String)> {
	let path = disk_path.replace('\\', "/");
	if path.split('/').any(|p| p == ".." || p.is_empty()) {
		return None;
	}
	let (top, rest) = path.split_once('/')?;
	let (module, inner) = rest.split_once('/')?;
	if !top.eq_ignore_ascii_case("Mods") || !module.to_lowercase().ends_with(".rte") {
		return None;
	}
	Some((module.to_string(), inner.to_string()))
}

fn git_dir(module: &str) -> Option<PathBuf> {
	root().map(|r| r.join(format!("{}.git", module.to_lowercase())))
}

fn signature() -> Result<Signature<'static>, git2::Error> {
	Signature::now("Cortex Studio", "studio@cortex.invalid")
}

/// The mod's history with `tree` as its work tree, made (with the mod as it is now as the first commit) if it has none.
fn open(module: &str, tree: &Path) -> Result<Repository, git2::Error> {
	let dir = git_dir(module).ok_or_else(|| git2::Error::from_str("no config folder"))?;
	let fresh = !dir.join("HEAD").is_file();
	let repo = if fresh { Repository::init_bare(&dir)? } else { Repository::open_bare(&dir)? };
	repo.set_workdir(tree, false)?;
	if fresh {
		let _ = fs::create_dir_all(dir.join("info"));
		let _ = fs::write(dir.join("info").join("exclude"), IGNORED);
		// Files are kept byte for byte: a user's global core.autocrlf would otherwise turn CRLF into LF in every version.
		let mut config = repo.config()?;
		config.set_bool("core.autocrlf", false)?;
		config.set_str("core.eol", "native")?;
		config.set_str("studio.module", module)?;
	}
	if fresh {
		let mut index = repo.index()?;
		index.add_all(["*"], IndexAddOption::DEFAULT, None)?;
		index.write()?;
		let tree_id = index.write_tree()?;
		let sig = signature()?;
		repo.commit(Some("HEAD"), &sig, &sig, BEFORE, &repo.find_tree(tree_id)?, &[])?;
	}
	Ok(repo)
}

/// Stages one file as it is on disk (or its removal) and commits it with `message` if that changed anything.
fn commit_file(repo: &Repository, inner: &str, message: &str) -> Result<(), git2::Error> {
	let mut index = repo.index()?;
	let on_disk = repo.workdir().map(|w| w.join(inner).is_file()).unwrap_or(false);
	if on_disk {
		index.add_path(Path::new(inner))?;
	} else {
		let _ = index.remove_path(Path::new(inner));
	}
	index.write()?;
	let tree_id = index.write_tree()?;
	let parent = repo.head()?.peel_to_commit()?;
	if parent.tree_id() == tree_id {
		return Ok(());
	}
	let sig = signature()?;
	repo.commit(Some("HEAD"), &sig, &sig, message, &repo.find_tree(tree_id)?, &[&parent])?;
	Ok(())
}

/// Before Studio writes or deletes a file in a mod: makes the mod's history if needed, and commits the file as it is on disk if it
/// changed since (saved in another editor, or by the game), so that version can be put back too.
pub fn before(root_dir: &Path, disk_path: &str) {
	let Some((module, inner)) = split(disk_path) else { return };
	let _lock = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	if let Ok(repo) = open(&module, &root_dir.join("Mods").join(&module)) {
		let _ = commit_file(&repo, &inner, &format!("Changed outside Studio: {inner}"));
	}
}

/// After Studio wrote or deleted a file in a mod: commits it.
pub fn after(root_dir: &Path, disk_path: &str, deleted: bool) {
	let Some((module, inner)) = split(disk_path) else { return };
	let _lock = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	if let Ok(repo) = open(&module, &root_dir.join("Mods").join(&module)) {
		let _ = commit_file(&repo, &inner, &format!("{} {inner}", if deleted { "Deleted" } else { "Saved" }));
	}
}

/// "Edit this mod": keeps the mod as it is now (its first commit), which lets Studio's designer change it. Mods Studio made itself
/// don't need this.
pub fn start(root_dir: &Path, disk_path: &str) -> Result<(), String> {
	let path = disk_path.replace('\\', "/");
	let module = path.strip_prefix("Mods/").filter(|m| !m.contains('/') && m.to_lowercase().ends_with(".rte")).ok_or("Only a mod in Mods can be edited.")?;
	let _lock = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	open(module, &root_dir.join("Mods").join(module)).map(|_| ()).map_err(|e| e.message().to_string())
}

/// The mods with a history, so the ones you chose to edit: their folder names.
pub fn mods() -> serde_json::Value {
	let mut names = Vec::new();
	for entry in root().and_then(|r| fs::read_dir(r).ok()).into_iter().flatten().flatten() {
		if let Some(stem) = entry.file_name().to_string_lossy().strip_suffix(".git") {
			let name = Repository::open_bare(entry.path()).ok().and_then(|r| r.config().ok()).and_then(|c| c.get_string("studio.module").ok());
			names.push(name.unwrap_or_else(|| stem.to_string()));
		}
	}
	serde_json::json!(names)
}

/// The commits of a history, newest first.
fn commits(repo: &Repository) -> Vec<git2::Commit<'_>> {
	let Ok(mut walk) = repo.revwalk() else { return Vec::new() };
	if walk.push_head().is_err() {
		return Vec::new();
	}
	walk.filter_map(|id| id.ok().and_then(|id| repo.find_commit(id).ok())).collect()
}

/// The files a commit changed against its parent (none for the first commit, which is the mod before Studio).
fn changed(repo: &Repository, commit: &git2::Commit) -> Vec<String> {
	let Ok(parent) = commit.parent(0) else { return Vec::new() };
	let (Ok(old), Ok(new)) = (parent.tree(), commit.tree()) else { return Vec::new() };
	let Ok(diff) = repo.diff_tree_to_tree(Some(&old), Some(&new), None) else { return Vec::new() };
	diff.deltas().filter_map(|d| d.new_file().path().or(d.old_file().path()).map(|p| p.to_string_lossy().replace('\\', "/"))).collect()
}

/// Every file Studio changed, newest change first: [{key: "Mods/X.rte/a.ini", time, count}].
pub fn files() -> serde_json::Value {
	let _lock = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	let mut found: Vec<serde_json::Value> = Vec::new();
	for entry in root().and_then(|r| fs::read_dir(r).ok()).into_iter().flatten().flatten() {
		if !entry.file_name().to_string_lossy().ends_with(".git") {
			continue;
		}
		let Ok(repo) = Repository::open_bare(entry.path()) else { continue };
		let module = repo.config().and_then(|c| c.get_string("studio.module")).unwrap_or_default();
		let mut seen: Vec<(String, i64, u32)> = Vec::new();
		for commit in commits(&repo) {
			for file in changed(&repo, &commit) {
				match seen.iter_mut().find(|(f, _, _)| *f == file) {
					Some(row) => row.2 += 1,
					None => seen.push((file, commit.time().seconds(), 1)),
				}
			}
		}
		for (file, time, count) in seen {
			found.push(serde_json::json!({ "key": format!("Mods/{module}/{file}"), "time": time, "count": count }));
		}
	}
	found.sort_by(|a, b| b["time"].as_i64().cmp(&a["time"].as_i64()));
	serde_json::Value::Array(found)
}

fn open_existing(module: &str) -> Option<Repository> {
	Repository::open_bare(git_dir(module)?).ok()
}

/// The versions of one file, newest first: [{id (commit), time, message, original, deleted}].
pub fn versions(key: &str) -> serde_json::Value {
	let Some((module, inner)) = split(key) else { return serde_json::json!([]) };
	let _lock = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	let Some(repo) = open_existing(&module) else { return serde_json::json!([]) };
	let blob_at = |c: &git2::Commit| c.tree().ok().and_then(|t| t.get_path(Path::new(&inner)).ok()).map(|e| e.id());
	let mut rows = Vec::new();
	for commit in commits(&repo) {
		let now = blob_at(&commit);
		let before = commit.parent(0).ok().map(|p| blob_at(&p));
		let first = before.is_none();
		if (first && now.is_none()) || before == Some(now) {
			continue;
		}
		rows.push(serde_json::json!({
			"id": commit.id().to_string(),
			"time": commit.time().seconds(),
			"message": commit.summary().unwrap_or(""),
			"original": first,
			"deleted": now.is_none(),
		}));
	}
	serde_json::Value::Array(rows)
}

/// One version of a file: its bytes at that commit.
pub fn read(key: &str, id: &str) -> Option<Vec<u8>> {
	let (module, inner) = split(key)?;
	let _lock = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	let repo = open_existing(&module)?;
	let commit = repo.find_commit(Oid::from_str(id).ok()?).ok()?;
	let entry = commit.tree().ok()?.get_path(Path::new(&inner)).ok()?;
	let blob = repo.find_blob(entry.id()).ok()?;
	Some(blob.content().to_vec())
}
