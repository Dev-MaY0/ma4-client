// Prints what the "now playing" reader sees (and, with --lyrics, the lyrics lookup). Not part of the CMake build.
#include "client/misc/NowPlaying.h"

#include <cstdio>
#include <cstdlib>
#include <thread>

int wmain(int argc, wchar_t** argv) {
    using namespace nowplaying;
    int seconds = argc > 1 ? _wtoi(argv[1]) : 8;
    bool any = argc > 2 && std::wstring(argv[2]) == L"--any";
    Reader::get().setAnyPlayer(any);
    Reader::get().acquire();
    uint64_t artSerial = 0;
    for (int i = 0; i < seconds * 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        Snapshot s = Reader::get().snapshot();
        Art art;
        bool gotArt = Reader::get().copyArt(art, artSerial);
        if (!s.available) {
            std::wprintf(L"[%2d] nothing playing\n", i);
            continue;
        }
        std::wprintf(L"[%2d] %s | %s - %s | album '%s' | %s %.1fs / %.1fs%s | art serial %llu%s rgb(%d,%d,%d)\n", i, s.source.c_str(),
                     s.artist.c_str(), s.title.c_str(), s.album.c_str(), s.playing ? L"playing" : L"paused", s.positionMs / 1000.0,
                     s.durationMs / 1000.0, s.approximate ? L" (approx)" : L"", static_cast<unsigned long long>(s.artSerial),
                     gotArt ? L" NEW" : L"", s.artRgb[0], s.artRgb[1], s.artRgb[2]);
    }
    Reader::get().shutdown();
    return 0;
}
