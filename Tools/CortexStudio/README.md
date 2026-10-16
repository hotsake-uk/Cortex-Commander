# Cortex Studio

Mod Studio as one desktop app for Windows, macOS and Linux, built with [Tauri](https://tauri.app). It shows Mod Studio's page
(`Tools/ModStudio`, built into the app) in its own window and does the work a web page can't: reading and saving the game folder,
the file bridge that reloads scripts and INI files you save in any editor, and the Windows launcher's jobs on every platform:
setting up, picking a version of the game, building it and launching it to play or to test mods.

## Using it

Install it from the build artefacts of the **Cortex Studio** workflow (`.msi` or `-setup.exe` on Windows, `.dmg` on macOS,
`.AppImage` or `.deb` on Linux) and start it. It uses the game folder it used last, the game folder it sits in, or asks for one
(the folder with `Data/Base.rte` inside).

    cortex-studio [--game-dir <game folder>] [--bridge] [--no-bridge] [--port <port>]

- `--bridge` runs only the file bridge and Studio's server, with no window, printing Studio's address. Leave the game and the
  bridge running and work in your own editor (the same as `Mod Studio Bridge.bat`).
- `--no-bridge` turns the file watching off.
- The page also opens in any browser at the address printed, as with `Mod Studio.bat`.

### The Game screen

The **▶ Game** button at the top left (only in the app) opens it. The first time, the setup asks three things:

1. **Simple or Advanced.** Simple keeps one branch (maindev unless you change it) built and ready: **Update & build** fetches the
   newest commit and builds it if it changed. Advanced is the Windows launcher's version picker: any branch, tag, version or
   commit, with the game version of each.
2. **What to install.** It checks this computer for what building the game needs (Git, Visual Studio's C++ tools on Windows;
   meson, ninja, g++ and the libraries on Linux and macOS) and shows the command that installs each missing one, with a Copy button.
3. **Where the game's code is**: your git clone of the game. Studio only fetches from it. Versions are built in
   `CortexVersions/Studio` next to it (the Windows launcher's `CortexVersions/Current` is left alone), so the next build is quick.

Then:

- **Play** launches the built version: Play, Test in game, Sandbox (with a map, Maginot if you leave it empty) or the Actor Editor.
  Launching points Studio at that version, so saves, Preview in game and the console talk to the game you started. "Edit the repo
  checkout" points it back.
- **Versions** (Advanced): pick a commit, then **Build** or **Build & play**. The build's output shows at the bottom, with Cancel.
- **Launch settings**: your Settings.ini (copied in before each launch), a shared presets folder (linked as `Userdata/Presets`),
  your mods folder (each `.rte` linked into `Mods`, changed files only; files Studio adds to or deletes from a linked mod in the version are added to or deleted from your mods folder too) and a world (planet skin). They start as the Windows
  launcher's settings if you used it, and are kept in `CortexStudio/launcher.json` in your config folder.
- **Unlock everything** (Play): for messing about. Studio copies your mods folder into `CortexStudio/Backups/Mods <date> <time>`,
  notes the commit the version is at, then lets the designer and the code editor change anything in the built version where it
  is: the game's own Lua and INI in `Data` (Base.rte and the rest) and every mod, with no copies and no Edit this mod. A version's
  mods are links to your mods folder, so saves there change your mods folder too. **Reset** lists every file it will throw away,
  then checks the version out at that commit again (new files in `Data` go too) and makes your mods folder the backup again.
  **Lock again** keeps your changes. Update & build waits while it's unlocked, since it moves the version to another commit.

Installers aren't code-signed yet, so Windows SmartScreen and macOS Gatekeeper warn the first time it's opened ("More info",
"Run anyway" on Windows; right-click, Open on macOS).

## How it works

- `src-tauri/src/server.rs`: a small server on 127.0.0.1 answering the same requests as `Tools/ModStudio/Start-ModStudio.ps1`
  (`info`, `inis`, `luas`, `files`, `list`, `tree`, `tail`, `file`, `delete`, `changes`, `history`, `history-file`,
  `history-start`), only for requests carrying the token in
  the page's address. On Linux and macOS, paths written in other letter case than the files on disk are matched to them, as
  Windows does.
- `src-tauri/src/watcher.rs`: the file bridge. A `.lua` or `.ini` saved outside Studio in `Data` or `Mods` is written to
  `ModStudio/Scripts.ini` as `ReloadScript` or `ReloadFile` for a game started with `-modstudio`, and listed for the page.
  Studio's own saves are left alone.
- `src-tauri/src/history.rs`: each mod's history, with libgit2 built in (the `git2` crate, vendored), so nobody needs git for it.
  A git folder per mod in `CortexStudio/History/<mod>.git`, with the mod's folder as its work tree: the mod as it was before Studio
  first changed it, then a commit per save or delete. Restores are written by the page as normal saves (in place, so the hard links
  between your mods folder and the versions stay one file).
- `src-tauri/src/launcher.rs`: the Game screen's work (setup check, versions, builds, launching), ported from `Tools/Launcher`,
  answering the server's `launcher/...` requests. The screen itself is `Tools/ModStudio/js/launcher.js`.
- `src-tauri/src/main.rs`: finds the game folder (remembered in `CortexStudio/settings.json` in the user's config folder),
  starts the server and opens the window.

## Building and running it on your PC

### Windows: first time

1. Install **Rust** from [rustup.rs](https://rustup.rs) (or `winget install Rustlang.Rustup`), keeping the default
   `stable-x86_64-pc-windows-msvc` toolchain. Open a new terminal afterwards so `cargo` is on the PATH.
2. Install **Visual Studio 2022** (or its Build Tools) with the **Desktop development with C++** workload. You already have it if
   you build the game.
3. Install **Node.js** from [nodejs.org](https://nodejs.org) (only needed to build the installers).
4. WebView2, which shows the window, comes with Windows 10 and 11.

### Windows: every day

- **Run Cortex Studio.bat** builds the app from source and opens it on the game folder it used last (this checkout the first time). The first build takes a few minutes and later
  ones take seconds. Mod Studio's page is served straight from `Tools\ModStudio`, so after editing the page press **Ctrl+R** or
  **F5** in the app to see it. After changing the Rust code in `src-tauri`, close the app and run the .bat again.
  - `"Run Cortex Studio.bat" -GameDir D:\Games\Cortex` uses another game folder.
  - `"Run Cortex Studio.bat" -Bridge` runs only the file bridge, with no window, and prints Studio's address.
- **Build Cortex Studio.bat** builds the release app with the page built in, plus the `.msi` and setup `.exe` installers, and
  opens the folder they're in (`src-tauri\target\release\bundle`). The app on its own is `src-tauri\target\release\cortex-studio.exe`.

### macOS and Linux

Install Rust from [rustup.rs](https://rustup.rs) and Node.js (Node is only needed for the installers).

- **macOS:** `xcode-select --install` (Xcode's command line tools). To build and play the game from Studio's Game screen you also
  need Homebrew's tools: the Game screen's setup check lists what is missing and shows the `brew install` command for it.
- **Linux:** `sudo apt install libwebkit2gtk-4.1-dev libayatana-appindicator3-dev librsvg2-dev libxdo-dev`

Then run Studio from source, in this folder:

    ./dev.sh [game folder] [--bridge]      # run from source, like Run Cortex Studio.bat
    npx @tauri-apps/cli@2 build            # the release app and its installers, in src-tauri/target/release/bundle

On macOS you can double-click **Run Cortex Studio.command** instead (the first build takes a few minutes; later ones take seconds).
`dev.sh` checks that Rust, Xcode's tools (macOS) and a game folder with `Data/Base.rte` are there, and says what to install if not.
The app finds Homebrew's tools (`meson`, `ninja`, `gcc-13`) even when it was opened from the Finder, which gives programs a bare PATH.

The Cortex Studio workflow also starts the built app with no window on macOS and Linux (`smoke.sh`) and asks it for its page, so a
change that stops it running there fails the build.

Both .bat files call `dev.ps1` or the Tauri command line, so the same commands work from a terminal on Windows too.
