# UI harness

Renders every screen of the iOS style UI (module list, module detail, settings, popover, colour sheet, replay
recorder and viewer; dark / light / right-to-left) to PNG files **without starting Minecraft**. The harness links the
same `src/client/screen/ios/*` sources that the DLL uses; only the data (fake modules / settings / replay clip) and the
procedural background are fake.

    build.bat
    out\harness.exe <output-dir> ..\..\assets\logo.png

It is not part of the CMake build.

`out\harness.exe --bench [width height]` does not write PNGs: it draws the fake game scene (optionally with the menu
blur) and every menu page 150 times, waits for the GPU after each frame and prints the frame time (average, median,
95th percentile, slowest) plus how many text layouts, gradients, path geometries and layers the UI created per frame.
