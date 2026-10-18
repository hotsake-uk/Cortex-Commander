// Which Cortex Studio this is, picked when it is built (CORTEX_STUDIO_EDITION):
//
//   commander  Cortex Commander only, the release build for this game: nothing about Community Edition shows.
//   community  Cortex Studio CE, the release build for Community Edition modders: only 6.2.2 and the nightly, nothing Commander's
//              engine alone does, and its own name and settings folder so it installs beside the Commander one.
//   dev        both, with the game the mods are for picked in Settings: what a debug build is unless told otherwise.
//
// A debug build also reads CORTEX_STUDIO_EDITION when it starts, so ./dev.sh can try either release edition without building it.
// The page learns the edition from the data-edition its HTML is served with (server.rs), and js/target.js does the rest.

pub fn edition() -> &'static str {
	let pick = |e: &str| match e.trim().to_ascii_lowercase().as_str() {
		"commander" => Some("commander"),
		"community" | "ce" => Some("community"),
		"dev" | "both" => Some("dev"),
		_ => None,
	};
	if cfg!(debug_assertions) {
		if let Some(e) = std::env::var("CORTEX_STUDIO_EDITION").ok().as_deref().and_then(pick) {
			return e;
		}
	}
	match option_env!("CORTEX_STUDIO_EDITION").and_then(pick) {
		Some(e) => e,
		None if cfg!(debug_assertions) => "dev",
		None => "commander",
	}
}

pub fn community() -> bool {
	edition() == "community"
}

/// The app's name, in its window and dialogs.
pub fn app_name() -> &'static str {
	if community() { "Cortex Studio CE" } else { "Cortex Studio" }
}
