#pragma once
// Game independent iOS cards drawn by HUD modules: the Spotify "Now Playing" card and the Tab key player list. They take
// plain data (so the UI harness can render them without Minecraft) and draw with the toolkit in the current transform.

#include "IosUi.h"

#include <string>
#include <vector>

namespace ios {

    // ------------------------------------------------------------------------------------------ Now Playing
    constexpr float kNowPlayingTopH = 88.f;
    constexpr float kNowPlayingLyricH = 100.f;

    struct NowPlayingData {
        float width = 320.f;
        bool showArt = true, showProgress = true, showLyrics = true;
        bool available = true, playing = true;
        std::wstring title, artist;
        double positionMs = 0;
        int64_t durationMs = 0;                 // 0 = unknown
        ID2D1BitmapBrush1* artBrush = nullptr;  // 128 x 128 artwork (its transform is set while drawing), or null
        Col tint = Col(0.30f, 0.22f, 0.46f, 1.f); // average colour of the artwork

        enum class Lyric { Idle, Loading, None, Instrumental, Ready };
        Lyric lyric = Lyric::Idle;
        std::vector<std::wstring> lines;        // the lines around the current one: [firstLine, firstLine + size)
        int firstLine = 0;
        int line = -1;                          // index of the current line in the song
        std::wstring textIdle, textLoading, textNone, textInstrumental;
    };

    /// Remembers what the card showed last frame (to slide the lyrics when the line changes).
    struct NowPlayingState {
        int lastLine = -2;
    };

    [[nodiscard]] inline float nowPlayingHeight(NowPlayingData const& d) {
        return kNowPlayingTopH + (d.showLyrics ? kNowPlayingLyricH : 0.f);
    }

    /// Draws the card with its top-left corner at the origin of the current transform (1 unit = 1 HUD pixel: call it with
    /// a Ui whose scale is 1). The card is `d.width` x nowPlayingHeight(d).
    void drawNowPlaying(Ui& ui, NowPlayingData const& d, NowPlayingState& state);

    // ------------------------------------------------------------------------------------------ Tab list
    struct TabRow {
        std::wstring text;              // the name with Minecraft colour codes (section sign + character)
        ID2D1BitmapBrush1* face = nullptr; // the player's face (8x8 or 16x16 pixels), or null
        float faceSize = 8.f;           // size of the bitmap in pixels
    };

    struct TabData {
        std::wstring title, subtitle;
        std::vector<TabRow> rows;
        Col textColor = Col(1.f, 1.f, 1.f, 1.f);
        Col veil = Col(0.f, 0.f, 0.f, 0.5f); // the "background colour" setting: darkens the glass
        float screenW = 1920.f, screenH = 1080.f;
    };

    /// Draws the whole card centred at the top of the screen. `ui.scale` decides the size; if the card would not fit
    /// the width of the screen it is scaled down. `popId` names the spring of the pop-in animation, `restart` starts it.
    void drawTabList(Ui& ui, TabData const& d, uint64_t popId, bool restart);

} // namespace ios
