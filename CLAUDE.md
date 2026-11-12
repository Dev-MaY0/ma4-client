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

- `src/client/screen/ios/` - game-independent iOS 26 "Liquid Glass" UI toolkit (`IosUi` controls, `IosGlass.cpp` glass
  material / springs / glass buttons / scroll edge), the Settings-app shell (`IosShell`, replay pages in
  `IosShellReplay.cpp`, shared layout in `IosShellCommon.h`), data model + text input. `IosBridge` connects it to the
  renderer / settings. HUD plates use `feature/module/GlassPlate.h` (works with a bare device context).
- `src/client/screen/screens/ClickGUI.*` - adapter between modules / settings / plugins and the shell.
- `src/client/misc/ReplayStore.*`, `ReplayTerrain.*` - replay recorder, `.ma4replay` files, terrain map sampler.
- `launcher/` - `Ma4Launcher.exe` (lock-screen style launcher: clock, "Ma4" title, wallpaper, Play). It must stay
  **outside `src/`** (everything in `src/` is compiled into the DLL). It compiles `IosUi.cpp` + `IosGlass.cpp` for its
  controls, reads the version from `src/client/Ma4.h` (CMake), and its resources (icon, logo) are compiled by a custom
  `rc.exe` command because the MSYS2 CMake has no `cmcldeps` for Ninja + RC. `Ma4Launcher.exe --render out.png` draws
  a frame without a window (use it to check the launcher visually).
- `src/client/screen/ios/IosCards.*` - game independent cards for HUD modules (Spotify "Now Playing", Tab player list).
  They take plain data so the harness renders them; the modules (`modules/hud/SpotifyHUD.*`, `TabList.*`) only gather the
  data. `client/misc/NowPlaying.*` (Windows media controls via C++/WinRT, thread) and `client/misc/Lyrics.*` (LRCLIB,
  thread, injectable fetch) are game independent too: `tools/nowplaying-probe` builds them standalone.
- `tools/ui-harness/` - renders every screen to PNG without Minecraft (`build.bat`, then `out\harness.exe <dir>`).
  Use it to check UI changes visually; it links the same sources as the DLL. `out\harness.exe --bench [W H]` prints
  the frame cost and the layers / layouts / gradients per frame: run it after touching the UI toolkit.
- Source files must stay ASCII or have a BOM (MSVC reads BOM-less files as the system code page): use `\uXXXX` escapes
  for non-ASCII characters in string literals.
- UI performance rules (the menu is drawn on the game's render thread every frame): no clip layer per row (use
  `Ui::beginCard` / `fillCell`), no network or disk work in `ClickGUI::onRender` (the plugin registry is fetched on a
  thread), create gradient brushes through `Ui::linearBrush` (cached) and text through `Ui::text` (layouts cached).
- Never block the render thread or the key handlers: network, WinRT calls and directory scans live on their own threads
  (SpotifyHUD, the plugin registry). Code that draws from inside another toolkit frame (a HUD preview in the menu) must
  check `Ui::inFrame()` instead of calling `beginFrame()` again.
- Game hooks / signatures (`src/mc`, `src/client/memory`) are version specific and cannot be verified without the game.
