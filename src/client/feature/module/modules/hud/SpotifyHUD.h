#pragma once
#include "../../HUDModule.h"
#include "client/misc/NowPlaying.h"
#include "client/screen/ios/IosCards.h"

/// iOS "Now Playing" card for Spotify: artwork, title, artist, a progress bar with the elapsed / total time and the
/// synced lyrics of the song (current line in the middle, the neighbours fading out above and below, sliding with a
/// spring when the line changes). Data comes from client/misc/NowPlaying.* (Windows media controls) and
/// client/misc/Lyrics.* (LRCLIB).
class SpotifyHUD : public HUDModule {
public:
    SpotifyHUD();
    ~SpotifyHUD() override;

    void onEnable() override;
    void onDisable() override;
    void onCleanup(Event&);
    void render(DrawUtil& dc, bool isDefault, bool inEditor) override;

private:
    struct View; // everything one frame draws (defined in the .cpp)

    void drawIos(D2DUtil& dc, View const& v);
    void drawPlain(DrawUtil& dc, View const& v);
    void updateArt(D2DUtil& dc, nowplaying::Snapshot const& snap);

    ValueType showArt = BoolValue(true);
    ValueType showProgress = BoolValue(true);
    ValueType showLyrics = BoolValue(true);
    ValueType hideWhenIdle = BoolValue(true);
    ValueType anyPlayer = BoolValue(false);
    ValueType cardWidth = FloatValue(320.f);
    ValueType lyricOffset = FloatValue(0.f); // ms, shifts the lyrics earlier (+) or later (-)

    // artwork (Direct2D objects belong to the renderer's device: released on renderer cleanup)
    ComPtr<ID2D1Bitmap> artBitmap;
    ComPtr<ID2D1BitmapBrush1> artBrush;
    uint64_t artSerial = 0;     // last artwork version taken from the reader
    uint64_t artTrack = 0;      // track the artwork belongs to
    uint8_t artRgb[3] = { 90, 90, 100 };

    uint64_t requestedTrack = 0; // track the lyrics were requested for
    ios::NowPlayingState cardState; // remembers the lyric line of the last frame (to slide on a change)
    bool active_ = false;        // acquired the reader
};
