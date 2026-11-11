#include "IosUi.h"

#include <algorithm>
#include <cmath>

// Liquid Glass (iOS 26) layer of the toolkit: springs with overshoot, interactive press bounce, the glass surface
// itself (shadow + frosted backing + lit sheen + specular rim), merging "container" shapes, glass buttons and the
// scroll edge effect. The SwiftUI gallery this is modelled on uses `glassEffect(.regular / .clear)`, `.interactive()`,
// `.tint()`, `GlassEffectContainer` and `scrollEdgeEffectStyle`; none of that exists in Direct2D, so each piece is
// rebuilt from gradients, strokes and layered shadows. The frosted backing replaces the real backdrop blur: the
// window behind the menu is already blurred by the host, controls that float over scrolling content get a backing and
// a scroll edge fade so text underneath does not fight the glass.

namespace ios {

    namespace {
        constexpr float kPi = 3.14159265358979f;

        D2D1_POINT_2F pt(float x, float y) {
            return D2D1::Point2F(x, y);
        }

        D2D1_GRADIENT_STOP stop(float pos, Col c) {
            return D2D1_GRADIENT_STOP { pos, c.get() };
        }

        Col white(float a) {
            return Col(1.f, 1.f, 1.f, a);
        }

        Col black(float a) {
            return Col(0.f, 0.f, 0.f, a);
        }
    } // namespace

    // ======================================================================================== springs
    float Ui::spring(uint64_t id, float target, float response, float damping) {
        auto it = springs_.find(id);
        if (it == springs_.end()) {
            springs_[id] = { target, 0.f };
            return target;
        }
        Anim& a = it->second;
        response = std::max(0.05f, response);
        const float omega = 2.f * kPi / response;
        const float k = omega * omega;
        const float c = 2.f * damping * omega;
        float remaining = dt;
        while (remaining > 1.0e-6f) {
            float h = std::min(remaining, 1.f / 240.f);
            float acc = k * (target - a.v) - c * a.vel;
            a.vel += acc * h;
            a.v += a.vel * h;
            remaining -= h;
        }
        if (std::fabs(a.v - target) < 5.0e-4f && std::fabs(a.vel) < 5.0e-3f) {
            a.v = target;
            a.vel = 0.f;
        }
        return a.v;
    }

    float Ui::springVelocity(uint64_t id) const {
        auto it = springs_.find(id);
        return it == springs_.end() ? 0.f : it->second.vel;
    }

    void Ui::springSet(uint64_t id, float value) {
        springs_[id] = { value, 0.f };
    }

    float Ui::pressSpring(uint64_t id, bool pressed) {
        return spring(id, pressed ? 1.f : 0.f, 0.32f, 0.58f);
    }

    void Ui::pushScale(Rect const& r, float s) {
        D2D1::Matrix3x2F old;
        g.ctx->GetTransform(&old);
        scaleStack_.push_back(old);
        g.ctx->SetTransform(D2D1::Matrix3x2F::Scale(s, s, D2D1::Point2F(r.centerX(), r.centerY())) * D2D1::Matrix3x2F(old));
    }

    void Ui::popScale() {
        if (scaleStack_.empty()) return;
        g.ctx->SetTransform(scaleStack_.back());
        scaleStack_.pop_back();
    }

    // ======================================================================================== brush helpers
    ComPtr<ID2D1LinearGradientBrush> Ui::linearBrush(Vec2 a, Vec2 b, std::vector<D2D1_GRADIENT_STOP> const& stops) {
        ComPtr<ID2D1GradientStopCollection> coll;
        ComPtr<ID2D1LinearGradientBrush> brush;
        if (!g.ctx || stops.empty()) return brush;
        if (FAILED(g.ctx->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()), coll.GetAddressOf()))) {
            return brush;
        }
        g.ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(pt(a.x, a.y), pt(b.x, b.y)), coll.Get(),
                                         brush.GetAddressOf());
        return brush;
    }

    void Ui::fillBrush(Rect const& r, float radius, ID2D1Brush* brush) {
        float w = r.getWidth(), h = r.getHeight();
        if (w <= 0.f || h <= 0.f || !brush) return;
        radius = std::min(radius, std::min(w, h) * 0.5f);
        if (radius < 6.f || radius >= std::min(w, h) * 0.5f - 0.5f) {
            if (radius < 0.5f) {
                g.ctx->FillRectangle(r.get(), brush);
            } else {
                g.ctx->FillRoundedRectangle(D2D1::RoundedRect(r.get(), radius, radius), brush);
            }
            return;
        }
        auto geo = smoothGeometry(w, h, radius, false, 0.f, 0.f);
        if (!geo) {
            g.ctx->FillRoundedRectangle(D2D1::RoundedRect(r.get(), radius, radius), brush);
            return;
        }
        D2D1::Matrix3x2F old;
        g.ctx->GetTransform(&old);
        // the geometry is built at the origin: move it into place and move the brush back so that gradients keep
        // using the absolute coordinates they were created with
        D2D1::Matrix3x2F brushShift = D2D1::Matrix3x2F::Translation(-r.left, -r.top);
        brush->SetTransform(brushShift);
        g.ctx->SetTransform(D2D1::Matrix3x2F::Translation(r.left, r.top) * D2D1::Matrix3x2F(old));
        g.ctx->FillGeometry(geo.Get(), brush);
        g.ctx->SetTransform(old);
        D2D1::Matrix3x2F ident = D2D1::Matrix3x2F::Identity();
        brush->SetTransform(ident);
    }

    void Ui::strokeBrush(Rect const& r, float radius, ID2D1Brush* brush, float thickness) {
        float w = r.getWidth() - thickness, h = r.getHeight() - thickness;
        if (w <= 0.f || h <= 0.f || !brush) return;
        radius = std::max(0.f, std::min(radius - thickness * 0.5f, std::min(w, h) * 0.5f));
        Rect in(r.left + thickness * 0.5f, r.top + thickness * 0.5f, r.right - thickness * 0.5f, r.bottom - thickness * 0.5f);
        if (radius < 6.f || radius >= std::min(w, h) * 0.5f - 0.5f) {
            if (radius < 0.5f) {
                g.ctx->DrawRectangle(in.get(), brush, thickness);
            } else {
                g.ctx->DrawRoundedRectangle(D2D1::RoundedRect(in.get(), radius, radius), brush, thickness);
            }
            return;
        }
        auto geo = smoothGeometry(w, h, radius, false, 0.f, 0.f);
        if (!geo) {
            g.ctx->DrawRoundedRectangle(D2D1::RoundedRect(in.get(), radius, radius), brush, thickness);
            return;
        }
        D2D1::Matrix3x2F old;
        g.ctx->GetTransform(&old);
        D2D1::Matrix3x2F brushShift = D2D1::Matrix3x2F::Translation(-in.left, -in.top);
        brush->SetTransform(brushShift);
        g.ctx->SetTransform(D2D1::Matrix3x2F::Translation(in.left, in.top) * D2D1::Matrix3x2F(old));
        g.ctx->DrawGeometry(geo.Get(), brush, thickness);
        g.ctx->SetTransform(old);
        D2D1::Matrix3x2F ident = D2D1::Matrix3x2F::Identity();
        brush->SetTransform(ident);
    }

    // ======================================================================================== glass surface
    // `geo` (absolute coordinates) is used instead of the rounded rectangle for merged shapes.
    void Ui::glassSurface(Rect const& b, float radius, GlassStyle const& st, ID2D1Geometry* geo) {
        const float w = b.getWidth(), h = b.getHeight();
        if (w <= 1.f || h <= 1.f || !g.ctx) return;
        const float op = clamp01(st.opacity);
        if (op <= 0.003f) return;
        const bool dark = pal.dark;
        const float px = std::max(1.f, scale * 0.75f);
        radius = std::min(radius, std::min(w, h) * 0.5f);

        auto fill = [&](ID2D1Brush* br) {
            if (!br) return;
            if (geo) {
                g.ctx->FillGeometry(geo, br);
            } else {
                fillBrush(b, radius, br);
            }
        };
        auto stroke = [&](ID2D1Brush* br, float t, Rect const& rr, float rad) {
            if (!br) return;
            if (geo) {
                g.ctx->DrawGeometry(geo, br, t);
            } else {
                strokeBrush(rr, rad, br, t);
            }
        };

        // ---- soft drop shadow: one wide ambient layer, one tight contact layer
        if (st.shadow > 0.01f) {
            const float k = st.shadow * op;
            const float base = pal.shadow.a;
            if (!geo) {
                shadow(b, radius, S(18.f), S(7.f), pal.shadow.asAlpha(base * 0.50f * k));
                shadow(b, radius, S(3.5f), S(1.2f), pal.shadow.asAlpha(base * 0.55f * k));
            } else {
                D2D1::Matrix3x2F old;
                g.ctx->GetTransform(&old);
                for (int pass = 0; pass < 2; ++pass) {
                    const float blur = pass == 0 ? S(16.f) : S(3.f);
                    const float dy = pass == 0 ? S(7.f) : S(1.2f);
                    const float aa = base * (pass == 0 ? 0.50f : 0.55f) * k;
                    g.ctx->SetTransform(D2D1::Matrix3x2F::Translation(0.f, dy) * D2D1::Matrix3x2F(old));
                    const int n = 6;
                    for (int i = n; i >= 1; --i) {
                        float t = static_cast<float>(i) / static_cast<float>(n);
                        float fall = 1.f - t;
                        float a = std::min(0.35f, aa * (0.55f / static_cast<float>(n)) * (0.35f + 2.2f * fall * fall));
                        g.brush->SetColor(pal.shadow.asAlpha(a).get());
                        g.ctx->DrawGeometry(geo, g.brush, blur * t * 2.f, roundStroke_.Get());
                    }
                    g.brush->SetColor(pal.shadow.asAlpha(aa * 0.5f).get());
                    g.ctx->FillGeometry(geo, g.brush);
                }
                g.ctx->SetTransform(old);
            }
        }

        // ---- frosted backing (stands in for the backdrop blur)
        if (st.backing > 0.01f) {
            const float ba = (st.clear ? 0.14f : 0.64f) * clamp01(st.backing) * op;
            g.brush->SetColor(pal.glassBacking.asAlpha(ba).get());
            fill(g.brush);
        }

        // ---- sheen: lit at the top, dimmer towards the bottom
        {
            Col top = st.clear ? pal.clearSheenTop : pal.glassSheenTop;
            Col bot = st.clear ? pal.clearSheenBottom : pal.glassSheenBottom;
            top.a *= op;
            bot.a *= op;
            auto br = linearBrush({ b.left, b.top }, { b.left, b.bottom }, { stop(0.f, top), stop(0.5f, mix(top, bot, 0.6f)), stop(1.f, bot) });
            fill(br.Get());
        }

        // ---- tint (`.tint(color)`)
        if (st.tint.a > 0.004f) {
            Col t1 = mix(st.tint, white(st.tint.a), 0.20f);
            Col t2 = Col(st.tint.r * 0.86f, st.tint.g * 0.86f, st.tint.b * 0.86f, st.tint.a * 0.96f);
            t1.a *= op;
            t2.a *= op;
            auto br = linearBrush({ b.left, b.top }, { b.left, b.bottom }, { stop(0.f, t1), stop(1.f, t2) });
            fill(br.Get());
        }

        // ---- interactive feedback (press / hover) lights the glass up
        {
            float lift = clamp01(st.press) * 0.9f + clamp01(st.lift) * 0.4f;
            if (lift > 0.005f) {
                g.brush->SetColor((dark ? white(0.14f * lift * op) : black(0.07f * lift * op)).get());
                fill(g.brush);
            }
        }

        // ---- edge: dark hairline outside, specular gradient rim inside
        g.brush->SetColor(pal.glassEdge.asAlpha(pal.glassEdge.a * op).get());
        stroke(g.brush, px, b, radius);
        {
            Rect rr(b.left + px, b.top + px, b.right - px, b.bottom - px);
            float rad = std::max(0.f, radius - px);
            float k = (st.clear ? 0.8f : 1.f) * op;
            Col a0 = dark ? white(0.62f * k) : white(1.0f * k);
            Col a1 = dark ? white(0.17f * k) : white(0.55f * k);
            Col a2 = dark ? white(0.07f * k) : white(0.35f * k);
            Col a3 = dark ? white(0.38f * k) : white(0.95f * k);
            auto br = linearBrush({ b.left + w * 0.12f, b.top }, { b.right - w * 0.12f, b.bottom },
                                  { stop(0.f, a0), stop(0.28f, a1), stop(0.62f, a2), stop(1.f, a3) });
            stroke(br.Get(), px, rr, rad);
        }

        // ---- specular glint under the top edge
        if (!geo) {
            float gh = std::clamp(h * 0.06f, px * 1.5f, std::max(px * 1.5f, S(2.6f)));
            float gx = std::min(radius * 0.62f, w * 0.30f);
            Rect gr(b.left + gx, b.top + px * 2.f, b.right - gx, b.top + px * 2.f + gh);
            if (gr.getWidth() > 4.f && gr.getHeight() > 0.5f) {
                float ga = (dark ? 0.50f : 0.95f) * (st.clear ? 0.7f : 1.f) * op;
                auto br = linearBrush({ gr.left, gr.top }, { gr.right, gr.top },
                                      { stop(0.f, white(0.f)), stop(0.5f, white(ga)), stop(1.f, white(0.f)) });
                if (br) g.ctx->FillRoundedRectangle(D2D1::RoundedRect(gr.get(), gh * 0.5f, gh * 0.5f), br.Get());
            }
        }
    }

    void Ui::glass(Rect const& r, float radius, GlassStyle const& st) {
        glassSurface(r, radius, st, nullptr);
    }

    void Ui::materialPill(Rect const& r, float radius) {
        glass(r, radius, GlassStyle {});
    }

    // ======================================================================================== merging shapes
    void Ui::glassBlobs(std::vector<Vec2> const& centers, float radius, float spacing, GlassStyle const& st) {
        const size_t n = centers.size();
        if (n == 0 || !g.d2d) return;
        spacing = std::max(1.f, spacing);

        size_t i = 0;
        while (i < n) {
            size_t j = i;
            while (j + 1 < n && centers[j + 1].x - centers[j].x - radius * 2.f < spacing) ++j;
            const size_t m = j - i + 1;
            if (m == 1) {
                Vec2 c = centers[i];
                glass(Rect(c.x - radius, c.y - radius, c.x + radius, c.y + radius), radius, st);
                i = j + 1;
                continue;
            }

            // neck angle per neighbouring pair: pi/2 = fully merged (capsule), 0 = just touching
            std::vector<float> alpha(m - 1);
            for (size_t k = 0; k + 1 < m; ++k) {
                float gap = centers[i + k + 1].x - centers[i + k].x - radius * 2.f;
                float blend = clamp01(1.f - gap / spacing);
                alpha[k] = (kPi * 0.5f) * std::pow(blend, 1.7f);
            }
            const float cy = centers[i].y;
            auto cx = [&](size_t k) { return centers[i + k].x; };
            auto topNeck = [&](size_t k, float a, bool left) {
                // attach point of neck k on circle k (right side, left=false) or circle k+1 (left side, left=true)
                float x = left ? cx(k + 1) - radius * std::cos(a) : cx(k) + radius * std::cos(a);
                return std::pair<float, float>(x, radius * std::sin(a));
            };

            ComPtr<ID2D1PathGeometry> geo;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(g.d2d->CreatePathGeometry(geo.GetAddressOf())) || FAILED(geo->Open(sink.GetAddressOf()))) {
                i = j + 1;
                continue;
            }
            const D2D1_SIZE_F rs = D2D1::SizeF(radius, radius);

            auto neckBezier = [&](float sx, float sy, float ex, float ey, float dirX1, float dirY1, float dirX2, float dirY2) {
                float dist = std::hypot(ex - sx, ey - sy);
                float L = dist / 3.f;
                sink->AddBezier(D2D1::BezierSegment(pt(sx + dirX1 * L, sy + dirY1 * L), pt(ex - dirX2 * L, ey - dirY2 * L), pt(ex, ey)));
            };

            // ---- top, left to right
            auto s0 = topNeck(0, alpha[0], false);
            sink->BeginFigure(pt(s0.first, cy - s0.second), D2D1_FIGURE_BEGIN_FILLED);
            for (size_t k = 0; k + 1 < m; ++k) {
                float a = alpha[k];
                auto p1 = topNeck(k, a, false);
                auto p2 = topNeck(k, a, true);
                neckBezier(p1.first, cy - p1.second, p2.first, cy - p2.second, std::sin(a), std::cos(a), std::sin(a), -std::cos(a));
                if (k + 2 < m) {
                    // over the top of the middle circle to its next neck
                    auto q = topNeck(k + 1, alpha[k + 1], false);
                    sink->AddArc(D2D1::ArcSegment(pt(q.first, cy - q.second), rs, 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                }
            }
            {
                // round the right end: top attach -> bottom attach of the last neck
                auto p2 = topNeck(m - 2, alpha[m - 2], true);
                sink->AddArc(D2D1::ArcSegment(pt(p2.first, cy + p2.second), rs, 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
            }
            // ---- bottom, right to left
            for (size_t k = m - 1; k-- > 0;) {
                float a = alpha[k];
                auto q1 = topNeck(k, a, true);
                auto q2 = topNeck(k, a, false);
                neckBezier(q1.first, cy + q1.second, q2.first, cy + q2.second, -std::sin(a), -std::cos(a), -std::sin(a), std::cos(a));
                if (k > 0) {
                    auto q = topNeck(k - 1, alpha[k - 1], true);
                    sink->AddArc(D2D1::ArcSegment(pt(q.first, cy + q.second), rs, 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                }
            }
            {
                auto p1 = topNeck(0, alpha[0], false);
                sink->AddArc(D2D1::ArcSegment(pt(p1.first, cy - p1.second), rs, 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
            }
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();

            Rect bounds(cx(0) - radius, cy - radius, cx(m - 1) + radius, cy + radius);
            ComPtr<ID2D1Geometry> g2;
            geo.As(&g2);
            glassSurface(bounds, radius, st, g2.Get());
            i = j + 1;
        }
    }

    // ======================================================================================== buttons
    bool Ui::glassButton(uint64_t id, Rect const& r, std::wstring const& label, GlassButtonKind kind, float fontPt,
                         bool enabled) {
        auto cr = click(id, r, enabled);
        float pr = pressSpring(id ^ 0x6B55ull, cr.pressed && enabled);
        float hov = anim(id ^ 0x6B66ull, (cr.hovered && enabled) ? 1.f : 0.f, 0.08f);
        float s = std::clamp(1.f + 0.07f * pr, 0.92f, 1.14f);

        pushScale(r, s);
        GlassStyle gs;
        gs.press = clamp01(pr);
        gs.lift = hov;
        gs.opacity = enabled ? 1.f : 0.5f;
        Col fg = pal.label;
        switch (kind) {
        case GlassButtonKind::Regular:
            break;
        case GlassButtonKind::Prominent:
            gs.tint = withAlpha(pal.accent, 0.93f);
            gs.backing = 0.f;
            fg = white(1.f);
            break;
        case GlassButtonKind::Clear:
            gs.clear = true;
            gs.shadow = 0.5f;
            break;
        }
        glass(r, r.getHeight() * 0.5f, gs);
        TextStyle ts(Font::Semibold, S(fontPt), mulAlpha(fg, enabled ? 1.f : 0.5f), HAlign::Center, VAlign::Center);
        text(inset(r, S(8.f), 0.f), label, ts);
        popScale();
        return cr.clicked;
    }

    bool Ui::glassIconButton(uint64_t id, Rect const& r, Glyph gl, Col tint, float glyphScale, bool enabled) {
        auto cr = click(id, r, enabled);
        float pr = pressSpring(id ^ 0x6B55ull, cr.pressed && enabled);
        float hov = anim(id ^ 0x6B66ull, (cr.hovered && enabled) ? 1.f : 0.f, 0.08f);
        float s = std::clamp(1.f + 0.09f * pr, 0.9f, 1.2f);

        pushScale(r, s);
        GlassStyle gs;
        gs.press = clamp01(pr);
        gs.lift = hov;
        gs.tint = tint;
        gs.opacity = enabled ? 1.f : 0.45f;
        if (tint.a > 0.01f) gs.backing = 0.f;
        const float d = std::min(r.getWidth(), r.getHeight());
        glass(r, d * 0.5f, gs);
        Col fg = tint.a > 0.01f ? white(1.f) : pal.label;
        const float gsz = d * glyphScale;
        glyph(gl, Rect(r.centerX() - gsz * 0.5f, r.centerY() - gsz * 0.5f, r.centerX() + gsz * 0.5f, r.centerY() + gsz * 0.5f),
              mulAlpha(fg, enabled ? 1.f : 0.45f), 1.1f);
        popScale();
        return cr.clicked;
    }

    // ======================================================================================== HUD plates
    void drawGlassPlate(ID2D1DeviceContext* ctx, Rect const& r, float radius, float strength) {
        const float w = r.getWidth(), h = r.getHeight();
        strength = clamp01(strength);
        if (!ctx || w < 2.f || h < 2.f || strength < 0.01f) return;
        radius = std::clamp(radius, 0.f, std::min(w, h) * 0.5f);

        auto brushFor = [&](D2D1_POINT_2F a, D2D1_POINT_2F b, std::vector<D2D1_GRADIENT_STOP> const& stops) {
            ComPtr<ID2D1GradientStopCollection> coll;
            ComPtr<ID2D1LinearGradientBrush> br;
            if (SUCCEEDED(ctx->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()), coll.GetAddressOf()))) {
                ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(a, b), coll.Get(), br.GetAddressOf());
            }
            return br;
        };
        const float k = strength;

        // sheen: lit at the top, a touch of shade at the bottom
        if (auto br = brushFor(pt(r.left, r.top), pt(r.left, r.bottom),
                               { stop(0.f, white(0.17f * k)), stop(0.45f, white(0.03f * k)), stop(0.55f, black(0.f)),
                                 stop(1.f, black(0.10f * k)) })) {
            if (radius < 0.5f) {
                ctx->FillRectangle(r.get(), br.Get());
            } else {
                ctx->FillRoundedRectangle(D2D1::RoundedRect(r.get(), radius, radius), br.Get());
            }
        }

        // specular rim: bright at the top-left and bottom-right, dim in between
        const float t = 1.f;
        Rect in(r.left + t * 0.5f, r.top + t * 0.5f, r.right - t * 0.5f, r.bottom - t * 0.5f);
        if (auto br = brushFor(pt(r.left + w * 0.12f, r.top), pt(r.right - w * 0.12f, r.bottom),
                               { stop(0.f, white(0.46f * k)), stop(0.30f, white(0.12f * k)), stop(0.65f, white(0.06f * k)),
                                 stop(1.f, white(0.28f * k)) })) {
            if (radius < 0.5f) {
                ctx->DrawRectangle(in.get(), br.Get(), t);
            } else {
                float rr = std::max(0.f, radius - t * 0.5f);
                ctx->DrawRoundedRectangle(D2D1::RoundedRect(in.get(), rr, rr), br.Get(), t);
            }
        }
    }

    // ======================================================================================== scroll edge
    void Ui::scrollEdge(Rect const& r, bool top, Col base, float strength, float solid) {
        strength = clamp01(strength);
        if (strength <= 0.005f || r.getHeight() <= 1.f || !g.ctx) return;
        const float a = base.a * strength;
        const float y0 = top ? r.top : r.bottom;
        const float y1 = top ? r.bottom : r.top;
        // `solid` = fraction of the band (from the edge) that stays at full strength before the soft fall-off
        solid = std::clamp(solid, 0.f, 0.9f);
        auto at = [&](float u) { return solid + (1.f - solid) * u; };
        auto br = linearBrush({ r.left, y0 }, { r.left, y1 },
                              { stop(0.f, base.asAlpha(a)), stop(at(0.f), base.asAlpha(a)), stop(at(0.30f), base.asAlpha(a * 0.80f)),
                                stop(at(0.55f), base.asAlpha(a * 0.44f)), stop(at(0.80f), base.asAlpha(a * 0.14f)),
                                stop(1.f, base.asAlpha(0.f)) });
        if (br) g.ctx->FillRectangle(r.get(), br.Get());
    }

} // namespace ios
