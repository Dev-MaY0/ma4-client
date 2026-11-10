@echo off
rem Builds the offscreen UI harness for the iOS style toolkit (src/client/screen/ios).
rem Run it from an "x64 Native Tools Command Prompt for VS" so that cl.exe is available.
rem   build.bat            -> builds out\harness.exe
rem   out\harness.exe <output-dir> [path\to\logo.png]   -> renders PNG screenshots of every screen
set SRC=%~dp0..\..\src
if not exist "%~dp0out" mkdir "%~dp0out"
cd /d "%~dp0out"
cl /nologo /EHsc /std:c++latest /utf-8 /O2 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I "%SRC%" "%~dp0harness.cpp" "%SRC%\client\screen\ios\IosUi.cpp" "%SRC%\client\screen\ios\IosShell.cpp" "%SRC%\client\screen\ios\IosShellReplay.cpp" /Fe:harness.exe /link d3d11.lib d2d1.lib dwrite.lib dxgi.lib windowscodecs.lib ole32.lib user32.lib dxguid.lib
