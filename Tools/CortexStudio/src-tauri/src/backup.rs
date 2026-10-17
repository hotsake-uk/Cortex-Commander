// Backups of a whole mod: a copy of its folder, taken whenever you ask, kept in CortexStudio/Backups/<mod>/<date and time>/ in the user's config
// folder. Nothing else: Studio writes your files straight to the mods folder and keeps no history of its own; use git or anything else beside it
// if you want one. A restore puts the mod's folder back as it was in the backup (after taking a backup of how it is now, so a restore can be undone).

use std::fs;
use std::path::{Path, PathBuf};

const INFO: &str = ".backup-info";
// Marker files the launchers write into a version's Mods, never part of a mod.
fn skipped(name: &str) -> bool {
	name == INFO || name == ".launcher-copy" || name == ".launcher-world" || name == ".cortex-studio-built"
}

pub fn root() -> Option<PathBuf> {
	crate::launcher::config_dir().map(|d| d.join("Backups"))
}

/// A mod's folder name, as in "Mods/MyMod.rte": one name, no separators.
pub fn valid_mod(name: &str) -> bool {
	!name.is_empty() && name != "." && name != ".." && !name.contains(['/', '\\', ':']) && name.to_ascii_lowercase().ends_with(".rte")
}

fn valid_backup_name(name: &str) -> bool {
	!name.is_empty() && name != "." && name != ".." && !name.contains(['/', '\\', ':'])
}

fn walk(dir: &Path, base: &Path, out: &mut Vec<PathBuf>) {
	let Ok(entries) = fs::read_dir(dir) else { return };
	for entry in entries.flatten() {
		let name = entry.file_name().to_string_lossy().into_owned();
		if skipped(&name) {
			continue;
		}
		let path = entry.path();
		if path.is_dir() {
			walk(&path, base, out);
		} else if let Ok(relative) = path.strip_prefix(base) {
			out.push(relative.to_path_buf());
		}
	}
}

fn files_of(dir: &Path) -> Vec<PathBuf> {
	let mut out = Vec::new();
	walk(dir, dir, &mut out);
	out
}

fn stamp() -> String {
	chrono::Local::now().format("%Y-%m-%d_%H%M%S").to_string()
}

/// Copies the mod's folder into a new backup. Returns its name.
pub fn create(game_root: &Path, module: &str, prefix: &str) -> Result<String, String> {
	if !valid_mod(module) {
		return Err("That isn't a mod folder name.".into());
	}
	let source = game_root.join("Mods").join(module);
	if !source.is_dir() {
		return Err(format!("{module} isn't in the Mods folder."));
	}
	let base = root().ok_or("No place to keep backups.")?.join(module);
	let mut name = format!("{prefix}{}", stamp());
	let mut n = 2;
	while base.join(&name).exists() {
		name = format!("{prefix}{}-{n}", stamp());
		n += 1;
	}
	let target = base.join(&name);
	let files = files_of(&source);
	let mut bytes = 0u64;
	for relative in &files {
		let to = target.join(relative);
		if let Some(parent) = to.parent() {
			fs::create_dir_all(parent).map_err(|e| e.to_string())?;
		}
		bytes += fs::copy(source.join(relative), &to).map_err(|e| format!("{}: {e}", relative.display()))?;
	}
	fs::create_dir_all(&target).map_err(|e| e.to_string())?;
	let _ = fs::write(target.join(INFO), format!("{{\"files\":{},\"bytes\":{}}}", files.len(), bytes));
	Ok(name)
}

/// The backups of one mod (or of every mod), newest first: {mod, name, files, bytes}.
pub fn list(module: Option<&str>) -> serde_json::Value {
	let mut out = Vec::new();
	let Some(base) = root() else { return serde_json::json!({"folder": null, "backups": []}) };
	let mods: Vec<String> = match module {
		Some(m) if valid_mod(m) => vec![m.to_string()],
		Some(_) => Vec::new(),
		None => fs::read_dir(&base).map(|d| d.flatten().filter(|e| e.path().is_dir()).map(|e| e.file_name().to_string_lossy().into_owned()).collect()).unwrap_or_default(),
	};
	for m in mods {
		let Ok(entries) = fs::read_dir(base.join(&m)) else { continue };
		let mut names: Vec<String> = entries.flatten().filter(|e| e.path().is_dir()).map(|e| e.file_name().to_string_lossy().into_owned()).collect();
		names.sort();
		names.reverse();
		for name in names {
			let info: serde_json::Value = fs::read_to_string(base.join(&m).join(&name).join(INFO)).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or(serde_json::json!({}));
			out.push(serde_json::json!({"mod": m, "name": name, "files": info["files"].as_u64().unwrap_or(0), "bytes": info["bytes"].as_u64().unwrap_or(0)}));
		}
	}
	serde_json::json!({"folder": base.to_string_lossy(), "backups": out})
}

/// What a restore changed in the mod's folder: (path, deleted).
pub type Changes = Vec<(PathBuf, bool)>;

/// Puts the mod's folder back as it was in the backup: its files written over (in place, so a file that is linked elsewhere stays linked) and any
/// file the backup doesn't have taken out. A backup of how it is now is taken first.
pub fn restore(game_root: &Path, module: &str, name: &str) -> Result<(String, Changes), String> {
	if !valid_mod(module) || !valid_backup_name(name) {
		return Err("That isn't a backup.".into());
	}
	let from = root().ok_or("No place to keep backups.")?.join(module).join(name);
	if !from.is_dir() {
		return Err("That backup is gone.".into());
	}
	let target = game_root.join("Mods").join(module);
	let safety = if target.is_dir() { create(game_root, module, "before-restore-")? } else { String::new() };
	let wanted = files_of(&from);
	let mut changes: Changes = Vec::new();
	for relative in files_of(&target) {
		if !wanted.contains(&relative) {
			let path = target.join(&relative);
			if fs::remove_file(&path).is_ok() {
				changes.push((path, true));
			}
		}
	}
	for relative in &wanted {
		let to = target.join(relative);
		if let Some(parent) = to.parent() {
			fs::create_dir_all(parent).map_err(|e| e.to_string())?;
		}
		fs::copy(from.join(relative), &to).map_err(|e| format!("{}: {e}", relative.display()))?;
		changes.push((to, false));
	}
	Ok((safety, changes))
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn a_backup_is_a_copy_of_the_whole_mod_and_a_restore_puts_it_back() {
		let temp = std::env::temp_dir().join(format!("cs-backup-test-{}", std::process::id()));
		let _ = fs::remove_dir_all(&temp);
		let game = temp.join("game");
		let module = game.join("Mods").join("Test.rte");
		fs::create_dir_all(module.join("Sprites")).unwrap();
		fs::write(module.join("Index.ini"), "one").unwrap();
		fs::write(module.join("Sprites").join("a.png"), "pic").unwrap();
		// Backups go under the config folder, which comes from APPDATA (Windows), HOME or XDG_CONFIG_HOME.
		let config = temp.join("config");
		fs::create_dir_all(&config).unwrap();
		std::env::set_var("APPDATA", &config);
		std::env::set_var("XDG_CONFIG_HOME", &config);
		std::env::set_var("HOME", &config);

		let name = create(&game, "Test.rte", "").unwrap();
		let listed = list(Some("Test.rte"));
		assert_eq!(listed["backups"][0]["name"], name);
		assert_eq!(listed["backups"][0]["files"], 2);

		// Change the mod: edit one file, add one, remove one.
		fs::write(module.join("Index.ini"), "two").unwrap();
		fs::write(module.join("New.ini"), "new").unwrap();
		fs::remove_file(module.join("Sprites").join("a.png")).unwrap();

		let (safety, changes) = restore(&game, "Test.rte", &name).unwrap();
		assert!(safety.starts_with("before-restore-"));
		assert_eq!(fs::read_to_string(module.join("Index.ini")).unwrap(), "one");
		assert_eq!(fs::read_to_string(module.join("Sprites").join("a.png")).unwrap(), "pic");
		assert!(!module.join("New.ini").exists());
		assert!(changes.iter().any(|(p, deleted)| *deleted && p.ends_with("New.ini")));
		// The restore kept how it was just before: two backups now.
		assert_eq!(list(Some("Test.rte"))["backups"].as_array().unwrap().len(), 2);
		assert!(create(&game, "../Test.rte", "").is_err());
		let _ = fs::remove_dir_all(&temp);
	}
}
