@echo off
rem Builds the now-playing / lyrics probes (needs an "x64 Native Tools" prompt or vcvars64): build.bat -> out\probe.exe, out\lyrics_probe.exe
rem (the nlohmann json header comes from the CMake dependency cache: configure the project once first)
set SRC=%~dp0..\..\src
set JSON=%~dp0..\..\out\build\x64-release\_deps\json-src\single_include
if not exist "%~dp0out" mkdir "%~dp0out"
cd /d "%~dp0out"
cl /nologo /EHsc /std:c++20 /GR- /MT /D_HAS_STATIC_RTTI=0 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /W3 /I "%SRC%" /I "%JSON%" "%~dp0probe.cpp" "%SRC%\client\misc\NowPlaying.cpp" /Fe:probe.exe /link windowsapp.lib ole32.lib windowscodecs.lib user32.lib
cl /nologo /EHsc /std:c++20 /GR- /MT /D_HAS_STATIC_RTTI=0 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /W3 /I "%SRC%" /I "%JSON%" "%~dp0lyrics_probe.cpp" "%SRC%\client\misc\Lyrics.cpp" /Fe:lyrics_probe.exe /link winhttp.lib user32.lib
