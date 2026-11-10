# ma4 Client - notes for Claude

Minecraft Bedrock (Windows) client DLL, a rename + redesign of Latite Client (GPLv3). Remote:
https://github.com/Dev-MaY0/ma4-client (branch `main`).

## Release rule (the owner asked for this, it is pre-authorised)

After **every update you finish in this project**:

1. Bump the version (semver, e.g. 1.0.1 -> 1.0.2) and add a section for it at the top of `CHANGELOG.md`.
2. Make sure `cmake --preset x64-release && cmake --build out/build/x64-release` succeeds (see "Build").
3. Commit + tag locally with `tools/publish.ps1 -Version X.Y.Z -Message "<summary>" -NoPush` (add the usual
   `Co-Authored-By` trailer to the message). It commits everything, tags `vX.Y.Z`, refuses to run if the git top-level
   is not this folder and never force-pushes.
4. Push from the **user's Terminal panel** (`run_in_terminal`, cwd = this folder): `git push origin main --tags`.
   The Bash / PowerShell tools of a session have no GitHub credentials (push fails with "could not read Username");
   the user's own terminal has Git Credential Manager and works. If even that asks for a sign-in, tell the user and
   wait. Never bypass it with `--force`.

Do not commit build output (`out/` is git-ignored) or personal files. The folder lives inside the user's home folder,
which is itself a git repository - always work with the repository in this folder (`git -C <this folder>`).

## Build

Run from a VS developer prompt (or via `vcvars64.bat`) with MinGW's `ld.exe` on `PATH`:

    cmake --preset x64-release
    cmake --build out/build/x64-release     # -> out/build/x64-release/ma4.dll

CMake caches absolute paths: if this folder is moved / renamed, delete `out/` first.

## Layout

- `src/client/screen/ios/` - game-independent iOS UI toolkit (`IosUi`), the Settings-app shell (`IosShell`, replay
  pages in `IosShellReplay.cpp`), data model + text input. `IosBridge` connects it to the renderer / settings.
- `src/client/screen/screens/ClickGUI.*` - adapter between modules / settings / plugins and the shell.
- `src/client/misc/ReplayStore.*`, `ReplayTerrain.*` - replay recorder, `.ma4replay` files, terrain map sampler.
- `tools/ui-harness/` - renders every screen to PNG without Minecraft (`build.bat`, then `out\harness.exe <dir>`).
  Use it to check UI changes visually; it links the same sources as the DLL.
- Source files must stay ASCII or have a BOM (MSVC reads BOM-less files as the system code page): use `\uXXXX` escapes
  for non-ASCII characters in string literals.
- Game hooks / signatures (`src/mc`, `src/client/memory`) are version specific and cannot be verified without the game.
