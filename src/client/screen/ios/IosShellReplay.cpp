// Replay pages of the iOS shell: recorder card + recordings list, and the replay viewer
// (top-down map, scrubber, transport, speed and a simple keyframed camera path).
#include "IosShell.h"
#include "IosShellCommon.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ios {

    namespace {
        std::wstring mmss(float secs) {
            int s = static_cast<int>(secs);
            wchar_t b[32];
            swprintf(b, 32, L"%02d:%02d", s / 60, s % 60);
            return b;
        }
    } // namespace

    // ---------------------------------------------------------------------------------- recorder + list
    float Shell::drawReplayRoot(Ui& ui, Frame& f, float x, float y, float w, Rect const& view, bool interactive) {
        auto S = [&](float v) { return ui.S(v); };
        const Palette& pal = ui.pal;
        ReplayHost& rp = f.replay;

        // ---- recorder card
        const float rowH = S(84.f);
        const float cardH = rowH + (rp.recording ? S(kRowH) : 0.f);
        Rect card(x, y, x + w, y + cardH);
        drawCard(ui, card);
        ui.pushClipRRect(card, S(kGroupR));

        Rect tile(card.left + S(16.f), card.top + rowH * 0.5f - S(24.f), card.left + S(16.f) + S(48.f),
                  card.top + rowH * 0.5f + S(24.f));
        float pulse = 1.f;
        if (rp.recording) pulse = 0.7f + 0.3f * std::sin(rp.recordingSeconds * 5.f);
        ui.iconTile(tile, rp.recording ? mulAlpha(pal.red, pulse) : pal.gray, Glyph::Record);

        std::wstring title = rp.recording ? f.str.recordingNow : f.str.recorder;
        std::wstring sub;
        if (rp.recording) {
            sub = mmss(rp.recordingSeconds);
            if (rp.recordingMarkers > 0) {
                std::wstring mk = f.str.markersCount;
                size_t p = mk.find(L"%d");
                if (p != std::wstring::npos) mk.replace(p, 2, std::to_wstring(rp.recordingMarkers));
                sub += L"  \u00B7  " + mk;
            }
        } else {
            sub = f.str.recordReady;
        }

        std::wstring btnLabel = rp.recording ? f.str.stopRecording : f.str.startRecording;
        float bw = std::max(S(120.f), ui.textWidth(btnLabel, TextStyle(Font::Semibold, S(16.f), pal.label)) + S(64.f));
        Rect btn(card.right - S(16.f) - bw, card.top + rowH * 0.5f - S(18.f), card.right - S(16.f),
                 card.top + rowH * 0.5f + S(18.f));

        Rect labelR(tile.right + S(14.f), card.top, btn.left - S(12.f), card.top + rowH);
        float blockH = S(22.f) + S(2.f) + S(18.f);
        float top = labelR.centerY() - blockH * 0.5f;
        ui.text(Rect(labelR.left, top, labelR.right, top + S(22.f)), title,
                TextStyle(Font::Semibold, S(18.f), pal.label, HAlign::Leading, VAlign::Center));
        ui.text(Rect(labelR.left, top + S(24.f), labelR.right, top + S(24.f) + S(18.f)), sub,
                TextStyle(Font::Regular, S(14.f), pal.secondaryLabel, HAlign::Leading, VAlign::Center));

        {
            uint64_t id = Ui::hashId("rec.toggle");
            auto cr = ui.click(id, btn, interactive);
            float pr = clamp01(ui.pressSpring(id ^ 0x1ull, cr.pressed));
            float hot = ui.anim(id ^ 0x2ull, cr.hovered ? 1.f : 0.f, 0.08f);
            ui.pushScale(btn, 1.f + 0.07f * pr);
            GlassStyle gs;
            gs.tint = withAlpha(pal.red, 0.93f);
            gs.backing = 0.f;
            gs.press = pr;
            gs.lift = hot;
            ui.glass(btn, btn.getHeight() * 0.5f, gs);
            ui.glyph(rp.recording ? Glyph::Stop : Glyph::Record,
                     Rect(btn.left + S(14.f), btn.centerY() - S(9.f), btn.left + S(14.f) + S(18.f), btn.centerY() + S(9.f)),
                     Col(1.f, 1.f, 1.f, 1.f), 1.f);
            ui.text(Rect(btn.left + S(30.f), btn.top, btn.right - S(8.f), btn.bottom), btnLabel,
                    TextStyle(Font::Semibold, S(16.f), Col(1.f, 1.f, 1.f, 1.f), HAlign::Center, VAlign::Center));
            ui.popScale();
            if (cr.clicked && rp.toggleRecording) rp.toggleRecording();
        }

        if (rp.recording) {
            Rect mr(card.left, card.top + rowH, card.right, card.bottom);
            ui.hairlineH(mr.left + S(16.f), mr.right, mr.top, pal.separator);
            uint64_t id = Ui::hashId("rec.marker");
            auto cr = ui.click(id, mr, interactive);
            if (cr.pressed) ui.fillRect(mr, pal.cellPressed);
            float tw = ui.textWidth(f.str.addMarker, TextStyle(Font::Regular, S(17.f), pal.accent));
            float cx = mr.centerX();
            ui.glyph(Glyph::Flag,
                     Rect(cx - tw * 0.5f - S(30.f), mr.centerY() - S(10.f), cx - tw * 0.5f - S(10.f), mr.centerY() + S(10.f)),
                     pal.accent, 1.f);
            ui.text(Rect(cx - tw * 0.5f - S(4.f), mr.top, cx + tw * 0.5f + S(8.f), mr.bottom), f.str.addMarker,
                    TextStyle(Font::Regular, S(17.f), pal.accent, HAlign::Leading, VAlign::Center));
            if (cr.clicked && rp.addMarker) rp.addMarker();
        }
        ui.popClipRRect();
        y = card.bottom + S(26.f);

        // ---- recordings
        ui.text(Rect(x + S(16.f), y, x + w, y + S(20.f)), f.str.recordings,
                TextStyle(Font::Regular, S(13.f), pal.secondaryLabel, HAlign::Leading, VAlign::Center));
        y += S(24.f);

        if (rp.list.empty()) {
            Rect empty(x, y, x + w, y + S(120.f));
            drawCard(ui, empty);
            ui.text(Rect(empty.left, empty.top + S(26.f), empty.right, empty.top + S(56.f)), f.str.noRecordings,
                    TextStyle(Font::Semibold, S(18.f), pal.secondaryLabel, HAlign::Center, VAlign::Center));
            ui.text(Rect(empty.left + S(24.f), empty.top + S(58.f), empty.right - S(24.f), empty.bottom - S(14.f)),
                    f.str.noRecordingsHint,
                    TextStyle(Font::Regular, S(14.f), pal.tertiaryLabel, HAlign::Center, VAlign::Top, true));
            return empty.bottom + S(24.f);
        }

        const float lrh = S(64.f);
        Rect list(x, y, x + w, y + lrh * static_cast<float>(rp.list.size()));
        drawCard(ui, list);
        ui.pushClipRRect(list, S(kGroupR));
        for (size_t i = 0; i < rp.list.size(); ++i) {
            auto& e = rp.list[i];
            Rect rc(x, y + lrh * static_cast<float>(i), x + w, y + lrh * static_cast<float>(i + 1));
            if (rc.bottom < view.top - S(2.f) || rc.top > view.bottom + S(2.f)) continue;
            auto cr = ui.click(e.id ^ 0x4E71ull, rc, interactive);
            if (cr.pressed) ui.fillRect(rc, pal.cellPressed);
            Rect t(rc.left + S(14.f), rc.centerY() - S(19.f), rc.left + S(14.f) + S(38.f), rc.centerY() + S(19.f));
            ui.iconTile(t, pal.indigo, Glyph::Play);
            float th = S(22.f), sh = S(17.f);
            float tp = rc.centerY() - (th + sh) * 0.5f;
            ui.text(Rect(t.right + S(13.f), tp, rc.right - S(40.f), tp + th), e.title,
                    TextStyle(Font::Medium, S(17.f), pal.label, HAlign::Leading, VAlign::Center));
            ui.text(Rect(t.right + S(13.f), tp + th, rc.right - S(40.f), tp + th + sh), e.subtitle,
                    TextStyle(Font::Regular, S(13.f), pal.secondaryLabel, HAlign::Leading, VAlign::Top));
            ui.glyph(Glyph::ChevronRight,
                     Rect(rc.right - S(24.f), rc.centerY() - S(8.f), rc.right - S(14.f), rc.centerY() + S(8.f)),
                     pal.tertiaryLabel, 1.1f);
            if (i + 1 < rp.list.size()) {
                ui.hairlineH(t.right + S(13.f), rc.right, rc.bottom - std::max(1.f, std::round(ui.scale * 0.5f)),
                             pal.separator);
            }
            if (cr.clicked) pushReplay(e.id);
        }
        ui.popClipRRect();
        return list.bottom + S(24.f);
    }

    // ---------------------------------------------------------------------------------- viewer
    void Shell::drawReplayViewer(Ui& ui, Frame& f, Rect const& content, float offX, bool interactive) {
        auto S = [&](float v) { return ui.S(v); };
        const Palette& pal = ui.pal;

        // load the clip on first use
        if (!viewer_.clip) {
            viewer_.id = detailId_;
            if (f.replay.load) viewer_.clip = f.replay.load(detailId_);
            if (!viewer_.clip || viewer_.clip->frames.empty()) {
                detailOpen_ = false;
                viewer_.clip.reset();
                return;
            }
        }
        ReplayClip& clip = *viewer_.clip;
        const float duration = std::max(0.01f, clip.duration > 0.f ? clip.duration : clip.frames.back().t);

        // opaque base while sliding over the list
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
        Rect canvas(x, content.top + S(kNavH) + S(2.f), x + w, content.bottom - S(16.f));
        const float panelH = S(184.f);
        Rect panel(canvas.left + S(10.f), canvas.bottom - S(10.f) - panelH, canvas.right - S(10.f), canvas.bottom - S(10.f));
        // the map is centred in the part of the canvas that is not covered by the floating transport
        Rect view(canvas.left, canvas.top, canvas.right, panel.top - S(8.f));

        // ---- fit the camera to the whole path once
        if (!viewer_.fitted) {
            float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
            for (auto& fr : clip.frames) {
                minX = std::min(minX, fr.px);
                maxX = std::max(maxX, fr.px);
                minZ = std::min(minZ, fr.pz);
                maxZ = std::max(maxZ, fr.pz);
            }
            viewer_.camX = (minX + maxX) * 0.5f;
            viewer_.camZ = (minZ + maxZ) * 0.5f;
            float spanX = std::max(24.f, maxX - minX + 16.f), spanZ = std::max(24.f, maxZ - minZ + 16.f);
            float zx = view.getWidth() / ui.scale / spanX, zz = view.getHeight() / ui.scale / spanZ;
            viewer_.zoom = std::clamp(std::min(zx, zz), 1.5f, 40.f);
            viewer_.fitted = true;
        }

        // ---- playback
        static const float kSpeeds[] = { 0.5f, 1.f, 2.f, 4.f };
        if (interactive && viewer_.playing) {
            viewer_.time += ui.dt * kSpeeds[std::clamp(viewer_.speed, 0, 3)];
            if (viewer_.time >= duration) {
                viewer_.time = duration;
                viewer_.playing = false;
            }
        }
        viewer_.time = std::clamp(viewer_.time, 0.f, duration);

        // frame lookup + interpolation
        auto it = std::upper_bound(clip.frames.begin(), clip.frames.end(), viewer_.time,
                                   [](float tt, ReplayFrame const& fr) { return tt < fr.t; });
        size_t i1 = it == clip.frames.begin() ? 0 : static_cast<size_t>(std::distance(clip.frames.begin(), it)) - 1;
        size_t i2 = std::min(i1 + 1, clip.frames.size() - 1);
        ReplayFrame const& fa = clip.frames[i1];
        ReplayFrame const& fb = clip.frames[i2];
        float seg = fb.t - fa.t;
        float a = seg > 1e-4f ? clamp01((viewer_.time - fa.t) / seg) : 0.f;
        float pX = fa.px + (fb.px - fa.px) * a, pZ = fa.pz + (fb.pz - fa.pz) * a, pY = fa.py + (fb.py - fa.py) * a;
        float yawDiff = std::fmod(fb.yaw - fa.yaw + 540.f, 360.f) - 180.f;
        float pYaw = fa.yaw + yawDiff * a;

        // ---- camera (follow / keyframe path / free)
        const bool pathActive = viewer_.cameraPath && viewer_.keys.size() >= 2;
        if (pathActive) {
            auto const& ks = viewer_.keys;
            size_t k = 0;
            while (k + 1 < ks.size() && ks[k + 1].t <= viewer_.time) ++k;
            if (viewer_.time <= ks.front().t) {
                viewer_.camX = ks.front().cx, viewer_.camZ = ks.front().cz, viewer_.zoom = ks.front().zoom;
            } else if (k + 1 >= ks.size()) {
                viewer_.camX = ks.back().cx, viewer_.camZ = ks.back().cz, viewer_.zoom = ks.back().zoom;
            } else {
                float u = clamp01((viewer_.time - ks[k].t) / std::max(1e-3f, ks[k + 1].t - ks[k].t));
                u = u * u * (3.f - 2.f * u);
                viewer_.camX = ks[k].cx + (ks[k + 1].cx - ks[k].cx) * u;
                viewer_.camZ = ks[k].cz + (ks[k + 1].cz - ks[k].cz) * u;
                viewer_.zoom = ks[k].zoom + (ks[k + 1].zoom - ks[k].zoom) * u;
            }
        } else if (viewer_.follow) {
            float k = clamp01(ui.dt * 9.f);
            viewer_.camX += (pX - viewer_.camX) * k;
            viewer_.camZ += (pZ - viewer_.camZ) * k;
        }

        const float zoomPx = viewer_.zoom * ui.scale;
        auto toScreen = [&](float wx, float wz) {
            return Vec2(view.centerX() + (wx - viewer_.camX) * zoomPx, view.centerY() + (wz - viewer_.camZ) * zoomPx);
        };

        // ---- map canvas
        const float R = S(30.f);
        ui.fillSmooth(canvas, R, pal.dark ? Col(0.055f, 0.070f, 0.105f, 1.f) : Col(0.90f, 0.92f, 0.95f, 1.f));
        ui.pushClipRRect(canvas, R);

        // ---- terrain map (recorded surface colours + hill shading)
        const bool haveTerrain = !clip.terrain.empty();
        auto chunkKey = [](int cx, int cz) {
            return (static_cast<int64_t>(cx) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(cz));
        };
        if (haveTerrain && !viewer_.terrainIndexed) {
            viewer_.terrainIndex.clear();
            for (size_t i = 0; i < clip.terrain.size(); ++i) {
                viewer_.terrainIndex[chunkKey(clip.terrain[i].cx, clip.terrain[i].cz)] = i;
            }
            viewer_.terrainIndexed = true;
        }
        const bool drawTerrain = haveTerrain && viewer_.showTerrain;
        if (drawTerrain) {
            auto heightAt = [&](ReplayTerrainChunk const& ch, int lx, int lz) -> int {
                if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16) return ch.height[static_cast<size_t>(lz * 16 + lx)];
                int ncx = ch.cx + (lx < 0 ? -1 : (lx > 15 ? 1 : 0));
                int ncz = ch.cz + (lz < 0 ? -1 : (lz > 15 ? 1 : 0));
                auto nit = viewer_.terrainIndex.find(chunkKey(ncx, ncz));
                if (nit == viewer_.terrainIndex.end()) return ReplayTerrainChunk::kUnknown;
                return clip.terrain[nit->second].height[static_cast<size_t>(((lz + 16) & 15) * 16 + ((lx + 16) & 15))];
            };
            auto bitmapFor = [&](size_t index) -> ID2D1Bitmap* {
                auto const& ch = clip.terrain[index];
                int64_t key = chunkKey(ch.cx, ch.cz);
                auto found = viewer_.terrainBitmaps.find(key);
                if (found != viewer_.terrainBitmaps.end()) return found->second.Get();

                uint32_t pixels[256] = {};
                for (int lz = 0; lz < 16; ++lz) {
                    for (int lx = 0; lx < 16; ++lx) {
                        size_t i = static_cast<size_t>(lz * 16 + lx);
                        int h = ch.height[i];
                        if (h == ReplayTerrainChunk::kUnknown) continue;
                        int hse = heightAt(ch, lx + 1, lz + 1), hnw = heightAt(ch, lx - 1, lz - 1);
                        if (hse == ReplayTerrainChunk::kUnknown) hse = h;
                        if (hnw == ReplayTerrainChunk::kUnknown) hnw = h;
                        float shade = 1.f + std::clamp(static_cast<float>(hse - hnw), -8.f, 8.f) * 0.05f;
                        auto ch8 = [&](int c) {
                            return static_cast<uint32_t>(std::clamp(static_cast<float>(ch.rgb[i * 3 + static_cast<size_t>(c)]) * shade, 0.f, 255.f));
                        };
                        pixels[i] = 0xFF000000u | (ch8(0) << 16) | (ch8(1) << 8) | ch8(2);
                    }
                }
                ComPtr<ID2D1Bitmap1> bmp;
                D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                if (FAILED(ui.ctx()->CreateBitmap(D2D1::SizeU(16, 16), pixels, 16 * 4, props, bmp.GetAddressOf()))) {
                    return nullptr;
                }
                ComPtr<ID2D1Bitmap> base;
                bmp.As(&base);
                auto inserted = viewer_.terrainBitmaps.emplace(key, base);
                return inserted.first->second.Get();
            };

            float halfW = canvas.getWidth() / zoomPx, halfH = canvas.getHeight() / zoomPx;
            int cx0 = static_cast<int>(std::floor((viewer_.camX - halfW) / 16.f)), cx1 = static_cast<int>(std::floor((viewer_.camX + halfW) / 16.f));
            int cz0 = static_cast<int>(std::floor((viewer_.camZ - halfH) / 16.f)), cz1 = static_cast<int>(std::floor((viewer_.camZ + halfH) / 16.f));
            if (static_cast<long long>(cx1 - cx0 + 1) * (cz1 - cz0 + 1) <= 6000) {
                for (int cz = cz0; cz <= cz1; ++cz) {
                    for (int cx = cx0; cx <= cx1; ++cx) {
                        auto cit = viewer_.terrainIndex.find(chunkKey(cx, cz));
                        if (cit == viewer_.terrainIndex.end()) continue;
                        ID2D1Bitmap* bmp = bitmapFor(cit->second);
                        if (!bmp) continue;
                        Vec2 a0 = toScreen(static_cast<float>(cx * 16), static_cast<float>(cz * 16));
                        Vec2 a1 = toScreen(static_cast<float>(cx * 16 + 16), static_cast<float>(cz * 16 + 16));
                        // snap shared edges to whole pixels so neighbouring chunks never leave seams
                        Rect dest(std::round(a0.x), std::round(a0.y), std::round(a1.x), std::round(a1.y));
                        if (dest.getWidth() < 1.f || dest.getHeight() < 1.f) continue;
                        ui.ctx()->DrawBitmap(bmp, dest.get(), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                    }
                }
            }
            // keep the path / labels readable on top of bright terrain
            ui.fillRect(canvas, pal.dark ? Col(0.f, 0.f, 0.f, 0.22f) : Col(1.f, 1.f, 1.f, 0.10f));
        }
        {
            // grid
            float gridBlocks = 1.f;
            const float steps[] = { 1.f, 2.f, 5.f, 10.f, 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f };
            for (float s : steps) {
                gridBlocks = s;
                if (s * zoomPx >= S(34.f)) break;
            }
            float halfW = canvas.getWidth() / zoomPx, halfH = canvas.getHeight() / zoomPx;
            Col base = pal.dark ? Col(1.f, 1.f, 1.f, 1.f) : Col(0.f, 0.f, 0.f, 1.f);
            Col gl = withAlpha(base, 0.06f);
            Col gm = withAlpha(base, 0.13f);
            for (float gx = std::floor((viewer_.camX - halfW) / gridBlocks) * gridBlocks; gx < viewer_.camX + halfW;
                 gx += gridBlocks) {
                Vec2 s = toScreen(gx, 0.f);
                bool major = std::fabs(std::fmod(gx, gridBlocks * 5.f)) < 0.01f;
                ui.fillRect(Rect(s.x, canvas.top, s.x + 1.f, canvas.bottom), major ? gm : gl);
            }
            for (float gz = std::floor((viewer_.camZ - halfH) / gridBlocks) * gridBlocks; gz < viewer_.camZ + halfH;
                 gz += gridBlocks) {
                Vec2 s = toScreen(0.f, gz);
                bool major = std::fabs(std::fmod(gz, gridBlocks * 5.f)) < 0.01f;
                ui.fillRect(Rect(canvas.left, s.y, canvas.right, s.y + 1.f), major ? gm : gl);
            }

            // path (full = faint, played = accent)
            size_t stride = std::max<size_t>(1, clip.frames.size() / 600);
            for (size_t i = stride; i < clip.frames.size(); i += stride) {
                auto& p0 = clip.frames[i - stride];
                auto& p1 = clip.frames[i];
                bool played = p1.t <= viewer_.time;
                ui.line(toScreen(p0.px, p0.pz), toScreen(p1.px, p1.pz),
                        played ? withAlpha(pal.accent, 0.95f) : withAlpha(pal.label, 0.16f), played ? S(2.6f) : S(1.8f),
                        false);
            }

            // markers
            for (auto& m : clip.markers) {
                Vec2 s = toScreen(m.x, m.z);
                ui.glyph(Glyph::Flag, Rect(s.x - S(10.f), s.y - S(20.f), s.x + S(10.f), s.y), pal.orange, 1.0f);
            }

            // other entities at this moment
            for (auto& e : fa.entities) {
                float ex = e.x, ez = e.z;
                for (auto& e2 : fb.entities) {
                    if (e2.id == e.id) {
                        ex = e.x + (e2.x - e.x) * a;
                        ez = e.z + (e2.z - e.z) * a;
                        break;
                    }
                }
                Vec2 s = toScreen(ex, ez);
                if (s.x < canvas.left - S(20.f) || s.x > canvas.right + S(20.f) || s.y < canvas.top - S(20.f) ||
                    s.y > canvas.bottom + S(20.f)) {
                    continue;
                }
                ui.circle(s, e.player ? S(5.5f) : S(3.8f), e.player ? pal.green : withAlpha(pal.orange, 0.9f));
                if (e.player) ui.circleStroke(s, S(5.5f), Col(1.f, 1.f, 1.f, 0.85f), std::max(1.f, S(1.f)));
                if (e.player && !e.name.empty() && zoomPx >= 3.f) {
                    ui.text(Rect(s.x - S(70.f), s.y + S(7.f), s.x + S(70.f), s.y + S(23.f)), e.name,
                            TextStyle(Font::Medium, S(11.f), withAlpha(pal.label, 0.9f), HAlign::Center, VAlign::Top));
                }
            }

            // local player
            Vec2 ps = toScreen(pX, pZ);
            float yr = pYaw * (3.14159265f / 180.f);
            Vec2 dir = { -std::sin(yr), std::cos(yr) };
            ui.line(ps, Vec2(ps.x + dir.x * S(22.f), ps.y + dir.y * S(22.f)), Col(1.f, 1.f, 1.f, 0.95f), S(2.4f), true);
            ui.circle(ps, S(8.f), pal.accent);
            ui.circleStroke(ps, S(8.f), Col(1.f, 1.f, 1.f, 1.f), S(2.f));
        }
        ui.scrollEdge(Rect(canvas.left, canvas.top, canvas.right, canvas.top + S(76.f)), true, Col(0.f, 0.f, 0.f, 0.46f), 1.f);
        ui.scrollEdge(Rect(canvas.left, panel.top - S(46.f), canvas.right, canvas.bottom), false, Col(0.f, 0.f, 0.f, 0.30f), 1.f);
        ui.popClipRRect();
        ui.strokeSmooth(canvas, R, pal.cardBorder, std::max(1.f, S(0.5f)));

        // pan / zoom
        {
            auto d = ui.drag(Ui::hashId("replay.canvas"), view);
            if (interactive && d.began) viewer_.dragLast = ui.mouse();
            if (interactive && d.active) {
                Vec2 m = ui.mouse();
                if (!pathActive) {
                    viewer_.camX -= (m.x - viewer_.dragLast.x) / zoomPx;
                    viewer_.camZ -= (m.y - viewer_.dragLast.y) / zoomPx;
                    viewer_.follow = false;
                }
                viewer_.dragLast = m;
            }
            if (interactive && ui.in.wheel != 0.f && view.contains(ui.mouse()) && !pathActive) {
                viewer_.zoom = std::clamp(viewer_.zoom * std::exp(-ui.in.wheel * 0.0025f), 0.8f, 80.f);
                ui.in.wheel = 0.f;
            }
        }

        // canvas overlays
        {
            wchar_t buf[96];
            swprintf(buf, 96, L"X %d   Y %d   Z %d", static_cast<int>(std::floor(pX)), static_cast<int>(std::floor(pY)),
                     static_cast<int>(std::floor(pZ)));
            TextStyle cs(Font::Medium, S(13.f), pal.label, HAlign::Center, VAlign::Center);
            float cw = ui.textWidth(buf, cs) + S(24.f);
            Rect chip(canvas.left + S(12.f), canvas.top + S(12.f), canvas.left + S(12.f) + cw,
                      canvas.top + S(12.f) + S(28.f));
            ui.glass(chip, chip.getHeight() * 0.5f, GlassStyle {});
            ui.text(chip, buf, cs);

            float rightEdge = canvas.right - S(12.f);
            if (haveTerrain) {
                TextStyle ms(Font::Semibold, S(13.f), viewer_.showTerrain ? pal.accent : pal.secondaryLabel, HAlign::Center,
                             VAlign::Center);
                float mw = ui.textWidth(f.str.mapToggle, ms) + S(28.f);
                Rect mr(rightEdge - mw, canvas.top + S(12.f), rightEdge, canvas.top + S(12.f) + S(28.f));
                auto mc = ui.click(Ui::hashId("replay.map"), mr, interactive);
                ui.materialPill(mr, mr.getHeight() * 0.5f);
                if (mc.pressed) ui.rrect(mr, mr.getHeight() * 0.5f, withAlpha(pal.label, 0.10f));
                ui.text(mr, f.str.mapToggle, ms);
                if (mc.clicked) viewer_.showTerrain = !viewer_.showTerrain;
                rightEdge = mr.left - S(8.f);
            }
            if (!viewer_.follow && !pathActive) {
                float fw = ui.textWidth(f.str.follow, TextStyle(Font::Semibold, S(13.f), pal.accent)) + S(28.f);
                Rect fr(rightEdge - fw, canvas.top + S(12.f), rightEdge, canvas.top + S(12.f) + S(28.f));
                if (ui.button(Ui::hashId("replay.follow"), fr, f.str.follow, ButtonKind::Material, 13.f, interactive)) {
                    viewer_.follow = true;
                }
            }
        }

        // ---- transport panel
        {
            GlassStyle gs;
            gs.shadow = 1.4f;
            ui.glass(panel, S(24.f), gs);
            ui.fillSmooth(panel, S(24.f), pal.dark ? Col(0.12f, 0.12f, 0.135f, 0.58f) : Col(0.985f, 0.985f, 0.995f, 0.50f));
        }
        {
            const float padX = S(20.f);
            float ty = panel.top + S(26.f);
            TextStyle tstyle(Font::Medium, S(13.f), pal.secondaryLabel, HAlign::Leading, VAlign::Center);
            ui.text(Rect(panel.left + padX, ty - S(10.f), panel.left + padX + S(52.f), ty + S(10.f)), mmss(viewer_.time),
                    tstyle);
            tstyle.h = HAlign::Trailing;
            ui.text(Rect(panel.right - padX - S(52.f), ty - S(10.f), panel.right - padX, ty + S(10.f)), mmss(duration),
                    tstyle);
            Rect track(panel.left + padX + S(62.f), ty - S(3.f), panel.right - padX - S(62.f), ty + S(3.f));

            // marker / keyframe ticks first so they win the click over the scrubber
            for (size_t m = 0; m < clip.markers.size(); ++m) {
                float mx = track.left + track.getWidth() * clamp01(clip.markers[m].t / duration);
                Rect hit(mx - S(7.f), ty - S(20.f), mx + S(7.f), ty - S(2.f));
                auto cr = ui.click(Ui::hashId("replay.mk") ^ m, hit, interactive);
                ui.glyph(Glyph::Flag, Rect(mx - S(8.f), ty - S(21.f), mx + S(8.f), ty - S(3.f)),
                         cr.hovered ? pal.yellow : pal.orange, 0.9f);
                if (cr.clicked) viewer_.time = clip.markers[m].t;
            }
            for (size_t k = 0; k < viewer_.keys.size(); ++k) {
                float kx = track.left + track.getWidth() * clamp01(viewer_.keys[k].t / duration);
                Rect hit(kx - S(7.f), ty + S(5.f), kx + S(7.f), ty + S(19.f));
                auto cr = ui.click(Ui::hashId("replay.key") ^ k, hit, interactive);
                ui.circle({ kx, ty + S(12.f) }, S(4.f), cr.hovered ? pal.yellow : pal.indigo);
                if (cr.clicked) viewer_.time = viewer_.keys[k].t;
                if (interactive && ui.in.rightPressed && ui.hit(hit)) {
                    viewer_.keys.erase(viewer_.keys.begin() + static_cast<std::ptrdiff_t>(k));
                    if (viewer_.keys.size() < 2) viewer_.cameraPath = false;
                    break;
                }
            }

            // scrubber
            Rect scrubHit(track.left - S(10.f), ty - S(14.f), track.right + S(10.f), ty + S(14.f));
            auto d = ui.drag(Ui::hashId("replay.scrub"), scrubHit);
            if (interactive && (d.active || d.began)) {
                viewer_.time = clamp01((ui.mouse().x - track.left) / track.getWidth()) * duration;
            }
            float tv = viewer_.time / duration;
            ui.rrect(track, track.getHeight() * 0.5f, pal.tertiaryFill);
            float fx = track.left + track.getWidth() * tv;
            if (fx - track.left > 1.f) {
                ui.rrect(Rect(track.left, track.top, fx, track.bottom), track.getHeight() * 0.5f, pal.accent);
            }
            {
                // capsule knob that grows and turns to glass while it is dragged (same as the sliders)
                float grow = clamp01(ui.spring(Ui::hashId("replay.scrub") ^ 0x3Cull, d.active ? 1.f : 0.f, 0.30f, 0.64f));
                float tw2 = S(30.f) * (1.f + 0.14f * grow), th2 = S(20.f) * (1.f + 0.14f * grow);
                Rect thumb(fx - tw2 * 0.5f, ty - th2 * 0.5f, fx + tw2 * 0.5f, ty + th2 * 0.5f);
                ui.shadow(thumb, th2 * 0.5f, S(8.f), S(3.f), Col(0.f, 0.f, 0.f, 0.22f + 0.06f * grow));
                ui.rrect(thumb, th2 * 0.5f, mulAlpha(pal.thumb, 1.f - 0.5f * grow));
                if (grow > 0.01f) {
                    GlassStyle gs;
                    gs.clear = true;
                    gs.shadow = 0.f;
                    gs.backing = 0.f;
                    gs.opacity = grow;
                    ui.glass(thumb, th2 * 0.5f, gs);
                }
            }

            // transport: three round glass buttons that merge like the shapes of a GlassEffectContainer and
            // separate a little while one is pressed
            float cy = panel.top + S(90.f);
            float cx = panel.centerX();
            const float tr = S(25.f);
            const float gap0 = S(5.f);
            struct Btn {
                uint64_t id;
                Glyph glyph;
                bool accent;
            };
            const Btn btns[3] = { { Ui::hashId("replay.back"), Glyph::StepBack, false },
                                  { Ui::hashId("replay.play"), viewer_.playing ? Glyph::Pause : Glyph::Play, true },
                                  { Ui::hashId("replay.fwd"), Glyph::StepForward, false } };
            float prs[3] = {};
            bool clicked[3] = {};
            float maxPress = 0.f;
            for (int i = 0; i < 3; ++i) {
                float bx = cx + static_cast<float>(i - 1) * (tr * 2.f + gap0);
                auto cr = ui.click(btns[i].id, Rect(bx - tr, cy - tr, bx + tr, cy + tr), interactive);
                prs[i] = clamp01(ui.pressSpring(btns[i].id ^ 0x5ull, cr.pressed));
                clicked[i] = cr.clicked;
                maxPress = std::max(maxPress, prs[i]);
            }
            const float gap = gap0 + S(11.f) * maxPress;
            std::vector<Vec2> centers;
            for (int i = 0; i < 3; ++i) centers.push_back({ cx + static_cast<float>(i - 1) * (tr * 2.f + gap), cy });
            {
                GlassStyle gs;
                gs.press = maxPress * 0.5f;
                ui.glassBlobs(centers, tr, S(20.f), gs);
            }
            for (int i = 0; i < 3; ++i) {
                Vec2 c = centers[static_cast<size_t>(i)];
                if (prs[i] > 0.01f) ui.circle(c, tr * (1.f + 0.04f * prs[i]), withAlpha(pal.label, 0.09f * prs[i]));
                float gs2 = tr * (0.52f + 0.10f * prs[i]);
                ui.glyph(btns[i].glyph, Rect(c.x - gs2, c.y - gs2, c.x + gs2, c.y + gs2), btns[i].accent ? pal.accent : pal.label,
                         1.1f);
            }
            if (clicked[0]) viewer_.time = std::max(0.f, viewer_.time - 5.f);
            if (clicked[1]) {
                if (!viewer_.playing && viewer_.time >= duration - 0.05f) viewer_.time = 0.f;
                viewer_.playing = !viewer_.playing;
            }
            if (clicked[2]) viewer_.time = std::min(duration, viewer_.time + 5.f);

            // speed (left)
            Rect speedR(panel.left + padX, cy - S(17.f), panel.left + padX + S(210.f), cy + S(17.f));
            int sp = ui.segmented(Ui::hashId("replay.speed"), speedR, { L"0.5\u00D7", L"1\u00D7", L"2\u00D7", L"4\u00D7" },
                                  viewer_.speed);
            if (sp != viewer_.speed && interactive) viewer_.speed = sp;

            // add keyframe (right)
            float kw = std::max(S(132.f),
                                ui.textWidth(f.str.addKeyframe, TextStyle(Font::Semibold, S(15.f), pal.accent)) + S(34.f));
            Rect keyR(panel.right - padX - kw, cy - S(17.f), panel.right - padX, cy + S(17.f));
            if (ui.button(Ui::hashId("replay.addkey"), keyR, f.str.addKeyframe, ButtonKind::Tinted, 15.f, interactive)) {
                Viewer::Key key { viewer_.time, viewer_.camX, viewer_.camZ, viewer_.zoom };
                bool replaced = false;
                for (auto& existing : viewer_.keys) {
                    if (std::fabs(existing.t - key.t) < 0.25f) {
                        existing = key;
                        replaced = true;
                    }
                }
                if (!replaced) viewer_.keys.push_back(key);
                std::sort(viewer_.keys.begin(), viewer_.keys.end(),
                          [](Viewer::Key const& l, Viewer::Key const& r) { return l.t < r.t; });
            }

            // bottom row: camera path toggle + clear + delete
            float by = panel.bottom - S(26.f);
            bool canPath = viewer_.keys.size() >= 2;
            ui.text(Rect(panel.left + padX, by - S(14.f), panel.left + padX + S(130.f), by + S(14.f)), f.str.cameraPath,
                    TextStyle(Font::Regular, S(15.f), canPath ? pal.label : pal.tertiaryLabel, HAlign::Leading,
                              VAlign::Center));
            Rect sw(panel.left + padX + S(132.f), by - S(kSwitchH * 0.5f), panel.left + padX + S(132.f) + S(kSwitchW), by + S(kSwitchH * 0.5f));
            bool pathOn = viewer_.cameraPath && canPath;
            if (ui.toggle(Ui::hashId("replay.path"), sw, pathOn, interactive && canPath)) {
                viewer_.cameraPath = pathOn;
                viewer_.follow = !pathOn;
            }
            if (!canPath) {
                ui.text(Rect(sw.right + S(12.f), by - S(14.f), sw.right + S(12.f) + S(300.f), by + S(14.f)),
                        f.str.cameraPathHint,
                        TextStyle(Font::Regular, S(12.f), pal.tertiaryLabel, HAlign::Leading, VAlign::Center));
            } else {
                Rect clr(sw.right + S(16.f), by - S(15.f), sw.right + S(16.f) + S(76.f), by + S(15.f));
                if (ui.button(Ui::hashId("replay.clearkeys"), clr, f.str.clearKeyframes, ButtonKind::Gray, 14.f,
                              interactive)) {
                    viewer_.keys.clear();
                    viewer_.cameraPath = false;
                }
            }

            float dw = ui.textWidth(f.str.deleteReplay, TextStyle(Font::Semibold, S(15.f), pal.red)) + S(30.f);
            Rect del(panel.right - padX - dw, by - S(16.f), panel.right - padX, by + S(16.f));
            if (ui.button(Ui::hashId("replay.delete"), del, f.str.deleteReplay, ButtonKind::Destructive, 15.f,
                          interactive)) {
                if (f.replay.remove) f.replay.remove(detailId_);
                detailOpen_ = false;
                viewer_ = Viewer {};
            }
        }

        ui.popClip();
        navEdge_ = false;
        drawNavBar(ui, f, content, clip.title, 1000.f, true, f.str.replay);
        navEdge_ = true;
        ui.ctx()->SetTransform(old);
    }

} // namespace ios
