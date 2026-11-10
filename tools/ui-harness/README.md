# UI harness

Renders every screen of the iOS style UI (module list, module detail, settings, popover, colour sheet, replay
recorder and viewer; dark / light / right-to-left) to PNG files **without starting Minecraft**. The harness links the
same `src/client/screen/ios/*` sources that the DLL uses; only the data (fake modules / settings / replay clip) and the
procedural background are fake.

    build.bat
    out\harness.exe <output-dir> ..\..\assets\logo.png

It is not part of the CMake build.
