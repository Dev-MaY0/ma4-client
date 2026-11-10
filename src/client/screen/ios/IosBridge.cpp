#include "pch.h"
#include "IosBridge.h"
#include "IosShell.h"
#include "client/Ma4.h"
#include "client/render/Renderer.h"

namespace ios {

    Gfx gameGfx() {
        auto& rend = Ma4::getRenderer();
        Gfx gfx;
        gfx.ctx = rend.getDeviceContext();
        gfx.brush = rend.getSolidBrush();
        gfx.dwrite = rend.getDWriteFactory();
        gfx.d2d = rend.getFactory();
        gfx.fonts[static_cast<int>(Font::Light)] = rend.getTextFormat(Renderer::FontSelection::PrimaryLight);
        gfx.fonts[static_cast<int>(Font::Regular)] = rend.getTextFormat(Renderer::FontSelection::PrimaryRegular);
        gfx.fonts[static_cast<int>(Font::Medium)] = rend.getTextFormat(Renderer::FontSelection::PrimaryMedium);
        gfx.fonts[static_cast<int>(Font::Semibold)] = rend.getTextFormat(Renderer::FontSelection::PrimarySemibold);
        gfx.fonts[static_cast<int>(Font::Bold)] = rend.getTextFormat(Renderer::FontSelection::PrimaryBold);
        return gfx;
    }

    Palette gamePalette() {
        auto accent = d2d::Color(Ma4::get().getAccentColor().getMainColor());
        return Palette::make(Ma4::get().isLightAppearance() ? Appearance::Light : Appearance::Dark, accent);
    }

    float gameScale() {
        auto ss = Ma4::getRenderer().getScreenSize();
        return Shell::computeScale(Rect(0.f, 0.f, ss.width, ss.height), Ma4::get().getUiScale());
    }

    Ui& beginGameFrame(Input const& input) {
        Input in = input;
        in.dt = Ma4::getRenderer().getDeltaTime() * 0.017f;
        auto& ui = Ui::get();
        ui.beginFrame(gameGfx(), in, gamePalette(), gameScale(),
                      Ma4::get().getL10nData().isSelectedLanguageRightToLeft());
        return ui;
    }

} // namespace ios
