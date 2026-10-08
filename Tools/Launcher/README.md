# Cortex Launcher

A small native Windows (WinForms, .NET 8) app that pulls any branch, tag or commit of this repository, builds it and runs it, so you can jump between versions quickly (e.g. `v8.2.3`, `engine/...`, `visuals/...`, `dev-8.2`).

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
1. Pick a **Branch** (or type any branch / tag / sha and press Enter). The latest commit is selected automatically.
2. Pick the configuration and, optionally, a Settings.ini.
3. Press **Build & Run**.

To use an older commit, or to Build / Run separately, delete the cached build, open its folder or change the repo path, expand **Commits**. Builds run `msbuild /m /p:Configuration=... /p:Platform=x64 RTEA.sln` after copying `fmod.dll` next to the exe. The repo is only used for `git fetch` and as the worktree source; its working tree is never touched.

**Run latest** fetches, moves to the newest commit of the chosen branch, and runs it, building only if that commit has not been built yet.

## Mods folder
Set **Mods folder** to a folder containing `*.rte` mods (or a single `.rte` folder). Before each run the launcher creates a directory junction for each one inside the version's `Data` folder, skipping any the version already has. Nothing is copied, so every version sees the same mods.

## Settings.ini
Set the **Settings.ini** box (or browse for a file) to have the launcher copy that file into the version's folder every time it runs a version, replacing whatever is there. The game rewrites its own copy on exit, so your chosen file is never modified. Clear the box to let each version use its own settings.

## Live feed
The **Live feed** tab polls the remote (`git ls-remote`, every 10s by default; adjustable, or untick Live). When a branch or tag moves it fetches and adds a row per new commit: time, branch or tag, sha, game version, author and message, newest on top, with a chime and an "(N new)" badge on the tab. New branches show their own commits, force-pushes show the new tip, and new tags (e.g. `v8.2.4`) appear as they are created. Double-click a row to jump to that commit in the main list, then Build & Run.

## Where things go
Each commit is a `git worktree` in `<repo parent>\CortexVersions\<first 10 chars of sha>`. Built versions stay runnable without rebuilding, and your main checkout is never switched. Settings live in `%APPDATA%\CortexLauncher\settings.json` (`VersionsDir` can relocate the cache).

Note: each version is a full checkout plus a full C++ build (first build per commit is slow, later ones reuse intermediates only within that folder), so delete old versions when you are done with them.
