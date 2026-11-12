#pragma once
// "Now playing" reader: what Spotify (or, optionally, any media player) is playing right now.
//
// The source is the Windows media transport controls (the same data the volume flyout and the lock screen show):
// title, artist, album, artwork, play state and the timeline (duration + position + the time that position was
// reported at). The position the HUD shows is that report plus the time that has passed since, so it stays accurate
// between updates and after seeks. If the Windows API is not reachable the title of the Spotify window is used instead
// (title / artist only; the position is then an estimate counted from the moment the title changed).
//
// Everything runs on its own thread; snapshot() / copyArt() are cheap and may be called from the render thread.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nowplaying {

    struct Snapshot {
        bool available = false;   // a player with a track was found
        bool playing = false;
        bool approximate = false; // position is an estimate (window title fallback)
        std::wstring source;      // "Spotify" / app name
        std::wstring title, artist, album;
        int64_t durationMs = 0;   // 0 = unknown
        double positionMs = 0;    // position right now
        uint64_t trackSerial = 0; // changes whenever the track changes
        uint64_t artSerial = 0;   // changes whenever the artwork of the track changes (0 = none yet)
        uint8_t artRgb[3] = { 90, 90, 100 }; // average artwork colour
    };

    /// Artwork as premultiplied BGRA pixels, size x size.
    struct Art {
        int size = 0;
        std::vector<uint8_t> bgra;
    };

    class Reader {
    public:
        static Reader& get();

        /// The thread runs while at least one user holds a reference.
        void acquire();
        void release();
        /// Stops the thread and waits for it (module destruction / eject only: it can take a moment).
        void shutdown();

        /// false (default): only Spotify. true: the current session of whatever player is active.
        void setAnyPlayer(bool any) { anyPlayer.store(any, std::memory_order_relaxed); }

        [[nodiscard]] Snapshot snapshot();
        /// Copies the artwork when it is newer than `serial` (which is updated). Returns true if `out` was filled.
        bool copyArt(Art& out, uint64_t& serial);

    private:
        Reader() = default;
        void run();

        std::mutex m;
        Snapshot snap;
        std::chrono::steady_clock::time_point observed {}; // when snap.positionMs was valid
        Art art;
        uint64_t artVersion = 0;

        std::mutex lifeMutex;
        std::thread worker;
        int users = 0;
        std::atomic<bool> stop { false };
        std::atomic<bool> running { false };
        std::atomic<bool> anyPlayer { false };
        double rate = 1.0; // playback rate of the current session (guarded by m)
    };

} // namespace nowplaying
