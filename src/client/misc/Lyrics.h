#pragma once
// Synced lyrics for the track that is playing, from LRCLIB (https://lrclib.net): a free, open lyrics database that
// needs no account. The lookup (title / artist / album / length of the track) runs on its own thread and never on the
// render thread; the result is a list of timestamped lines the HUD follows with the playback position.
//
// This is the only thing the Spotify HUD sends over the network, and only while "Show lyrics" is on.

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lyrics {

    struct Line {
        int64_t timeMs = 0;
        std::wstring text; // may be empty (an instrumental gap)
    };

    struct Result {
        enum class State {
            Idle,
            Loading,
            Ready,        // synced lines available
            NotFound,
            Instrumental,
            Unsynced,     // only plain text exists: not shown, there is nothing to follow
            Failed        // network error
        };
        State state = State::Idle;
        std::vector<Line> lines; // sorted by time
        uint64_t serial = 0;     // track serial this result belongs to
    };

    /// "[01:23.45] text" lines (several time tags per line allowed, metadata tags ignored).
    std::vector<Line> parseLrc(std::string const& lrc);
    /// Index of the line that is current at `positionMs` (-1 before the first line).
    int indexAt(std::vector<Line> const& lines, int64_t positionMs);
    /// "Song (feat. X) - Remastered 2011" -> "Song"
    std::wstring cleanTitle(std::wstring title);
    /// "A, B & C" -> "A"
    std::wstring primaryArtist(std::wstring const& artist);

    /// Network access is injected so that the logic can be tested without the game (the HUD passes the client's HTTP
    /// helper). Returns false on a transport error; `status` is the HTTP status.
    using Fetch = std::function<bool(std::wstring const& url, std::string& body, int& status)>;

    class Provider {
    public:
        static Provider& get();

        void setFetch(Fetch fetch);
        /// Starts a lookup for a new track (ignored when `serial` was already requested).
        void request(uint64_t serial, std::wstring const& title, std::wstring const& artist, std::wstring const& album,
                     int64_t durationMs);
        /// Forgets the pending request (the module was switched off).
        void cancel();
        [[nodiscard]] std::shared_ptr<const Result> snapshot();
        /// Waits for the thread (module destruction / eject).
        void shutdown();

    private:
        Provider() = default;
        void run();
        std::shared_ptr<Result> lookup(uint64_t serial, std::wstring title, std::wstring artist, std::wstring album,
                                       int64_t durationMs);

        struct Pending {
            uint64_t serial = 0;
            std::wstring title, artist, album;
            int64_t durationMs = 0;
        };

        std::mutex m;
        std::condition_variable cv;
        std::thread worker;
        bool stop = false;
        bool hasPending = false;
        Pending pending;
        uint64_t requestedSerial = 0;
        std::shared_ptr<const Result> current = std::make_shared<Result>();
        Fetch fetch;
    };

} // namespace lyrics
