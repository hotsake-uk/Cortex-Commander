// Builds Mod Studio's page (Tools/ModStudio, with Vite) into Tools/ModStudio/dist before the app is compiled, since server.rs puts
// dist/ inside the app. Needs Node.js (nodejs.org); the first build also installs Vite into Tools/ModStudio/node_modules.
use std::path::Path;
use std::process::Command;

fn main() {
	build_page();
	tauri_build::build()
}

fn build_page() {
	let page = Path::new(env!("CARGO_MANIFEST_DIR")).join("..").join("..").join("ModStudio");
	for input in ["js", "studio.css", "ModStudio.html", "vendor", "data", "package.json", "package-lock.json", "vite.config.js"] {
		println!("cargo:rerun-if-changed={}", page.join(input).display());
	}
	println!("cargo:rerun-if-env-changed=CORTEX_STUDIO_SKIP_PAGE_BUILD");
	if std::env::var_os("CORTEX_STUDIO_SKIP_PAGE_BUILD").is_some() && page.join("dist").join("ModStudio.html").is_file() {
		return;
	}
	let npm = if cfg!(windows) { "npm.cmd" } else { "npm" };
	let run = |args: &[&str]| {
		let status = Command::new(npm).args(args).current_dir(&page).status();
		match status {
			Ok(s) if s.success() => {}
			Ok(s) => panic!("`npm {}` in {} stopped with {s}.", args.join(" "), page.display()),
			Err(e) => panic!("Couldn't run npm ({e}). Building Cortex Studio needs Node.js: install it from https://nodejs.org, then open a new terminal."),
		}
	};
	if !page.join("node_modules").join("vite").is_dir() {
		run(&["ci", "--no-audit", "--no-fund"]);
	}
	run(&["run", "build", "--silent"]);
}
