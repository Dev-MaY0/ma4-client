# Changelog

All notable changes to ma4 Client. Versions follow `MAJOR.MINOR.PATCH`; the current version lives in
`src/client/Ma4.h` (`Ma4::version`).

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
