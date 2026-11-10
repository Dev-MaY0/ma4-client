#include "pch.h"
#include "Notifications.h"
#include <client/render/Renderer.h>
#include <client/Ma4.h>
#include <client/render/asset/Assets.h>
#include <client/screen/ios/IosBridge.h>
#include <cmath>

namespace {
    float easeOutBack(float t) {
        const float c1 = 1.1f;
        const float c3 = c1 + 1.f;
        float u = t - 1.f;
        return 1.f + c3 * u * u * u + c1 * u * u;
    }

    float easeInCubic(float t) {
        return t * t * t;
    }
} // namespace

Notifications::Notifications() {
    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&Notifications::onRender, 10);
}

void Notifications::push(std::wstring message) {
    if (!toasts.empty() && toasts.back().message == message) {
        toasts.back().createTime = std::chrono::system_clock::now();
        return;
    }

    toasts.emplace(std::move(message));
}

// iOS style banner: a floating capsule that drops in from the top of the screen.
void Notifications::onRender(Event& evG) {
    if (toasts.empty()) return;
    if (!Ma4::getRenderer().hasInitialized() || !Ma4::getRenderer().getDeviceContext()) return;

    const float transitionMs = 420.f;
    const float stayMs = 2800.f;

    auto& toast = toasts.front();
    if (!toast.init) toast.initialize();

    auto now = std::chrono::system_clock::now();
    float dur = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(now - toast.createTime).count());
    float maxDur = stayMs + transitionMs * 2.f;

    float shown; // 0 = hidden above the screen, 1 = resting position
    float opacity = 1.f;
    if (dur < transitionMs) {
        float t = dur / transitionMs;
        shown = easeOutBack(t);
        opacity = std::min(1.f, t * 2.f);
    } else if (dur > maxDur - transitionMs) {
        float t = std::clamp((dur - (maxDur - transitionMs)) / transitionMs, 0.f, 1.f);
        shown = 1.f - easeInCubic(t);
        opacity = 1.f - t;
    } else {
        shown = 1.f;
    }

    auto& ui = ios::beginGameFrame();
    auto S = [&](float v) { return ui.S(v); };
    auto ss = Ma4::getRenderer().getScreenSize();

    ios::TextStyle ts(ios::Font::Medium, S(17.f), ui.pal.label, ios::HAlign::Leading, ios::VAlign::Center);
    float textW = std::min(ui.textWidth(toast.message, ts) + S(2.f), S(620.f));

    const float h = S(56.f);
    const float iconS = S(34.f);
    float w = S(12.f) + iconS + S(14.f) + textW + S(26.f);
    float x = (ss.width - w) * 0.5f;
    float restY = S(22.f);
    float y = restY + (shown - 1.f) * (h + restY + S(24.f));

    ios::Rect pill(std::round(x), y, std::round(x) + w, y + h);

    ui.pushOpacity(pill, opacity);
    ui.materialPill(pill, h * 0.5f);
    ios::Rect icon(pill.left + S(12.f), pill.centerY() - iconS * 0.5f, pill.left + S(12.f) + iconS,
                   pill.centerY() + iconS * 0.5f);
    if (auto* logo = Ma4::getAssets().ma4Logo.getBitmap()) {
        ui.bitmap(logo, icon);
    } else {
        ui.iconTile(icon, ui.pal.accent, L"m");
    }
    ui.text(ios::Rect(icon.right + S(14.f), pill.top, pill.right - S(18.f), pill.bottom), toast.message, ts);
    ui.popOpacity();
    ui.endFrame();

    if (dur > maxDur) {
        toasts.pop();
    }
}
