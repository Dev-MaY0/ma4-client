#include "pch.h"
#include "Notifications.h"
#include <client/render/Renderer.h>
#include <client/Ma4.h>
#include <client/render/asset/Assets.h>
#include <client/screen/ios/IosBridge.h>
#include <cmath>

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

    // 0 = hidden above the screen, 1 = resting position. The banner drops in on a spring (response 0.46, damping
    // 0.7: a small overshoot, like a system banner) and leaves the same way.
    float opacity = 1.f;
    const bool leaving = dur > maxDur - transitionMs;
    if (leaving) {
        opacity = 1.f - std::clamp((dur - (maxDur - transitionMs)) / transitionMs, 0.f, 1.f);
    } else if (dur < transitionMs) {
        opacity = std::min(1.f, dur / transitionMs * 2.f);
    }

    auto& ui = ios::beginGameFrame();
    static const uint64_t kBannerSpring = ios::Ui::hashId("ma4.banner");
    if (dur < 40.f) ui.springSet(kBannerSpring, 0.f);
    float shown = ui.spring(kBannerSpring, leaving ? 0.f : 1.f, 0.46f, 0.70f);
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
    {
        ios::GlassStyle gs;
        gs.shadow = 1.3f;
        ui.glass(pill, h * 0.5f, gs);
    }
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
