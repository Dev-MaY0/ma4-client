# Changelog

All notable changes to ma4 Client. Versions follow `MAJOR.MINOR.PATCH`; the current version lives in
`src/client/Ma4.h` (`Ma4::version`).

## [1.0.5] - 2026-11-11

### Changed
- **The whole UI is now Liquid Glass (iOS 26).** Menu, HUD editor, HUD plates, banners and replay viewer were
  redesigned after the screen structure, state handling and animations of the SwiftUI *Liquid Glass Examples*
  gallery (nothing was copied: SwiftUI is not usable from C++, the ideas were rebuilt in Direct2D).
  - **Glass material** (`Ui::glass`): soft two-layer shadow, frosted backing, lit sheen, gradient specular rim and a
    top glint; `regular` / `clear` variants, `tint`, interactive press / hover light-up (mirrors `glassEffect`).
  - **Springs with overshoot** (`Ui::spring`, SwiftUI `response` / `dampingFraction` model) drive the window, popovers,
    sheets, banners, switches, sliders and the selection capsule. Interactive glass swells and bounces back on press.
  - **Floating sidebar** with concentric corners and a single selection capsule (tinted glass) that springs to the
    chosen row and stretches while it travels. Search is a floating glass capsule at the bottom of the sidebar; while
    typing it splits off a round glass cancel button.
  - **Navigation**: no bar any more. Round floating glass back / close buttons, the inline title fades in on scroll,
    and content fades into the page colour under the controls (scroll edge effect).
  - **Controls**: 63x28 switch with an oval knob that turns into glass while touched, 6 pt slider track with a capsule
    knob, glass segmented control whose selection stretches, glass / prominent-glass / tinted capsule buttons,
    capsule text fields and 24 pt grouped cards.
  - **Popover** is a glass menu with capsule highlights; the **colour picker** is a floating bottom sheet with a
    grabber, circular close / confirm buttons and pull-down-to-dismiss.
  - **Replay viewer**: full-bleed map with a floating glass transport panel; the three transport buttons merge like a
    `GlassEffectContainer` and pull apart while one is pressed; capsule scrubber knob.
  - **HUD**: text modules and keystroke keys get the glass sheen + rim, editor name labels are glass chips, the
    *Mod Settings* button is interactive glass, notification banners drop in on a spring.

## [1.0.4] - 2026-11-10

### Added
- **Discord shows "Playing Ma4".** Discord Presence now uses the Ma4 Discord application
  (id `1569672619685453834`) instead of Latite's, and it is **on by default**: the first time a config sees the module
  it is switched on once (turn it off in the menu and that stays off). The presence uses the "ma4" art asset only
  (see README > Discord Presence). `docs/discord/ma4.png` is the 1024px icon to upload.

### Changed
- Presence images no longer reference Latite's art keys (server logos, "minecraft", "latite"); the large image is the
  `ma4` asset with the text "Ma4 Client v<version>".

## [1.0.3] - 2026-11-10

### Changed
- HUD editor (opened with the menu key): the hint line "drag to move / scroll to resize / right-click for settings /
  middle-click to hide" is gone; only the *Mod Settings* button is left.
- The menu no longer shows module descriptions: the grey line under every module in the list and under the module name
  on the detail page are removed (plugin rows keep their "by author" line, per-setting descriptions are unchanged).
  List rows are a little shorter as a result.
- **Bigger replay map.** Default map radius 3 -> 6 chunks, up to 12 (slider *Map Radius*, new setting key so the old
  saved value does not override it), 256 columns per tick (was 160), the scan reaches 32 blocks above you (was 24),
  and chunks that were not loaded yet are retried up to 8 times with growing delays; fully unloaded chunks are skipped
  cheaply.

## [1.0.2] - 2026-11-10

### Fixed
- **Game crash when a replay recording started.** The map sampler called `BlockSource::getBlock(x, y, z)`, which lands
  in the wrong vtable slot on this build (the game read the x coordinate as a pointer -> access violation). It now uses
  the `getBlock(BlockPos)` overload that WAILA already relies on.
- Replay hardening: every read of game memory (nearby entities, block source, blocks) is guarded; if one ever faults the
  recording is stopped (or the map switched off) with a log line and a notification instead of crashing the game.
  Actor components are null-checked, the level comes from `Minecraft::getLevel()` like in the other modules, and the
  recorder is protected against the menu (render thread) and the game tick racing each other.

### Changed
- Docs: the data folder is `%LocalAppData%\ma4` (logs and crash dumps are in `%LocalAppData%\ma4\Logs`).

## [1.0.1] - 2026-11-10

### Added
- **Replay map.** The replay recorder now also records the terrain around you (top block colour + height of every
  column, nearest chunks first, a few hundred columns per tick). The replay viewer draws it as a top-down map with hill
  shading under the path, players and markers. A **Map** button in the viewer toggles it.
  - New module options: *Record Map* and *Map Radius (chunks)*.
  - `.ma4replay` file format v2 (v1 recordings still open, they just have no map).
- New ma4 icon (circular watercolour "Ma4" logo) in the menu and notification banners.
- `tools/publish.ps1`: one command that commits, tags and pushes a release to
  https://github.com/Dev-MaY0/ma4-client.

### Changed
- Version numbering restarts at the ma4 line: **1.0.1** (the inherited Latite 2.9.x numbers are gone).

## [1.0.0] - 2026-11-10

### Added
- Renamed from Latite Client to **ma4 Client** (`ma4.dll`, `%AppData%\Roaming\ma4`).
- iOS styled mod menu (iPadOS "Settings app" layout), HUD editor, notification banners and HUD defaults.
- Replay (beta): data recorder (path, nearby players / mobs, markers) and a top-down viewer with scrubber, speed
  control, pan / zoom and a keyframed camera path.
- Dark / Light appearance and menu scale settings, right-to-left mirroring.
