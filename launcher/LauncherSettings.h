#pragma once
// ma4 Launcher - persistent settings (a small ini file in %LocalAppData%\ma4\Launcher).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "Injector.h"

struct LauncherSettings {
    std::wstring wallpaper;    // copy of the picture the user chose; empty = built-in wallpaper
    float dim = 0.18f;         // 0..0.7 darkening of the wallpaper
    float blur = 0.f;          // 0..40 px gaussian blur of the wallpaper
    int clock24 = -1;          // -1 = follow the system, 0 = 12 hour, 1 = 24 hour
    bool closeAfterLaunch = false;
    std::wstring dllPath;      // empty = look next to the launcher

    static std::wstring folder() {
        std::wstring dir = inject::dataDirectory() + L"\\Launcher";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    static std::wstring iniPath() { return folder() + L"\\launcher.ini"; }

    void load() {
        const std::wstring ini = iniPath();
        auto str = [&](wchar_t const* key, wchar_t const* def) {
            wchar_t buf[1024] = {};
            GetPrivateProfileStringW(L"Launcher", key, def, buf, static_cast<DWORD>(std::size(buf)), ini.c_str());
            return std::wstring(buf);
        };
        auto num = [&](wchar_t const* key, float def) {
            std::wstring v = str(key, L"");
            if (v.empty()) return def;
            try {
                return std::stof(v);
            } catch (...) {
                return def;
            }
        };
        wallpaper = str(L"wallpaper", L"");
        std::error_code ec;
        if (!wallpaper.empty() && !std::filesystem::exists(wallpaper, ec)) wallpaper.clear();
        dim = std::clamp(num(L"dim", 0.18f), 0.f, 0.7f);
        blur = std::clamp(num(L"blur", 0.f), 0.f, 40.f);
        clock24 = static_cast<int>(std::clamp(num(L"clock24", -1.f), -1.f, 1.f));
        closeAfterLaunch = num(L"closeAfterLaunch", 0.f) > 0.5f;
        dllPath = str(L"dll", L"");
    }

    void save() const {
        const std::wstring ini = iniPath();
        auto put = [&](wchar_t const* key, std::wstring const& v) { WritePrivateProfileStringW(L"Launcher", key, v.c_str(), ini.c_str()); };
        auto fl = [](float v) {
            wchar_t b[32];
            swprintf(b, 32, L"%.3f", v);
            return std::wstring(b);
        };
        put(L"wallpaper", wallpaper);
        put(L"dim", fl(dim));
        put(L"blur", fl(blur));
        put(L"clock24", std::to_wstring(clock24));
        put(L"closeAfterLaunch", closeAfterLaunch ? L"1" : L"0");
        put(L"dll", dllPath);
    }
};
