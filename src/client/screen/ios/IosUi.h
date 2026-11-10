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
        Camera
    };

    enum class ButtonKind {
        Filled,      // accent background, white label
        Tinted,      // accent @ 15% background, accent label
        Gray,        // quaternary fill, accent label ("Installed", "Open")
        Plain,       // accent label, no background
        Destructive, // red label, no background
        Material     // floating translucent pill
    };

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
        Vec2 measure(std::wstring const& s, TextStyle const& st, float maxWidth = 100000.f);
        float textWidth(std::wstring const& s, TextStyle const& st) { return measure(s, st).x; }
        /// Width of the first `count` characters (used to position a text caret)
        float prefixWidth(std::wstring const& s, size_t count, TextStyle const& st);

        // ---------------------------------------------------------------- glyphs & composites
        void glyph(Glyph g, Rect const& box, Col c, float weight = 1.f, Col knockout = { 0.f, 0.f, 0.f, 0.f });
        /// Settings-style coloured rounded square with a white glyph / monogram.
        void iconTile(Rect const& r, Col base, std::wstring const& monogram);
        void iconTile(Rect const& r, Col base, Glyph g);
        /// Floating translucent surface (toolbar buttons, banners, popovers).
        void materialPill(Rect const& r, float radius);

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

        std::unordered_map<uint64_t, Anim> anims_;
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
