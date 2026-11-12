#include "IosCards.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace ios {

    namespace {
        float lerpf(float a, float b, float t) {
            return a + (b - a) * t;
        }

        std::wstring clockText(double ms) {
            int total = static_cast<int>(std::max(0.0, ms) / 1000.0);
            wchar_t buf[24];
            swprintf(buf, 24, L"%d:%02d", total / 60, total % 60);
            return buf;
        }
    } // namespace

    // ======================================================================================== Now Playing
    void drawNowPlaying(Ui& ui, NowPlayingData const& d, NowPlayingState& state) {
        const float W = d.width, H = nowPlayingHeight(d);
        const float kPad = 12.f, kArt = 64.f, kRadius = 22.f;
        const Rect plate(0.f, 0.f, W, H);

        // ---- glass plate, tinted with the average colour of the artwork (like the system Now Playing card)
        {
            GlassStyle gs;
            gs.tint = Col(d.tint.r, d.tint.g, d.tint.b, 0.55f);
            gs.shadow = 0.9f;
            ui.glass(plate, kRadius, gs);
            ui.fillSmooth(plate, kRadius, Col(0.f, 0.f, 0.f, 0.20f)); // a little depth so that white text always reads
        }

        // ---- artwork
        Rect artR(kPad, kPad, kPad + kArt, kPad + kArt);
        if (d.showArt) {
            ui.shadow(artR, 12.f, 10.f, 3.f, Col(0.f, 0.f, 0.f, 0.35f));
            if (d.artBrush) {
                d.artBrush->SetTransform(D2D1::Matrix3x2F::Scale(kArt / 128.f, kArt / 128.f) *
                                         D2D1::Matrix3x2F::Translation(artR.left, artR.top));
                ui.fillBrush(artR, 12.f, d.artBrush);
                ui.rrectStroke(artR, 12.f, Col(1.f, 1.f, 1.f, 0.16f), 1.f);
            } else {
                ui.rrect(artR, 12.f, Col(1.f, 1.f, 1.f, 0.14f));
                ui.text(artR, L"\u266A", TextStyle(Font::Semibold, 30.f, Col(1.f, 1.f, 1.f, 0.65f), HAlign::Center, VAlign::Center));
            }
        }

        // ---- title / artist / progress
        const float textX = d.showArt ? kPad + kArt + 12.f : kPad + 4.f;
        const float right = W - kPad - 4.f;
        const bool progress = d.showProgress && d.durationMs > 0;
        const float shift = progress ? 0.f : 6.f;
        const float eqW = 16.f;
        ui.text(Rect(textX, 12.f + shift, right - eqW - 6.f, 34.f + shift), d.available ? d.title : L"Spotify",
                TextStyle(Font::Semibold, 16.f, Col(1.f, 1.f, 1.f, 1.f), HAlign::Leading, VAlign::Center));
        ui.text(Rect(textX, 34.f + shift, right - eqW - 6.f, 52.f + shift), d.artist,
                TextStyle(Font::Regular, 13.f, Col(1.f, 1.f, 1.f, 0.68f), HAlign::Leading, VAlign::Center));

        // play state: animated bars while playing, a pause mark otherwise
        {
            const float ex = right - eqW, ey = 16.f;
            if (d.playing) {
                const double t = static_cast<double>(GetTickCount64() % 100000) / 1000.0;
                for (int i = 0; i < 3; ++i) {
                    const float h = 4.f + 10.f * (0.5f + 0.5f * static_cast<float>(std::sin(t * (5.3 + i * 1.7) + i * 1.9)));
                    ui.rrect(Rect(ex + i * 5.5f, ey + 14.f - h, ex + i * 5.5f + 3.f, ey + 14.f), 1.5f, Col(1.f, 1.f, 1.f, 0.92f));
                }
            } else {
                ui.rrect(Rect(ex + 3.f, ey + 1.f, ex + 6.5f, ey + 14.f), 1.5f, Col(1.f, 1.f, 1.f, 0.8f));
                ui.rrect(Rect(ex + 9.5f, ey + 1.f, ex + 13.f, ey + 14.f), 1.5f, Col(1.f, 1.f, 1.f, 0.8f));
            }
        }

        if (progress) {
            const float frac = static_cast<float>(std::clamp(d.positionMs / static_cast<double>(d.durationMs), 0.0, 1.0));
            Rect track(textX, 62.f, right, 66.f);
            ui.rrect(track, 2.f, Col(1.f, 1.f, 1.f, 0.24f));
            if (frac > 0.002f) {
                ui.rrect(Rect(track.left, track.top, track.left + std::max(4.f, track.getWidth() * frac), track.bottom), 2.f, Col(1.f, 1.f, 1.f, 0.95f));
            }
            TextStyle ts(Font::Medium, 10.5f, Col(1.f, 1.f, 1.f, 0.55f), HAlign::Leading, VAlign::Center);
            ui.text(Rect(textX, 68.f, textX + 70.f, 82.f), clockText(d.positionMs), ts);
            ts.h = HAlign::Trailing;
            ui.text(Rect(right - 70.f, 68.f, right, 82.f), clockText(static_cast<double>(d.durationMs)), ts);
        } else if (d.showProgress && d.available) {
            // length unknown: only the elapsed time
            ui.text(Rect(textX, 62.f, textX + 90.f, 78.f), clockText(d.positionMs),
                    TextStyle(Font::Medium, 10.5f, Col(1.f, 1.f, 1.f, 0.55f), HAlign::Leading, VAlign::Center));
        }

        // ---- lyrics: the current line in the middle, its neighbours fading out above and below
        if (!d.showLyrics) return;
        ui.rrect(Rect(kPad + 4.f, kNowPlayingTopH - 2.f, W - kPad - 4.f, kNowPlayingTopH - 1.f), 0.5f, Col(1.f, 1.f, 1.f, 0.14f));
        const float cy = kNowPlayingTopH + kNowPlayingLyricH * 0.5f - 2.f;
        const float lineH = 25.f;
        const float availW = W - kPad * 2.f - 8.f;
        auto note = [&](std::wstring const& text) {
            ui.text(Rect(kPad, cy - 14.f, W - kPad, cy + 14.f), text,
                    TextStyle(Font::Medium, 13.f, Col(1.f, 1.f, 1.f, 0.5f), HAlign::Center, VAlign::Center));
        };

        using Lyric = NowPlayingData::Lyric;
        if (!d.available) {
            note(d.textIdle);
        } else if (d.lyric == Lyric::Idle || d.lyric == Lyric::Loading) {
            note(d.textLoading);
        } else if (d.lyric == Lyric::Instrumental) {
            note(L"\u266A  " + d.textInstrumental);
        } else if (d.lyric != Lyric::Ready || d.lines.empty()) {
            note(d.textNone);
        } else {
            static const uint64_t kSlide = Ui::hashId("ma4.nowplaying.slide");
            const int idx = d.line;
            if (idx != state.lastLine) {
                // the lines slide up by one slot; the first line shown after a track change just appears
                if (state.lastLine != -2) ui.springSet(kSlide, 1.f);
                state.lastLine = idx;
            }
            const float slide = ui.spring(kSlide, 0.f, 0.42f, 0.86f);

            for (int j = idx - 2; j <= idx + 2; ++j) {
                const int k = j - d.firstLine;
                if (k < 0 || k >= static_cast<int>(d.lines.size())) continue;
                const float rel = static_cast<float>(j - idx) + slide;
                const float a = std::fabs(rel);
                const float alpha = a <= 1.f ? lerpf(1.f, 0.40f, a) : lerpf(0.40f, 0.f, std::min(1.f, a - 1.f));
                if (alpha <= 0.02f) continue;
                std::wstring text = d.lines[static_cast<size_t>(k)];
                if (text.empty()) text = L"\u266A";
                const float size = std::round(lerpf(16.5f, 12.5f, std::min(1.f, a)) * 2.f) / 2.f;
                TextStyle ts(a < 0.5f ? Font::Bold : Font::Medium, size, Col(1.f, 1.f, 1.f, alpha), HAlign::Center, VAlign::Center);
                const float w = ui.textWidth(text, ts);
                if (w > availW) ts.size = std::max(10.f, std::floor(size * std::max(0.72f, availW / w) * 2.f) / 2.f);
                const float y = cy + rel * lineH;
                ui.text(Rect(kPad + 4.f, y - 14.f, W - kPad - 4.f, y + 14.f), text, ts);
            }
        }
    }

    // ======================================================================================== Tab list
    namespace {
        // Minecraft formatting codes (the section sign + a character) -> coloured, optionally bold runs
        struct Run {
            std::wstring text;
            Col color;
            bool bold = false;
        };

        bool codeColor(wchar_t c, Col& out) {
            struct Entry {
                wchar_t code;
                int r, g, b;
            };
            static const Entry table[] = {
                { L'0', 0, 0, 0 },       { L'1', 0, 0, 170 },    { L'2', 0, 170, 0 },     { L'3', 0, 170, 170 },    { L'4', 170, 0, 0 },
                { L'5', 170, 0, 170 },   { L'6', 255, 170, 0 },  { L'7', 170, 170, 170 }, { L'8', 85, 85, 85 },     { L'9', 85, 85, 255 },
                { L'a', 85, 255, 85 },   { L'b', 85, 255, 255 }, { L'c', 255, 85, 85 },   { L'd', 255, 85, 255 },   { L'e', 255, 255, 85 },
                { L'f', 255, 255, 255 }, { L'g', 221, 214, 5 },  { L'h', 227, 212, 209 }, { L'i', 206, 202, 202 },  { L'j', 68, 58, 59 },
                { L'm', 151, 22, 7 },    { L'n', 180, 104, 77 }, { L'p', 222, 177, 45 },  { L'q', 17, 160, 54 },    { L's', 44, 186, 168 },
                { L't', 33, 73, 123 },   { L'u', 154, 92, 198 }, { L'v', 235, 114, 20 },
            };
            for (auto const& e : table) {
                if (e.code == c) {
                    out = Col(static_cast<float>(e.r) / 255.f, static_cast<float>(e.g) / 255.f, static_cast<float>(e.b) / 255.f, 1.f);
                    return true;
                }
            }
            return false;
        }

        // The colours were made for the game's black chat background: on the dark glass a dark one (black, dark blue,
        // dark grey) would be unreadable, so it is lifted towards white.
        Col readable(Col c) {
            const float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            if (lum >= 0.30f) return c;
            const float k = 0.62f * (1.f - lum / 0.30f) + 0.18f;
            return Col(c.r + (1.f - c.r) * k, c.g + (1.f - c.g) * k, c.b + (1.f - c.b) * k, c.a);
        }

        std::vector<Run> parseRuns(std::wstring const& text, Col defaultColor) {
            std::vector<Run> runs;
            Col color = defaultColor;
            bool bold = false;
            std::wstring cur;
            auto flush = [&]() {
                if (!cur.empty()) {
                    runs.push_back({ cur, color, bold });
                    cur.clear();
                }
            };
            for (size_t i = 0; i < text.size(); ++i) {
                const wchar_t ch = text[i];
                if (ch == 0x00A7 && i + 1 < text.size()) {
                    const wchar_t code = static_cast<wchar_t>(std::towlower(text[++i]));
                    Col c;
                    if (codeColor(code, c)) {
                        flush();
                        color = readable(c);
                        bold = false;
                    } else if (code == L'l') {
                        flush();
                        bold = true;
                    } else if (code == L'r') {
                        flush();
                        color = defaultColor;
                        bold = false;
                    }
                    // o (italic), k (obfuscated), m / n (strike / underline in the Java edition) are not drawn
                    continue;
                }
                cur.push_back(ch);
            }
            flush();
            return runs;
        }

        // DirectWrite does not count trailing spaces in the width of a run ("[VIP] " + "Notch" would touch): measure with a
        // visible character appended and subtract it
        float runWidth(Ui& ui, std::wstring const& text, TextStyle st) {
            st.ellipsis = false;
            if (!text.empty() && text.back() == L' ') return ui.textWidth(text + L"|", st) - ui.textWidth(L"|", st);
            return ui.textWidth(text, st);
        }

        struct Layout {
            float rowH = 0, avatar = 0, pad = 0, gap = 0, colW = 0, nameX = 0, headerH = 0, panelW = 0, panelH = 0;
            size_t perCol = 16, cols = 1;
        };
    } // namespace

    void drawTabList(Ui& ui, TabData const& d, uint64_t popId, bool restart) {
        const size_t n = d.rows.size();
        std::vector<std::vector<Run>> runs(n);
        Layout L;

        auto layout = [&]() {
            auto P = [&](float v) { return ui.S(v); };
            L.rowH = P(38.f);
            L.avatar = P(26.f);
            L.pad = P(12.f);
            L.gap = P(10.f);
            L.headerH = P(62.f);
            L.nameX = P(12.f) + L.avatar + P(10.f);
            const float rightPad = P(14.f);

            float longest = 0.f;
            for (size_t i = 0; i < n; ++i) {
                runs[i] = parseRuns(d.rows[i].text, d.textColor);
                float w = 0.f;
                for (auto const& r : runs[i]) {
                    TextStyle st(r.bold ? Font::Bold : Font::Medium, P(16.f), r.color);
                    w += runWidth(ui, r.text, st);
                }
                longest = std::max(longest, w);
            }
            TextStyle titleStyle(Font::Bold, P(20.f), d.textColor);
            titleStyle.ellipsis = false;
            const float titleW = ui.textWidth(d.title, titleStyle) + P(40.f);

            L.colW = std::clamp(L.nameX + longest + rightPad, P(190.f), P(440.f));
            const float maxRowsH = d.screenH * 0.80f - L.headerH - L.pad * 2.f;
            L.perCol = std::clamp(static_cast<size_t>(std::max(0.f, std::floor(maxRowsH / L.rowH))), static_cast<size_t>(6), static_cast<size_t>(16));
            L.cols = std::max<size_t>(1, (n + L.perCol - 1) / L.perCol);
            const size_t tallest = std::min(n, L.perCol);
            const float bodyW = static_cast<float>(L.cols) * L.colW + static_cast<float>(L.cols - 1) * L.gap;
            L.panelW = std::max(bodyW, titleW) + L.pad * 2.f;
            L.panelH = L.pad + L.headerH + static_cast<float>(tallest) * L.rowH + L.pad;
        };

        layout();
        if (L.panelW > d.screenW * 0.94f) {
            ui.scale *= std::max(0.4f, d.screenW * 0.94f / L.panelW);
            layout();
        }
        auto P = [&](float v) { return ui.S(v); };

        const float x0 = std::round((d.screenW - L.panelW) * 0.5f);
        const float y0 = P(20.f);
        Rect panel(x0, y0, x0 + L.panelW, y0 + L.panelH);
        const float R = P(26.f);

        // ---- pop-in
        if (restart) ui.springSet(popId, 0.f);
        const float pop = ui.spring(popId, 1.f, 0.40f, 0.82f);
        ui.pushScale(panel, 0.94f + 0.06f * std::clamp(pop, 0.f, 1.2f));
        ui.pushOpacity(Rect(panel.left - P(40.f), panel.top - P(40.f), panel.right + P(40.f), panel.bottom + P(60.f)), clamp01(pop * 1.6f));

        {
            GlassStyle gs;
            gs.shadow = 1.2f;
            ui.glass(panel, R, gs);
            // the "background colour" setting decides how dark the glass is
            ui.fillSmooth(panel, R, Col(d.veil.r, d.veil.g, d.veil.b, std::clamp(d.veil.a * 0.6f, 0.f, 0.85f)));
        }

        // ---- header
        {
            TextStyle ts(Font::Bold, P(20.f), d.textColor, HAlign::Center, VAlign::Center);
            ui.text(Rect(panel.left + L.pad, panel.top + P(10.f), panel.right - L.pad, panel.top + P(36.f)), d.title, ts);
            TextStyle sub(Font::Medium, P(13.f), Col(d.textColor.r, d.textColor.g, d.textColor.b, 0.62f), HAlign::Center, VAlign::Center);
            ui.text(Rect(panel.left + L.pad, panel.top + P(36.f), panel.right - L.pad, panel.top + P(56.f)), d.subtitle, sub);
        }

        // ---- columns of rounded lists
        const float bodyTop = panel.top + L.pad + L.headerH;
        const float bodyW = static_cast<float>(L.cols) * L.colW + static_cast<float>(L.cols - 1) * L.gap;
        const float bodyLeft = panel.left + (L.panelW - bodyW) * 0.5f;

        for (size_t col = 0; col < L.cols; ++col) {
            const size_t first = col * L.perCol;
            const size_t last = std::min(n, first + L.perCol);
            if (first >= last) break;
            const float cx = bodyLeft + static_cast<float>(col) * (L.colW + L.gap);
            Rect card(cx, bodyTop, cx + L.colW, bodyTop + static_cast<float>(last - first) * L.rowH);
            ui.rrect(card, P(18.f), ui.pal.cell);

            for (size_t i = first; i < last; ++i) {
                auto const& row = d.rows[i];
                const float ry = card.top + static_cast<float>(i - first) * L.rowH;
                Rect rc(card.left, ry, card.right, ry + L.rowH);

                // face
                Rect av(rc.left + P(12.f), rc.centerY() - L.avatar * 0.5f, rc.left + P(12.f) + L.avatar, rc.centerY() + L.avatar * 0.5f);
                if (row.face) {
                    const float k = L.avatar / std::max(1.f, row.faceSize);
                    row.face->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(av.left, av.top));
                    ui.fillBrush(av, P(8.f), row.face);
                    ui.rrectStroke(av, P(8.f), Col(1.f, 1.f, 1.f, 0.16f), std::max(1.f, P(0.75f)));
                } else {
                    ui.rrect(av, P(8.f), Col(1.f, 1.f, 1.f, 0.14f));
                    std::wstring plain;
                    for (auto const& r : runs[i]) plain += r.text;
                    ui.text(av, plain.empty() ? L"?" : std::wstring(1, plain[0]),
                            TextStyle(Font::Semibold, P(14.f), Col(1.f, 1.f, 1.f, 0.7f), HAlign::Center, VAlign::Center));
                }

                // name, one run per colour
                float tx = rc.left + L.nameX;
                const float maxX = rc.right - P(12.f);
                for (auto const& r : runs[i]) {
                    TextStyle st(r.bold ? Font::Bold : Font::Medium, P(16.f), r.color, HAlign::Leading, VAlign::Center);
                    st.ellipsis = false;
                    const float w = runWidth(ui, r.text, st);
                    if (tx + w > maxX) {
                        st.ellipsis = true;
                        ui.text(Rect(tx, rc.top, maxX, rc.bottom), r.text, st);
                        break;
                    }
                    ui.text(Rect(tx, rc.top, tx + w + P(2.f), rc.bottom), r.text, st);
                    tx += w;
                }

                if (i + 1 < last) {
                    ui.hairlineH(rc.left + L.nameX - P(2.f), rc.right - P(10.f), rc.bottom - std::max(1.f, std::round(ui.scale * 0.5f)),
                                 ui.pal.separator);
                }
            }
        }

        ui.popOpacity();
        ui.popScale();
    }

} // namespace ios
