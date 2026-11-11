#include "IosShell.h"
#include "IosShellCommon.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <cstdio>
#include <cstring>

namespace ios {

    namespace {
        // ---- layout constants (iOS points) ----
        constexpr float kWinW = 1040.f;
        constexpr float kWinH = 660.f;
        constexpr float kSidebarW = 252.f; // width of the floating glass sidebar panel
        constexpr float kInset = 10.f;     // gap between the window edge and the floating sidebar
        constexpr float kWinR = 36.f;      // window corner radius; the sidebar radius is concentric (kWinR - kInset)
        constexpr float kModRowH = 58.f;

        constexpr uint64_t kIdOpen = 0x0A110001ull;
        constexpr uint64_t kIdPop = 0x0A110003ull;
        constexpr uint64_t kIdSheet = 0x0A110004ull;
        constexpr uint64_t kIdSearch = 0x0A110005ull;
        constexpr uint64_t kIdOpenSpring = 0x0A110006ull;
        constexpr uint64_t kIdSelTop = 0x0A110007ull;
        constexpr uint64_t kIdSearchFocus = 0x0A110008ull;
        constexpr uint64_t kIdSheetDrag = 0x0A110009ull;
        constexpr uint64_t kIdSheetDragSpring = 0x0A11000Aull;
        constexpr uint64_t kIdPopSpring = 0x0A11000Bull;

        std::wstring widen(std::string const& s) {
            return std::wstring(s.begin(), s.end());
        }

        std::wstring lower(std::wstring s) {
            for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
            return s;
        }

        bool containsLower(std::wstring const& hay, std::wstring const& needleLower) {
            if (needleLower.empty()) return true;
            return lower(hay).find(needleLower) != std::wstring::npos;
        }

        std::wstring formatNumber(float v) {
            wchar_t buf[48];
            if (std::fabs(v) >= 1000.f) {
                swprintf(buf, 48, L"%.0f", v);
            } else {
                swprintf(buf, 48, L"%.3f", v);
                std::wstring s = buf;
                while (!s.empty() && s.back() == L'0') s.pop_back();
                if (!s.empty() && s.back() == L'.') s.pop_back();
                if (s.empty() || s == L"-0") s = L"0";
                return s;
            }
            return buf;
        }

        bool parseNumber(std::wstring const& s, float& out) {
            if (s.empty()) return false;
            for (wchar_t c : s) {
                if (!(c == L'-' || c == L'.' || c == L'+' || (c >= L'0' && c <= L'9'))) return false;
            }
            wchar_t* end = nullptr;
            float v = std::wcstof(s.c_str(), &end);
            if (!end || *end != L'\0' || !std::isfinite(v)) return false;
            out = v;
            return true;
        }

        void hsv2rgb(float h, float s, float v, float& r, float& g, float& b) {
            h = std::fmod(std::max(h, 0.f), 360.f) / 60.f;
            int i = static_cast<int>(std::floor(h));
            float f = h - static_cast<float>(i);
            float p = v * (1.f - s), q = v * (1.f - s * f), t = v * (1.f - s * (1.f - f));
            switch (i % 6) {
            case 0:
                r = v, g = t, b = p;
                break;
            case 1:
                r = q, g = v, b = p;
                break;
            case 2:
                r = p, g = v, b = t;
                break;
            case 3:
                r = p, g = q, b = v;
                break;
            case 4:
                r = t, g = p, b = v;
                break;
            default:
                r = v, g = p, b = q;
                break;
            }
        }

        void rgb2hsv(float r, float g, float b, float& h, float& s, float& v) {
            float mx = std::max({ r, g, b }), mn = std::min({ r, g, b });
            float d = mx - mn;
            v = mx;
            s = mx <= 0.f ? 0.f : d / mx;
            if (d <= 1e-6f) {
                h = 0.f;
            } else if (mx == r) {
                h = 60.f * std::fmod((g - b) / d, 6.f);
            } else if (mx == g) {
                h = 60.f * ((b - r) / d + 2.f);
            } else {
                h = 60.f * ((r - g) / d + 4.f);
            }
            if (h < 0.f) h += 360.f;
        }

        void fillGradient(Ui& ui, Rect const& r, float radius, std::vector<D2D1_GRADIENT_STOP> const& stops,
                          bool vertical) {
            ComPtr<ID2D1GradientStopCollection> coll;
            if (FAILED(ui.ctx()->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()),
                                                              coll.GetAddressOf()))) {
                return;
            }
            ComPtr<ID2D1LinearGradientBrush> brush;
            D2D1_POINT_2F a = vertical ? D2D1::Point2F(r.left, r.top) : D2D1::Point2F(r.left, r.top);
            D2D1_POINT_2F b = vertical ? D2D1::Point2F(r.left, r.bottom) : D2D1::Point2F(r.right, r.top);
            if (FAILED(ui.ctx()->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(a, b), coll.Get(),
                                                           brush.GetAddressOf()))) {
                return;
            }
            radius = std::min(radius, std::min(r.getWidth(), r.getHeight()) * 0.5f);
            if (radius < 0.5f) {
                ui.ctx()->FillRectangle(r.get(), brush.Get());
            } else {
                ui.ctx()->FillRoundedRectangle(D2D1::RoundedRect(r.get(), radius, radius), brush.Get());
            }
        }

        void drawChecker(Ui& ui, Rect const& r, float cell) {
            Col a = ui.pal.dark ? Col(0.30f, 0.30f, 0.32f, 1.f) : Col(0.86f, 0.86f, 0.88f, 1.f);
            Col b = ui.pal.dark ? Col(0.44f, 0.44f, 0.46f, 1.f) : Col(1.f, 1.f, 1.f, 1.f);
            ui.fillRect(r, a);
            int row = 0;
            for (float y = r.top; y < r.bottom; y += cell, ++row) {
                int col = row % 2;
                for (float x = r.left; x < r.right; x += cell, ++col) {
                    if (col % 2 == 0) {
                        ui.fillRect(Rect(x, y, std::min(x + cell, r.right), std::min(y + cell, r.bottom)), b);
                    }
                }
            }
        }

        void drawColorWell(Ui& ui, Vec2 c, float radius, Col col) {
            Rect box(c.x - radius, c.y - radius, c.x + radius, c.y + radius);
            if (col.a < 0.999f) {
                ui.pushClipRRect(box, radius);
                drawChecker(ui, box, radius * 0.5f);
                ui.circle(c, radius, col);
                ui.popClipRRect();
            } else {
                ui.circle(c, radius, col);
            }
            ui.circleStroke(c, radius - ui.S(0.5f), Col(1.f, 1.f, 1.f, ui.pal.dark ? 0.35f : 0.75f), std::max(1.f, ui.S(1.f)));
            ui.circleStroke(c, radius + ui.S(0.5f), Col(0.f, 0.f, 0.f, 0.18f), std::max(1.f, ui.S(0.75f)));
        }

        TextStyle titleStyle(Ui& ui) {
            return TextStyle(Font::Regular, ui.S(17.f), ui.pal.label, HAlign::Leading, VAlign::Center);
        }

        TextStyle subStyle(Ui& ui) {
            return TextStyle(Font::Regular, ui.S(13.f), ui.pal.secondaryLabel, HAlign::Leading, VAlign::Top, true);
        }

        bool useSegmented(SettingRow const& r) {
            if (r.kind != SettingRow::Kind::Choice) return false;
            size_t n = r.options.size();
            if (n < 2 || n > 4) return false;
            for (auto const& o : r.options) {
                if (o.size() > 12) return false;
            }
            return true;
        }

        float labelBlockHeight(Ui& ui, std::wstring const& subtitle, float width) {
            float h = ui.S(22.f);
            if (!subtitle.empty() && width > 8.f) {
                float sh = ui.measure(subtitle, subStyle(ui), width).y;
                sh = std::min(sh, ui.S(13.f) * 1.35f * 3.f);
                h += ui.S(2.f) + sh;
            }
            return h;
        }

        void drawLabel(Ui& ui, Rect const& rc, std::wstring const& title, std::wstring const& subtitle) {
            float blockH = labelBlockHeight(ui, subtitle, rc.getWidth());
            float top = rc.centerY() - blockH * 0.5f;
            Rect tr(rc.left, top, rc.right, top + ui.S(22.f));
            ui.text(tr, title, titleStyle(ui));
            if (!subtitle.empty()) {
                Rect sr(rc.left, tr.bottom + ui.S(2.f), rc.right, rc.centerY() + blockH * 0.5f);
                ui.text(sr, subtitle, subStyle(ui));
            }
        }

        /// The floating glass sidebar panel inside the window.
        Rect sidebarPanel(Ui& ui, Rect const& win) {
            float inset = ui.S(kInset);
            return Rect(win.left + inset, win.top + inset, win.left + inset + ui.S(kSidebarW), win.bottom - inset);
        }
    } // namespace

    // ======================================================================================== lifetime
    Shell::Shell() {
        makeEdit_ = [](int maxChars) {
            return std::make_shared<TextEdit>(maxChars);
        };
    }

    float Shell::computeScale(Rect const& screen, float userScale) {
        float s = std::min(screen.getWidth() * 0.70f / kWinW, screen.getHeight() * 0.74f / kWinH) * userScale;
        return std::clamp(s, 0.5f, 2.6f);
    }

    Rect Shell::computeWindow(Rect const& screen, float scale) {
        float w = kWinW * scale, h = kWinH * scale;
        float x = screen.left + (screen.getWidth() - w) * 0.5f;
        float y = screen.top + (screen.getHeight() - h) * 0.5f;
        return Rect(std::round(x), std::round(y), std::round(x + w), std::round(y + h));
    }

    std::shared_ptr<TextEdit> Shell::searchEdit() {
        if (!search_) search_ = makeEdit_(48);
        return search_;
    }

    std::shared_ptr<TextEdit> Shell::editFor(uint64_t id, int maxChars) {
        auto it = edits_.find(id);
        if (it != edits_.end()) return it->second;
        auto e = makeEdit_(maxChars);
        e->setMaxChars(maxChars);
        edits_[id] = e;
        return e;
    }

    void Shell::blurAll() {
        if (search_) search_->setSelected(false);
        for (auto& [id, e] : edits_) e->setSelected(false);
        if (sheet_.hex) sheet_.hex->setSelected(false);
    }

    bool Shell::textFocused() const {
        if (search_ && search_->isSelected()) return true;
        for (auto const& [id, e] : edits_) {
            if (e->isSelected()) return true;
        }
        return sheet_.hex && sheet_.hex->isSelected();
    }

    void Shell::reset(bool instant) {
        closing_ = false;
        openRestart_ = instant ? 2 : 1;
        detailId_ = 0;
        detailOpen_ = false;
        pop_.open = false;
        sheet_.open = false;
        capturingRow_ = 0;
        capturedKey_ = 0;
        for (auto& s : rootScroll_) s = Scroll {};
        detailScroll_ = Scroll {};
        blurAll();
    }

    void Shell::onClosed() {
        closing_ = true;
        blurAll();
        pop_.open = false;
        sheet_.open = false;
        capturingRow_ = 0;
        capturedKey_ = 0;
    }

    bool Shell::onEscape() {
        if (capturingRow_ != 0) {
            capturingRow_ = 0;
            capturedKey_ = 0;
            return true;
        }
        if (pop_.open) {
            pop_.open = false;
            return true;
        }
        if (sheet_.open) {
            sheet_.open = false;
            blurAll();
            return true;
        }
        if (textFocused()) {
            blurAll();
            return true;
        }
        if (detailOpen_) {
            detailOpen_ = false;
            return true;
        }
        return false;
    }

    void Shell::selectSection(Section s) {
        if (s != section_) {
            section_ = s;
            detailOpen_ = false;
            detailId_ = 0;
            pop_.open = false;
            blurAll();
        } else if (detailOpen_) {
            detailOpen_ = false;
        }
    }

    void Shell::pushReplay(uint64_t id) {
        detailKind_ = DetailKind::Replay;
        viewer_ = Viewer {};
        viewer_.id = id;
        detailId_ = id;
        detailOpen_ = true;
        detailScroll_ = Scroll {};
        pop_.open = false;
        blurAll();
    }

    void Shell::pushDetail(uint64_t id) {
        detailKind_ = DetailKind::Module;
        detailId_ = id;
        detailOpen_ = true;
        detailScroll_ = Scroll {};
        pop_.open = false;
        blurAll();
    }

    void Shell::jumpToModule(uint64_t id) {
        section_ = Section::All;
        pushDetail(id);
        instantPush_ = true;
    }

    // ======================================================================================== frame
    bool Shell::frame(Ui& ui, Rect const& screen, Frame& f, bool active) {
        closing_ = !active;
        const bool restartSpring = openRestart_ == 1;
        if (openRestart_ == 1) {
            ui.animSet(kIdOpen, 0.f);
            openRestart_ = 0;
        } else if (openRestart_ == 2) {
            ui.animSet(kIdOpen, 1.f);
            ui.springSet(kIdOpenSpring, 1.f);
            openRestart_ = 0;
        }
        float open = ui.animFrom(kIdOpen, 0.f, active ? 1.f : 0.f, active ? 0.08f : 0.06f);
        openAmount_ = open;
        if (!active && open < 0.02f) return false;

        if (instantPush_) {
            ui.animSet(kIdPush, 1.f);
            instantPush_ = false;
        }

        win_ = computeWindow(screen, ui.scale);
        const float R = ui.S(kWinR);
        const bool animating = open < 0.985f || !active;

        // the window pops in with a spring (response 0.5 / damping 0.8: a hair of overshoot, like a sheet presenting)
        if (restartSpring) {
            ui.springSet(kIdOpenSpring, 0.f);
        }
        float pop = ui.spring(kIdOpenSpring, active ? 1.f : 0.f, 0.50f, 0.80f);

        // search text changes reset scroll
        {
            std::wstring now = searchText();
            if (now != lastSearch_) {
                lastSearch_ = now;
                for (auto& s : rootScroll_) s = Scroll {};
                detailScroll_ = Scroll {};
            }
        }
        if (search_) search_->rtl = ui.rtl;
        for (auto& [id, e] : edits_) e->rtl = ui.rtl;

        if (ui.rtl) ui.pushMirror(win_.centerX());

        D2D1::Matrix3x2F oldTransform;
        ui.ctx()->GetTransform(&oldTransform);
        float popScale = 0.925f + 0.075f * pop;
        ui.ctx()->SetTransform(D2D1::Matrix3x2F::Scale(popScale, popScale, D2D1::Point2F(win_.centerX(), win_.centerY())) *
                               D2D1::Matrix3x2F(oldTransform));
        ui.pushOpacity(screen, open);

        claimedFocus_ = false;

        // window: a large glass sheet
        ui.shadow(win_, R, ui.S(56.f), ui.S(24.f), Col(0.f, 0.f, 0.f, ui.pal.dark ? 0.58f : 0.30f));
        ui.fillSmooth(win_, R, ui.pal.windowFill);
        {
            GlassStyle ws;
            ws.clear = true;
            ws.shadow = 0.f;
            ws.backing = 0.f;
            ui.glass(win_, R, ws);
        }

        ui.pushClipRRect(win_, R);
        const float sideW = ui.S(kSidebarW);
        Rect panel = sidebarPanel(ui, win_);
        {
            // floating sidebar: concentric corners, its own glass
            GlassStyle ss;
            ss.shadow = 0.55f;
            ss.backing = 0.f;
            ui.glass(panel, R - ui.S(kInset), ss);
        }

        const bool overlay = pop_.open || sheet_.open;
        ui.lockInput(animating || overlay);

        drawSidebar(ui, f, win_, sideW);
        Rect content(panel.right, win_.top, win_.right, win_.bottom);
        drawContent(ui, f, content);

        ui.lockInput(animating);
        drawPopover(ui, win_);
        drawColorSheet(ui, f, win_);
        ui.popClipRRect();

        ui.popOpacity();
        ui.ctx()->SetTransform(oldTransform);
        if (ui.rtl) ui.popMirror();

        // a click that did not land on any text field removes the focus
        if (ui.in.pressed && !claimedFocus_ && !animating) {
            if (textFocused()) blurAll();
        }
        return true;
    }

    // ======================================================================================== sidebar
    void Shell::drawSidebar(Ui& ui, Frame& f, Rect const& win, float /*sideW*/) {
        auto S = [&](float v) { return ui.S(v); };
        const Palette& pal = ui.pal;
        Rect panel = sidebarPanel(ui, win);
        const float x0 = panel.left, y0 = panel.top;
        const float panelW = panel.getWidth();
        const float pad = S(16.f);

        // brand header
        Rect logo(x0 + S(18.f), y0 + S(20.f), x0 + S(18.f) + S(42.f), y0 + S(20.f) + S(42.f));
        if (f.logo) {
            ui.bitmap(f.logo, logo);
        } else {
            ui.iconTile(logo, pal.accent, L"m");
        }
        Rect nameR(logo.right + S(11.f), logo.top - S(1.f), x0 + panelW - pad, logo.top + S(25.f));
        ui.text(nameR, f.str.appName, TextStyle(Font::Bold, S(22.f), pal.label, HAlign::Leading, VAlign::Center));
        Rect verR(logo.right + S(11.f), logo.top + S(24.f), x0 + panelW - pad, logo.bottom + S(1.f));
        ui.text(verR, f.version, TextStyle(Font::Regular, S(12.f), pal.secondaryLabel, HAlign::Leading, VAlign::Center));

        struct Item {
            Section sec;
            int action; // 0 = section, 1 = hud editor, 2 = replay
            std::wstring label;
            Col color;
            Glyph glyph;
            std::wstring mono;
        };
        std::vector<Item> modulesItems = {
            { Section::All, 0, f.str.all, pal.blue, Glyph::Grid, L"" },
            { Section::Game, 0, f.str.game, pal.green, Glyph::Gamepad, L"" },
            { Section::Hud, 0, f.str.hud, pal.orange, Glyph::Widgets, L"" },
            { Section::Plugins, 0, f.str.plugins, pal.purple, Glyph::Grid, L"{ }" },
        };
        std::vector<Item> clientItems = {
            { Section::Settings, 0, f.str.settings, pal.gray, Glyph::Gear, L"" },
            { Section::All, 1, f.str.hudEditor, pal.pink, Glyph::Sliders, L"" },
        };
        if (f.replay.available) clientItems.push_back({ Section::Replay, 0, f.str.replay, pal.red, Glyph::Camera, L"" });

        // ---- layout pass (positions first, so one selection capsule can travel between rows)
        struct Slot {
            Item const* it;
            Rect rr;
            int index;
        };
        struct Head {
            std::wstring text;
            float y;
        };
        std::vector<Slot> slots;
        std::vector<Head> heads;
        float y = y0 + S(84.f);
        int idx = 0;
        auto addHeader = [&](std::wstring const& text) {
            heads.push_back({ text, y });
            y += S(26.f);
        };
        auto addItem = [&](Item const& it) {
            slots.push_back({ &it, Rect(x0 + S(10.f), y, x0 + panelW - S(10.f), y + S(42.f)), idx++ });
            y += S(44.f);
        };
        addHeader(f.str.modulesHeader);
        for (auto& it : modulesItems) addItem(it);
        y += S(10.f);
        addHeader(f.str.clientHeader);
        for (auto& it : clientItems) addItem(it);

        for (auto const& h : heads) {
            ui.text(Rect(x0 + pad + S(10.f), h.y, x0 + panelW - pad, h.y + S(18.f)), h.text,
                    TextStyle(Font::Semibold, S(12.f), pal.secondaryLabel, HAlign::Leading, VAlign::Center));
        }

        // ---- selection capsule: tinted glass that springs to the chosen row and stretches while it travels
        {
            const Slot* sel = nullptr;
            for (auto const& s : slots) {
                if (s.it->action == 0 && s.it->sec == section_) sel = &s;
            }
            if (sel) {
                float top = ui.spring(kIdSelTop, sel->rr.top, 0.42f, 0.78f);
                float vel = ui.springVelocity(kIdSelTop);
                float stretch = std::min(S(9.f), std::fabs(vel) * 0.022f);
                Rect ind(sel->rr.left, top - stretch * 0.5f, sel->rr.right, top + sel->rr.getHeight() + stretch * 0.5f);
                GlassStyle gs;
                gs.tint = withAlpha(pal.accent, 0.92f);
                gs.backing = 0.f;
                gs.shadow = 0.5f;
                ui.glass(ind, ind.getHeight() * 0.5f, gs);
            }
        }

        for (auto const& s : slots) {
            Item const& it = *s.it;
            Rect const& rr = s.rr;
            bool selected = it.action == 0 && it.sec == section_;
            uint64_t id = Ui::hashId(std::string("side") + std::to_string(s.index) + std::to_string(it.action));
            auto cr = ui.click(id, rr);
            float hoverA = ui.anim(id ^ 0x91ull, (cr.hovered && !selected) ? 1.f : 0.f, 0.07f);
            float selA = ui.anim(id ^ 0x92ull, selected ? 1.f : 0.f, 0.10f);
            float pr = ui.pressSpring(id ^ 0x93ull, cr.pressed);
            if (hoverA > 0.01f || pr > 0.01f) {
                ui.rrect(rr, rr.getHeight() * 0.5f, withAlpha(pal.label, 0.07f * hoverA + 0.06f * clamp01(pr)));
            }
            Rect tile(rr.left + S(10.f), rr.centerY() - S(14.f), rr.left + S(10.f) + S(28.f), rr.centerY() + S(14.f));
            if (!it.mono.empty()) {
                ui.iconTile(tile, it.color, it.mono);
            } else {
                ui.iconTile(tile, it.color, it.glyph);
            }
            Col tc = mix(pal.label, Col(1.f, 1.f, 1.f, 1.f), selA);
            ui.text(Rect(tile.right + S(11.f), rr.top, rr.right - S(8.f), rr.bottom), it.label,
                    TextStyle(selected ? Font::Semibold : Font::Regular, S(17.f), tc, HAlign::Leading, VAlign::Center));
            if (cr.clicked) {
                if (it.action == 1) {
                    if (f.openHudEditor) f.openHudEditor();
                } else {
                    selectSection(it.sec);
                }
            }
        }

        // ---- search: a floating glass capsule at the bottom (iOS 26). While it has focus it splits off a round
        // glass cancel button, the way the system search field does.
        {
            auto edit = searchEdit();
            const bool focused = edit->isSelected();
            const float sfH = S(44.f);
            float fs = ui.spring(kIdSearchFocus, focused ? 1.f : 0.f, 0.36f, 0.82f);
            float fT = clamp01(fs);
            float shrink = std::clamp(fs, 0.f, 1.1f) * (sfH + S(8.f));
            Rect sf(x0 + S(12.f), panel.bottom - S(12.f) - sfH, x0 + panelW - S(12.f) - shrink, panel.bottom - S(12.f));
            TextStyle sst(Font::Regular, S(17.f), pal.label, HAlign::Leading, VAlign::Center);
            float caretX = ui.prefixWidth(edit->getText(), static_cast<size_t>(edit->getCaretLocation()), sst);
            bool clear = false;
            bool clicked = ui.searchField(kIdSearch, sf, edit->getText(),
                                          section_ == Section::Settings ? f.str.searchSettings : f.str.search, focused,
                                          caretX, focused && edit->shouldBlink(), &clear);
            if (fT > 0.02f) {
                Rect cancelR(sf.right + S(8.f), sf.top, sf.right + S(8.f) + sfH, sf.bottom);
                ui.pushOpacity(cancelR, fT);
                bool cancel = ui.glassIconButton(Ui::hashId("sidecancel"), cancelR, Glyph::XMark, Col(0.f, 0.f, 0.f, 0.f), 0.40f, fT > 0.6f);
                ui.popOpacity();
                if (cancel) {
                    edit->reset();
                    blurAll();
                    claimedFocus_ = true;
                }
            }
            if (clear) {
                edit->reset();
                edit->setSelected(true);
                claimedFocus_ = true;
            } else if (clicked) {
                blurAll();
                edit->setSelected(true);
                edit->setCaretLocation(static_cast<int>(edit->getText().size()));
                claimedFocus_ = true;
            }
        }
    }

    // ======================================================================================== content
    void Shell::applyScroll(Ui& ui, Scroll& sc, Rect const& view, bool interactive) {
        sc.max = std::max(0.f, sc.max);
        if (interactive && !pop_.open && !sheet_.open && ui.in.wheel != 0.f && view.contains(ui.mouse())) {
            sc.target += ui.in.wheel;
        }
        sc.target = std::clamp(sc.target, 0.f, sc.max);
        sc.pos = ui.anim(Ui::idOf(&sc), sc.target, 0.075f);
        sc.pos = std::clamp(sc.pos, 0.f, std::max(sc.max, 0.f));
        sc.moving = std::fabs(sc.pos - sc.target) > 0.6f || (interactive && ui.in.wheel != 0.f);
    }

    void Shell::drawContent(Ui& ui, Frame& f, Rect const& content) {
        float pushT = ui.anim(kIdPush, detailOpen_ ? 1.f : 0.f, 0.11f);
        if (!detailOpen_ && pushT < 0.002f) {
            detailId_ = 0;
            if (viewer_.clip) viewer_ = Viewer {};
        }
        const bool rootInteractive = !detailOpen_ && pushT < 0.002f;
        const bool detailInteractive = detailOpen_ && pushT > 0.998f;
        const float cw = content.getWidth();

        if (pushT < 0.999f) {
            bool prevLock = ui.inputLocked();
            if (!rootInteractive) ui.lockInput(true);
            drawRootPage(ui, f, content, -pushT * cw * 0.28f, rootInteractive);
            ui.lockInput(prevLock);
        }
        if (pushT > 0.001f && detailId_ != 0) {
            bool prevLock = ui.inputLocked();
            if (!detailInteractive) ui.lockInput(true);
            drawDetailPage(ui, f, content, (1.f - pushT) * cw, detailInteractive);
            ui.lockInput(prevLock);
        }
    }

    void Shell::drawNavBar(Ui& ui, Frame& f, Rect const& content, std::wstring const& title, float scroll,
                           bool showBack, std::wstring const& /*backTitle*/) {
        auto S = [&](float v) { return ui.S(v); };
        const Palette& pal = ui.pal;
        Rect bar(content.left, content.top, content.right, content.top + S(kNavH));
        float t = clamp01((scroll - S(14.f)) / S(18.f));

        // scroll edge effect: no bar, the content fades into the page colour under the floating controls
        if (navEdge_) {
            // solid under the floating buttons and title, then a soft fall-off into the content
            float band = S(kNavH) + S(34.f);
            ui.scrollEdge(Rect(content.left, content.top, content.right, content.top + band), true,
                          solidBase(ui).asAlpha(1.f), t, S(kNavH - 6.f) / band);
        }

        // inline title appears once the large title has scrolled away
        ui.text(Rect(bar.left + S(76.f), bar.top, bar.right - S(76.f), bar.bottom), title,
                TextStyle(Font::Semibold, S(17.f), withAlpha(pal.label, t), HAlign::Center, VAlign::Center));

        const float d = S(kBtnD);
        const float cy = bar.top + S(kNavH) * 0.5f;

        // floating glass close button (trailing)
        Rect closeR(bar.right - S(16.f) - d, cy - d * 0.5f, bar.right - S(16.f), cy + d * 0.5f);
        if (ui.glassIconButton(Ui::hashId("navclose") ^ (showBack ? 0x1111ull : 0x2222ull), closeR, Glyph::XMark,
                               Col(0.f, 0.f, 0.f, 0.f), 0.40f)) {
            if (f.closeWindow) f.closeWindow();
        }

        // floating glass back button (leading): just a chevron in a circle, like the iOS 26 navigation bar
        if (showBack) {
            Rect backR(bar.left + S(16.f), cy - d * 0.5f, bar.left + S(16.f) + d, cy + d * 0.5f);
            uint64_t id = Ui::hashId("navback") ^ static_cast<uint64_t>(content.left);
            if (ui.glassIconButton(id, backR, Glyph::ChevronLeft, Col(0.f, 0.f, 0.f, 0.f), 0.46f)) detailOpen_ = false;
        }
    }

    // ---------------------------------------------------------------------------------- module list
    float Shell::drawModuleList(Ui& ui, Frame& f, std::vector<ModuleRow*> const& rows, float x, float y, float w,
                                Rect const& view, bool interactive) {
        auto S = [&](float v) { return ui.S(v); };
        const Palette& pal = ui.pal;
        if (rows.empty()) return y;

        const float rowH = S(kModRowH);
        Rect card(x, y, x + w, y + rowH * static_cast<float>(rows.size()));
        const bool cardVisible = card.bottom > view.top && card.top < view.bottom;
        if (cardVisible) drawCard(ui, card);

        ui.beginCard(card, S(kGroupR));
        for (size_t i = 0; i < rows.size(); ++i) {
            ModuleRow& m = *rows[i];
            Rect rc(x, y + rowH * static_cast<float>(i), x + w, y + rowH * static_cast<float>(i + 1));
            if (rc.bottom < view.top - S(2.f) || rc.top > view.bottom + S(2.f)) continue;

            const bool navigable = !m.market;
            uint64_t rid = m.id ^ 0x0B0Dull;

            // trailing control rectangle first, so the row tap never swallows presses on it
            const float chevW = navigable ? S(18.f) : 0.f;
            float right = rc.right - S(14.f);
            Rect ctl(0.f, 0.f, 0.f, 0.f);
            bool hasCtl = false;
            std::wstring ctlLabel;
            if (m.trailing == ModuleRow::Trailing::Toggle) {
                ctl = Rect(right - chevW - S(kSwitchW), rc.centerY() - S(kSwitchH * 0.5f), right - chevW, rc.centerY() + S(kSwitchH * 0.5f));
                hasCtl = true;
            } else if (m.trailing == ModuleRow::Trailing::Open) {
                ctlLabel = f.str.open;
                float bw = std::max(S(64.f), ui.textWidth(ctlLabel, TextStyle(Font::Semibold, S(15.f), pal.accent)) + S(30.f));
                ctl = Rect(right - chevW - bw, rc.centerY() - S(15.f), right - chevW, rc.centerY() + S(15.f));
                hasCtl = true;
            } else if (m.trailing == ModuleRow::Trailing::Install || m.trailing == ModuleRow::Trailing::Installed) {
                ctlLabel = m.trailing == ModuleRow::Trailing::Installed ? f.str.installed : f.str.install;
                float bw = std::max(S(72.f), ui.textWidth(ctlLabel, TextStyle(Font::Semibold, S(15.f), pal.accent)) + S(34.f));
                ctl = Rect(right - bw, rc.centerY() - S(15.f), right, rc.centerY() + S(15.f));
                hasCtl = true;
            }

            auto rowClick = ui.click(rid, Rect(rc.left, rc.top, hasCtl ? ctl.left - S(6.f) : rc.right, rc.bottom),
                                     navigable && interactive);
            ModuleRow::Trailing tr = m.trailing;
            Ui::ClickResult chevClick;
            if (hasCtl && navigable) {
                chevClick = ui.click(rid ^ 0xC1ull, Rect(ctl.right + S(6.f), rc.top, rc.right, rc.bottom), interactive);
            }
            if (rowClick.pressed || chevClick.pressed) ui.fillCell(rc, pal.cellPressed);
            (void)tr;

            // icon tile
            Rect tile(rc.left + S(14.f), rc.centerY() - S(19.f), rc.left + S(14.f) + S(38.f), rc.centerY() + S(19.f));
            std::wstring mono = m.name.empty() ? L"?" : std::wstring(1, static_cast<wchar_t>(std::towupper(m.name[0])));
            ui.iconTile(tile, pal.tileColor(m.key.empty() ? std::to_string(m.id) : m.key), mono);

            // trailing control
            if (m.trailing == ModuleRow::Trailing::Toggle) {
                bool v = m.enabled;
                if (ui.toggle(m.id ^ 0x70661Eull, ctl, v, interactive)) {
                    m.enabled = v;
                    if (m.setEnabled) m.setEnabled(v);
                }
                right = ctl.left - S(12.f);
            } else if (m.trailing == ModuleRow::Trailing::Open) {
                if (ui.button(m.id ^ 0x09E4ull, ctl, ctlLabel, ButtonKind::Tinted, 15.f, interactive)) {
                    if (m.open) m.open();
                }
                right = ctl.left - S(12.f);
            } else if (m.trailing == ModuleRow::Trailing::HoldKey) {
                float tw = ui.textWidth(m.keyText, TextStyle(Font::Medium, S(14.f), pal.label));
                Rect b(right - std::max(S(44.f), tw + S(22.f)) - S(18.f), rc.centerY() - S(14.f), right - S(18.f),
                       rc.centerY() + S(14.f));
                ui.rrect(b, b.getHeight() * 0.5f, pal.tertiaryFill);
                ui.text(b, m.keyText, TextStyle(Font::Medium, S(14.f), pal.label, HAlign::Center, VAlign::Center));
                right = b.left - S(12.f);
            } else if (m.trailing == ModuleRow::Trailing::Install || m.trailing == ModuleRow::Trailing::Installed) {
                bool inst = m.trailing == ModuleRow::Trailing::Installed;
                if (ui.button(m.id ^ 0x1257ull, ctl, ctlLabel, inst ? ButtonKind::Gray : ButtonKind::Filled, 15.f,
                              interactive && !inst)) {
                    if (m.install) m.install();
                }
                right = ctl.left - S(12.f);
            }

            if (navigable) {
                Rect chev(rc.right - S(14.f) - S(10.f), rc.centerY() - S(8.f), rc.right - S(14.f), rc.centerY() + S(8.f));
                ui.glyph(Glyph::ChevronRight, chev, pal.tertiaryLabel, 1.1f);
            }

            // texts
            Rect textR(tile.right + S(13.f), rc.top, right, rc.bottom);
            // module descriptions are not shown; only a plugin's author line stays
            std::wstring sub;
            if (m.market && !m.author.empty()) {
                sub = f.str.byAuthor;
                size_t p = sub.find(L"%s");
                if (p != std::wstring::npos) sub.replace(p, 2, m.author);
            }
            float th = S(22.f), sh = sub.empty() ? 0.f : S(17.f);
            float block = th + sh;
            float top = rc.centerY() - block * 0.5f;
            ui.text(Rect(textR.left, top, textR.right, top + th), m.name,
                    TextStyle(Font::Medium, S(17.f), pal.label, HAlign::Leading, VAlign::Center));
            if (!sub.empty()) {
                ui.text(Rect(textR.left, top + th, textR.right, top + th + sh), sub,
                        TextStyle(Font::Regular, S(13.f), pal.secondaryLabel, HAlign::Leading, VAlign::Top));
            }

            if (i + 1 < rows.size()) {
                ui.hairlineH(tile.right + S(13.f), rc.right, rc.bottom - std::max(1.f, std::round(ui.scale * 0.5f)),
                             pal.separator);
            }

            if (rowClick.clicked || chevClick.clicked) pushDetail(m.id);
        }
        ui.endCard();
        return card.bottom;
    }

    // ---------------------------------------------------------------------------------- root page
    void Shell::drawRootPage(Ui& ui, Frame& f, Rect const& content, float offX, bool interactive) {
        auto S = [&](float v) { return ui.S(v); };
        const Palette& pal = ui.pal;
        Scroll& sc = rootScroll_[static_cast<int>(section_)];
        applyScroll(ui, sc, content, interactive);

        D2D1::Matrix3x2F old;
        ui.ctx()->GetTransform(&old);
        if (std::fabs(offX) > 0.01f) {
            ui.ctx()->SetTransform(D2D1::Matrix3x2F::Translation(offX, 0.f) * D2D1::Matrix3x2F(old));
        }
        ui.pushClip(content);

        std::wstring filter = lower(searchText());
        const float x = content.left + S(kMargin);
        const float w = content.getWidth() - S(kMargin) * 2.f;
        float y = content.top + S(kNavH) - sc.pos;

        std::wstring title;
        switch (section_) {
        case Section::All:
            title = f.str.modulesBack;
            break;
        case Section::Game:
            title = f.str.game;
            break;
        case Section::Hud:
            title = f.str.hud;
            break;
        case Section::Plugins:
            title = f.str.plugins;
            break;
        case Section::Settings:
            title = f.str.settings;
            break;
        case Section::Replay:
            title = f.str.replay;
            break;
        }

        ui.text(Rect(x + S(4.f), y, x + w, y + S(42.f)), title,
                TextStyle(Font::Bold, S(34.f), pal.label, HAlign::Leading, VAlign::Center));
        y += S(42.f) + S(12.f) + (section_ == Section::Settings ? S(10.f) : 0.f);

        bool any = false;
        if (section_ == Section::Replay) {
            y = drawReplayRoot(ui, f, x, y, w, content, interactive);
            any = true;
        } else if (section_ == Section::Settings) {
            for (auto& sec : f.clientSettings) {
                float ny = drawSection(ui, sec, x, y, w, content, interactive, filter);
                if (ny > y) any = true;
                y = ny;
            }
        } else {
            std::vector<ModuleRow*> rows;
            for (auto& m : f.modules) {
                bool include;
                if (!filter.empty()) {
                    include = containsLower(m.name, filter) || containsLower(m.desc, filter) ||
                              containsLower(m.author, filter) || containsLower(widen(m.key), filter);
                } else {
                    switch (section_) {
                    case Section::All:
                        include = !m.market;
                        break;
                    case Section::Game:
                        include = !m.market && m.inGame;
                        break;
                    case Section::Hud:
                        include = !m.market && m.inHud;
                        break;
                    case Section::Plugins:
                        include = m.market;
                        break;
                    default:
                        include = false;
                        break;
                    }
                }
                if (include) rows.push_back(&m);
            }
            std::sort(rows.begin(), rows.end(), [](ModuleRow* a, ModuleRow* b) {
                if (a->market != b->market) return a->market;
                return a->name < b->name;
            });
            float ny = drawModuleList(ui, f, rows, x, y, w, content, interactive);
            any = !rows.empty();
            y = ny + S(24.f);
        }

        if (!any) {
            ui.text(Rect(x, y + S(40.f), x + w, y + S(80.f)), f.str.noResults,
                    TextStyle(Font::Semibold, S(20.f), pal.tertiaryLabel, HAlign::Center, VAlign::Center));
            y += S(100.f);
        }

        float contentH = (y + sc.pos) - content.top + S(24.f);
        sc.max = std::max(0.f, contentH - content.getHeight());

        ui.popClip();
        ui.scrollIndicator(Ui::idOf(&sc), Rect(content.left, content.top + S(kNavH), content.right, content.bottom),
                           sc.pos, sc.max, sc.moving);
        drawNavBar(ui, f, content, title, sc.pos, false, L"");
        ui.ctx()->SetTransform(old);
    }

    // ---------------------------------------------------------------------------------- detail page
    void Shell::drawDetailPage(Ui& ui, Frame& f, Rect const& content, float offX, bool interactive) {
        auto S = [&](float v) { return ui.S(v); };
        const Palette& pal = ui.pal;

        if (detailKind_ == DetailKind::Replay) {
            drawReplayViewer(ui, f, content, offX, interactive);
            return;
        }

        ModuleDetail d;
        if (!f.detail || !f.detail(detailId_, d)) {
            detailOpen_ = false;
            return;
        }
        applyScroll(ui, detailScroll_, content, interactive);

        // opaque base while the page slides over the list
        {
            float pushT = ui.anim(kIdPush, detailOpen_ ? 1.f : 0.f, 0.11f);
            Rect page(content.left + offX, content.top, content.right + offX, content.bottom);
            float baseA = clamp01(1.f - pushT) * 0.98f;
            if (baseA > 0.01f) {
                ui.fillRect(page, solidBase(ui).asAlpha(baseA));
                ui.hairlineV(page.left, page.top, page.bottom, mulAlpha(pal.separator, baseA));
            }
        }

        D2D1::Matrix3x2F old;
        ui.ctx()->GetTransform(&old);
        if (std::fabs(offX) > 0.01f) {
            ui.ctx()->SetTransform(D2D1::Matrix3x2F::Translation(offX, 0.f) * D2D1::Matrix3x2F(old));
        }
        ui.pushClip(content);

        const float x = content.left + S(kMargin);
        const float w = content.getWidth() - S(kMargin) * 2.f;
        float y = content.top + S(kNavH) + S(2.f) - detailScroll_.pos;
        std::wstring filter = lower(searchText());

        // ---- header card
        {
            const float tileS = S(64.f);
            const bool toggleable = d.hasToggle;
            float swW = toggleable ? S(kSwitchW) + S(16.f) : 0.f;
            float textW = w - S(16.f) - tileS - S(14.f) - S(16.f) - swW;
            TextStyle ds(Font::Regular, S(14.f), pal.secondaryLabel, HAlign::Leading, VAlign::Top, true);
            float descH = 0.f; // module descriptions are not shown
            (void)ds;
            (void)textW;
            float nameH = S(28.f);
            float cardH = std::max(S(88.f), S(16.f) + nameH + (descH > 0.f ? S(4.f) + descH : 0.f) + S(16.f));
            Rect card(x, y, x + w, y + cardH);
            drawCard(ui, card);
            Rect tile(card.left + S(16.f), card.centerY() - tileS * 0.5f, card.left + S(16.f) + tileS,
                      card.centerY() + tileS * 0.5f);
            std::wstring mono = d.name.empty() ? L"?" : std::wstring(1, static_cast<wchar_t>(std::towupper(d.name[0])));
            ui.iconTile(tile, pal.tileColor(d.key.empty() ? std::to_string(detailId_) : d.key), mono);

            float blockH = nameH + (descH > 0.f ? S(4.f) + descH : 0.f);
            float top = card.centerY() - blockH * 0.5f;
            float tx = tile.right + S(14.f);
            ui.text(Rect(tx, top, tx + textW, top + nameH), d.name,
                    TextStyle(Font::Bold, S(22.f), pal.label, HAlign::Leading, VAlign::Center));
            if (descH > 0.f) {
                ui.text(Rect(tx, top + nameH + S(4.f), tx + textW, top + nameH + S(4.f) + descH), d.desc, ds);
            }
            if (toggleable) {
                Rect sw(card.right - S(16.f) - S(kSwitchW), card.centerY() - S(kSwitchH * 0.5f), card.right - S(16.f),
                        card.centerY() + S(kSwitchH * 0.5f));
                bool v = d.enabled;
                if (ui.toggle(Ui::idOf(&detailId_, 0xDE7A11ull), sw, v, interactive)) {
                    if (d.setEnabled) d.setEnabled(v);
                }
            }
            y = card.bottom + S(18.f);
        }

        // ---- HUD preview
        if (d.hasPreview) {
            float ph = std::max(S(120.f), d.previewSize.y * 1.0f + S(56.f));
            Rect card(x, y, x + w, y + ph);
            drawCard(ui, card);
            if (d.drawPreview && card.bottom > content.top && card.top < content.bottom) {
                ui.pushClipRRect(card, S(kGroupR));
                d.drawPreview(card);
                ui.popClipRRect();
            }
            y = card.bottom + S(18.f);
        }

        // ---- settings
        for (auto& sec : d.sections) {
            y = drawSection(ui, sec, x, y, w, content, interactive, filter);
        }

        // ---- reset
        if (d.reset) {
            Rect card(x, y, x + w, y + S(kRowH));
            drawCard(ui, card);
            uint64_t id = Ui::idOf(&detailId_, 0x4E5E7ull);
            auto cr = ui.click(id, card, interactive);
            if (cr.pressed) ui.fillClipped(card, card, S(kGroupR), pal.cellPressed);
            ui.text(card, f.str.reset, TextStyle(Font::Regular, S(17.f), pal.red, HAlign::Center, VAlign::Center));
            if (cr.clicked) d.reset();
            y = card.bottom + S(24.f);
        }

        float contentH = (y + detailScroll_.pos) - content.top + S(28.f);
        detailScroll_.max = std::max(0.f, contentH - content.getHeight());

        ui.popClip();
        ui.scrollIndicator(Ui::idOf(&detailScroll_),
                           Rect(content.left, content.top + S(kNavH), content.right, content.bottom), detailScroll_.pos,
                           detailScroll_.max, detailScroll_.moving);

        std::wstring backTitle = section_ == Section::Game      ? f.str.game
                                 : section_ == Section::Hud     ? f.str.hud
                                 : section_ == Section::Plugins ? f.str.plugins
                                                                : f.str.modulesBack;
        drawNavBar(ui, f, content, d.name, std::max(0.f, detailScroll_.pos - S(46.f)), true, backTitle);
        ui.ctx()->SetTransform(old);
    }

    // ---------------------------------------------------------------------------------- settings groups
    bool Shell::filterMatches(std::wstring const& filter, SettingRow const& r) const {
        if (filter.empty()) return true;
        return containsLower(r.title, filter) || containsLower(r.subtitle, filter);
    }

    float Shell::rowHeight(Ui& ui, SettingRow& r, float w) {
        auto S = [&](float v) { return ui.S(v); };
        float pad = S(16.f);
        float labelW;
        switch (r.kind) {
        case SettingRow::Kind::Toggle:
            labelW = w - pad - S(kSwitchW) - S(12.f) - pad;
            break;
        case SettingRow::Kind::Key:
            labelW = w - pad - S(110.f) - pad;
            break;
        case SettingRow::Kind::Color:
            labelW = w - pad - S(34.f) - pad - S(12.f);
            break;
        case SettingRow::Kind::Text:
            labelW = w * 0.45f - pad;
            break;
        case SettingRow::Kind::Choice:
            labelW = useSegmented(r) ? w - pad * 2.f : w * 0.5f - pad;
            break;
        default:
            labelW = w - pad * 2.f;
            break;
        }
        float block = labelBlockHeight(ui, r.subtitle, labelW);
        switch (r.kind) {
        case SettingRow::Kind::Slider:
            return S(12.f) + block + S(8.f) + S(30.f) + S(10.f);
        case SettingRow::Kind::Choice:
            if (useSegmented(r)) return S(12.f) + block + S(10.f) + S(32.f) + S(12.f);
            return std::max(S(kRowH), block + S(22.f));
        default:
            return std::max(S(kRowH), block + S(22.f));
        }
    }

    float Shell::drawSection(Ui& ui, SettingsSection& sec, float x, float y, float w, Rect const& view,
                             bool interactive, std::wstring const& filter) {
        auto S = [&](float v) { return ui.S(v); };
        const Palette& pal = ui.pal;

        std::vector<SettingRow*> rows;
        for (auto& r : sec.rows) {
            if (filterMatches(filter, r)) rows.push_back(&r);
        }
        if (rows.empty()) return y;

        if (!sec.header.empty()) {
            ui.text(Rect(x + S(16.f), y, x + w, y + S(20.f)), sec.header,
                    TextStyle(Font::Regular, S(13.f), pal.secondaryLabel, HAlign::Leading, VAlign::Center));
            y += S(24.f);
        }

        std::vector<float> hs;
        float total = 0.f;
        for (auto* r : rows) {
            float h = rowHeight(ui, *r, w);
            hs.push_back(h);
            total += h;
        }

        Rect card(x, y, x + w, y + total);
        const bool visible = card.bottom > view.top - S(8.f) && card.top < view.bottom + S(8.f);
        if (visible) {
            drawCard(ui, card);
            ui.beginCard(card, S(kGroupR));
            float ry = y;
            for (size_t i = 0; i < rows.size(); ++i) {
                Rect rc(x, ry, x + w, ry + hs[i]);
                if (rc.bottom >= view.top - S(4.f) && rc.top <= view.bottom + S(4.f)) {
                    drawRow(ui, *rows[i], rc, interactive);
                    if (i + 1 < rows.size()) {
                        ui.hairlineH(x + S(16.f), x + w, rc.bottom - std::max(1.f, std::round(ui.scale * 0.5f)),
                                     pal.separator);
                    }
                }
                ry += hs[i];
            }
            ui.endCard();
        }
        y += total;

        if (!sec.footer.empty()) {
            TextStyle fs(Font::Regular, S(13.f), pal.secondaryLabel, HAlign::Leading, VAlign::Top, true);
            float fh = ui.measure(sec.footer, fs, w - S(32.f)).y;
            ui.text(Rect(x + S(16.f), y + S(8.f), x + w - S(16.f), y + S(8.f) + fh), sec.footer, fs);
            y += S(8.f) + fh;
        }
        return y + S(22.f);
    }

    void Shell::drawRow(Ui& ui, SettingRow& r, Rect const& rc, bool interactive) {
        auto S = [&](float v) { return ui.S(v); };
        const Palette& pal = ui.pal;
        const float pad = S(16.f);
        Rect inner(rc.left + pad, rc.top, rc.right - pad, rc.bottom);

        switch (r.kind) {
        case SettingRow::Kind::Toggle: {
            if (!r.boolean) return;
            Rect sw(inner.right - S(kSwitchW), rc.centerY() - S(kSwitchH * 0.5f), inner.right, rc.centerY() + S(kSwitchH * 0.5f));
            drawLabel(ui, Rect(inner.left, rc.top, sw.left - S(12.f), rc.bottom), r.title, r.subtitle);
            bool v = *r.boolean;
            if (ui.toggle(r.id, sw, v, interactive)) {
                *r.boolean = v;
                if (r.changed) r.changed();
            }
        } break;

        case SettingRow::Kind::Slider: {
            if (!r.number) return;
            float block = labelBlockHeight(ui, r.subtitle, inner.getWidth());
            std::wstring valueStr = formatNumber(*r.number);
            auto edit = editFor(r.id ^ 0x5EEDull, 14);
            const bool editing = edit->isSelected();
            if (editing) {
                float parsed;
                if (parseNumber(edit->getText(), parsed)) {
                    float nv = std::clamp(parsed, std::min(r.lo, r.hi), std::max(r.lo, r.hi));
                    if (r.step > 0.f) nv = std::clamp(std::round(nv / r.step) * r.step, r.lo, r.hi);
                    if (nv != *r.number) {
                        *r.number = nv;
                        if (r.changed) r.changed();
                    }
                }
            } else {
                edit->setText(valueStr);
                edit->setCaretLocation(static_cast<int>(valueStr.size()));
            }

            // title + subtitle (left), value (right)
            float valueW = S(86.f);
            Rect labelR(inner.left, rc.top + S(12.f), inner.right - valueW - S(8.f), rc.top + S(12.f) + block);
            ui.text(Rect(labelR.left, labelR.top, labelR.right, labelR.top + S(22.f)), r.title, titleStyle(ui));
            if (!r.subtitle.empty()) {
                ui.text(Rect(labelR.left, labelR.top + S(24.f), labelR.right, labelR.bottom), r.subtitle, subStyle(ui));
            }
            Rect vr(inner.right - valueW, rc.top + S(10.f), inner.right, rc.top + S(10.f) + S(26.f));
            TextStyle vs(Font::Regular, S(16.f), editing ? pal.label : pal.secondaryLabel, HAlign::Trailing, VAlign::Center);
            if (editing) {
                vs.h = HAlign::Center;
                if (ui.textField(r.id ^ 0x7E5ull, vr, edit->getText(), true, static_cast<size_t>(edit->getCaretLocation()),
                                 edit->shouldBlink(), vs, vr.getHeight() * 0.5f)) {
                    claimedFocus_ = true;
                }
                if (ui.in.pressed && !vr.contains(ui.mouse())) edit->setSelected(false);
            } else {
                auto cr = ui.click(r.id ^ 0x7E6ull, vr, interactive);
                if (cr.hovered) ui.rrect(vr, vr.getHeight() * 0.5f, withAlpha(pal.label, 0.06f));
                ui.text(Rect(vr.left, vr.top, vr.right - S(4.f), vr.bottom), valueStr, vs);
                if (cr.clicked) {
                    blurAll();
                    edit->setSelected(true);
                    edit->setCaretLocation(static_cast<int>(valueStr.size()));
                    claimedFocus_ = true;
                }
            }

            Rect track(inner.left, rc.bottom - S(10.f) - S(15.f) - S(2.f), inner.right, rc.bottom - S(10.f) - S(15.f) + S(2.f));
            float v = *r.number;
            if (ui.slider(r.id, track, v, r.lo, r.hi, r.step)) {
                *r.number = v;
                if (r.changed) r.changed();
            }
        } break;

        case SettingRow::Kind::Choice: {
            if (!r.choice || r.options.empty()) return;
            *r.choice = std::clamp(*r.choice, 0, static_cast<int>(r.options.size()) - 1);
            if (useSegmented(r)) {
                float block = labelBlockHeight(ui, r.subtitle, inner.getWidth());
                Rect labelR(inner.left, rc.top + S(12.f), inner.right, rc.top + S(12.f) + block);
                ui.text(Rect(labelR.left, labelR.top, labelR.right, labelR.top + S(22.f)), r.title, titleStyle(ui));
                if (!r.subtitle.empty()) {
                    ui.text(Rect(labelR.left, labelR.top + S(24.f), labelR.right, labelR.bottom), r.subtitle, subStyle(ui));
                }
                Rect seg(inner.left, rc.bottom - S(12.f) - S(32.f), inner.right, rc.bottom - S(12.f));
                int sel = ui.segmented(r.id, seg, r.options, *r.choice);
                if (sel != *r.choice) {
                    *r.choice = sel;
                    if (r.changed) r.changed();
                }
            } else {
                auto cr = ui.click(r.id, rc, interactive);
                if (cr.pressed) ui.fillCell(rc, pal.cellPressed);
                std::wstring cur = r.options[static_cast<size_t>(*r.choice)];
                float valueW = std::min(ui.textWidth(cur, TextStyle(Font::Regular, S(17.f), pal.secondaryLabel)) + S(4.f),
                                        inner.getWidth() * 0.5f);
                Rect chev(inner.right - S(16.f), rc.centerY() - S(10.f), inner.right, rc.centerY() + S(10.f));
                ui.glyph(Glyph::ChevronUpDown, chev, pal.tertiaryLabel, 1.0f);
                Rect valueR(chev.left - S(6.f) - valueW, rc.top, chev.left - S(6.f), rc.bottom);
                ui.text(valueR, cur, TextStyle(Font::Regular, S(17.f), pal.secondaryLabel, HAlign::Trailing, VAlign::Center));
                drawLabel(ui, Rect(inner.left, rc.top, valueR.left - S(10.f), rc.bottom), r.title, r.subtitle);
                if (cr.clicked) {
                    openPopover(r, Rect(valueR.left, rc.top + S(4.f), chev.right, rc.bottom - S(4.f)));
                }
            }
        } break;

        case SettingRow::Kind::Key: {
            if (!r.keycode) return;
            const bool capturing = capturingRow_ == r.id;
            std::wstring label = capturing ? L"\u2026" : (r.keyText.empty() ? L"\u2014" : r.keyText);
            TextStyle ks(Font::Medium, S(15.f), capturing ? pal.accent : pal.label, HAlign::Center, VAlign::Center);
            float tw = ui.textWidth(label, ks);
            float cw = std::clamp(tw + S(26.f), S(64.f), inner.getWidth() * 0.45f);
            Rect chip(inner.right - cw, rc.centerY() - S(15.f), inner.right, rc.centerY() + S(15.f));
            drawLabel(ui, Rect(inner.left, rc.top, chip.left - S(12.f), rc.bottom), r.title, r.subtitle);

            auto cr = ui.click(r.id, chip, interactive);
            float hot = ui.anim(r.id ^ 0x40Full, (capturing || cr.hovered) ? 1.f : 0.f, 0.07f);
            ui.rrect(chip, chip.getHeight() * 0.5f, mix(pal.tertiaryFill, pal.fill, 0.6f * hot));
            if (capturing) ui.rrectStroke(chip, chip.getHeight() * 0.5f, pal.accent, std::max(1.5f, S(1.5f)));
            ui.text(chip, label, ks);
            if (cr.clicked && !capturing) {
                blurAll();
                capturingRow_ = r.id;
                capturedKey_ = 0;
            } else if (ui.in.pressed && capturing && !chip.contains(ui.mouse())) {
                capturingRow_ = 0;
            }
            if (interactive && ui.in.rightPressed && ui.hit(chip)) {
                if (r.clear) r.clear();
                capturingRow_ = 0;
            }
            if (capturingRow_ == r.id && capturedKey_ > 0) {
                *r.keycode = capturedKey_;
                if (r.changed) r.changed();
                capturingRow_ = 0;
                capturedKey_ = 0;
            }
        } break;

        case SettingRow::Kind::Color: {
            auto cr = ui.click(r.id, rc, interactive);
            if (cr.pressed) ui.fillCell(rc, pal.cellPressed);
            Vec2 c = { inner.right - S(15.f), rc.centerY() };
            drawColorWell(ui, c, S(14.f), r.color);
            drawLabel(ui, Rect(inner.left, rc.top, c.x - S(26.f), rc.bottom), r.title, r.subtitle);
            if (cr.clicked) openColorSheet(r);
        } break;

        case SettingRow::Kind::Text: {
            if (!r.str) return;
            auto edit = editFor(r.id ^ 0x7E87ull, r.maxChars);
            if (!edit->isSelected()) {
                edit->setText(*r.str);
            } else if (*r.str != edit->getText()) {
                *r.str = edit->getText();
                if (r.changed) r.changed();
            }
            float fw = std::min(S(280.f), inner.getWidth() * 0.5f);
            Rect field(inner.right - fw, rc.centerY() - S(16.f), inner.right, rc.centerY() + S(16.f));
            drawLabel(ui, Rect(inner.left, rc.top, field.left - S(12.f), rc.bottom), r.title, r.subtitle);
            TextStyle ts(Font::Regular, S(16.f), pal.label, HAlign::Leading, VAlign::Center);
            if (ui.textField(r.id, field, edit->getText(), edit->isSelected(),
                             static_cast<size_t>(edit->getCaretLocation()), edit->shouldBlink(), ts, field.getHeight() * 0.5f) &&
                interactive) {
                blurAll();
                edit->setSelected(true);
                edit->setCaretLocation(static_cast<int>(edit->getText().size()));
                claimedFocus_ = true;
            }
        } break;

        case SettingRow::Kind::Action: {
            auto cr = ui.click(r.id, rc, interactive);
            if (cr.pressed) ui.fillCell(rc, pal.cellPressed);
            ui.text(rc, r.title, TextStyle(Font::Regular, S(17.f), pal.accent, HAlign::Center, VAlign::Center));
            if (cr.clicked && r.action) r.action();
        } break;
        }
    }

    // ======================================================================================== popover
    void Shell::openPopover(SettingRow const& row, Rect const& anchor) {
        pop_.open = true;
        pop_.row = row.id;
        pop_.anchor = anchor;
        pop_.options = row.options;
        pop_.selected = row.choice ? *row.choice : 0;
        pop_.scroll = 0.f;
        int* choice = row.choice;
        auto changed = row.changed;
        pop_.pick = [choice, changed](int i) {
            if (choice) *choice = i;
            if (changed) changed();
        };
        blurAll();
    }

    void Shell::drawPopover(Ui& ui, Rect const& win) {
        auto S = [&](float v) { return ui.S(v); };
        const Palette& pal = ui.pal;
        float sp = ui.spring(kIdPopSpring, pop_.open ? 1.f : 0.f, 0.34f, 0.80f);
        float a = clamp01(sp);
        if (a < 0.01f) return;
        const float PR = S(22.f);

        const int n = static_cast<int>(pop_.options.size());
        const float rowH = S(44.f);
        float maxText = 0.f;
        TextStyle ts(Font::Regular, S(17.f), pal.label);
        for (auto const& o : pop_.options) maxText = std::max(maxText, ui.textWidth(o, ts));
        float w = std::clamp(maxText + S(80.f), S(200.f), S(360.f));
        float contentH = rowH * static_cast<float>(n);
        float h = std::min(contentH, std::min(win.getHeight() * 0.72f, rowH * 8.5f));

        float x = pop_.anchor.right - w;
        x = std::clamp(x, win.left + S(12.f), win.right - w - S(12.f));
        float y = pop_.anchor.bottom + S(6.f);
        bool below = true;
        if (y + h > win.bottom - S(12.f)) {
            y = pop_.anchor.top - S(6.f) - h;
            below = false;
        }
        y = std::clamp(y, win.top + S(12.f), win.bottom - h - S(12.f));
        Rect pr(x, y, x + w, y + h);

        D2D1::Matrix3x2F old;
        ui.ctx()->GetTransform(&old);
        float sc = 0.82f + 0.18f * sp;
        D2D1_POINT_2F origin = D2D1::Point2F(pr.right, below ? pr.top : pr.bottom);
        ui.ctx()->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, origin) * D2D1::Matrix3x2F(old));
        ui.pushOpacity(pr, a);

        const bool interactive = pop_.open && a > 0.85f;
        ui.lockInput(!interactive);

        {
            // menu: regular glass with a little extra frost so the labels stay legible over the page
            GlassStyle gs;
            gs.shadow = 1.4f;
            ui.glass(pr, PR, gs);
            ui.fillSmooth(pr, PR, pal.dark ? Col(0.13f, 0.13f, 0.145f, 0.72f) : Col(0.985f, 0.985f, 0.995f, 0.66f));
        }

        float maxScroll = std::max(0.f, contentH - h);
        if (interactive && ui.in.wheel != 0.f && pr.contains(ui.mouse())) {
            pop_.scroll = std::clamp(pop_.scroll + ui.in.wheel, 0.f, maxScroll);
        }
        pop_.scroll = std::clamp(pop_.scroll, 0.f, maxScroll);

        ui.pushClipRRect(pr, PR);
        int picked = -1;
        for (int i = 0; i < n; ++i) {
            Rect rr(pr.left, pr.top + rowH * static_cast<float>(i) - pop_.scroll, pr.right,
                    pr.top + rowH * static_cast<float>(i + 1) - pop_.scroll);
            if (rr.bottom < pr.top || rr.top > pr.bottom) continue;
            auto cr = ui.click(Ui::hashId("popopt") ^ static_cast<uint64_t>(i), rr, interactive);
            if (cr.hovered || cr.pressed) ui.rrect(inset(rr, S(8.f), S(2.f)), S(16.f), withAlpha(pal.label, cr.pressed ? 0.12f : 0.07f));
            bool sel = i == pop_.selected;
            ui.text(Rect(rr.left + S(18.f), rr.top, rr.right - S(44.f), rr.bottom), pop_.options[static_cast<size_t>(i)],
                    TextStyle(sel ? Font::Semibold : Font::Regular, S(17.f), pal.label, HAlign::Leading, VAlign::Center));
            if (sel) {
                ui.glyph(Glyph::Check, Rect(rr.right - S(38.f), rr.centerY() - S(10.f), rr.right - S(18.f), rr.centerY() + S(10.f)),
                         pal.accent, 1.15f);
            }
            if (i + 1 < n) {
                ui.hairlineH(rr.left + S(18.f), rr.right - S(18.f), rr.bottom - std::max(1.f, std::round(ui.scale * 0.5f)), pal.separator);
            }
            if (cr.clicked) picked = i;
        }
        ui.popClipRRect();
        ui.popOpacity();
        ui.ctx()->SetTransform(old);

        if (picked >= 0 && pop_.open) {
            if (pop_.pick) pop_.pick(picked);
            pop_.selected = picked;
            pop_.open = false;
        } else if (pop_.open && interactive && ui.in.pressed && !pr.contains(ui.mouse())) {
            pop_.open = false;
        }
    }

    // ======================================================================================== colour sheet
    void Shell::openColorSheet(SettingRow const& row) {
        sheet_ = ColorSheet {};
        sheet_.open = true;
        sheet_.row = row.id;
        sheet_.title = row.title;
        rgb2hsv(row.color.r, row.color.g, row.color.b, sheet_.h, sheet_.s, sheet_.v);
        sheet_.a = row.color.a;
        sheet_.rgb = row.rgb;
        sheet_.hasRgb = row.hasRgbToggle;
        sheet_.apply = row.setColor;
        sheet_.hex = makeEdit_(7);
        sheet_.hex->setMaxChars(7);
        blurAll();
    }

    void Shell::drawColorSheet(Ui& ui, Frame& f, Rect const& win) {
        auto S = [&](float v) { return ui.S(v); };
        const Palette& pal = ui.pal;
        float sp = ui.spring(kIdSheet, sheet_.open ? 1.f : 0.f, 0.46f, 0.84f);
        float a = clamp01(sp);
        if (a < 0.01f) return;
        const bool interactive = sheet_.open && a > 0.9f;
        ui.lockInput(!interactive);

        // scrim
        ui.fillRect(win, Col(0.f, 0.f, 0.f, 0.34f * a));

        const float sw = std::min(S(420.f), win.getWidth() - S(24.f));
        const float pad = S(22.f);
        const float headH = S(68.f);
        const float svH = S(190.f);
        float sheetH = headH + S(8.f) + S(44.f) + S(16.f) + svH + S(18.f) + S(26.f) + S(16.f) + S(26.f) + S(22.f);
        if (sheet_.hasRgb) sheetH += S(52.f);
        // floating bottom sheet: rises with a spring, follows a pull-down on the header
        float dragShown = ui.spring(kIdSheetDragSpring, sheet_.dragOff, 0.18f, 0.92f);
        float rise = (1.f - sp) * (sheetH + S(40.f));
        const float bottom = win.bottom - S(12.f) + rise + dragShown;
        Rect sr(win.centerX() - sw * 0.5f, bottom - sheetH, win.centerX() + sw * 0.5f, bottom);

        const float SR = S(34.f);
        ui.pushOpacity(sr, a);
        {
            GlassStyle gs;
            gs.shadow = 1.6f;
            ui.glass(sr, SR, gs);
            ui.fillSmooth(sr, SR, pal.dark ? Col(0.12f, 0.12f, 0.135f, 0.86f) : Col(0.985f, 0.985f, 0.995f, 0.80f));
        }
        // grabber
        ui.rrect(Rect(sr.centerX() - S(18.f), sr.top + S(7.f), sr.centerX() + S(18.f), sr.top + S(12.f)), S(2.5f),
                 pal.tertiaryLabel);

        // header: circular glass close (leading) + prominent confirm (trailing), title between them
        {
            const float bd = S(40.f);
            Rect xR(sr.left + S(16.f), sr.top + S(16.f), sr.left + S(16.f) + bd, sr.top + S(16.f) + bd);
            Rect okR(sr.right - S(16.f) - bd, xR.top, sr.right - S(16.f), xR.bottom);
            ui.text(Rect(sr.left + S(70.f), xR.top, sr.right - S(70.f), xR.bottom),
                    sheet_.title.empty() ? f.str.color : sheet_.title,
                    TextStyle(Font::Semibold, S(17.f), pal.label, HAlign::Center, VAlign::Center));
            bool close = false;
            if (ui.glassIconButton(Ui::hashId("sheetclose"), xR, Glyph::XMark, Col(0.f, 0.f, 0.f, 0.f), 0.40f, interactive)) close = true;
            if (ui.glassIconButton(Ui::hashId("sheetdone"), okR, Glyph::Check, withAlpha(pal.accent, 0.93f), 0.46f, interactive)) close = true;
            if (close) {
                sheet_.open = false;
                sheet_.dragOff = 0.f;
                blurAll();
            }

            // the rest of the header is a drag handle (registered after the buttons so they win the press)
            auto d = ui.drag(kIdSheetDrag, Rect(sr.left, sr.top, sr.right, sr.top + headH));
            if (interactive) {
                if (d.began) {
                    sheet_.dragging = true;
                    sheet_.dragStartY = ui.mouse().y - sheet_.dragOff;
                }
                if (d.active && sheet_.dragging) {
                    sheet_.dragOff = std::max(0.f, ui.mouse().y - sheet_.dragStartY);
                } else if (sheet_.dragging && !d.active) {
                    sheet_.dragging = false;
                    if (sheet_.dragOff > sheetH * 0.22f) {
                        sheet_.open = false;
                        blurAll();
                    }
                    sheet_.dragOff = 0.f;
                }
            }
        }

        bool changed = false;
        float y = sr.top + headH + S(14.f);

        // preview + hex
        Col cur;
        {
            float rr, gg, bb;
            hsv2rgb(sheet_.h, sheet_.s, sheet_.v, rr, gg, bb);
            cur = Col(rr, gg, bb, sheet_.a);
        }
        Vec2 pc = { sr.left + pad + S(22.f), y + S(22.f) };
        drawColorWell(ui, pc, S(22.f), cur);

        // hex field
        auto hex = sheet_.hex;
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", static_cast<int>(std::lround(cur.r * 255.f)),
                          static_cast<int>(std::lround(cur.g * 255.f)), static_cast<int>(std::lround(cur.b * 255.f)));
            std::wstring want(buf, buf + std::strlen(buf));
            if (!hex->isSelected()) {
                hex->setText(want);
                hex->setCaretLocation(static_cast<int>(want.size()));
            } else {
                std::wstring typed = hex->getText();
                if (!typed.empty() && typed[0] == L'#') typed.erase(0, 1);
                if (typed.size() == 6) {
                    bool ok = true;
                    for (wchar_t c : typed) ok = ok && std::iswxdigit(c);
                    if (ok) {
                        unsigned v = static_cast<unsigned>(std::wcstoul(typed.c_str(), nullptr, 16));
                        float rr = static_cast<float>((v >> 16) & 0xFF) / 255.f;
                        float gg = static_cast<float>((v >> 8) & 0xFF) / 255.f;
                        float bb = static_cast<float>(v & 0xFF) / 255.f;
                        float nh, ns, nv;
                        rgb2hsv(rr, gg, bb, nh, ns, nv);
                        if (std::fabs(rr - cur.r) > 0.003f || std::fabs(gg - cur.g) > 0.003f ||
                            std::fabs(bb - cur.b) > 0.003f) {
                            sheet_.h = ns > 0.001f ? nh : sheet_.h;
                            sheet_.s = ns;
                            sheet_.v = nv;
                            changed = true;
                        }
                    }
                }
            }
        }
        Rect hexR(pc.x + S(22.f) + S(14.f), y + S(5.f), pc.x + S(22.f) + S(14.f) + S(132.f), y + S(39.f));
        TextStyle hs(Font::Medium, S(17.f), pal.label, HAlign::Center, VAlign::Center);
        if (ui.textField(Ui::hashId("sheethex"), hexR, hex->getText(), hex->isSelected(),
                         static_cast<size_t>(hex->getCaretLocation()), hex->shouldBlink(), hs, hexR.getHeight() * 0.5f) &&
            interactive) {
            hex->setSelected(true);
            hex->setCaretLocation(static_cast<int>(hex->getText().size()));
            claimedFocus_ = true;
        }
        {
            wchar_t ob[16];
            swprintf(ob, 16, L"%d%%", static_cast<int>(std::lround(sheet_.a * 100.f)));
            ui.text(Rect(hexR.right + S(12.f), y, sr.right - pad, y + S(44.f)), ob,
                    TextStyle(Font::Regular, S(15.f), pal.secondaryLabel, HAlign::Trailing, VAlign::Center));
        }
        y += S(44.f) + S(16.f);

        // saturation / value box
        {
            Rect box(sr.left + pad, y, sr.right - pad, y + svH);
            float rr, gg, bb;
            hsv2rgb(sheet_.h, 1.f, 1.f, rr, gg, bb);
            ui.pushClipRRect(box, S(20.f));
            fillGradient(ui, box, 0.f,
                         { D2D1_GRADIENT_STOP { 0.f, D2D1::ColorF(1.f, 1.f, 1.f, 1.f) },
                           D2D1_GRADIENT_STOP { 1.f, D2D1::ColorF(rr, gg, bb, 1.f) } },
                         false);
            fillGradient(ui, box, 0.f,
                         { D2D1_GRADIENT_STOP { 0.f, D2D1::ColorF(0.f, 0.f, 0.f, 0.f) },
                           D2D1_GRADIENT_STOP { 1.f, D2D1::ColorF(0.f, 0.f, 0.f, 1.f) } },
                         true);
            ui.popClipRRect();
            auto d = ui.drag(Ui::hashId("sheetsv"), box);
            if (interactive && (d.active || d.began)) {
                Vec2 m = ui.mouse();
                sheet_.s = clamp01((m.x - box.left) / box.getWidth());
                sheet_.v = 1.f - clamp01((m.y - box.top) / box.getHeight());
                changed = true;
            }
            Vec2 k = { box.left + box.getWidth() * sheet_.s, box.top + box.getHeight() * (1.f - sheet_.v) };
            float kr = S(11.f);
            ui.shadow(Rect(k.x - kr, k.y - kr, k.x + kr, k.y + kr), kr, S(6.f), S(2.f), Col(0.f, 0.f, 0.f, 0.35f));
            float r2, g2, b2;
            hsv2rgb(sheet_.h, sheet_.s, sheet_.v, r2, g2, b2);
            ui.circle(k, kr, Col(r2, g2, b2, 1.f));
            ui.circleStroke(k, kr - S(1.5f), Col(1.f, 1.f, 1.f, 1.f), S(3.f));
            y = box.bottom + S(18.f);
        }

        // slider helper (hue / opacity)
        auto bar = [&](uint64_t id, float t, bool alphaBar, float& outT) {
            Rect b(sr.left + pad, y, sr.right - pad, y + S(26.f));
            ui.pushClipRRect(b, b.getHeight() * 0.5f);
            if (alphaBar) {
                drawChecker(ui, b, S(7.f));
                float rr, gg, bb;
                hsv2rgb(sheet_.h, sheet_.s, sheet_.v, rr, gg, bb);
                fillGradient(ui, b, 0.f,
                             { D2D1_GRADIENT_STOP { 0.f, D2D1::ColorF(rr, gg, bb, 0.f) },
                               D2D1_GRADIENT_STOP { 1.f, D2D1::ColorF(rr, gg, bb, 1.f) } },
                             false);
            } else {
                std::vector<D2D1_GRADIENT_STOP> stops;
                for (int i = 0; i <= 6; ++i) {
                    float rr, gg, bb;
                    hsv2rgb(60.f * static_cast<float>(i), 1.f, 1.f, rr, gg, bb);
                    stops.push_back({ static_cast<float>(i) / 6.f, D2D1::ColorF(rr, gg, bb, 1.f) });
                }
                fillGradient(ui, b, 0.f, stops, false);
            }
            ui.popClipRRect();
            ui.strokeSmooth(b, b.getHeight() * 0.5f, withAlpha(pal.label, 0.12f), std::max(1.f, S(0.75f)));
            Rect hit(b.left - S(10.f), b.top - S(6.f), b.right + S(10.f), b.bottom + S(6.f));
            auto d = ui.drag(id, hit);
            outT = t;
            bool ch = false;
            if (interactive && (d.active || d.began)) {
                outT = clamp01((ui.mouse().x - b.left) / b.getWidth());
                ch = true;
            }
            Vec2 k = { b.left + b.getHeight() * 0.5f + (b.getWidth() - b.getHeight()) * outT, b.centerY() };
            float kr = b.getHeight() * 0.5f + S(1.f);
            ui.shadow(Rect(k.x - kr, k.y - kr, k.x + kr, k.y + kr), kr, S(6.f), S(2.f), Col(0.f, 0.f, 0.f, 0.3f));
            ui.circle(k, kr, Col(1.f, 1.f, 1.f, 1.f));
            float rr, gg, bb;
            if (alphaBar) {
                hsv2rgb(sheet_.h, sheet_.s, sheet_.v, rr, gg, bb);
                ui.circle(k, kr - S(3.f), Col(rr, gg, bb, std::max(0.05f, outT)));
            } else {
                hsv2rgb(outT * 360.f, 1.f, 1.f, rr, gg, bb);
                ui.circle(k, kr - S(3.f), Col(rr, gg, bb, 1.f));
            }
            y = b.bottom + S(16.f);
            return ch;
        };
        {
            float t;
            if (bar(Ui::hashId("sheethue"), sheet_.h / 360.f, false, t)) {
                sheet_.h = std::min(359.99f, t * 360.f);
                changed = true;
            }
            if (bar(Ui::hashId("sheetalpha"), sheet_.a, true, t)) {
                sheet_.a = std::round(t * 100.f) / 100.f;
                changed = true;
            }
        }

        if (sheet_.hasRgb) {
            Rect rr(sr.left + pad, y + S(2.f), sr.right - pad, y + S(2.f) + S(44.f));
            ui.fillSmooth(rr, S(22.f), pal.cell);
            ui.text(Rect(rr.left + S(16.f), rr.top, rr.right - S(80.f), rr.bottom), f.str.rgb,
                    TextStyle(Font::Regular, S(17.f), pal.label, HAlign::Leading, VAlign::Center));
            bool v = sheet_.rgb;
            Rect sw(rr.right - S(16.f) - S(kSwitchW), rr.centerY() - S(kSwitchH * 0.5f), rr.right - S(16.f), rr.centerY() + S(kSwitchH * 0.5f));
            if (ui.toggle(Ui::hashId("sheetrgb"), sw, v, interactive)) {
                sheet_.rgb = v;
                changed = true;
            }
        }
        ui.popOpacity();

        if (changed && sheet_.apply && sheet_.open) {
            float rr, gg, bb;
            hsv2rgb(sheet_.h, sheet_.s, sheet_.v, rr, gg, bb);
            sheet_.apply(Col(rr, gg, bb, sheet_.a), sheet_.rgb);
        }
        if (sheet_.open && interactive && ui.in.pressed && !sr.contains(ui.mouse())) {
            sheet_.open = false;
            blurAll();
        }
    }

} // namespace ios
