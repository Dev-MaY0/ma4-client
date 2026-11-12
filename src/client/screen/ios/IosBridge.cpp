#include "pch.h"
#include "IosBridge.h"
#include "IosShell.h"
#include "client/Ma4.h"
#include "client/render/Renderer.h"

namespace ios {

    namespace {
        // The menu must show text on every machine: if the renderer has no format for a weight (font creation failed
        // or the font family is missing on this PC) one is created here from a family that always exists.
        IDWriteTextFormat* fallbackFormat(IDWriteFactory* factory, Font which) {
            static ComPtr<IDWriteTextFormat> cache[static_cast<int>(Font::Count)];
            static IDWriteFactory* owner = nullptr;
            if (!factory) return nullptr;
            if (owner != factory) {
                for (auto& c : cache) c.Reset();
                owner = factory;
            }
            auto& slot = cache[static_cast<int>(which)];
            if (!slot) {
                static constexpr DWRITE_FONT_WEIGHT weights[static_cast<int>(Font::Count)] = {
                    DWRITE_FONT_WEIGHT_LIGHT, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_WEIGHT_BOLD
                };
                static constexpr const wchar_t* families[] = { L"Segoe UI", L"Arial", L"Tahoma" };
                for (auto* family : families) {
                    if (SUCCEEDED(factory->CreateTextFormat(family, nullptr, weights[static_cast<int>(which)], DWRITE_FONT_STYLE_NORMAL,
                                                            DWRITE_FONT_STRETCH_NORMAL, 10.f, L"en-us", slot.GetAddressOf()))) {
                        break;
                    }
                }
            }
            return slot.Get();
        }
    } // namespace

    Gfx gameGfx() {
        auto& rend = Ma4::getRenderer();
        Gfx gfx;
        gfx.ctx = rend.getDeviceContext();
        gfx.brush = rend.getSolidBrush();
        gfx.dwrite = rend.getDWriteFactory();
        gfx.d2d = rend.getFactory();
        const std::pair<Font, Renderer::FontSelection> fonts[] = {
            { Font::Light, Renderer::FontSelection::PrimaryLight },       { Font::Regular, Renderer::FontSelection::PrimaryRegular },
            { Font::Medium, Renderer::FontSelection::PrimaryMedium },     { Font::Semibold, Renderer::FontSelection::PrimarySemibold },
            { Font::Bold, Renderer::FontSelection::PrimaryBold },
        };
        for (auto const& [font, selection] : fonts) {
            IDWriteTextFormat* fmt = rend.getTextFormat(selection);
            if (!fmt) fmt = fallbackFormat(gfx.dwrite, font);
            gfx.fonts[static_cast<int>(font)] = fmt;
        }
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
