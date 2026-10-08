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
2. The left list shows tags first (newest version first, so `v8.2.N` is at the top), then remote branches by recent activity. Use the filter box, or type any branch / tag / sha in the box next to the buttons and press **Go**.
3. Pick a commit (each row shows the game version read from `GameVersion.h`) and press **Build & Run** (or double-click the row). **Build** and **Run** are also available separately.
4. The configuration dropdown picks `Final`, `Debug Release`, `Debug Minimal` or `Debug Full`. Builds run `msbuild /m /p:Configuration=... /p:Platform=x64 RTEA.sln`, as in `.github/workflows/msbuild.yml`, after copying `fmod.dll` next to the exe as the README requires.
5. **Delete cached** removes that commit's checkout and build. **Open folder** opens it in Explorer.

## Where things go
Each commit is a `git worktree` in `<repo parent>\CortexVersions\<first 10 chars of sha>`. Built versions stay runnable without rebuilding, and your main checkout is never switched. Settings live in `%APPDATA%\CortexLauncher\settings.json` (`VersionsDir` can relocate the cache).

Note: each version is a full checkout plus a full C++ build (first build per commit is slow, later ones reuse intermediates only within that folder), so delete old versions when you are done with them.
