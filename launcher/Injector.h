#pragma once
// ma4 Launcher - everything that touches Minecraft: finding / starting the game and loading ma4.dll into it.
// Plain Win32, no Direct2D, so it can also be driven from the command line (`Ma4Launcher.exe --inject <pid> <dll>`).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <string>

namespace inject {

    /// Process id of Minecraft.Windows.exe, or 0 when the game is not running.
    DWORD findMinecraft();

    /// True when a module with this file name (e.g. "ma4.dll") is loaded in the process.
    bool isModuleLoaded(DWORD pid, std::wstring const& fileName);

    /// True once the game has started its renderer (Direct3D loaded), i.e. it is safe to load the client.
    bool isRendererReady(DWORD pid);

    /// Starts Minecraft through its package activation (the same thing as the start menu entry).
    bool launchMinecraft(DWORD& pid, std::wstring& error);

    /// Lets "ALL APPLICATION PACKAGES" read and execute the file: Minecraft runs in an AppContainer sandbox and cannot
    /// load a DLL it has no access to.
    bool grantAppPackageAccess(std::wstring const& path, std::wstring& error);

    /// LoadLibraryW in the target process (CreateRemoteThread). Returns true when the module is loaded afterwards.
    bool injectDll(DWORD pid, std::wstring const& dllPath, std::wstring& error);

    /// The ma4.dll to load: `configured` if it exists, else next to the launcher, else the data folder. Empty if none.
    std::wstring resolveDll(std::wstring const& configured);

    /// "%LocalAppData%\ma4" (created on demand).
    std::wstring dataDirectory();

    std::wstring fileNameOf(std::wstring const& path);

} // namespace inject
