# Cortex Launcher

A small native Windows (WinForms, .NET 8) app that pulls any branch, tag or commit of this repository, builds it and runs it, so you can jump between versions quickly (e.g. `v8.2.3`, `engine/...`, `visuals/...`, `maindev`).

## Requirements
- Windows with Git on `PATH`
- Visual Studio 2022 with the "Desktop development with C++" workload (the launcher finds MSBuild via `vswhere`)
- .NET 8 SDK (only to build the launcher itself)

## Build and start
From a Developer PowerShell / any shell with `dotnet`:

    cd Tools\Launcher
    .\publish.ps1

That produces a single self-contained `Tools\Launcher\dist\CortexLauncher.exe`. Copy it anywhere and double-click it. (For development, `dotnet run` in this folder also works.)

## Using it
1. **Branch**: pick one from the list (or type any branch / tag / sha and press Enter). **Refresh** fetches from the remote and reloads the commits.
2. **Commit**: newest first by date and time; the Version column shows the game version, and commits after a version bump read `8.2.537+N` (N commits past the bump) so rows are distinguishable; the latest is selected for you; click an older one if you want it.
3. **Settings** and **Mods** (both optional): a Settings.ini file is copied to the version's `Userdata/Settings.ini`, and every `*.rte` folder in the mods folder is put in its `Mods/` folder, changed files only, as hard links to your files (copies when the mods folder is on another drive). A hard link is the same file, so a new version's first start doesn't read and virus-scan every mod afresh. The Settings.ini is never modified. A mod that rewrites one of its own files while the game runs (some keep saves in their folder) changes your original too; deleting a version only removes its links.
4. **Build & Launch**, or **Launch built** to run the one build that is on disk without building. Only one version is kept: building another commit moves the single checkout (`CortexVersions\Current`) to it, so builds are incremental and take no extra space. The Built column marks the commit that is built. **Delete build** removes it (and any old per-commit builds from earlier launcher versions).

Builds run `msbuild /m /p:Configuration=... /p:Platform=x64 RTEA.sln` after copying `fmod.dll` next to the exe. The repo is only used for `git fetch` and as the worktree source; its working tree is never touched.

## Mod check
**Check mods** (on the Mods row) checks every mod in the mods folder against a version of the game without starting it, using `Tools/Mods/Check-Mod.py`: Lua calls to engine functions that don't exist, INI properties and presets the game doesn't have, missing files and missing dependency mods. It checks against the build slot when it holds the selected commit, otherwise against your repo checkout. A window lists each mod's errors, warnings and notes; double-click a finding to open its file, or **Open report** for the full text report.

**Fix N problems** makes the fixes that have one right answer (renamed functions and properties, letter case in file paths, the nil checks for `.Magazine` and `GetMOFromID`). It lists them and asks first, changes the files in your mods folder, keeps a `.bak` of each changed file next to it, then checks again.

With **Check before launch** ticked (the default), Build & Launch and Launch built check the mods against the version being launched. If a mod has an error, the window opens with **Launch anyway** and **Don't launch**. An unchanged setup (same commit, same mod files) that already passed, or that you launched anyway, isn't checked again.

The check needs Python 3 (`py -3`, `python` or `python3`; from python.org, with "Add to PATH" ticked). Without it the launcher says so and launches as normal. Versions older than 8.5.228 have no checker of their own, so the one in your repo checkout is used for them. Reports are kept in `%APPDATA%\CortexLauncher\ModChecks`.

## Worlds
**World** picks a planet skin (backdrops, title-screen planet, scenery) for the next launch, or **Stock**. Skins live in `%APPDATA%\CortexLauncher\Worlds`, one folder per world laid out like `Data\Base.rte` (a `base.rte` folder inside it also works, which is how skin packs such as Cortex Command Worlds ship); **Open folder** opens it. On launch the version's `Data\Base.rte` is restored from git and the skin's files are copied over it. Files the version's Base.rte no longer has are skipped. The repo and your main checkout are never touched.

## Presets
Set **Presets** to a shared folder (for example your main checkout's `Userdata\Presets`). Before each run the launcher makes the version's `Userdata\Presets` a directory junction to it, so every version sees the same saved settings presets and presets you save in any version land in the shared folder. Presets already in a version's own folder are moved into the shared folder first.

## Defaults and clearing builds
`Defaults\` in this folder holds the default `Settings.ini` and settings presets. They are embedded in the exe, unpacked to `%APPDATA%\CortexLauncher\Defaults` on first run (existing files there are kept), and used as the starting values of the **Settings** and **Presets** boxes. Distributing the launcher therefore distributes these defaults. **Delete build** deletes the build and leaves the defaults, your presets and your mods alone.

## Adding mods from links
Paste mod.io download links into the **Add mods** box, one on each line, and press **Download & check**. On a mod's mod.io page, right-click **Download** and copy the link address; it looks like `https://g-508.modapi.io/v1/games/508/mods/<id>/files/<id>/download`. A mod's web-page address is not a download, and the launcher says so. For each link the launcher downloads the zip, unpacks it, puts every `.rte` folder it finds in your Mods folder (replacing an older copy of the same mod; with no Mods folder set it makes one under `%APPDATA%\CortexLauncher\Mods` and fills the box), then runs the mod check on just those mods and shows the usual results window, where Fix and Open report work as before. Links from mod.io can expire, so use a fresh one if a download fails.

## Where things go
Each commit is a `git worktree` in `<repo parent>\CortexVersions\<first 10 chars of sha>`. Built versions stay runnable without rebuilding, and your main checkout is never switched. Settings live in `%APPDATA%\CortexLauncher\settings.json` (`VersionsDir` can relocate the cache).

Note: each version is a full checkout plus a full C++ build (first build per commit is slow, later ones reuse intermediates only within that folder), so delete old versions when you are done with them.
