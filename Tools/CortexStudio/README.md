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

## Building it

Needs [Rust](https://rustup.rs) and Node.js (for the Tauri command line), plus on Linux:

    sudo apt install libwebkit2gtk-4.1-dev libayatana-appindicator3-dev librsvg2-dev libxdo-dev

Then in this folder:

    .\dev.ps1          # Windows: a debug build, run on this checkout (-GameDir <folder> for another, -Bridge for headless)
    ./dev.sh           # Linux and macOS: the same ([game folder] [--bridge])
    npx @tauri-apps/cli@2 build     # the release app and its installers, in src-tauri/target/release/bundle

The dev scripts serve Mod Studio's page straight from `Tools/ModStudio` (by setting `CORTEX_STUDIO_PAGE`), so edit the page and
reload the window (Ctrl+R or F5) to see it; a change to the Rust side needs the script run again. The release build has the page
built in.
