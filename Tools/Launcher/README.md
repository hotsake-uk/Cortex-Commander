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
1. Set **Repo** to your existing checkout (default `C:\Users\Liamn\Desktop\cortex\Cortex-Command-Community-Project`). It is only used for `git fetch` and as the worktree source; its working tree is never touched.
2. The left list shows versions first (`ver: 8.2.N`, read from `[8.2.N]` commit titles or `Version 8.2.N` lines on the first-parent history of `origin/dev-8.2`, since cloud threads cannot push tags), then any real tags, then remote branches by recent activity. Use the filter box, or type any branch / tag / sha in the box next to the buttons and press **Go**.
3. Pick a commit (each row shows the game version read from `GameVersion.h`) and press **Build & Run** (or double-click the row). **Build** and **Run** are also available separately.
4. The configuration dropdown picks `Final`, `Debug Release`, `Debug Minimal` or `Debug Full`. Builds run `msbuild /m /p:Configuration=... /p:Platform=x64 RTEA.sln`, as in `.github/workflows/msbuild.yml`, after copying `fmod.dll` next to the exe as the README requires.
5. **Delete cached** removes that commit's checkout and build. **Open folder** opens it in Explorer.

## Settings.ini
Set the **Settings.ini** box (or browse for a file) to have the launcher copy that file into the version's folder every time it runs a version, replacing whatever is there. The game rewrites its own copy on exit, so your chosen file is never modified. Clear the box to let each version use its own settings.

## Live feed
The **Live feed** tab polls the remote (`git ls-remote`, every 10s by default; adjustable, or untick Live). When a branch or tag moves it fetches and adds a row per new commit: time, branch or tag, sha, game version, author and message, newest on top, with a chime and an "(N new)" badge on the tab. New branches show their own commits, force-pushes show the new tip, and new tags (e.g. `v8.2.4`) appear as they are created. Double-click a row to jump to that commit in the main list, then Build & Run.

## Where things go
Each commit is a `git worktree` in `<repo parent>\CortexVersions\<first 10 chars of sha>`. Built versions stay runnable without rebuilding, and your main checkout is never switched. Settings live in `%APPDATA%\CortexLauncher\settings.json` (`VersionsDir` can relocate the cache).

Note: each version is a full checkout plus a full C++ build (first build per commit is slow, later ones reuse intermediates only within that folder), so delete old versions when you are done with them.
