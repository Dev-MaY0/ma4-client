#include "Lyrics.h"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwctype>

namespace lyrics {

    namespace {
        std::string toUtf8(std::wstring const& w) {
            if (w.empty()) return {};
            int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
            std::string s(static_cast<size_t>(std::max(0, n)), '\0');
            if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
            return s;
        }

        std::wstring fromUtf8(std::string const& s) {
            if (s.empty()) return {};
            int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(std::max(0, n)), L'\0');
            if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        std::wstring urlEncode(std::wstring const& text) {
            static const char* hex = "0123456789ABCDEF";
            std::wstring out;
            for (unsigned char c : toUtf8(text)) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                    c == '~') {
                    out.push_back(static_cast<wchar_t>(c));
                } else {
                    out.push_back(L'%');
                    out.push_back(static_cast<wchar_t>(hex[c >> 4]));
                    out.push_back(static_cast<wchar_t>(hex[c & 15]));
                }
            }
            return out;
        }

        std::wstring trim(std::wstring s) {
            size_t a = 0, b = s.size();
            while (a < b && std::iswspace(s[a])) ++a;
            while (b > a && std::iswspace(s[b - 1])) --b;
            return s.substr(a, b - a);
        }

        constexpr wchar_t kBase[] = L"https://lrclib.net/api/";
    } // namespace

    // ---------------------------------------------------------------------------------------------- parsing
    std::vector<Line> parseLrc(std::string const& lrc) {
        std::vector<Line> out;
        size_t pos = 0;
        while (pos <= lrc.size()) {
            size_t end = lrc.find('\n', pos);
            if (end == std::string::npos) end = lrc.size();
            std::string line = lrc.substr(pos, end - pos);
            pos = end + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // leading [mm:ss.xx] tags
            std::vector<int64_t> times;
            size_t i = 0;
            while (i < line.size() && line[i] == '[') {
                size_t close = line.find(']', i);
                if (close == std::string::npos) break;
                std::string tag = line.substr(i + 1, close - i - 1);
                size_t colon = tag.find(':');
                bool numeric = colon != std::string::npos && colon > 0;
                for (size_t k = 0; numeric && k < tag.size(); ++k) {
                    if (k != colon && tag[k] != '.' && !(tag[k] >= '0' && tag[k] <= '9')) numeric = false;
                }
                if (!numeric) { // [ar:Artist], [ti:Title], [offset:..]: not a time tag
                    i = close + 1;
                    if (!times.empty()) break;
                    continue;
                }
                const double minutes = std::atof(tag.substr(0, colon).c_str());
                const double seconds = std::atof(tag.substr(colon + 1).c_str());
                times.push_back(static_cast<int64_t>(std::llround((minutes * 60.0 + seconds) * 1000.0)));
                i = close + 1;
            }
            if (times.empty()) continue;
            std::string raw = line.substr(i);
            // "enhanced" LRC carries a time tag in front of every word: <00:53.89>Never <00:54.21>gonna ...
            for (size_t a = raw.find('<'); a != std::string::npos; a = raw.find('<', a)) {
                size_t b = raw.find('>', a);
                bool timeTag = b != std::string::npos && b > a + 1 && raw.find(':', a) < b;
                for (size_t k = a + 1; timeTag && k < b; ++k) {
                    if (raw[k] != ':' && raw[k] != '.' && !(raw[k] >= '0' && raw[k] <= '9')) timeTag = false;
                }
                if (timeTag) {
                    raw.erase(a, b - a + 1);
                } else {
                    ++a;
                }
            }
            std::wstring text = trim(fromUtf8(raw));
            for (int64_t t : times) out.push_back({ t, text });
        }
        std::stable_sort(out.begin(), out.end(), [](Line const& a, Line const& b) { return a.timeMs < b.timeMs; });
        return out;
    }

    int indexAt(std::vector<Line> const& lines, int64_t positionMs) {
        // last line whose time is <= position
        auto it = std::upper_bound(lines.begin(), lines.end(), positionMs, [](int64_t p, Line const& l) { return p < l.timeMs; });
        return static_cast<int>(it - lines.begin()) - 1;
    }

    std::wstring cleanTitle(std::wstring t) {
        // " - Remastered 2011", " - Live at ...", " - Radio Edit": everything after a spaced dash
        if (size_t p = t.find(L" - "); p != std::wstring::npos && p > 0) t.resize(p);
        // "(feat. X)", "[Deluxe]", full width brackets
        std::wstring out;
        int depth = 0;
        for (wchar_t c : t) {
            if (c == L'(' || c == L'[' || c == 0xFF08 || c == 0x3010) {
                ++depth;
            } else if (c == L')' || c == L']' || c == 0xFF09 || c == 0x3011) {
                if (depth > 0) --depth;
            } else if (depth == 0) {
                out.push_back(c);
            }
        }
        out = trim(out);
        return out.empty() ? trim(t) : out;
    }

    std::wstring primaryArtist(std::wstring const& artist) {
        std::wstring a = artist;
        for (wchar_t const* sep : { L", ", L" & ", L"; ", L" feat. ", L" feat ", L" ft. ", L" x ", L"\u3001" }) {
            if (size_t p = a.find(sep); p != std::wstring::npos && p > 0) a.resize(p);
        }
        return trim(a);
    }

    // ---------------------------------------------------------------------------------------------- provider
    Provider& Provider::get() {
        static Provider* instance = new Provider(); // leaked on purpose (thread may outlive static destruction)
        return *instance;
    }

    void Provider::setFetch(Fetch f) {
        std::lock_guard<std::mutex> lock(m);
        fetch = std::move(f);
    }

    void Provider::request(uint64_t serial, std::wstring const& title, std::wstring const& artist, std::wstring const& album,
                           int64_t durationMs) {
        std::lock_guard<std::mutex> lock(m);
        if (serial == requestedSerial && serial != 0) return;
        requestedSerial = serial;
        pending = { serial, title, artist, album, durationMs };
        hasPending = true;
        auto loading = std::make_shared<Result>();
        loading->state = Result::State::Loading;
        loading->serial = serial;
        current = loading;
        if (!worker.joinable()) worker = std::thread([this] { run(); });
        cv.notify_all();
    }

    void Provider::cancel() {
        std::lock_guard<std::mutex> lock(m);
        hasPending = false;
        requestedSerial = 0;
        current = std::make_shared<Result>();
    }

    std::shared_ptr<const Result> Provider::snapshot() {
        std::lock_guard<std::mutex> lock(m);
        return current;
    }

    void Provider::shutdown() {
        {
            std::lock_guard<std::mutex> lock(m);
            stop = true;
            hasPending = false;
        }
        cv.notify_all();
        if (worker.joinable()) worker.join();
    }

    void Provider::run() {
        for (;;) {
            Pending job;
            {
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, [this] { return stop || hasPending; });
                if (stop) return;
                job = pending;
                hasPending = false;
            }
            auto result = lookup(job.serial, job.title, job.artist, job.album, job.durationMs);
            std::lock_guard<std::mutex> lock(m);
            // the track may have changed while the request was running: only the latest one is kept
            if (requestedSerial == job.serial) current = result;
        }
    }

    std::shared_ptr<Result> Provider::lookup(uint64_t serial, std::wstring title, std::wstring artist, std::wstring album,
                                             int64_t durationMs) {
        auto result = std::make_shared<Result>();
        result->serial = serial;
        result->state = Result::State::NotFound;

        Fetch fetchFn;
        {
            std::lock_guard<std::mutex> lock(m);
            fetchFn = fetch;
        }
        if (!fetchFn) {
            result->state = Result::State::Failed;
            return result;
        }

        const int durationSec = static_cast<int>(std::llround(static_cast<double>(durationMs) / 1000.0));
        bool networkError = false;

        struct Candidate {
            bool synced = false, plain = false, instrumental = false;
            std::string syncedText;
            double duration = 0.0;
        };
        auto readEntry = [](nlohmann::json const& j) {
            Candidate c;
            if (!j.is_object()) return c;
            if (j.contains("instrumental") && j["instrumental"].is_boolean()) c.instrumental = j["instrumental"].get<bool>();
            if (j.contains("syncedLyrics") && j["syncedLyrics"].is_string()) {
                c.syncedText = j["syncedLyrics"].get<std::string>();
                c.synced = !c.syncedText.empty();
            }
            if (j.contains("plainLyrics") && j["plainLyrics"].is_string()) c.plain = !j["plainLyrics"].get<std::string>().empty();
            if (j.contains("duration") && j["duration"].is_number()) c.duration = j["duration"].get<double>();
            return c;
        };
        auto call = [&](std::wstring const& url, nlohmann::json& out) {
            std::string body;
            int status = 0;
            if (!fetchFn(url, body, status)) {
                networkError = true;
                return false;
            }
            if (status != 200) return false;
            try {
                out = nlohmann::json::parse(body);
            } catch (nlohmann::json::exception const&) {
                return false;
            }
            return true;
        };
        auto finish = [&](Candidate const& c) {
            if (c.instrumental && !c.synced) {
                result->state = Result::State::Instrumental;
            } else if (c.synced) {
                result->lines = parseLrc(c.syncedText);
                result->state = result->lines.empty() ? Result::State::Unsynced : Result::State::Ready;
            } else if (c.plain) {
                result->state = Result::State::Unsynced;
            }
            return result;
        };

        const std::wstring firstArtist = primaryArtist(artist);
        const std::wstring cleaned = cleanTitle(title);

        auto getUrl = [&](std::wstring const& a, std::wstring const& t, bool withAlbum) {
            std::wstring url = std::wstring(kBase) + L"get?artist_name=" + urlEncode(a) + L"&track_name=" + urlEncode(t);
            if (withAlbum && !album.empty()) url += L"&album_name=" + urlEncode(album);
            if (withAlbum && durationSec > 0) url += L"&duration=" + std::to_wstring(durationSec);
            return url;
        };

        // 1-3: exact lookups, from the most to the least strict
        struct Try {
            std::wstring artist, title;
            bool withAlbum;
        };
        std::vector<Try> tries = { { artist, title, true } };
        if (firstArtist != artist) tries.push_back({ firstArtist, title, true });
        tries.push_back({ firstArtist, cleaned, true });
        tries.push_back({ firstArtist, cleaned, false });
        std::vector<std::wstring> seen;
        for (auto const& t : tries) {
            std::wstring url = getUrl(t.artist, t.title, t.withAlbum);
            if (std::find(seen.begin(), seen.end(), url) != seen.end()) continue;
            seen.push_back(url);
            nlohmann::json j;
            if (call(url, j)) {
                Candidate c = readEntry(j);
                if (c.synced || c.instrumental) return finish(c);
                if (c.plain && result->state == Result::State::NotFound) finish(c); // remember, keep looking for synced
            }
        }

        // 4: search, best length match with synced lyrics
        std::vector<std::wstring> searches = { std::wstring(kBase) + L"search?track_name=" + urlEncode(cleaned) + L"&artist_name=" +
                                                   urlEncode(firstArtist),
                                               std::wstring(kBase) + L"search?q=" + urlEncode(firstArtist + L" " + cleaned) };
        for (auto const& url : searches) {
            nlohmann::json j;
            if (!call(url, j) || !j.is_array()) continue;
            const Candidate* bestPtr = nullptr;
            Candidate best;
            double bestDiff = 1e18;
            for (auto const& entry : j) {
                Candidate c = readEntry(entry);
                if (!c.synced) continue;
                const double diff = durationSec > 0 && c.duration > 0 ? std::fabs(c.duration - durationSec) : 0.0;
                if (diff < bestDiff) {
                    bestDiff = diff;
                    best = c;
                    bestPtr = &best;
                }
            }
            // a hit that is more than a few seconds off is another recording of the song: its lines would drift
            if (bestPtr && (durationSec <= 0 || bestDiff <= 6.0)) return finish(best);
        }

        if (result->state == Result::State::NotFound && networkError) result->state = Result::State::Failed;
        return result;
    }

} // namespace lyrics
