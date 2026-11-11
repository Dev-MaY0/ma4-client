#pragma once
// Liquid Glass finish for HUD plates (text modules, keystrokes, editor labels).
#include "client/screen/ios/IosUi.h"
#include "util/DrawContext.h"

#include <algorithm>

namespace hud {
    /// Call after the plate was filled: adds the glass sheen and specular rim on the Direct2D renderer. With the
    /// Minecraft renderer (no gradients available) it falls back to the plain light edge used before.
    inline void glassPlate(DrawUtil& dc, d2d::Rect const& rc, float radius, float plateAlpha) {
        float k = std::min(1.f, plateAlpha * 2.f);
        if (k <= 0.01f) return;
        if (!dc.isMinecraft() && dc.renderType == DrawUtil::RenderType::D2D) {
            auto* d2 = static_cast<D2DUtil*>(&dc);
            if (d2->ctx) {
                ios::drawGlassPlate(d2->ctx, rc, radius, k);
                return;
            }
        }
        dc.drawRoundedRectangle(rc, d2d::Color(1.f, 1.f, 1.f, 0.10f * k), radius, 1.f, DrawUtil::OutlinePosition::Inside);
    }
} // namespace hud
