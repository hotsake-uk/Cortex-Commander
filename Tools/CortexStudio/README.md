# Cortex Studio

Mod Studio as one desktop app for Windows, macOS and Linux, built with [Tauri](https://tauri.app). It shows Mod Studio's page
(`Tools/ModStudio`, built into the app) in its own window and does the work a web page can't: reading and saving the game folder,
and the file bridge that reloads scripts and INI files you save in any editor. This is phase 1 of the Cortex Studio plan; the
launcher's jobs (versions, builds, launching, mod folders) come next.

## Using it

Install it from the build artefacts of the **Cortex Studio** workflow (`.msi` or `-setup.exe` on Windows, `.dmg` on macOS,
`.AppImage` or `.deb` on Linux) and start it. It uses the game folder it used last, the game folder it sits in, or asks for one
(the folder with `Data/Base.rte` inside).

    cortex-studio [--game-dir <game folder>] [--bridge] [--no-bridge] [--port <port>]

- `--bridge` runs only the file bridge and Studio's server, with no window, printing Studio's address. Leave the game and the
  bridge running and work in your own editor (the same as `Mod Studio Bridge.bat`).
- `--no-bridge` turns the file watching off.
- The page also opens in any browser at the address printed, as with `Mod Studio.bat`.

Installers aren't code-signed yet, so Windows SmartScreen and macOS Gatekeeper warn the first time it's opened ("More info",
"Run anyway" on Windows; right-click, Open on macOS).

## How it works

- `src-tauri/src/server.rs`: a small server on 127.0.0.1 answering the same requests as `Tools/ModStudio/Start-ModStudio.ps1`
  (`info`, `inis`, `luas`, `files`, `list`, `tree`, `tail`, `file`, `delete`, `changes`), only for requests carrying the token in
  the page's address. On Linux and macOS, paths written in other letter case than the files on disk are matched to them, as
  Windows does.
- `src-tauri/src/watcher.rs`: the file bridge. A `.lua` or `.ini` saved outside Studio in `Data` or `Mods` is written to
  `ModStudio/Scripts.ini` as `ReloadScript` or `ReloadFile` for a game started with `-modstudio`, and listed for the page.
  Studio's own saves are left alone.
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

- **Run Cortex Studio.bat** builds the app from source and opens it on this checkout. The first build takes a few minutes and later
  ones take seconds. Mod Studio's page is served straight from `Tools\ModStudio`, so after editing the page press **Ctrl+R** or
  **F5** in the app to see it. After changing the Rust code in `src-tauri`, close the app and run the .bat again.
  - `"Run Cortex Studio.bat" -GameDir D:\Games\Cortex` uses another game folder.
  - `"Run Cortex Studio.bat" -Bridge` runs only the file bridge, with no window, and prints Studio's address.
- **Build Cortex Studio.bat** builds the release app with the page built in, plus the `.msi` and setup `.exe` installers, and
  opens the folder they're in (`src-tauri\target\release\bundle`). The app on its own is `src-tauri\target\release\cortex-studio.exe`.

### Linux and macOS

Install Rust from [rustup.rs](https://rustup.rs) and Node.js. On Linux also:

    sudo apt install libwebkit2gtk-4.1-dev libayatana-appindicator3-dev librsvg2-dev libxdo-dev

macOS needs Xcode's command line tools (`xcode-select --install`). Then in this folder:

    ./dev.sh [game folder] [--bridge]      # run from source, like Run Cortex Studio.bat
    npx @tauri-apps/cli@2 build            # the release app and its installers, in src-tauri/target/release/bundle

Both .bat files call `dev.ps1` or the Tauri command line, so the same commands work from a terminal on Windows too.
