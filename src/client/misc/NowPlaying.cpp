#include "NowPlaying.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwctype>

#ifdef min
#undef min
#undef max
#endif

namespace wmc = winrt::Windows::Media::Control;
using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

namespace nowplaying {

    namespace {
        std::wstring lowered(std::wstring s) {
            for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
            return s;
        }

        std::wstring toWide(winrt::hstring const& h) {
            return std::wstring(h.c_str(), h.size());
        }

        uint64_t fnv(std::vector<uint8_t> const& bytes) {
            uint64_t h = 1469598103934665603ull;
            for (uint8_t b : bytes) {
                h ^= b;
                h *= 1099511628211ull;
            }
            return h;
        }

        struct Decoded {
            Art art;
            uint8_t rgb[3] = { 90, 90, 100 };
            bool ok = false;
        };

        // Any image format Windows can decode -> a centre-cropped 128x128 premultiplied BGRA picture + average colour.
        Decoded decodeArt(std::vector<uint8_t> const& bytes) {
            Decoded d;
            ComPtr<IWICImagingFactory> wic;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf())))) return d;
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapDecoder> dec;
            ComPtr<IWICBitmapFrameDecode> frame;
            if (FAILED(wic->CreateStream(stream.GetAddressOf())) ||
                FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
                FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, dec.GetAddressOf())) ||
                FAILED(dec->GetFrame(0, frame.GetAddressOf()))) {
                return d;
            }
            UINT w = 0, h = 0;
            frame->GetSize(&w, &h);
            if (!w || !h) return d;

            ComPtr<IWICBitmapSource> source = frame;
            if (w != h) { // album art is square: crop the middle of anything else
                const UINT side = std::min(w, h);
                WICRect rc = { static_cast<INT>((w - side) / 2), static_cast<INT>((h - side) / 2), static_cast<INT>(side), static_cast<INT>(side) };
                ComPtr<IWICBitmapClipper> clipper;
                if (SUCCEEDED(wic->CreateBitmapClipper(clipper.GetAddressOf())) && SUCCEEDED(clipper->Initialize(frame.Get(), &rc))) {
                    source = clipper;
                    w = h = side;
                }
            }
            constexpr UINT kSize = 128;
            ComPtr<IWICBitmapScaler> scaler;
            ComPtr<IWICFormatConverter> conv;
            if (FAILED(wic->CreateBitmapScaler(scaler.GetAddressOf())) ||
                FAILED(scaler->Initialize(source.Get(), kSize, kSize, WICBitmapInterpolationModeFant)) ||
                FAILED(wic->CreateFormatConverter(conv.GetAddressOf())) ||
                FAILED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom))) {
                return d;
            }
            d.art.size = static_cast<int>(kSize);
            d.art.bgra.resize(static_cast<size_t>(kSize) * kSize * 4);
            if (FAILED(conv->CopyPixels(nullptr, kSize * 4, static_cast<UINT>(d.art.bgra.size()), d.art.bgra.data()))) return d;

            uint64_t r = 0, g = 0, b = 0;
            const size_t n = static_cast<size_t>(kSize) * kSize;
            for (size_t i = 0; i < n; ++i) {
                b += d.art.bgra[i * 4];
                g += d.art.bgra[i * 4 + 1];
                r += d.art.bgra[i * 4 + 2];
            }
            d.rgb[0] = static_cast<uint8_t>(r / n);
            d.rgb[1] = static_cast<uint8_t>(g / n);
            d.rgb[2] = static_cast<uint8_t>(b / n);
            d.ok = true;
            return d;
        }

        // ---- fallback: the title of the Spotify window ("Artist - Title" while playing, "Spotify ..." otherwise)
        struct WindowScan {
            std::wstring title;
            bool found = false;
        };

        bool isSpotifyProcess(DWORD pid) {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) return false;
            wchar_t path[MAX_PATH * 2] = {};
            DWORD len = static_cast<DWORD>(std::size(path));
            bool ok = false;
            if (QueryFullProcessImageNameW(h, 0, path, &len)) {
                std::wstring p = lowered(std::wstring(path, len));
                ok = p.size() >= 11 && p.compare(p.size() - 11, 11, L"spotify.exe") == 0;
            }
            CloseHandle(h);
            return ok;
        }

        BOOL CALLBACK scanWindow(HWND hwnd, LPARAM lp) {
            auto* scan = reinterpret_cast<WindowScan*>(lp);
            wchar_t title[512] = {};
            int n = GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
            if (n <= 0) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (!pid || !isSpotifyProcess(pid)) return TRUE;
            std::wstring t(title, static_cast<size_t>(n));
            // the main window's title is the song; helper windows ("GDI+ Window", "Default IME", ...) are ignored
            if (t.rfind(L"Spotify", 0) == 0) { // "Spotify", "Spotify Free", "Spotify Premium": nothing is playing
                if (!scan->found) {
                    scan->title = t;
                    scan->found = true;
                }
                return TRUE;
            }
            if (t.find(L" - ") != std::wstring::npos) {
                scan->title = t;
                scan->found = true;
                return FALSE;
            }
            return TRUE;
        }
    } // namespace

    Reader& Reader::get() {
        // leaked on purpose: the thread may still be winding down when static destructors run on unload
        static Reader* instance = new Reader();
        return *instance;
    }

    void Reader::acquire() {
        std::lock_guard<std::mutex> lock(lifeMutex);
        ++users;
        stop = false;
        if (worker.joinable() && !running.load()) worker.join(); // an earlier run finished
        if (!worker.joinable()) {
            running = true;
            worker = std::thread([this] { run(); });
        }
    }

    void Reader::release() {
        std::lock_guard<std::mutex> lock(lifeMutex);
        if (users > 0 && --users == 0) stop = true; // the thread notices within a fraction of a second; never block here
    }

    void Reader::shutdown() {
        {
            std::lock_guard<std::mutex> lock(lifeMutex);
            users = 0;
            stop = true;
        }
        if (worker.joinable()) worker.join();
    }

    Snapshot Reader::snapshot() {
        std::lock_guard<std::mutex> lock(m);
        Snapshot s = snap;
        if (s.available && s.playing) {
            const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - observed).count();
            s.positionMs += std::clamp(elapsed, 0.0, 6.0 * 3600.0 * 1000.0) * rate;
        }
        if (s.durationMs > 0) s.positionMs = std::clamp(s.positionMs, 0.0, static_cast<double>(s.durationMs));
        return s;
    }

    bool Reader::copyArt(Art& out, uint64_t& serial) {
        std::lock_guard<std::mutex> lock(m);
        if (artVersion == serial || art.bgra.empty()) return false;
        out = art;
        serial = artVersion;
        return true;
    }

    // ================================================================================================================
    void Reader::run() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
            // already initialised on this thread in another mode: carry on, WinRT still works
        }

        wmc::GlobalSystemMediaTransportControlsSessionManager mgr { nullptr };
        winrt::Windows::Storage::Streams::IRandomAccessStreamReference thumbnail { nullptr };

        std::wstring sessionId;
        std::wstring trackKey;
        std::wstring title, artist, album;
        uint64_t trackSerial = 0;
        uint64_t artSerialOfTrack = 0;
        uint64_t artHash = 0, previousTrackArtHash = 0;
        int artTries = 0;
        auto lastProps = std::chrono::steady_clock::time_point {};
        auto lastArt = std::chrono::steady_clock::time_point {};
        auto trackStart = std::chrono::steady_clock::now();
        uint8_t artRgb[3] = { 90, 90, 100 };
        std::wstring lastWindowTitle;

        for (;;) {
            while (!stop.load()) {
                const auto now = std::chrono::steady_clock::now();
                Snapshot next;
                double nextRate = 1.0;
                bool have = false;

                // ---- 1. Windows media transport controls
                try {
                    if (!mgr) mgr = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

                    wmc::GlobalSystemMediaTransportControlsSession chosen { nullptr };
                    for (auto const& s : mgr.GetSessions()) {
                        const std::wstring id = lowered(toWide(s.SourceAppUserModelId()));
                        if (id.find(L"spotify") == std::wstring::npos) continue;
                        if (!chosen) {
                            chosen = s;
                        } else if (s.GetPlaybackInfo().PlaybackStatus() ==
                                   wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                            chosen = s; // prefer the one that is actually playing
                        }
                    }
                    if (!chosen && anyPlayer.load(std::memory_order_relaxed)) chosen = mgr.GetCurrentSession();

                    if (chosen) {
                        const std::wstring id = toWide(chosen.SourceAppUserModelId());
                        const bool sessionChanged = id != sessionId;
                        sessionId = id;

                        // title / artist / album / artwork: re-read about once a second (and right after a change of session)
                        if (sessionChanged || now - lastProps > 700ms) {
                            lastProps = now;
                            auto props = chosen.TryGetMediaPropertiesAsync().get();
                            title = toWide(props.Title());
                            artist = toWide(props.Artist());
                            album = toWide(props.AlbumTitle());
                            thumbnail = props.Thumbnail();
                        }

                        const std::wstring key = title + L"\x1f" + artist + L"\x1f" + album;
                        if (key != trackKey) {
                            trackKey = key;
                            ++trackSerial;
                            artSerialOfTrack = 0;
                            previousTrackArtHash = artHash;
                            artTries = 0;
                            lastArt = {};
                            trackStart = now;
                        }

                        auto info = chosen.GetPlaybackInfo();
                        next.playing = info.PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                        if (auto r = info.PlaybackRate()) {
                            const double v = r.Value();
                            if (std::isfinite(v) && v > 0.05 && v < 8.0) nextRate = v;
                        }

                        auto tl = chosen.GetTimelineProperties();
                        auto toMs = [](winrt::Windows::Foundation::TimeSpan t) {
                            return std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
                        };
                        const int64_t startMs = toMs(tl.StartTime());
                        const int64_t endMs = toMs(tl.EndTime());
                        next.durationMs = endMs > startMs ? endMs - startMs : 0;
                        double pos = static_cast<double>(toMs(tl.Position()) - startMs);
                        if (next.playing) {
                            // the position was reported at LastUpdatedTime: add what has passed since
                            const auto updated = tl.LastUpdatedTime();
                            if (updated.time_since_epoch().count() > 0) {
                                const double elapsed =
                                    std::chrono::duration<double, std::milli>(winrt::clock::now() - updated).count();
                                if (elapsed > 0.0 && elapsed < 6.0 * 3600.0 * 1000.0) pos += elapsed * nextRate;
                            }
                        }
                        next.positionMs = std::max(0.0, pos);

                        std::wstring source = lowered(id).find(L"spotify") != std::wstring::npos ? L"Spotify" : id;
                        if (source.size() > 4 && source.compare(source.size() - 4, 4, L".exe") == 0) source.resize(source.size() - 4);
                        next.source = source;
                        next.title = title;
                        next.artist = artist;
                        next.album = album;
                        next.trackSerial = trackSerial;
                        next.available = !(title.empty() && artist.empty());
                        have = next.available;

                        // artwork: fetched once per track (the picture of the previous track can still be there for a
                        // moment right after a change, so a picture identical to the previous one is retried)
                        const bool wantArt = thumbnail && artTries < 14 && now - lastArt > 900ms &&
                                             (artSerialOfTrack == 0 || now - trackStart < 12s);
                        if (have && wantArt) {
                            lastArt = now;
                            ++artTries;
                            try {
                                auto stream = thumbnail.OpenReadAsync().get();
                                const uint64_t size = stream.Size();
                                if (size > 0 && size < 12ull * 1024ull * 1024ull) {
                                    winrt::Windows::Storage::Streams::Buffer buffer(static_cast<uint32_t>(size));
                                    stream.ReadAsync(buffer, static_cast<uint32_t>(size),
                                                     winrt::Windows::Storage::Streams::InputStreamOptions::None)
                                        .get();
                                    std::vector<uint8_t> bytes(buffer.Length());
                                    auto reader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
                                    reader.ReadBytes(winrt::array_view<uint8_t>(bytes));
                                    const uint64_t h = fnv(bytes);
                                    const bool stale = h == previousTrackArtHash && artTries < 5;
                                    if (!stale && (artSerialOfTrack == 0 || h != artHash)) {
                                        Decoded d = decodeArt(bytes);
                                        if (d.ok) {
                                            artHash = h;
                                            std::lock_guard<std::mutex> lock(m);
                                            art = std::move(d.art);
                                            ++artVersion;
                                            artSerialOfTrack = artVersion;
                                            std::copy(std::begin(d.rgb), std::end(d.rgb), artRgb);
                                        }
                                    }
                                }
                            } catch (...) {
                            }
                        }
                        next.artSerial = artSerialOfTrack;
                        std::copy(std::begin(artRgb), std::end(artRgb), next.artRgb);
                    }
                } catch (winrt::hresult_error const&) {
                    mgr = nullptr; // asked again next round (the service restarts, the player closes, ...)
                } catch (...) {
                    mgr = nullptr;
                }

                // ---- 2. fallback: the window title of Spotify
                if (!have) {
                    WindowScan scan;
                    EnumWindows(scanWindow, reinterpret_cast<LPARAM>(&scan));
                    if (scan.found) {
                        const std::wstring& t = scan.title;
                        const size_t dash = t.find(L" - ");
                        if (t.rfind(L"Spotify", 0) != 0 && dash != std::wstring::npos) {
                            next.available = true;
                            next.playing = true;
                            next.approximate = true;
                            next.source = L"Spotify";
                            next.artist = t.substr(0, dash);
                            next.title = t.substr(dash + 3);
                            if (t != lastWindowTitle) {
                                lastWindowTitle = t;
                                ++trackSerial;
                                trackStart = now;
                                artSerialOfTrack = 0;
                            }
                            next.trackSerial = trackSerial;
                            next.positionMs = std::chrono::duration<double, std::milli>(now - trackStart).count();
                            have = true;
                        }
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(m);
                    if (have) {
                        snap = next;
                        observed = std::chrono::steady_clock::now();
                        rate = nextRate;
                    } else {
                        snap = Snapshot {};
                    }
                }

                for (int i = 0; i < 5 && !stop.load(); ++i) std::this_thread::sleep_for(50ms);
            }

            // leave only when nobody asked for us again in the meantime
            std::lock_guard<std::mutex> lock(lifeMutex);
            if (users == 0) {
                running = false;
                break;
            }
            stop = false;
        }

        {
            std::lock_guard<std::mutex> lock(m);
            snap = Snapshot {};
        }
        try {
            winrt::uninit_apartment();
        } catch (...) {
        }
    }

} // namespace nowplaying
