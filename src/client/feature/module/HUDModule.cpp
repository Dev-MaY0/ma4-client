#include "pch.h"
#include "HUDModule.h"
#include <client/Ma4.h>

// ---- HUD editor chrome (iOS style selection frames) ----
namespace {
    d2d::Color accentColor() {
        return d2d::Color(Ma4::get().getAccentColor().getMainColor());
    }
}

void HUDModule::renderSelected(DrawUtil& ctx) {
    auto rc = this->getRect();
    float rad = std::min(14.f, std::min(rc.getWidth(), rc.getHeight()) * 0.5f);
    auto accent = accentColor();
    ctx.fillRoundedRectangle(rc, accent.asAlpha(0.18f), rad);
    ctx.drawRoundedRectangle(rc, accent.asAlpha(1.f), rad, 2.f, DrawUtil::OutlinePosition::Outside);
}

void HUDModule::renderFrame(DrawUtil& ctx) {
    auto rc = this->getRect();
    float rad = std::min(14.f, std::min(rc.getWidth(), rc.getHeight()) * 0.5f);
    auto accent = accentColor();
    ctx.fillRoundedRectangle(rc, accent.asAlpha(0.09f), rad);
    ctx.drawRoundedRectangle(rc, accent.asAlpha(0.55f), rad, 1.5f, DrawUtil::OutlinePosition::Outside);
}

void HUDModule::renderPost(DrawUtil& ctx) {
    auto rc = this->getRect();
    std::wstring name = this->getDisplayName();
    const float size = 14.f;
    Vec2 ts = ctx.getTextSize(name, Renderer::FontSelection::PrimarySemibold, size, false, false);
    float h = ts.y + 8.f;
    float w = ts.x + 20.f;
    float top = rc.top - h - 6.f;
    if (top < 2.f) top = rc.bottom + 6.f;
    d2d::Rect label = { rc.centerX() - w * 0.5f, top, rc.centerX() + w * 0.5f, top + h };
    ctx.fillRoundedRectangle(label, d2d::Color(0.11f, 0.11f, 0.12f, 0.86f), h * 0.5f);
    ctx.drawText(label, name, d2d::Color(1.f, 1.f, 1.f, 1.f), Renderer::FontSelection::PrimarySemibold, size,
                 DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
}

void HUDModule::afterLoadConfig() {
    loadStoredPosition();
}

void HUDModule::loadStoredPosition() {
    auto ss = SDK::ClientInstance::get()->getGuiData()->screenSize;
    auto& sp = std::get<Vec2Value>(this->storedPos);
    setPos({ sp.x * ss.x, sp.y * ss.y });
}

void HUDModule::storePos(Vec2 const& ss) {
    auto& vec = std::get<Vec2Value>(storedPos);
    vec.x = rect.left / ss.x;
    vec.y = rect.top / ss.y;
}

d2d::Rect HUDModule::getRect() {
    return { rect.left, rect.top, rect.left + ((rect.getWidth()) * getScale()),
             rect.top + ((rect.getHeight()) * getScale()) };
}

void HUDModule::loadConfig(SettingGroup& resolvedGroup) {
    Module::loadConfig(resolvedGroup);
}
