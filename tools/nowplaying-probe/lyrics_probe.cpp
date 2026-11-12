// Looks lyrics up the way the HUD does and prints the result. Not part of the CMake build.
//   lyrics_probe.exe "Title" "Artist" "Album" durationSeconds [positionSeconds]
#include "client/misc/Lyrics.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <thread>

static bool httpGet(std::wstring const& url, std::string& body, int& status) {
    URL_COMPONENTS u {};
    u.dwStructSize = sizeof(u);
    u.dwSchemeLength = u.dwHostNameLength = u.dwUrlPathLength = u.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u)) return false;
    HINTERNET s = WinHttpOpen(L"ma4 probe/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!s) return false;
    std::wstring host(u.lpszHostName, u.dwHostNameLength), path(u.lpszUrlPath, u.dwUrlPathLength);
    path.append(u.lpszExtraInfo, u.dwExtraInfoLength);
    HINTERNET c = WinHttpConnect(s, host.c_str(), u.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE) : nullptr;
    bool ok = r && WinHttpSendRequest(r, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr);
    if (ok) {
        DWORD code = 0, sz = sizeof(code);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &sz, nullptr);
        status = static_cast<int>(code);
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(r, &avail) && avail) {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            WinHttpReadData(r, chunk.data(), avail, &read);
            body.append(chunk.data(), read);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 5) {
        std::wprintf(L"usage: lyrics_probe Title Artist Album DurationSeconds [PositionSeconds]\n");
        return 2;
    }
    // self test of the parser first
    {
        auto lines = lyrics::parseLrc("[ar:Someone]\n[00:12.50] first\n[00:05.00][01:00.00]chorus\n[01:30.1]\n");
        std::printf("parse: %zu lines, idx(0s)=%d idx(5.0s)=%d idx(12.6s)=%d idx(61s)=%d idx(95s)=%d\n", lines.size(),
                    lyrics::indexAt(lines, 0), lyrics::indexAt(lines, 5000), lyrics::indexAt(lines, 12600),
                    lyrics::indexAt(lines, 61000), lyrics::indexAt(lines, 95000));
        std::wprintf(L"clean: '%s' / '%s'\n", lyrics::cleanTitle(L"Song Name (feat. X) - Remastered 2011").c_str(),
                     lyrics::primaryArtist(L"Main Artist, Guest & Other").c_str());
    }
    auto& p = lyrics::Provider::get();
    p.setFetch(httpGet);
    p.request(1, argv[1], argv[2], argv[3], static_cast<int64_t>(_wtof(argv[4]) * 1000.0));
    for (int i = 0; i < 60; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto r = p.snapshot();
        if (r->state == lyrics::Result::State::Loading) continue;
        std::printf("state=%d lines=%zu\n", static_cast<int>(r->state), r->lines.size());
        double pos = argc > 5 ? _wtof(argv[5]) : 30.0;
        int idx = lyrics::indexAt(r->lines, static_cast<int64_t>(pos * 1000.0));
        for (int k = std::max(0, idx - 1); k <= idx + 1 && k < static_cast<int>(r->lines.size()); ++k) {
            std::printf("%s [%7.2fs] ", k == idx ? ">>" : "  ", r->lines[k].timeMs / 1000.0);
            std::wprintf(L"%s\n", r->lines[k].text.c_str());
        }
        break;
    }
    p.shutdown();
    return 0;
}
