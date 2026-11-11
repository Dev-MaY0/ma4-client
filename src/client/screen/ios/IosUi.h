#pragma once
// ma4 Client - iOS style UI toolkit.
//
// A small immediate-mode toolkit on top of Direct2D / DirectWrite that reproduces the look and feel of iOS
// (grouped inset lists, UISwitch, UISlider, segmented controls, SF-style typography, continuous corners,
// translucent materials, spring-like motion). It deliberately has no dependency on the game or on the
// client singletons so it can be exercised from a standalone harness.
//
// All sizes passed to / returned from the toolkit are in device pixels. Use Ui::S(points) to convert from
// iOS "points" into pixels using the current UI scale.

#include "util/DXUtil.h"

#include <d2d1_1helper.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ios {

    using Rect = d2d::Rect;
    using Col = d2d::Color;

    enum class Font : int {
        Light = 0,
        Regular,
        Medium,
        Semibold,
        Bold,
        Count
    };

    /// Everything the toolkit needs from the host renderer.
    struct Gfx {
        ID2D1DeviceContext* ctx = nullptr;
        ID2D1SolidColorBrush* brush = nullptr;
        IDWriteFactory* dwrite = nullptr;
        ID2D1Factory* d2d = nullptr;
        IDWriteTextFormat* fonts[static_cast<int>(Font::Count)] = {};
    };

    /// Per-frame pointer / timing input (device pixels, seconds).
    struct Input {
        Vec2 mouse = {};
        bool down = false;         // primary button currently held
        bool pressed = false;      // primary button went down since last frame
        bool rightPressed = false; // secondary button went down since last frame
        float wheel = 0.f;         // scroll delta in pixels, positive = content moves up (scroll down)
        float dt = 1.f / 60.f;     // seconds
    };

    enum class Appearance {
        Dark,
        Light
    };

    /// Semantic colours modelled after the UIKit system colours (dark / light variants).
    struct Palette {
        bool dark = true;

        Col windowFill, windowBorder, sidebarFill;
        Col cell, cellPressed, separator, opaqueSeparator;
        Col label, secondaryLabel, tertiaryLabel, quaternaryLabel;
        Col fill, secondaryFill, tertiaryFill, quaternaryFill;
        Col switchOff, thumb;
        Col material, materialBorder, materialHighlight;
        Col shadow;
        // ---- Liquid Glass (iOS 26) ----
        Col grouped;                       // grouped background behind the cards (opaque)
        Col glassBacking;                  // "frost" under a glass surface so content below stays out of the way
        Col glassSheenTop, glassSheenBottom; // regular glass sheen (top lit, bottom dim)
        Col clearSheenTop, clearSheenBottom; // clear glass
        Col glassEdge;                     // outer hairline that separates glass from what is behind it
        Col cardBorder;                    // faint inner edge of grouped cards
        Col blue, green, red, orange, yellow, pink, purple, teal, indigo, gray, mint;
        Col accent;

        static Palette make(Appearance appearance, Col accent);
        /// A deterministic "settings icon" colour for an arbitrary string.
        Col tileColor(std::string_view key) const;
    };

    enum class HAlign {
        Leading,
        Center,
        Trailing
    };
    enum class VAlign {
        Top,
        Center,
        Bottom
    };

    struct TextStyle {
        Font font = Font::Regular;
        float size = 17.f;
        Col color = { 1.f, 1.f, 1.f, 1.f };
        HAlign h = HAlign::Leading;
        VAlign v = VAlign::Center;
        bool wrap = false;
        bool ellipsis = true;

        TextStyle() = default;
        TextStyle(Font font, float size, Col color, HAlign h = HAlign::Leading, VAlign v = VAlign::Center,
                  bool wrap = false)
            : font(font)
            , size(size)
            , color(color)
            , h(h)
            , v(v)
            , wrap(wrap) {}
    };

    enum class Glyph {
        ChevronRight,
        ChevronLeft,
        ChevronDown,
        ChevronUp,
        ChevronUpDown,
        Check,
        XMark,
        Plus,
        Magnifier,
        Gear,
        Sliders,
        Grid,
        Gamepad,
        Widgets,
        Play,
        Pause,
        StepBack,
        StepForward,
        Record,
        Stop,
        Flag,
        Camera,
        Minus
    };

    enum class ButtonKind {
        Filled,      // accent background, white label
        Tinted,      // accent @ 15% background, accent label
        Gray,        // quaternary fill, accent label ("Installed", "Open")
        Plain,       // accent label, no background
        Destructive, // red label, no background
        Material     // floating Liquid Glass capsule
    };

    /// `.buttonStyle(.glass)` / `.glassProminent` / clear glass.
    enum class GlassButtonKind {
        Regular,
        Prominent, // accent tinted glass, white label
        Clear      // very light glass for use over media / busy content
    };

    /// Mirror of SwiftUI's `Glass` (regular / clear, tint, interactive).
    struct GlassStyle {
        bool clear = false;      // .clear instead of .regular
        Col tint = { 0.f, 0.f, 0.f, 0.f }; // .tint(color): alpha is the strength
        float press = 0.f;       // 0..1 interactive press feedback (brighter)
        float lift = 0.f;        // 0..1 hover feedback
        float shadow = 1.f;      // drop shadow strength
        float backing = 1.f;     // frosted backing strength (0 = see through)
        float opacity = 1.f;     // overall opacity of the surface
    };

    // Liquid Glass switch metrics (iOS 26 UISwitch is 63 x 28 pt)
    constexpr float kSwitchW = 63.f;
    constexpr float kSwitchH = 28.f;

    class Ui {
    public:
        static Ui& get();

        // ---------------------------------------------------------------- frame management
        void beginFrame(Gfx const& gfx, Input const& input, Palette const& palette, float uiScale, bool rightToLeft);
        void endFrame();
        /// Drop all cached Direct2D resources (call from the renderer cleanup event).
        void releaseDeviceResources();

        Gfx g;
        Input in;
        Palette pal;
        float scale = 1.f;
        bool rtl = false;
        float dt = 1.f / 60.f;

        /// Per-frame counters of the expensive Direct2D / DirectWrite calls (reset by beginFrame). The harness prints
        /// them in `--bench` mode so a regression in the number of layers / layouts shows up immediately.
        struct Stats {
            uint32_t layouts = 0;    // IDWriteTextLayout created
            uint32_t layoutHits = 0; // IDWriteTextLayout served from the cache
            uint32_t gradients = 0;  // gradient stop collections + brushes created
            uint32_t geometries = 0; // path geometries built
            uint32_t layers = 0;     // PushLayer calls
        };
        mutable Stats stats;

        /// iOS points -> device pixels
        [[nodiscard]] float S(float points) const { return points * scale; }

        // ---------------------------------------------------------------- mirroring (right-to-left locales)
        void pushMirror(float centerX);
        void popMirror();
        [[nodiscard]] bool mirrored() const { return mirrorDepth > 0; }
        [[nodiscard]] Vec2 mouse() const;

        // ---------------------------------------------------------------- input
        void lockInput(bool locked) { locked_ = locked; }
        [[nodiscard]] bool inputLocked() const { return locked_; }
        [[nodiscard]] bool hit(Rect const& r) const;

        struct ClickResult {
            bool clicked = false;
            bool pressed = false;
            bool hovered = false;
        };
        /// Touch-up-inside style click handling with press/hover feedback.
        ClickResult click(uint64_t id, Rect const& r, bool enabled = true);

        struct DragResult {
            bool began = false;
            bool active = false;
            bool hovered = false;
        };
        /// Press inside `r` starts a drag that stays active until the button is released.
        DragResult drag(uint64_t id, Rect const& r);
        [[nodiscard]] uint64_t activeId() const { return activeId_; }
        void cancelActive() { activeId_ = 0; }

        // ---------------------------------------------------------------- animation
        /// Critically damped spring towards `target`. First use snaps to `target`.
        float anim(uint64_t id, float target, float smoothTime = 0.10f);
        float animFrom(uint64_t id, float from, float target, float smoothTime = 0.10f);
        void animSet(uint64_t id, float value);
        [[nodiscard]] bool animSettled(uint64_t id, float target, float eps = 0.002f) const;

        /// Under-damped spring, the SwiftUI `.spring(response:dampingFraction:)` model: it can overshoot, which is
        /// what gives Liquid Glass its bounce. Call once per frame per id; first use snaps to `target`.
        float spring(uint64_t id, float target, float response = 0.42f, float damping = 0.80f);
        [[nodiscard]] float springVelocity(uint64_t id) const;
        void springSet(uint64_t id, float value);
        /// "Interactive glass" press amount: 0 -> ~1 with a small overshoot, and a bounce back on release.
        float pressSpring(uint64_t id, bool pressed);
        /// Uniformly scales everything drawn until popScale() around the centre of `r` (interactive press bounce).
        void pushScale(Rect const& r, float s);
        void popScale();

        static uint64_t hashId(std::string_view s, uint64_t seed = 1469598103934665603ull);
        static uint64_t idOf(const void* p, uint64_t salt = 0) {
            return (reinterpret_cast<uintptr_t>(p) * 1099511628211ull) ^ (salt * 0x9E3779B97F4A7C15ull);
        }

        // ---------------------------------------------------------------- primitives
        void fillRect(Rect const& r, Col c);
        void rrect(Rect const& r, float radius, Col c);
        void rrectStroke(Rect const& r, float radius, Col c, float thickness);
        void rrectGradient(Rect const& r, float radius, Col top, Col bottom);
        void circle(Vec2 center, float radius, Col c);
        void circleStroke(Vec2 center, float radius, Col c, float thickness);
        void line(Vec2 a, Vec2 b, Col c, float thickness, bool roundCaps = true);
        /// 1 device pixel line (or 0.5pt scaled) used for separators
        void hairlineH(float x0, float x1, float y, Col c);
        void hairlineV(float x, float y0, float y1, Col c);
        void shadow(Rect const& r, float radius, float blur, float dy, Col c);
        void bitmap(ID2D1Bitmap* bmp, Rect const& r, float opacity = 1.f);

        void pushClip(Rect const& r);
        void popClip();
        void pushClipRRect(Rect const& r, float radius);
        void popClipRRect();
        void pushOpacity(Rect const& bounds, float opacity);
        void popOpacity();

        // ---------------------------------------------------------------- text
        void text(Rect const& r, std::wstring const& s, TextStyle const& st);
        /// Same as text(), but painted with `brush` (gradient fills) instead of the style colour.
        void textBrush(Rect const& r, std::wstring const& s, TextStyle const& st, ID2D1Brush* brush);
        Vec2 measure(std::wstring const& s, TextStyle const& st, float maxWidth = 100000.f);
        float textWidth(std::wstring const& s, TextStyle const& st) { return measure(s, st).x; }
        /// Width of the first `count` characters (used to position a text caret)
        float prefixWidth(std::wstring const& s, size_t count, TextStyle const& st);

        // ---------------------------------------------------------------- glyphs & composites
        void glyph(Glyph g, Rect const& box, Col c, float weight = 1.f, Col knockout = { 0.f, 0.f, 0.f, 0.f });
        /// Settings-style coloured rounded square with a white glyph / monogram.
        void iconTile(Rect const& r, Col base, std::wstring const& monogram);
        void iconTile(Rect const& r, Col base, Glyph g);
        /// Floating translucent surface (toolbar buttons, banners, popovers). Now a regular Liquid Glass surface.
        void materialPill(Rect const& r, float radius);

        // ---------------------------------------------------------------- Liquid Glass
        /// `.glassEffect(.regular / .clear, in: shape)`: soft shadow, frosted backing, lit sheen, specular rim.
        void glass(Rect const& r, float radius, GlassStyle const& st = GlassStyle {});
        /// A row of round glass shapes (left to right) that merge when closer than `spacing`, like the shapes of a
        /// `GlassEffectContainer`. They are separate circles above `spacing` and one capsule at zero gap.
        void glassBlobs(std::vector<Vec2> const& centers, float radius, float spacing, GlassStyle const& st = GlassStyle {});
        /// Capsule `.buttonStyle(.glass)` / `.glassProminent`. Returns true when clicked.
        bool glassButton(uint64_t id, Rect const& r, std::wstring const& label, GlassButtonKind kind = GlassButtonKind::Regular,
                         float fontPt = 15.f, bool enabled = true);
        /// Circular glass icon button (toolbar back / close / playback controls).
        bool glassIconButton(uint64_t id, Rect const& r, Glyph g, Col tint = { 0.f, 0.f, 0.f, 0.f }, float glyphScale = 0.52f,
                             bool enabled = true);
        /// `scrollEdgeEffectStyle`: content fades into `base` towards the edge so floating glass controls stay legible.
        void scrollEdge(Rect const& r, bool top, Col base, float strength, float solid = 0.f);
        /// A real backdrop for glass: a pre-blurred picture of what is behind the window, drawn into `dest`. Regular glass
        /// surfaces then show it (clipped to their shape) instead of the flat frosted backing. Used by the launcher, whose
        /// wallpaper is a bitmap; the in-game menu has no such picture and keeps the frosted backing. Call every frame
        /// after beginFrame(); pass nullptr to switch it off.
        void setBackdrop(ID2D1Bitmap1* bmp, Rect const& dest);
        /// True while any spring / animation is still moving (hosts that render on demand keep drawing until it is false).
        [[nodiscard]] bool animating() const;
        /// Fill / stroke a rounded (continuous-corner) rectangle with an arbitrary brush.
        void fillBrush(Rect const& r, float radius, ID2D1Brush* brush);
        void strokeBrush(Rect const& r, float radius, ID2D1Brush* brush, float thickness);
        ComPtr<ID2D1LinearGradientBrush> linearBrush(Vec2 a, Vec2 b, std::vector<D2D1_GRADIENT_STOP> const& stops);

        // ---------------------------------------------------------------- controls
        /// UISwitch (51x31 pt). Returns true when the value changed.
        bool toggle(uint64_t id, Rect const& r, bool& value, bool enabled = true);
        /// UISlider. `track` is the visual bar rectangle; returns true when the value changed.
        bool slider(uint64_t id, Rect const& track, float& value, float lo, float hi, float step);
        /// UISegmentedControl. Returns the (possibly new) selected index.
        int segmented(uint64_t id, Rect const& r, std::vector<std::wstring> const& labels, int selected);
        /// Text button. Returns true when clicked.
        bool button(uint64_t id, Rect const& r, std::wstring const& label, ButtonKind kind, float fontPt = 15.f,
                    bool enabled = true);
        /// Round glass icon button (e.g. close / back). Returns true when clicked.
        bool iconButton(uint64_t id, Rect const& r, Glyph g, bool filledCircle = true);
        /// Rounded search field background + magnifier + placeholder/text. Returns click on the field.
        bool searchField(uint64_t id, Rect const& r, std::wstring const& text, std::wstring const& placeholder,
                         bool focused, float caretWidthPx, bool caretVisible, bool* clearClicked);
        /// Draws a scroll indicator for a scroll view.
        void scrollIndicator(uint64_t id, Rect const& viewport, float scroll, float maxScroll, bool scrolling);

        // text field helper: background + text + caret. Returns true when clicked.
        bool textField(uint64_t id, Rect const& r, std::wstring const& text, bool focused, size_t caret,
                       bool caretVisible, TextStyle st, float radius);

        // ---------------------------------------------------------------- shapes
        /// Fills a continuous-corner ("squircle") rounded rectangle.
        void fillSmooth(Rect const& r, float radius, Col c);
        void strokeSmooth(Rect const& r, float radius, Col c, float thickness);
        /// Fills `r` clipped to the rounded rectangle `card` (the pressed highlight of a first / last row of a grouped
        /// card). Exact, but it builds a geometry, so only use it for rare states: a clip layer every frame is far
        /// more expensive.
        void fillClipped(Rect const& r, Rect const& card, float cardRadius, Col c);
        /// A grouped card is no longer clipped with a layer. While a card is active, fillCell() (the pressed highlight of
        /// a row) follows its rounded corners instead.
        void beginCard(Rect const& card, float radius) {
            cardRect_ = card;
            cardRadius_ = radius;
            hasCard_ = true;
        }
        void endCard() { hasCard_ = false; }
        void fillCell(Rect const& r, Col c);

        ID2D1DeviceContext* ctx() const { return g.ctx; }

    private:
        struct Anim {
            float v = 0.f;
            float vel = 0.f;
        };

        ComPtr<IDWriteTextLayout> makeLayout(std::wstring const& s, TextStyle const& st, float w, float h) const;
        ComPtr<ID2D1Geometry> smoothGeometry(float w, float h, float radius, bool absolute, float ox, float oy);
        ComPtr<ID2D1PathGeometry> buildSmoothPath(float x, float y, float w, float h, float radius);
        void setColor(Col c);
        static float smoothDamp(float cur, float target, float& vel, float smoothTime, float dt);
        void ensureFactoryResources();

        void glassSurface(Rect const& bounds, float radius, GlassStyle const& st, ID2D1Geometry* geo);
        void tileSheen(Rect const& r, float radius);
        void drawText(Rect const& r, std::wstring const& s, TextStyle const& st, ID2D1Brush* brush);
        ComPtr<ID2D1BitmapBrush1> backdropBrush_;
        ID2D1Bitmap1* backdropBmp_ = nullptr;

        // DirectWrite layouts are expensive to create and almost every string is identical from frame to frame, so
        // they are cached (key: text + format + size + box + flags) and dropped after ~3 s without use.
        struct TextEntry {
            std::wstring s;
            IDWriteTextFormat* fmt = nullptr;
            float size = 0.f, w = 0.f, h = 0.f;
            uint32_t flags = 0;
            ComPtr<IDWriteTextLayout> layout;
            uint64_t lastFrame = 0;
        };
        // Gradient stop collections + brushes are cached by their stops; only the end points change per use.
        struct GradEntry {
            ComPtr<ID2D1GradientStopCollection> coll;
            ComPtr<ID2D1LinearGradientBrush> brush;
            uint64_t lastFrame = 0;
        };
        mutable std::unordered_map<uint64_t, TextEntry> textCache_;
        std::unordered_map<uint64_t, GradEntry> gradCache_;
        std::vector<char> layerFlags_; // 1 = pushOpacity really pushed a layer
        IDWriteTextFormat* fontSig_[static_cast<int>(Font::Count)] = {}; // to notice a renderer that rebuilt its fonts
        ID2D1DeviceContext* ctxSig_ = nullptr;                          // ... or its device context
        Rect cardRect_ = {};
        float cardRadius_ = 0.f;
        bool hasCard_ = false;

        std::unordered_map<uint64_t, Anim> anims_;
        std::unordered_map<uint64_t, Anim> springs_;
        std::vector<D2D1::Matrix3x2F> scaleStack_;
        std::unordered_map<uint64_t, ComPtr<ID2D1PathGeometry>> smoothCache_;
        ComPtr<ID2D1StrokeStyle> roundStroke_;
        ID2D1Factory* cachedFactory_ = nullptr;

        std::vector<Rect> hitClips_;
        std::vector<int> clipKinds_; // 0 = axis aligned, 1 = layer
        std::vector<D2D1::Matrix3x2F> mirrorStack_;
        int mirrorDepth = 0;
        float mirrorCx_ = 0.f;
        bool locked_ = false;
        bool prevDown_ = false;
        uint64_t activeId_ = 0;
        uint64_t frame_ = 0;
    };

    /// Liquid Glass sheen + specular rim drawn over a plate that was already filled (HUD modules). It only needs a
    /// device context, no Ui frame. `strength` (0..1) follows the opacity of the plate underneath.
    void drawGlassPlate(ID2D1DeviceContext* ctx, Rect const& r, float radius, float strength);
    /// Drops the gradient brushes drawGlassPlate keeps between frames (renderer cleanup).
    void releaseGlassPlateCache();

    /// Default UI font family: SF Pro (if installed) -> Segoe UI Variable -> Segoe UI.
    std::wstring defaultFontFamily();

    /// Little helpers
    inline Col withAlpha(Col c, float a) { return c.asAlpha(a); }
    inline Col mulAlpha(Col c, float k) { return c.asAlpha(c.a * k); }
    inline Col mix(Col a, Col b, float t) {
        return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
    }
    inline float clamp01(float v) {
        return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    }
    inline Rect inset(Rect const& r, float dx, float dy) {
        return { r.left + dx, r.top + dy, r.right - dx, r.bottom - dy };
    }

} // namespace ios
