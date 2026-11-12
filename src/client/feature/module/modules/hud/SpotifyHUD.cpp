#include "pch.h"
#include "SpotifyHUD.h"

#include "client/Ma4.h"
#include "../../GlassPlate.h"
#include "client/event/events/RendererCleanupEvent.h"
#include "client/misc/Lyrics.h"
#include "client/screen/ios/IosBridge.h"
#include "client/screen/ios/IosUi.h"
#include "util/HttpClient.h"

#include <algorithm>
#include <cmath>

#ifdef min
#undef min
#undef max
#endif

namespace {
    constexpr float kPad = 12.f;
    constexpr float kArt = 64.f;
    constexpr float kTopH = 88.f;
    constexpr float kLyricH = 100.f;
    constexpr float kRadius = 22.f;

    std::wstring clockText(double ms) {
        int total = static_cast<int>(std::max(0.0, ms) / 1000.0);
        wchar_t buf[24];
        swprintf(buf, 24, L"%d:%02d", total / 60, total % 60);
        return buf;
    }

    float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    bool fetchLrclib(std::wstring const& url, std::string& body, int& status) {
        auto res = http::request(L"GET", url, {}, { { L"Lrclib-Client", L"ma4-client (https://github.com/Dev-MaY0/ma4-client)" } });
        if (!res) return false;
        status = static_cast<int>(res->status);
        body.assign(res->body.begin(), res->body.end());
        return true;
    }
} // namespace

struct SpotifyHUD::View {
    nowplaying::Snapshot snap;
    std::shared_ptr<const lyrics::Result> lyr;
    bool demo = false;
    bool wantLyrics = true;
    bool wantArt = true;
    bool wantProgress = true;
    float width = 320.f;
    float height = 0.f;
    int64_t lyricPosMs = 0;
};

SpotifyHUD::SpotifyHUD()
    : HUDModule("SpotifyHUD", LocalizeString::get("client.hudmodule.spotify.name"),
                LocalizeString::get("client.hudmodule.spotify.desc"), HUD) {
    addSetting("showArt", LocalizeString::get("client.hudmodule.spotify.showArt.name"), L"", showArt);
    addSetting("showProgress", LocalizeString::get("client.hudmodule.spotify.showProgress.name"), L"", showProgress);
    addSetting("showLyrics", LocalizeString::get("client.hudmodule.spotify.showLyrics.name"),
               LocalizeString::get("client.hudmodule.spotify.showLyrics.desc"), showLyrics);
    addSliderSetting("lyricOffset", LocalizeString::get("client.hudmodule.spotify.lyricOffset.name"),
                     LocalizeString::get("client.hudmodule.spotify.lyricOffset.desc"), lyricOffset, FloatValue(-3000.f),
                     FloatValue(3000.f), FloatValue(50.f), "showLyrics"_istrue);
    addSetting("hideWhenIdle", LocalizeString::get("client.hudmodule.spotify.hideWhenIdle.name"),
               LocalizeString::get("client.hudmodule.spotify.hideWhenIdle.desc"), hideWhenIdle);
    addSetting("anyPlayer", LocalizeString::get("client.hudmodule.spotify.anyPlayer.name"),
               LocalizeString::get("client.hudmodule.spotify.anyPlayer.desc"), anyPlayer);
    addSliderSetting("cardWidth", LocalizeString::get("client.hudmodule.spotify.width.name"), L"", cardWidth, FloatValue(240.f),
                     FloatValue(480.f), FloatValue(5.f));

    listen<RendererCleanupEvent>(static_cast<EventListenerFunc>(&SpotifyHUD::onCleanup), true);
    lyrics::Provider::get().setFetch(fetchLrclib);
}

SpotifyHUD::~SpotifyHUD() {
    // the module manager destroys its modules while the client is ejected: both threads must be gone before the DLL is
    nowplaying::Reader::get().shutdown();
    lyrics::Provider::get().shutdown();
}

void SpotifyHUD::onEnable() {
    if (!active_) {
        active_ = true;
        nowplaying::Reader::get().acquire();
    }
}

void SpotifyHUD::onDisable() {
    if (active_) {
        active_ = false;
        nowplaying::Reader::get().release();
    }
    lyrics::Provider::get().cancel();
    requestedTrack = 0;
}

void SpotifyHUD::onCleanup(Event&) {
    artBrush.Reset();
    artBitmap.Reset();
    artSerial = 0;
}

void SpotifyHUD::updateArt(D2DUtil& dc, nowplaying::Snapshot const& snap) {
    if (!dc.ctx) return;
    if (snap.artSerial == 0) {
        // this track has no artwork (yet): do not keep showing the previous one
        if (artBitmap && artTrack != snap.trackSerial) {
            artBrush.Reset();
            artBitmap.Reset();
        }
        return;
    }
    nowplaying::Art art;
    uint64_t serial = artSerial;
    if (!nowplaying::Reader::get().copyArt(art, serial) || art.size <= 0) return;
    artSerial = serial;

    ComPtr<ID2D1Bitmap> bmp;
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    UINT32 size = static_cast<UINT32>(art.size);
    if (FAILED(dc.ctx->CreateBitmap(D2D1::SizeU(size, size), art.bgra.data(), size * 4, props, bmp.GetAddressOf()))) return;

    ComPtr<ID2D1BitmapBrush1> brush;
    D2D1_BITMAP_BRUSH_PROPERTIES1 bp = D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_INTERPOLATION_MODE_LINEAR);
    if (FAILED(dc.ctx->CreateBitmapBrush(bmp.Get(), &bp, nullptr, brush.GetAddressOf()))) return;

    artBitmap = bmp;
    artBrush = brush;
    artTrack = snap.trackSerial;
    std::copy(std::begin(snap.artRgb), std::end(snap.artRgb), artRgb);
}

void SpotifyHUD::render(DrawUtil& dc, bool isDefault, bool inEditor) {
    View v;
    v.demo = isDefault || inEditor;
    v.wantLyrics = std::get<BoolValue>(showLyrics).value;
    v.wantArt = std::get<BoolValue>(showArt).value;
    v.wantProgress = std::get<BoolValue>(showProgress).value;
    v.width = std::clamp(std::get<FloatValue>(cardWidth).value, 240.f, 480.f);

    auto& reader = nowplaying::Reader::get();
    reader.setAnyPlayer(std::get<BoolValue>(anyPlayer).value);

    if (v.demo) {
        // a sample card: placing the module in the editor / the menu preview should not depend on a running player
        v.snap.available = true;
        v.snap.playing = true;
        v.snap.title = L"Song Title";
        v.snap.artist = L"Artist Name";
        v.snap.durationMs = 215000;
        v.snap.positionMs = 83000 + static_cast<double>(GetTickCount64() % 20000);
        v.snap.source = L"Spotify";
        auto lyr = std::make_shared<lyrics::Result>();
        lyr->state = lyrics::Result::State::Ready;
        lyr->lines = { { 70000, L"Your lyrics are shown here" }, { 80000, L"in time with the song" }, { 90000, L"line by line" },
                       { 100000, L"as it plays" } };
        v.lyr = lyr;
        v.lyricPosMs = 72000 + static_cast<int64_t>(GetTickCount64() % 30000); // a new line every 10 s
    } else {
        v.snap = reader.snapshot();

        if (v.snap.available && v.wantLyrics) {
            // ask for the lyrics once the track length is known (a wrong length can match another recording)
            static uint64_t seenTrack = 0;
            static auto seenAt = std::chrono::steady_clock::now();
            if (v.snap.trackSerial != seenTrack) {
                seenTrack = v.snap.trackSerial;
                seenAt = std::chrono::steady_clock::now();
            }
            const bool lengthKnown = v.snap.durationMs > 0 || v.snap.approximate ||
                                     std::chrono::steady_clock::now() - seenAt > std::chrono::milliseconds(2500);
            if (lengthKnown && requestedTrack != v.snap.trackSerial) {
                requestedTrack = v.snap.trackSerial;
                lyrics::Provider::get().request(v.snap.trackSerial, v.snap.title, v.snap.artist, v.snap.album, v.snap.durationMs);
            }
            v.lyr = lyrics::Provider::get().snapshot();
            if (v.lyr && v.lyr->serial != v.snap.trackSerial) v.lyr.reset(); // result of the previous song
        }
        v.lyricPosMs = static_cast<int64_t>(v.snap.positionMs + std::get<FloatValue>(lyricOffset).value + 150.0);
    }

    if (!v.snap.available && std::get<BoolValue>(hideWhenIdle).value) {
        // nothing to show: collapse to nothing (the editor always shows the sample card)
        rect.right = rect.left + 1.f;
        rect.bottom = rect.top + 1.f;
        return;
    }

    v.height = kTopH + (v.wantLyrics ? kLyricH : 0.f);
    rect.right = rect.left + v.width;
    rect.bottom = rect.top + v.height;

    if (!dc.isMinecraft() && dc.renderType == DrawUtil::RenderType::D2D) {
        auto& d2 = static_cast<D2DUtil&>(dc);
        if (!v.demo) updateArt(d2, v.snap);
        drawIos(d2, v);
    } else {
        drawPlain(dc, v);
    }
}

void SpotifyHUD::drawIos(D2DUtil& dc, View const& v) {
    if (!dc.ctx) return;
    auto& ui = ios::Ui::get();

    // The menu preview draws this card inside the menu's own toolkit frame: join it instead of starting a new one
    // (a second beginFrame would wipe the clip stack the menu is in the middle of using).
    const bool nested = ui.inFrame();
    const float savedScale = ui.scale;
    const ios::Palette savedPal = ui.pal;
    ios::Palette pal = ios::Palette::make(ios::Appearance::Dark, d2d::Color(Ma4::get().getAccentColor().getMainColor()));
    if (nested) {
        ui.scale = 1.f;
        ui.pal = pal;
    } else {
        ios::Input in;
        in.dt = Ma4::getRenderer().getDeltaTime() * 0.017f;
        ui.beginFrame(ios::gameGfx(), in, pal, 1.f, false);
    }

    auto const& s = v.snap;
    ios::NowPlayingData d;
    d.width = v.width;
    d.showArt = v.wantArt;
    d.showProgress = v.wantProgress;
    d.showLyrics = v.wantLyrics;
    d.available = s.available;
    d.playing = s.playing;
    d.title = s.title;
    d.artist = s.artist;
    d.positionMs = s.positionMs;
    d.durationMs = s.durationMs;
    if (artBrush && artBitmap && !v.demo) {
        d.artBrush = artBrush.Get();
        d.tint = ios::Col(static_cast<float>(artRgb[0]) / 255.f, static_cast<float>(artRgb[1]) / 255.f, static_cast<float>(artRgb[2]) / 255.f, 1.f);
    }
    d.textIdle = LocalizeString::get("client.hudmodule.spotify.idle").value();
    d.textLoading = LocalizeString::get("client.hudmodule.spotify.loadingLyrics").value();
    d.textNone = LocalizeString::get("client.hudmodule.spotify.noLyrics").value();
    d.textInstrumental = LocalizeString::get("client.hudmodule.spotify.instrumental").value();

    using State = lyrics::Result::State;
    using Lyric = ios::NowPlayingData::Lyric;
    if (!v.lyr || v.lyr->state == State::Idle) {
        d.lyric = Lyric::Idle;
    } else if (v.lyr->state == State::Loading) {
        d.lyric = Lyric::Loading;
    } else if (v.lyr->state == State::Instrumental) {
        d.lyric = Lyric::Instrumental;
    } else if (v.lyr->state == State::Ready && !v.lyr->lines.empty()) {
        d.lyric = Lyric::Ready;
        const int idx = lyrics::indexAt(v.lyr->lines, v.lyricPosMs);
        d.line = idx;
        d.firstLine = std::max(0, idx - 2);
        const int last = std::min(static_cast<int>(v.lyr->lines.size()) - 1, idx + 2);
        for (int j = d.firstLine; j <= last; ++j) d.lines.push_back(v.lyr->lines[static_cast<size_t>(j)].text);
    } else {
        d.lyric = Lyric::None;
    }

    ios::drawNowPlaying(ui, d, cardState);

    if (nested) {
        ui.scale = savedScale;
        ui.pal = savedPal;
    } else {
        ui.endFrame();
    }
}

// Minecraft renderer (no gradients, no bitmaps): the same information as plain rounded shapes and text.
void SpotifyHUD::drawPlain(DrawUtil& dc, View const& v) {
    const float W = v.width, H = v.height;
    auto const& s = v.snap;
    dc.fillRoundedRectangle({ 0.f, 0.f, W, H }, d2d::Color(0.09f, 0.09f, 0.11f, 0.82f), 18.f);
    hud::glassPlate(dc, { 0.f, 0.f, W, H }, 18.f, 0.82f);

    const float x = kPad + 4.f;
    dc.drawText({ x, 10.f, W - x, 32.f }, s.available ? s.title : L"Spotify", d2d::Color(1.f, 1.f, 1.f, 1.f),
                Renderer::FontSelection::PrimarySemibold, 16.f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    dc.drawText({ x, 32.f, W - x, 50.f }, s.artist, d2d::Color(1.f, 1.f, 1.f, 0.68f), Renderer::FontSelection::PrimaryRegular, 13.f,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    if (v.wantProgress && s.durationMs > 0) {
        const float frac = static_cast<float>(std::clamp(s.positionMs / static_cast<double>(s.durationMs), 0.0, 1.0));
        dc.fillRoundedRectangle({ x, 60.f, W - x, 64.f }, d2d::Color(1.f, 1.f, 1.f, 0.24f), 2.f);
        if (frac > 0.002f) dc.fillRoundedRectangle({ x, 60.f, x + std::max(4.f, (W - 2.f * x) * frac), 64.f }, d2d::Color(1.f, 1.f, 1.f, 0.95f), 2.f);
        dc.drawText({ x, 66.f, x + 70.f, 82.f }, clockText(s.positionMs), d2d::Color(1.f, 1.f, 1.f, 0.55f),
                    Renderer::FontSelection::PrimaryRegular, 11.f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
        dc.drawText({ W - x - 70.f, 66.f, W - x, 82.f }, clockText(static_cast<double>(s.durationMs)), d2d::Color(1.f, 1.f, 1.f, 0.55f),
                    Renderer::FontSelection::PrimaryRegular, 11.f, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    }
    if (v.wantLyrics && v.lyr && v.lyr->state == lyrics::Result::State::Ready && !v.lyr->lines.empty()) {
        const int idx = lyrics::indexAt(v.lyr->lines, v.lyricPosMs);
        if (idx >= 0) {
            dc.drawText({ kPad, kTopH + 30.f, W - kPad, kTopH + 62.f }, v.lyr->lines[static_cast<size_t>(idx)].text, d2d::Color(1.f, 1.f, 1.f, 1.f),
                        Renderer::FontSelection::PrimarySemibold, 15.f, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
    }
    dc.flush();
}
