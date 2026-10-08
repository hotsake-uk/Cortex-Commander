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
1. **Branch**: pick one from the list (or type any branch / tag / sha and press Enter). **Refresh** fetches from the remote and reloads the commits.
2. **Commit**: newest first by date and time; the latest is selected for you; click an older one if you want it.
3. **Settings** and **Mods** (both optional): a Settings.ini file is copied to the version's `Userdata/Settings.ini`, and every `*.rte` folder in the mods folder is put in its `Mods/` folder, changed files only, as hard links to your files (copies when the mods folder is on another drive). A hard link is the same file, so a new version's first start doesn't read and virus-scan every mod afresh. The Settings.ini is never modified. A mod that rewrites one of its own files while the game runs (some keep saves in their folder) changes your original too; deleting a version only removes its links.
4. **Build & Launch**.

Builds run `msbuild /m /p:Configuration=... /p:Platform=x64 RTEA.sln` after copying `fmod.dll` next to the exe. The repo is only used for `git fetch` and as the worktree source; its working tree is never touched.

## Where things go
Each commit is a `git worktree` in `<repo parent>\CortexVersions\<first 10 chars of sha>`. Built versions stay runnable without rebuilding, and your main checkout is never switched. Settings live in `%APPDATA%\CortexLauncher\settings.json` (`VersionsDir` can relocate the cache).

Note: each version is a full checkout plus a full C++ build (first build per commit is slow, later ones reuse intermediates only within that folder), so delete old versions when you are done with them.
