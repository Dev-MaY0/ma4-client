#include "IosUi.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <dwrite.h>

namespace ios {

    namespace {
        constexpr float kPi = 3.14159265358979f;

        float rad(float deg) {
            return deg * (kPi / 180.f);
        }

        Col rgb(int r, int g, int b, float a = 1.f) {
            return Col(static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f, static_cast<float>(b) / 255.f, a);
        }

        D2D1_POINT_2F pt(float x, float y) {
            return D2D1::Point2F(x, y);
        }

        std::chrono::steady_clock::time_point& activeTouch() {
            static std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
            return t;
        }
    } // namespace

    // ======================================================================================== font family
    std::wstring defaultFontFamily() {
        static std::wstring cached;
        if (!cached.empty()) return cached;

        cached = L"Segoe UI";
        ComPtr<IDWriteFactory> factory;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) {
            return cached;
        }

        ComPtr<IDWriteFontCollection> collection;
        if (FAILED(factory->GetSystemFontCollection(collection.GetAddressOf(), FALSE))) return cached;

        // SF Pro is the real iOS typeface; if the user has it installed we use it automatically.
        static constexpr const wchar_t* candidates[] = { L"SF Pro Display", L"SF Pro Text", L"Segoe UI Variable Display",
                                                         L"Segoe UI Variable Text", L"Segoe UI" };
        for (auto* name : candidates) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(collection->FindFamilyName(name, &index, &exists)) && exists) {
                cached = name;
                break;
            }
        }
        return cached;
    }

    // ======================================================================================== palette
    Palette Palette::make(Appearance appearance, Col accent) {
        Palette p;
        p.dark = appearance == Appearance::Dark;
        p.accent = accent;

        if (p.dark) {
            p.windowFill = rgb(28, 28, 31, 0.80f);
            p.windowBorder = rgb(255, 255, 255, 0.14f);
            p.sidebarFill = rgb(0, 0, 0, 0.12f);
            p.cell = rgb(255, 255, 255, 0.075f);
            p.cellPressed = rgb(255, 255, 255, 0.16f);
            p.separator = rgb(255, 255, 255, 0.12f);
            p.opaqueSeparator = rgb(56, 56, 58);
            p.label = rgb(255, 255, 255);
            p.secondaryLabel = rgb(235, 235, 245, 0.60f);
            p.tertiaryLabel = rgb(235, 235, 245, 0.30f);
            p.quaternaryLabel = rgb(235, 235, 245, 0.18f);
            p.fill = rgb(120, 120, 128, 0.36f);
            p.secondaryFill = rgb(120, 120, 128, 0.32f);
            p.tertiaryFill = rgb(118, 118, 128, 0.24f);
            p.quaternaryFill = rgb(116, 116, 128, 0.18f);
            p.switchOff = rgb(120, 120, 128, 0.32f);
            p.thumb = rgb(255, 255, 255);
            p.material = rgb(44, 44, 46, 0.86f);
            p.materialBorder = rgb(255, 255, 255, 0.14f);
            p.materialHighlight = rgb(255, 255, 255, 0.18f);
            p.shadow = rgb(0, 0, 0, 0.42f);
            p.blue = rgb(10, 132, 255);
            p.green = rgb(48, 209, 88);
            p.red = rgb(255, 69, 58);
            p.orange = rgb(255, 159, 10);
            p.yellow = rgb(255, 214, 10);
            p.pink = rgb(255, 55, 95);
            p.purple = rgb(191, 90, 242);
            p.teal = rgb(64, 200, 224);
            p.indigo = rgb(94, 92, 230);
            p.gray = rgb(142, 142, 147);
            p.mint = rgb(99, 230, 226);
        } else {
            p.windowFill = rgb(242, 242, 247, 0.84f);
            p.windowBorder = rgb(0, 0, 0, 0.12f);
            p.sidebarFill = rgb(0, 0, 0, 0.035f);
            p.cell = rgb(255, 255, 255, 0.92f);
            p.cellPressed = rgb(60, 60, 67, 0.14f);
            p.separator = rgb(60, 60, 67, 0.29f);
            p.opaqueSeparator = rgb(198, 198, 200);
            p.label = rgb(0, 0, 0);
            p.secondaryLabel = rgb(60, 60, 67, 0.60f);
            p.tertiaryLabel = rgb(60, 60, 67, 0.30f);
            p.quaternaryLabel = rgb(60, 60, 67, 0.18f);
            p.fill = rgb(120, 120, 128, 0.20f);
            p.secondaryFill = rgb(120, 120, 128, 0.16f);
            p.tertiaryFill = rgb(118, 118, 128, 0.12f);
            p.quaternaryFill = rgb(116, 116, 128, 0.08f);
            p.switchOff = rgb(120, 120, 128, 0.20f);
            p.thumb = rgb(255, 255, 255);
            p.material = rgb(255, 255, 255, 0.86f);
            p.materialBorder = rgb(0, 0, 0, 0.08f);
            p.materialHighlight = rgb(255, 255, 255, 0.60f);
            p.shadow = rgb(0, 0, 0, 0.20f);
            p.blue = rgb(0, 122, 255);
            p.green = rgb(52, 199, 89);
            p.red = rgb(255, 59, 48);
            p.orange = rgb(255, 149, 0);
            p.yellow = rgb(255, 204, 0);
            p.pink = rgb(255, 45, 85);
            p.purple = rgb(175, 82, 222);
            p.teal = rgb(48, 176, 199);
            p.indigo = rgb(88, 86, 214);
            p.gray = rgb(142, 142, 147);
            p.mint = rgb(0, 199, 190);
        }
        return p;
    }

    Col Palette::tileColor(std::string_view key) const {
        const Col choices[] = { blue, green, orange, pink, purple, red, teal, indigo };
        uint64_t h = Ui::hashId(key);
        return choices[(h >> 7) % (sizeof(choices) / sizeof(choices[0]))];
    }

    // ======================================================================================== Ui basics
    Ui& Ui::get() {
        static Ui instance;
        return instance;
    }

    uint64_t Ui::hashId(std::string_view s, uint64_t seed) {
        uint64_t h = seed;
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ull;
        }
        return h;
    }

    float Ui::smoothDamp(float cur, float target, float& vel, float smoothTime, float dt) {
        smoothTime = std::max(0.0001f, smoothTime);
        float omega = 2.f / smoothTime;
        float x = omega * dt;
        float e = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);
        float change = cur - target;
        float temp = (vel + omega * change) * dt;
        vel = (vel - omega * temp) * e;
        float out = target + (change + temp) * e;
        if ((target - cur > 0.f) == (out > target)) {
            out = target;
            vel = 0.f;
        }
        return out;
    }

    void Ui::ensureFactoryResources() {
        if (g.d2d == cachedFactory_) return;
        smoothCache_.clear();
        roundStroke_.Reset();
        cachedFactory_ = g.d2d;
        if (!g.d2d) return;

        D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10.f,
            D2D1_DASH_STYLE_SOLID, 0.f);
        g.d2d->CreateStrokeStyle(props, nullptr, 0, roundStroke_.GetAddressOf());
    }

    void Ui::beginFrame(Gfx const& gfx, Input const& input, Palette const& palette, float uiScale,
                        bool rightToLeft) {
        g = gfx;
        in = input;
        pal = palette;
        scale = uiScale;
        rtl = rightToLeft;
        dt = std::clamp(input.dt, 0.001f, 0.05f);
        locked_ = false;
        hitClips_.clear();
        hitClips_.push_back(Rect(-1.0e7f, -1.0e7f, 1.0e7f, 1.0e7f));
        clipKinds_.clear();
        mirrorStack_.clear();
        mirrorDepth = 0;
        ensureFactoryResources();

        // A widget that stopped being drawn must not keep the pointer captured forever.
        if (activeId_ != 0 &&
            std::chrono::steady_clock::now() - activeTouch() > std::chrono::milliseconds(300)) {
            activeId_ = 0;
        }
        ++frame_;
    }

    void Ui::endFrame() {
        // Safety net for unbalanced clip pushes.
        while (!clipKinds_.empty()) {
            if (clipKinds_.back() == 0) {
                popClip();
            } else {
                popClipRRect();
            }
        }
        while (mirrorDepth > 0) popMirror();
    }

    void Ui::releaseDeviceResources() {
        smoothCache_.clear();
        roundStroke_.Reset();
        cachedFactory_ = nullptr;
        anims_.clear();
        g = Gfx {};
        activeId_ = 0;
    }

    void Ui::pushMirror(float centerX) {
        if (!g.ctx) return;
        D2D1::Matrix3x2F old;
        g.ctx->GetTransform(&old);
        mirrorStack_.push_back(old);
        D2D1::Matrix3x2F m = D2D1::Matrix3x2F::Scale(-1.f, 1.f, D2D1::Point2F(centerX, 0.f)) * D2D1::Matrix3x2F(old);
        g.ctx->SetTransform(m);
        mirrorCx_ = centerX;
        ++mirrorDepth;
    }

    void Ui::popMirror() {
        if (mirrorDepth <= 0 || !g.ctx) return;
        g.ctx->SetTransform(mirrorStack_.back());
        mirrorStack_.pop_back();
        --mirrorDepth;
    }

    Vec2 Ui::mouse() const {
        if (mirrorDepth > 0) return { 2.f * mirrorCx_ - in.mouse.x, in.mouse.y };
        return in.mouse;
    }

    bool Ui::hit(Rect const& r) const {
        if (locked_) return false;
        Vec2 m = mouse();
        return r.contains(m) && hitClips_.back().contains(m);
    }

    Ui::ClickResult Ui::click(uint64_t id, Rect const& r, bool enabled) {
        ClickResult res;
        bool h = enabled && hit(r);
        res.hovered = h && (activeId_ == 0 || activeId_ == id);
        if (h && in.pressed && activeId_ == 0) {
            activeId_ = id;
        }
        if (activeId_ == id) {
            activeTouch() = std::chrono::steady_clock::now();
            res.pressed = h && in.down;
            if (!in.down) {
                if (h) res.clicked = true;
                activeId_ = 0;
            }
        }
        return res;
    }

    Ui::DragResult Ui::drag(uint64_t id, Rect const& r) {
        DragResult res;
        bool h = hit(r);
        res.hovered = h && (activeId_ == 0 || activeId_ == id);
        if (h && in.pressed && activeId_ == 0) {
            activeId_ = id;
            res.began = true;
        }
        if (activeId_ == id) {
            activeTouch() = std::chrono::steady_clock::now();
            if (!in.down) {
                activeId_ = 0;
            } else {
                res.active = true;
            }
        }
        return res;
    }

    float Ui::anim(uint64_t id, float target, float smoothTime) {
        auto it = anims_.find(id);
        if (it == anims_.end()) {
            anims_[id] = { target, 0.f };
            return target;
        }
        Anim& a = it->second;
        a.v = smoothDamp(a.v, target, a.vel, smoothTime, dt);
        if (std::fabs(a.v - target) < 1.0e-4f && std::fabs(a.vel) < 1.0e-3f) {
            a.v = target;
            a.vel = 0.f;
        }
        return a.v;
    }

    float Ui::animFrom(uint64_t id, float from, float target, float smoothTime) {
        if (anims_.find(id) == anims_.end()) anims_[id] = { from, 0.f };
        return anim(id, target, smoothTime);
    }

    void Ui::animSet(uint64_t id, float value) {
        anims_[id] = { value, 0.f };
    }

    bool Ui::animSettled(uint64_t id, float target, float eps) const {
        auto it = anims_.find(id);
        if (it == anims_.end()) return true;
        return std::fabs(it->second.v - target) < eps && std::fabs(it->second.vel) < 0.01f;
    }

    void Ui::setColor(Col c) {
        g.brush->SetColor(c.get());
    }

    // ======================================================================================== shapes
    void Ui::fillRect(Rect const& r, Col c) {
        if (c.a <= 0.001f) return;
        setColor(c);
        g.ctx->FillRectangle(r.get(), g.brush);
    }

    ComPtr<ID2D1PathGeometry> Ui::buildSmoothPath(float x, float y, float w, float h, float R) {
        ComPtr<ID2D1PathGeometry> geo;
        if (!g.d2d || FAILED(g.d2d->CreatePathGeometry(geo.GetAddressOf()))) return {};
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geo->Open(sink.GetAddressOf()))) return {};

        const float budget = std::min(w, h) * 0.5f;
        R = std::min(R, budget);
        float smoothing = 0.6f;
        if (R > 0.001f) smoothing = std::min(smoothing, budget / R - 1.f);
        smoothing = std::max(smoothing, 0.f);

        const float p = (1.f + smoothing) * R;
        const float arcMeasure = 90.f * (1.f - smoothing);
        const float arcLen = std::sin(rad(arcMeasure * 0.5f)) * R * std::sqrt(2.f);
        const float angleAlpha = (90.f - arcMeasure) * 0.5f;
        const float p3p4 = R * std::tan(rad(angleAlpha * 0.5f));
        const float angleBeta = 45.f * smoothing;
        const float c = p3p4 * std::cos(rad(angleBeta));
        const float d = c * std::tan(rad(angleBeta));
        const float b = (p - arcLen - c - d) / 3.f;
        const float a = 2.f * b;

        struct P {
            float x, y;
        };
        auto rot = [](int r, float dx, float dy) -> P {
            switch (r) {
            case 0:
                return { dx, dy };
            case 1:
                return { -dy, dx };
            case 2:
                return { -dx, -dy };
            default:
                return { dy, -dx };
            }
        };
        auto corner = [&](P s, int r) {
            P v1 = rot(r, a, 0.f), v2 = rot(r, a + b, 0.f), v3 = rot(r, a + b + c, d);
            const bool hasCurves = (a + b + c + d) > 0.001f;
            if (hasCurves) {
                sink->AddBezier(D2D1::BezierSegment(pt(s.x + v1.x, s.y + v1.y), pt(s.x + v2.x, s.y + v2.y),
                                                    pt(s.x + v3.x, s.y + v3.y)));
            }
            P j1 = { s.x + v3.x, s.y + v3.y };
            P va = rot(r, arcLen, arcLen);
            P j2 = { j1.x + va.x, j1.y + va.y };
            sink->AddArc(D2D1::ArcSegment(pt(j2.x, j2.y), D2D1::SizeF(R, R), 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                          D2D1_ARC_SIZE_SMALL));
            if (hasCurves) {
                P w1 = rot(r, d, c), w2 = rot(r, d, b + c), w3 = rot(r, d, a + b + c);
                sink->AddBezier(D2D1::BezierSegment(pt(j2.x + w1.x, j2.y + w1.y), pt(j2.x + w2.x, j2.y + w2.y),
                                                    pt(j2.x + w3.x, j2.y + w3.y)));
            }
        };

        sink->BeginFigure(pt(x + p, y), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(pt(x + w - p, y));
        corner({ x + w - p, y }, 0);
        sink->AddLine(pt(x + w, y + h - p));
        corner({ x + w, y + h - p }, 1);
        sink->AddLine(pt(x + p, y + h));
        corner({ x + p, y + h }, 2);
        sink->AddLine(pt(x, y + p));
        corner({ x, y + p }, 3);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        return geo;
    }

    ComPtr<ID2D1Geometry> Ui::smoothGeometry(float w, float h, float radius, bool absolute, float ox, float oy) {
        if (absolute) {
            ComPtr<ID2D1Geometry> out;
            auto path = buildSmoothPath(ox, oy, w, h, radius);
            if (path) path.As(&out);
            return out;
        }

        uint64_t w4 = static_cast<uint64_t>(std::lround(w * 4.f)) & 0xFFFFF;
        uint64_t h4 = static_cast<uint64_t>(std::lround(h * 4.f)) & 0xFFFFF;
        uint64_t r4 = static_cast<uint64_t>(std::lround(radius * 4.f)) & 0xFFFFF;
        uint64_t key = w4 | (h4 << 20) | (r4 << 40);
        auto it = smoothCache_.find(key);
        if (it == smoothCache_.end()) {
            if (smoothCache_.size() > 768) smoothCache_.clear();
            auto path = buildSmoothPath(0.f, 0.f, w, h, radius);
            if (!path) return {};
            it = smoothCache_.emplace(key, path).first;
        }
        ComPtr<ID2D1Geometry> out;
        it->second.As(&out);
        return out;
    }

    void Ui::fillSmooth(Rect const& r, float radius, Col c) {
        float w = r.getWidth(), h = r.getHeight();
        if (w <= 0.f || h <= 0.f || c.a <= 0.001f) return;
        radius = std::min(radius, std::min(w, h) * 0.5f);
        setColor(c);
        if (radius < 6.f || radius >= std::min(w, h) * 0.5f - 0.5f) {
            if (radius < 0.5f) {
                g.ctx->FillRectangle(r.get(), g.brush);
            } else {
                g.ctx->FillRoundedRectangle(D2D1::RoundedRect(r.get(), radius, radius), g.brush);
            }
            return;
        }
        auto geo = smoothGeometry(w, h, radius, false, 0.f, 0.f);
        if (!geo) {
            g.ctx->FillRoundedRectangle(D2D1::RoundedRect(r.get(), radius, radius), g.brush);
            return;
        }
        D2D1::Matrix3x2F old;
        g.ctx->GetTransform(&old);
        g.ctx->SetTransform(D2D1::Matrix3x2F::Translation(r.left, r.top) * D2D1::Matrix3x2F(old));
        g.ctx->FillGeometry(geo.Get(), g.brush);
        g.ctx->SetTransform(old);
    }

    void Ui::strokeSmooth(Rect const& r, float radius, Col c, float thickness) {
        float w = r.getWidth() - thickness, h = r.getHeight() - thickness;
        if (w <= 0.f || h <= 0.f || c.a <= 0.001f) return;
        radius = std::max(0.f, std::min(radius - thickness * 0.5f, std::min(w, h) * 0.5f));
        Rect in = Rect(r.left + thickness * 0.5f, r.top + thickness * 0.5f, r.right - thickness * 0.5f,
                       r.bottom - thickness * 0.5f);
        setColor(c);
        if (radius < 6.f || radius >= std::min(w, h) * 0.5f - 0.5f) {
            if (radius < 0.5f) {
                g.ctx->DrawRectangle(in.get(), g.brush, thickness);
            } else {
                g.ctx->DrawRoundedRectangle(D2D1::RoundedRect(in.get(), radius, radius), g.brush, thickness);
            }
            return;
        }
        auto geo = smoothGeometry(w, h, radius, false, 0.f, 0.f);
        if (!geo) {
            g.ctx->DrawRoundedRectangle(D2D1::RoundedRect(in.get(), radius, radius), g.brush, thickness);
            return;
        }
        D2D1::Matrix3x2F old;
        g.ctx->GetTransform(&old);
        g.ctx->SetTransform(D2D1::Matrix3x2F::Translation(in.left, in.top) * D2D1::Matrix3x2F(old));
        g.ctx->DrawGeometry(geo.Get(), g.brush, thickness);
        g.ctx->SetTransform(old);
    }

    void Ui::rrect(Rect const& r, float radius, Col c) {
        fillSmooth(r, radius, c);
    }

    void Ui::rrectStroke(Rect const& r, float radius, Col c, float thickness) {
        strokeSmooth(r, radius, c, thickness);
    }

    void Ui::rrectGradient(Rect const& r, float radius, Col top, Col bottom) {
        D2D1_GRADIENT_STOP stops[2] = { { 0.f, top.get() }, { 1.f, bottom.get() } };
        ComPtr<ID2D1GradientStopCollection> coll;
        if (FAILED(g.ctx->CreateGradientStopCollection(stops, 2, coll.GetAddressOf()))) return;
        ComPtr<ID2D1LinearGradientBrush> brush;
        if (FAILED(g.ctx->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(pt(r.left, r.top), pt(r.left, r.bottom)), coll.Get(),
                brush.GetAddressOf()))) {
            return;
        }
        radius = std::min(radius, std::min(r.getWidth(), r.getHeight()) * 0.5f);
        g.ctx->FillRoundedRectangle(D2D1::RoundedRect(r.get(), radius, radius), brush.Get());
    }

    void Ui::circle(Vec2 center, float radius, Col c) {
        if (c.a <= 0.001f) return;
        setColor(c);
        g.ctx->FillEllipse(D2D1::Ellipse(pt(center.x, center.y), radius, radius), g.brush);
    }

    void Ui::circleStroke(Vec2 center, float radius, Col c, float thickness) {
        if (c.a <= 0.001f) return;
        setColor(c);
        g.ctx->DrawEllipse(D2D1::Ellipse(pt(center.x, center.y), radius, radius), g.brush, thickness);
    }

    void Ui::line(Vec2 a, Vec2 b, Col c, float thickness, bool roundCaps) {
        if (c.a <= 0.001f) return;
        setColor(c);
        g.ctx->DrawLine(pt(a.x, a.y), pt(b.x, b.y), g.brush, thickness, roundCaps ? roundStroke_.Get() : nullptr);
    }

    void Ui::hairlineH(float x0, float x1, float y, Col c) {
        float t = std::max(1.f, std::round(scale * 0.5f));
        fillRect(Rect(x0, y, x1, y + t), c);
    }

    void Ui::hairlineV(float x, float y0, float y1, Col c) {
        float t = std::max(1.f, std::round(scale * 0.5f));
        fillRect(Rect(x, y0, x + t, y1), c);
    }

    void Ui::shadow(Rect const& r, float radius, float blur, float dy, Col c) {
        if (c.a <= 0.001f || blur <= 0.5f) return;
        const int n = 12;
        for (int i = n; i >= 1; --i) {
            float t = static_cast<float>(i) / static_cast<float>(n); // 1 = outermost
            float grow = blur * t;
            float falloff = (1.f - t);
            float a = c.a * (0.55f / static_cast<float>(n)) * (0.35f + 2.2f * falloff * falloff);
            Rect rr(r.left - grow, r.top - grow + dy, r.right + grow, r.bottom + grow + dy);
            fillSmooth(rr, radius + grow, c.asAlpha(std::min(a, 0.35f)));
        }
    }

    void Ui::bitmap(ID2D1Bitmap* bmp, Rect const& r, float opacity) {
        if (!bmp) return;
        g.ctx->DrawBitmap(bmp, r.get(), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    void Ui::pushClip(Rect const& r) {
        g.ctx->PushAxisAlignedClip(r.get(), D2D1_ANTIALIAS_MODE_ALIASED);
        Rect cur = hitClips_.back();
        hitClips_.push_back(Rect(std::max(cur.left, r.left), std::max(cur.top, r.top), std::min(cur.right, r.right),
                                 std::min(cur.bottom, r.bottom)));
        clipKinds_.push_back(0);
    }

    void Ui::popClip() {
        if (clipKinds_.empty() || clipKinds_.back() != 0) return;
        g.ctx->PopAxisAlignedClip();
        hitClips_.pop_back();
        clipKinds_.pop_back();
    }

    void Ui::pushClipRRect(Rect const& r, float radius) {
        ComPtr<ID2D1Geometry> geo;
        float w = r.getWidth(), h = r.getHeight();
        radius = std::min(radius, std::min(w, h) * 0.5f);
        if (radius < 6.f || radius >= std::min(w, h) * 0.5f - 0.5f) {
            ComPtr<ID2D1RoundedRectangleGeometry> rr;
            if (SUCCEEDED(g.d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r.get(), radius, radius),
                                                                rr.GetAddressOf()))) {
                rr.As(&geo);
            }
        } else {
            geo = smoothGeometry(w, h, radius, true, r.left, r.top);
        }

        D2D1_LAYER_PARAMETERS1 lp =
            D2D1::LayerParameters1(D2D1::InfiniteRect(), geo.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        g.ctx->PushLayer(&lp, nullptr);

        Rect cur = hitClips_.back();
        hitClips_.push_back(Rect(std::max(cur.left, r.left), std::max(cur.top, r.top), std::min(cur.right, r.right),
                                 std::min(cur.bottom, r.bottom)));
        clipKinds_.push_back(1);
    }

    void Ui::popClipRRect() {
        if (clipKinds_.empty() || clipKinds_.back() != 1) return;
        g.ctx->PopLayer();
        hitClips_.pop_back();
        clipKinds_.pop_back();
    }

    void Ui::pushOpacity(Rect const&, float opacity) {
        D2D1_LAYER_PARAMETERS1 lp = D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr,
                                                           D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
                                                           std::clamp(opacity, 0.f, 1.f));
        g.ctx->PushLayer(&lp, nullptr);
    }

    void Ui::popOpacity() {
        g.ctx->PopLayer();
    }

    // ======================================================================================== text
    ComPtr<IDWriteTextLayout> Ui::makeLayout(std::wstring const& s, TextStyle const& st, float w, float h) const {
        IDWriteTextFormat* fmt = g.fonts[static_cast<int>(st.font)];
        if (!fmt) {
            for (auto* f : g.fonts) {
                if (f) {
                    fmt = f;
                    break;
                }
            }
        }
        if (!fmt || !g.dwrite) return {};

        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(g.dwrite->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), fmt, std::max(1.f, w),
                                              std::max(1.f, h), layout.GetAddressOf()))) {
            return {};
        }
        DWRITE_TEXT_RANGE range { 0, static_cast<UINT32>(s.size()) };
        layout->SetFontSize(st.size, range);
        layout->SetWordWrapping(st.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        layout->SetReadingDirection(rtl ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT);
        layout->SetFlowDirection(DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM);
        if (!st.wrap && st.ellipsis) {
            ComPtr<IDWriteInlineObject> sign;
            if (SUCCEEDED(g.dwrite->CreateEllipsisTrimmingSign(layout.Get(), sign.GetAddressOf()))) {
                DWRITE_TRIMMING trimming { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                layout->SetTrimming(&trimming, sign.Get());
            }
        }
        return layout;
    }

    void Ui::text(Rect const& r, std::wstring const& s, TextStyle const& st) {
        if (s.empty() || st.color.a <= 0.001f) return;
        float w = r.getWidth(), h = r.getHeight();
        if (w <= 1.f || h <= 1.f) return;

        auto layout = makeLayout(s, st, w, h);
        if (!layout) return;

        layout->SetTextAlignment(st.h == HAlign::Leading  ? DWRITE_TEXT_ALIGNMENT_LEADING
                                 : st.h == HAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                          : DWRITE_TEXT_ALIGNMENT_TRAILING);
        layout->SetParagraphAlignment(st.v == VAlign::Top      ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                      : st.v == VAlign::Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                               : DWRITE_PARAGRAPH_ALIGNMENT_FAR);

        D2D1::Matrix3x2F old;
        const bool flip = mirrored();
        if (flip) {
            g.ctx->GetTransform(&old);
            g.ctx->SetTransform(D2D1::Matrix3x2F::Scale(-1.f, 1.f, D2D1::Point2F(r.centerX(), 0.f)) *
                                D2D1::Matrix3x2F(old));
        }
        setColor(st.color);
        g.ctx->DrawTextLayout(pt(r.left, r.top), layout.Get(), g.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (flip) g.ctx->SetTransform(old);
    }

    Vec2 Ui::measure(std::wstring const& s, TextStyle const& st, float maxWidth) {
        if (s.empty()) {
            TextStyle probe = st;
            auto layout = makeLayout(L"Ay", probe, maxWidth, 100000.f);
            if (!layout) return { 0.f, st.size * 1.2f };
            DWRITE_TEXT_METRICS m {};
            layout->GetMetrics(&m);
            return { 0.f, m.height };
        }
        TextStyle probe = st;
        probe.ellipsis = false;
        auto layout = makeLayout(s, probe, maxWidth, 100000.f);
        if (!layout) return { 0.f, st.size * 1.2f };
        DWRITE_TEXT_METRICS m {};
        layout->GetMetrics(&m);
        return { m.width, m.height };
    }

    float Ui::prefixWidth(std::wstring const& s, size_t count, TextStyle const& st) {
        if (s.empty() || count == 0) return 0.f;
        TextStyle probe = st;
        probe.ellipsis = false;
        auto layout = makeLayout(s, probe, 100000.f, 100000.f);
        if (!layout) return 0.f;
        float x = 0.f, y = 0.f;
        DWRITE_HIT_TEST_METRICS m {};
        layout->HitTestTextPosition(static_cast<UINT32>(std::min(count, s.size())), FALSE, &x, &y, &m);
        return x;
    }

    // ======================================================================================== glyphs
    void Ui::glyph(Glyph gl, Rect const& box, Col c, float weight, Col knockout) {
        if (c.a <= 0.001f) return;
        const float sz = std::min(box.getWidth(), box.getHeight());
        const float cx = box.centerX(), cy = box.centerY();
        const float t = std::max(1.2f, sz * 0.115f * weight);
        auto L = [&](float x0, float y0, float x1, float y1, float th) {
            line({ cx + x0 * sz, cy + y0 * sz }, { cx + x1 * sz, cy + y1 * sz }, c, th, true);
        };

        switch (gl) {
        case Glyph::ChevronRight:
            L(-0.16f, -0.32f, 0.16f, 0.f, t);
            L(0.16f, 0.f, -0.16f, 0.32f, t);
            break;
        case Glyph::ChevronLeft:
            L(0.16f, -0.32f, -0.16f, 0.f, t);
            L(-0.16f, 0.f, 0.16f, 0.32f, t);
            break;
        case Glyph::ChevronDown:
            L(-0.32f, -0.16f, 0.f, 0.16f, t);
            L(0.f, 0.16f, 0.32f, -0.16f, t);
            break;
        case Glyph::ChevronUp:
            L(-0.32f, 0.16f, 0.f, -0.16f, t);
            L(0.f, -0.16f, 0.32f, 0.16f, t);
            break;
        case Glyph::ChevronUpDown:
            L(-0.22f, -0.10f, 0.f, -0.32f, t);
            L(0.f, -0.32f, 0.22f, -0.10f, t);
            L(-0.22f, 0.10f, 0.f, 0.32f, t);
            L(0.f, 0.32f, 0.22f, 0.10f, t);
            break;
        case Glyph::Check:
            L(-0.34f, 0.02f, -0.10f, 0.27f, t * 1.15f);
            L(-0.10f, 0.27f, 0.36f, -0.30f, t * 1.15f);
            break;
        case Glyph::XMark:
            L(-0.28f, -0.28f, 0.28f, 0.28f, t);
            L(0.28f, -0.28f, -0.28f, 0.28f, t);
            break;
        case Glyph::Plus:
            L(-0.32f, 0.f, 0.32f, 0.f, t);
            L(0.f, -0.32f, 0.f, 0.32f, t);
            break;
        case Glyph::Magnifier: {
            circleStroke({ cx - 0.07f * sz, cy - 0.07f * sz }, 0.27f * sz, c, t);
            L(0.13f, 0.13f, 0.36f, 0.36f, t * 1.1f);
        } break;
        case Glyph::Gear: {
            ComPtr<ID2D1PathGeometry> geo;
            if (!g.d2d || FAILED(g.d2d->CreatePathGeometry(geo.GetAddressOf()))) break;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(geo->Open(sink.GetAddressOf()))) break;
            sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
            const float rOut = 0.47f * sz, rIn = 0.37f * sz, rHole = 0.17f * sz;
            bool first = true;
            for (int i = 0; i < 8; ++i) {
                float base = rad(i * 45.f);
                const float angles[4] = { -15.f, -9.f, 9.f, 15.f };
                const float radii[4] = { rIn, rOut, rOut, rIn };
                for (int k = 0; k < 4; ++k) {
                    float a = base + rad(angles[k]);
                    D2D1_POINT_2F p = pt(cx + std::cos(a) * radii[k], cy + std::sin(a) * radii[k]);
                    if (first) {
                        sink->BeginFigure(p, D2D1_FIGURE_BEGIN_FILLED);
                        first = false;
                    } else {
                        sink->AddLine(p);
                    }
                }
            }
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            const int hn = 28;
            for (int i = 0; i < hn; ++i) {
                float a = rad(360.f * static_cast<float>(i) / static_cast<float>(hn));
                D2D1_POINT_2F p = pt(cx + std::cos(a) * rHole, cy + std::sin(a) * rHole);
                if (i == 0) {
                    sink->BeginFigure(p, D2D1_FIGURE_BEGIN_FILLED);
                } else {
                    sink->AddLine(p);
                }
            }
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            setColor(c);
            g.ctx->FillGeometry(geo.Get(), g.brush);
        } break;
        case Glyph::Sliders: {
            const float ys[3] = { -0.28f, 0.f, 0.28f };
            const float ks[3] = { 0.14f, -0.16f, 0.2f };
            for (int i = 0; i < 3; ++i) {
                L(-0.42f, ys[i], 0.42f, ys[i], t * 0.8f);
                circle({ cx + ks[i] * sz, cy + ys[i] * sz }, 0.115f * sz, c);
                if (knockout.a > 0.f) circle({ cx + ks[i] * sz, cy + ys[i] * sz }, 0.06f * sz, knockout);
            }
        } break;
        case Glyph::Grid: {
            float s = 0.34f * sz, gap = 0.10f * sz;
            for (int ix = 0; ix < 2; ++ix) {
                for (int iy = 0; iy < 2; ++iy) {
                    float x0 = cx - gap * 0.5f - s + ix * (s + gap);
                    float y0 = cy - gap * 0.5f - s + iy * (s + gap);
                    fillSmooth(Rect(x0, y0, x0 + s, y0 + s), 0.09f * sz, c);
                }
            }
        } break;
        case Glyph::Gamepad: {
            Rect body(cx - 0.46f * sz, cy - 0.27f * sz, cx + 0.46f * sz, cy + 0.27f * sz);
            fillSmooth(body, 0.26f * sz, c);
            Col k = knockout.a > 0.f ? knockout : Col(0.f, 0.f, 0.f, 0.f);
            if (k.a > 0.f) {
                line({ cx - 0.27f * sz, cy }, { cx - 0.11f * sz, cy }, k, 0.075f * sz, true);
                line({ cx - 0.19f * sz, cy - 0.08f * sz }, { cx - 0.19f * sz, cy + 0.08f * sz }, k, 0.075f * sz, true);
                circle({ cx + 0.14f * sz, cy + 0.045f * sz }, 0.05f * sz, k);
                circle({ cx + 0.26f * sz, cy - 0.045f * sz }, 0.05f * sz, k);
            }
        } break;
        case Glyph::Widgets: {
            float s = 0.5f * sz;
            Rect back(cx - 0.40f * sz, cy - 0.38f * sz, cx - 0.40f * sz + s, cy - 0.38f * sz + s);
            Rect front(cx - 0.10f * sz, cy - 0.12f * sz, cx - 0.10f * sz + s, cy - 0.12f * sz + s);
            strokeSmooth(back, 0.11f * sz, c, std::max(1.2f, 0.075f * sz * weight));
            fillSmooth(front, 0.11f * sz, c);
        } break;
        case Glyph::Play: {
            ComPtr<ID2D1PathGeometry> geo;
            if (!g.d2d || FAILED(g.d2d->CreatePathGeometry(geo.GetAddressOf()))) break;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(geo->Open(sink.GetAddressOf()))) break;
            sink->BeginFigure(pt(cx - 0.17f * sz, cy - 0.26f * sz), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(pt(cx - 0.17f * sz, cy + 0.26f * sz));
            sink->AddLine(pt(cx + 0.27f * sz, cy));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            setColor(c);
            g.ctx->FillGeometry(geo.Get(), g.brush);
            g.ctx->DrawGeometry(geo.Get(), g.brush, 0.07f * sz, roundStroke_.Get());
        } break;
        case Glyph::Pause: {
            float w = 0.15f * sz, h = 0.5f * sz, gap = 0.12f * sz;
            fillSmooth(Rect(cx - gap - w, cy - h * 0.5f, cx - gap, cy + h * 0.5f), w * 0.3f, c);
            fillSmooth(Rect(cx + gap, cy - h * 0.5f, cx + gap + w, cy + h * 0.5f), w * 0.3f, c);
        } break;
        case Glyph::StepBack:
        case Glyph::StepForward: {
            const float dir = gl == Glyph::StepForward ? 1.f : -1.f;
            ComPtr<ID2D1PathGeometry> geo;
            if (!g.d2d || FAILED(g.d2d->CreatePathGeometry(geo.GetAddressOf()))) break;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(geo->Open(sink.GetAddressOf()))) break;
            sink->BeginFigure(pt(cx - dir * 0.24f * sz, cy - 0.24f * sz), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(pt(cx - dir * 0.24f * sz, cy + 0.24f * sz));
            sink->AddLine(pt(cx + dir * 0.16f * sz, cy));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            setColor(c);
            g.ctx->FillGeometry(geo.Get(), g.brush);
            g.ctx->DrawGeometry(geo.Get(), g.brush, 0.06f * sz, roundStroke_.Get());
            L(dir * 0.28f, -0.25f, dir * 0.28f, 0.25f, 0.075f * sz);
        } break;
        case Glyph::Record:
            circle({ cx, cy }, 0.3f * sz, c);
            break;
        case Glyph::Stop:
            fillSmooth(Rect(cx - 0.24f * sz, cy - 0.24f * sz, cx + 0.24f * sz, cy + 0.24f * sz), 0.07f * sz, c);
            break;
        case Glyph::Flag: {
            L(-0.22f, -0.36f, -0.22f, 0.38f, t * 0.9f);
            ComPtr<ID2D1PathGeometry> geo;
            if (!g.d2d || FAILED(g.d2d->CreatePathGeometry(geo.GetAddressOf()))) break;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(geo->Open(sink.GetAddressOf()))) break;
            sink->BeginFigure(pt(cx - 0.22f * sz, cy - 0.34f * sz), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(pt(cx + 0.34f * sz, cy - 0.16f * sz));
            sink->AddLine(pt(cx - 0.22f * sz, cy + 0.04f * sz));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            setColor(c);
            g.ctx->FillGeometry(geo.Get(), g.brush);
        } break;
        case Glyph::Camera: {
            Rect body(cx - 0.44f * sz, cy - 0.26f * sz, cx + 0.44f * sz, cy + 0.30f * sz);
            fillSmooth(body, 0.1f * sz, c);
            fillSmooth(Rect(cx - 0.16f * sz, cy - 0.38f * sz, cx + 0.1f * sz, cy - 0.24f * sz), 0.04f * sz, c);
            Col k = knockout.a > 0.f ? knockout : Col(0.f, 0.f, 0.f, 0.f);
            if (k.a > 0.f) circle({ cx, cy + 0.03f * sz }, 0.15f * sz, k);
        } break;
        }
    }

    void Ui::iconTile(Rect const& r, Col base, std::wstring const& monogram) {
        float rad = r.getWidth() * 0.2237f;
        fillSmooth(r, rad, base);
        // subtle top sheen like the Settings app tiles
        pushClipRRect(r, rad);
        rrectGradient(Rect(r.left, r.top, r.right, r.top + r.getHeight() * 0.55f), 0.f, Col(1.f, 1.f, 1.f, 0.16f),
                      Col(1.f, 1.f, 1.f, 0.f));
        popClipRRect();
        TextStyle st(Font::Semibold, r.getHeight() * 0.52f, Col(1.f, 1.f, 1.f, 1.f), HAlign::Center, VAlign::Center);
        st.ellipsis = false;
        text(r, monogram, st);
    }

    void Ui::iconTile(Rect const& r, Col base, Glyph gl) {
        float rad = r.getWidth() * 0.2237f;
        fillSmooth(r, rad, base);
        pushClipRRect(r, rad);
        rrectGradient(Rect(r.left, r.top, r.right, r.top + r.getHeight() * 0.55f), 0.f, Col(1.f, 1.f, 1.f, 0.16f),
                      Col(1.f, 1.f, 1.f, 0.f));
        popClipRRect();
        glyph(gl, inset(r, r.getWidth() * 0.18f, r.getHeight() * 0.18f), Col(1.f, 1.f, 1.f, 1.f), 1.05f, base);
    }

    void Ui::materialPill(Rect const& r, float radius) {
        shadow(r, radius, S(16.f), S(6.f), pal.shadow);
        fillSmooth(r, radius, pal.material);
        strokeSmooth(r, radius, pal.materialBorder, std::max(1.f, scale * 0.75f));
    }

    // ======================================================================================== controls
    bool Ui::toggle(uint64_t id, Rect const& r, bool& value, bool enabled) {
        const float w = r.getWidth(), h = r.getHeight();
        auto cr = click(id, r, enabled);
        float on = anim(id, value ? 1.f : 0.f, 0.075f);
        float press = anim(id ^ 0x7777ull, (cr.pressed && enabled) ? 1.f : 0.f, 0.05f);

        float alpha = enabled ? 1.f : 0.5f;
        Col track = mix(pal.switchOff, pal.green, on);
        rrect(r, h * 0.5f, mulAlpha(track, alpha));

        float pad = h * (2.f / 31.f);
        float thumbD = h - pad * 2.f;
        float thumbW = thumbD + press * thumbD * 0.18f;
        float left = (r.left + pad) + on * ((r.right - pad - thumbW) - (r.left + pad));
        Rect thumb(left, r.top + pad, left + thumbW, r.bottom - pad);
        shadow(thumb, thumbD * 0.5f, h * 0.16f, h * 0.07f, Col(0.f, 0.f, 0.f, 0.22f * alpha));
        rrect(thumb, thumbD * 0.5f, mulAlpha(pal.thumb, alpha));
        (void)w;

        if (cr.clicked) {
            value = !value;
            return true;
        }
        return false;
    }

    bool Ui::slider(uint64_t id, Rect const& track, float& value, float lo, float hi, float step) {
        const float trackH = track.getHeight();
        const float thumbD = S(28.f);
        Rect hitR(track.left - thumbD * 0.5f, track.centerY() - thumbD * 0.9f, track.right + thumbD * 0.5f,
                  track.centerY() + thumbD * 0.9f);
        auto d = drag(id, hitR);
        bool changed = false;
        if ((d.active || d.began) && track.getWidth() > 1.f) {
            float t = clamp01((mouse().x - track.left) / track.getWidth());
            float nv = lo + t * (hi - lo);
            if (step > 0.f) nv = lo + std::round((nv - lo) / step) * step;
            nv = std::clamp(nv, std::min(lo, hi), std::max(lo, hi));
            if (nv != value) {
                value = nv;
                changed = true;
            }
        }

        float t = hi > lo ? clamp01((value - lo) / (hi - lo)) : 0.f;
        float vis = anim(id ^ 0xA5A5ull, t, d.active ? 0.015f : 0.06f);
        rrect(track, trackH * 0.5f, pal.tertiaryFill);
        float x = track.left + track.getWidth() * vis;
        if (x - track.left > 0.5f) {
            rrect(Rect(track.left, track.top, std::max(track.left + trackH, x), track.bottom), trackH * 0.5f,
                  pal.accent);
        }
        float grow = anim(id ^ 0x3C3Cull, d.active ? 1.f : 0.f, 0.06f);
        float r = thumbD * 0.5f * (1.f + 0.04f * grow);
        Vec2 center = { x, track.centerY() };
        shadow(Rect(center.x - r, center.y - r, center.x + r, center.y + r), r, S(7.f), S(2.5f),
               Col(0.f, 0.f, 0.f, 0.28f));
        circle(center, r, pal.thumb);
        return changed;
    }

    int Ui::segmented(uint64_t id, Rect const& r, std::vector<std::wstring> const& labels, int selected) {
        const int n = static_cast<int>(labels.size());
        if (n == 0) return selected;
        const float h = r.getHeight();
        const float pad = S(2.f);
        rrect(r, S(9.f), pal.tertiaryFill);

        const float segW = (r.getWidth() - pad * 2.f) / static_cast<float>(n);
        float selPos = anim(id, static_cast<float>(selected), 0.07f);

        // dividers (hidden next to the selected segment)
        for (int i = 1; i < n; ++i) {
            float dist = std::fabs(selPos - (static_cast<float>(i) - 0.5f));
            float a = clamp01(dist - 0.55f) * 1.6f;
            if (a <= 0.01f) continue;
            float x = r.left + pad + segW * static_cast<float>(i);
            fillRect(Rect(x, r.top + h * 0.25f, x + std::max(1.f, scale * 0.5f), r.bottom - h * 0.25f),
                     mulAlpha(pal.opaqueSeparator, std::min(1.f, a) * 0.8f));
        }

        Rect thumb(r.left + pad + selPos * segW, r.top + pad, r.left + pad + selPos * segW + segW, r.bottom - pad);
        shadow(thumb, S(7.f), S(5.f), S(1.5f), Col(0.f, 0.f, 0.f, pal.dark ? 0.30f : 0.14f));
        rrect(thumb, S(7.f), pal.dark ? rgb(99, 99, 102) : rgb(255, 255, 255));

        int result = selected;
        for (int i = 0; i < n; ++i) {
            Rect seg(r.left + pad + segW * static_cast<float>(i), r.top, r.left + pad + segW * static_cast<float>(i + 1),
                     r.bottom);
            auto cr = click(id ^ (0x100ull + static_cast<uint64_t>(i)), seg);
            if (cr.clicked) result = i;
            bool sel = i == selected;
            TextStyle st(sel ? Font::Semibold : Font::Medium, S(13.f), pal.label, HAlign::Center, VAlign::Center);
            text(inset(seg, S(4.f), 0.f), labels[static_cast<size_t>(i)], st);
        }
        return result;
    }

    bool Ui::button(uint64_t id, Rect const& r, std::wstring const& label, ButtonKind kind, float fontPt,
                    bool enabled) {
        auto cr = click(id, r, enabled);
        float press = anim(id ^ 0x55ull, (cr.pressed ? 1.f : 0.f), 0.04f);
        float hover = anim(id ^ 0x66ull, (cr.hovered ? 1.f : 0.f), 0.08f);
        float alpha = enabled ? 1.f : 0.45f;
        const float rad = r.getHeight() * 0.5f;

        Col fg = pal.accent;
        switch (kind) {
        case ButtonKind::Filled:
            rrect(r, rad, mulAlpha(mix(pal.accent, Col(1.f, 1.f, 1.f, pal.accent.a), 0.10f * hover), alpha * (1.f - 0.25f * press)));
            fg = Col(1.f, 1.f, 1.f, 1.f);
            break;
        case ButtonKind::Tinted:
            rrect(r, rad, withAlpha(pal.accent, (0.15f + 0.06f * hover + 0.08f * press) * alpha));
            break;
        case ButtonKind::Gray:
            rrect(r, rad, mulAlpha(mix(pal.quaternaryFill, pal.fill, 0.5f * hover + 0.5f * press), alpha));
            break;
        case ButtonKind::Plain:
            break;
        case ButtonKind::Destructive:
            fg = pal.red;
            break;
        case ButtonKind::Material:
            materialPill(r, rad);
            fg = pal.label;
            break;
        }
        float fa = alpha * (kind == ButtonKind::Plain || kind == ButtonKind::Destructive ? (1.f - 0.5f * press) : 1.f);
        TextStyle st(Font::Semibold, S(fontPt), mulAlpha(fg, fa), HAlign::Center, VAlign::Center);
        text(inset(r, S(6.f), 0.f), label, st);
        return cr.clicked;
    }

    bool Ui::iconButton(uint64_t id, Rect const& r, Glyph gl, bool filledCircle) {
        auto cr = click(id, r);
        float press = anim(id ^ 0x55ull, cr.pressed ? 1.f : 0.f, 0.04f);
        float hover = anim(id ^ 0x66ull, cr.hovered ? 1.f : 0.f, 0.08f);
        Vec2 c = { r.centerX(), r.centerY() };
        float rad = std::min(r.getWidth(), r.getHeight()) * 0.5f;
        if (filledCircle) {
            circle(c, rad, mix(pal.fill, pal.secondaryFill, 0.f));
            if (hover > 0.01f || press > 0.01f) circle(c, rad, withAlpha(pal.label, 0.06f * hover + 0.08f * press));
        }
        glyph(gl, inset(r, rad * 0.55f, rad * 0.55f), mulAlpha(pal.secondaryLabel, 1.f), 1.1f);
        return cr.clicked;
    }

    bool Ui::searchField(uint64_t id, Rect const& r, std::wstring const& txt, std::wstring const& placeholder,
                         bool focused, float caretWidthPx, bool caretVisible, bool* clearClicked) {
        if (clearClicked) *clearClicked = false;
        const float h = r.getHeight();
        rrect(r, S(10.f), pal.tertiaryFill);

        Rect icon(r.left + S(9.f), r.top + h * 0.22f, r.left + S(9.f) + h * 0.56f, r.bottom - h * 0.22f);
        glyph(Glyph::Magnifier, icon, pal.secondaryLabel, 0.95f);

        float textLeft = icon.right + S(5.f);
        float textRight = r.right - S(10.f);
        bool clearVisible = !txt.empty();
        Rect clearR(r.right - h * 0.85f, r.top, r.right - S(2.f), r.bottom);
        if (clearVisible) {
            textRight = clearR.left;
            auto cc = click(id ^ 0xC1EA9ull, clearR);
            Vec2 c = { clearR.centerX(), clearR.centerY() };
            circle(c, h * 0.2f, mulAlpha(pal.secondaryLabel, 0.9f - 0.3f * (cc.pressed ? 1.f : 0.f)));
            glyph(Glyph::XMark, Rect(c.x - h * 0.1f, c.y - h * 0.1f, c.x + h * 0.1f, c.y + h * 0.1f),
                  pal.dark ? rgb(60, 60, 62) : rgb(255, 255, 255), 1.3f);
            if (cc.clicked && clearClicked) *clearClicked = true;
        }

        Rect textR(textLeft, r.top, textRight, r.bottom);
        TextStyle st(Font::Regular, S(17.f), pal.label, HAlign::Leading, VAlign::Center);
        if (txt.empty()) {
            st.color = pal.secondaryLabel;
            text(textR, placeholder, st);
        } else {
            text(textR, txt, st);
        }
        if (focused && caretVisible) {
            float cx = textLeft + caretWidthPx;
            fillRect(Rect(cx, r.top + h * 0.2f, cx + std::max(1.5f, scale * 1.5f), r.bottom - h * 0.2f), pal.accent);
        }
        return click(id, r).clicked;
    }

    bool Ui::textField(uint64_t id, Rect const& r, std::wstring const& txt, bool focused, size_t caret,
                       bool caretVisible, TextStyle st, float radius) {
        rrect(r, radius, pal.tertiaryFill);
        if (focused) strokeSmooth(r, radius, withAlpha(pal.accent, 0.9f), std::max(1.5f, scale * 1.5f));
        Rect inner = inset(r, S(9.f), 0.f);
        text(inner, txt, st);
        if (focused && caretVisible) {
            float cx;
            if (st.h == HAlign::Center) {
                float full = textWidth(txt, st);
                cx = inner.centerX() - full * 0.5f + prefixWidth(txt, caret, st);
            } else if (st.h == HAlign::Trailing) {
                float full = textWidth(txt, st);
                cx = inner.right - full + prefixWidth(txt, caret, st);
            } else {
                cx = inner.left + prefixWidth(txt, caret, st);
            }
            float hh = st.size * 1.05f;
            fillRect(Rect(cx, r.centerY() - hh * 0.5f, cx + std::max(1.5f, scale * 1.5f), r.centerY() + hh * 0.5f),
                     pal.accent);
        }
        return click(id, r).clicked;
    }

    void Ui::scrollIndicator(uint64_t id, Rect const& vp, float scroll, float maxScroll, bool scrolling) {
        if (maxScroll <= 1.f) return;
        float a = anim(id ^ 0x5C0Full, scrolling ? 1.f : 0.f, scrolling ? 0.04f : 0.35f);
        if (a < 0.01f) return;
        float h = vp.getHeight();
        float thumbH = std::max(S(36.f), h * (h / (h + maxScroll)));
        float y = vp.top + (h - thumbH) * clamp01(scroll / maxScroll);
        float w = S(3.f);
        Rect r(vp.right - w - S(3.f), y, vp.right - S(3.f), y + thumbH);
        rrect(r, w * 0.5f, withAlpha(pal.label, 0.38f * a));
    }

} // namespace ios
