// Standalone visual harness for the ios:: UI toolkit. Renders the shell to PNG files with Direct2D.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "client/screen/ios/IosShell.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ios;

struct Env {
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID2D1Factory1> d2dFactory;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> ctx;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<ID2D1Bitmap1> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IDWriteTextFormat> fonts[5];
    ComPtr<ID2D1Bitmap1> logo;
    UINT w = 1920, h = 1080;
};

static void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        std::printf("FAILED %s hr=0x%08lx\n", what, static_cast<unsigned long>(hr));
        std::exit(1);
    }
}

static bool initEnv(Env& e) {
    check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitialize");
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                   D3D11_SDK_VERSION, e.d3d.GetAddressOf(), &fl, nullptr);
    if (FAILED(hr)) {
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                D3D11_SDK_VERSION, e.d3d.GetAddressOf(), &fl, nullptr),
              "D3D11CreateDevice");
    }
    ComPtr<IDXGIDevice> dxgi;
    check(e.d3d.As(&dxgi), "dxgi");
    check(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), nullptr,
                            reinterpret_cast<void**>(e.d2dFactory.GetAddressOf())),
          "D2D1CreateFactory");
    check(e.d2dFactory->CreateDevice(dxgi.Get(), e.d2dDevice.GetAddressOf()), "CreateDevice");
    check(e.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, e.ctx.GetAddressOf()), "CreateDC");
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown**>(e.dwrite.GetAddressOf())),
          "DWrite");
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
                           reinterpret_cast<void**>(e.wic.GetAddressOf())),
          "WIC");

    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    check(e.ctx->CreateBitmap(D2D1::SizeU(e.w, e.h), nullptr, 0, bp, e.target.GetAddressOf()), "target");
    e.ctx->SetTarget(e.target.Get());
    check(e.ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), e.brush.GetAddressOf()), "brush");

    std::wstring fam = defaultFontFamily();
    std::wprintf(L"font family: %s\n", fam.c_str());
    const DWRITE_FONT_WEIGHT weights[5] = { DWRITE_FONT_WEIGHT_LIGHT, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_WEIGHT_MEDIUM,
                                            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_WEIGHT_BOLD };
    for (int i = 0; i < 5; ++i) {
        check(e.dwrite->CreateTextFormat(fam.c_str(), nullptr, weights[i], DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         10.f, L"en-us", e.fonts[i].GetAddressOf()),
              "textformat");
    }
    return true;
}

static void writePng(Env& e, const wchar_t* path) {
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> cpu;
    check(e.ctx->CreateBitmap(D2D1::SizeU(e.w, e.h), nullptr, 0, bp, cpu.GetAddressOf()), "cpu bitmap");
    check(cpu->CopyFromBitmap(nullptr, e.target.Get(), nullptr), "copy");
    D2D1_MAPPED_RECT mr;
    check(cpu->Map(D2D1_MAP_OPTIONS_READ, &mr), "map");

    ComPtr<IWICBitmap> wb;
    check(e.wic->CreateBitmapFromMemory(e.w, e.h, GUID_WICPixelFormat32bppPBGRA, mr.pitch, mr.pitch * e.h, mr.bits,
                                        wb.GetAddressOf()),
          "wicbitmap");
    ComPtr<IWICStream> stream;
    check(e.wic->CreateStream(stream.GetAddressOf()), "stream");
    check(stream->InitializeFromFilename(path, GENERIC_WRITE), "stream init");
    ComPtr<IWICBitmapEncoder> enc;
    check(e.wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.GetAddressOf()), "encoder");
    check(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache), "enc init");
    ComPtr<IWICBitmapFrameEncode> frame;
    check(enc->CreateNewFrame(frame.GetAddressOf(), nullptr), "frame");
    check(frame->Initialize(nullptr), "frame init");
    check(frame->SetSize(e.w, e.h), "size");
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&pf), "pf");
    check(frame->WriteSource(wb.Get(), nullptr), "write");
    check(frame->Commit(), "commit");
    check(enc->Commit(), "enc commit");
    cpu->Unmap();
    std::wprintf(L"wrote %s\n", path);
}

static void drawScene(Env& e, bool bright) {
    auto* c = e.ctx.Get();
    D2D1_SIZE_F sz = c->GetSize();
    // sky gradient
    ComPtr<ID2D1GradientStopCollection> coll;
    D2D1_GRADIENT_STOP stops[3];
    if (bright) {
        stops[0] = { 0.f, D2D1::ColorF(0.45f, 0.70f, 0.98f, 1.f) };
        stops[1] = { 0.6f, D2D1::ColorF(0.75f, 0.88f, 1.0f, 1.f) };
        stops[2] = { 1.f, D2D1::ColorF(0.95f, 0.97f, 1.0f, 1.f) };
    } else {
        stops[0] = { 0.f, D2D1::ColorF(0.02f, 0.03f, 0.10f, 1.f) };
        stops[1] = { 0.6f, D2D1::ColorF(0.06f, 0.10f, 0.22f, 1.f) };
        stops[2] = { 1.f, D2D1::ColorF(0.10f, 0.16f, 0.30f, 1.f) };
    }
    c->CreateGradientStopCollection(stops, 3, coll.GetAddressOf());
    ComPtr<ID2D1LinearGradientBrush> sky;
    c->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, sz.height * 0.62f)),
                                 coll.Get(), sky.GetAddressOf());
    c->FillRectangle(D2D1::RectF(0, 0, sz.width, sz.height), sky.Get());
    // ground
    e.brush->SetColor(bright ? D2D1::ColorF(0.30f, 0.60f, 0.22f) : D2D1::ColorF(0.07f, 0.18f, 0.07f));
    c->FillRectangle(D2D1::RectF(0, sz.height * 0.62f, sz.width, sz.height), e.brush.Get());
    // blocky trees / hills
    unsigned seed = 7;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.f;
    };
    for (int i = 0; i < 90; ++i) {
        float x = rnd() * sz.width, y = sz.height * 0.55f + rnd() * sz.height * 0.45f;
        float s = 30.f + rnd() * 90.f;
        float shade = 0.15f + rnd() * 0.5f;
        e.brush->SetColor(bright ? D2D1::ColorF(shade * 0.8f, 0.4f + shade * 0.4f, shade * 0.4f) : D2D1::ColorF(shade * 0.2f, shade * 0.5f, shade * 0.2f));
        c->FillRectangle(D2D1::RectF(x, y, x + s, y + s), e.brush.Get());
    }
    // crosshair + hotbar to sell the "in game" look
    e.brush->SetColor(D2D1::ColorF(1, 1, 1, 0.9f));
    c->FillRectangle(D2D1::RectF(sz.width / 2 - 1, sz.height / 2 - 12, sz.width / 2 + 1, sz.height / 2 + 12), e.brush.Get());
    c->FillRectangle(D2D1::RectF(sz.width / 2 - 12, sz.height / 2 - 1, sz.width / 2 + 12, sz.height / 2 + 1), e.brush.Get());
    // blur everything drawn so far (menu blur)
    ComPtr<ID2D1Bitmap1> copy;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    c->CreateBitmap(D2D1::SizeU(e.w, e.h), nullptr, 0, bp, copy.GetAddressOf());
    c->Flush();
    copy->CopyFromBitmap(nullptr, e.target.Get(), nullptr);
    ComPtr<ID2D1Effect> blur;
    c->CreateEffect(CLSID_D2D1GaussianBlur, blur.GetAddressOf());
    blur->SetInput(0, copy.Get());
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 18.f);
    c->DrawImage(blur.Get());
}

// ---------------------------------------------------------------------------------------------------------------
struct Fake {
    bool enabled = true;
    bool boolA = true, boolB = false, boolC = true;
    float sliderA = 0.35f, sliderB = 12.f, sliderC = 140.f;
    int choiceA = 1, choiceB = 2;
    int keyA = 'Z';
    Col colorA = Col(0.04f, 0.52f, 1.f, 1.f), colorB = Col(1.f, 0.62f, 0.f, 0.55f);
    std::wstring textA = L"[%FPS%] FPS";
} fake;

static bool g_recording = false;
static bool g_replayViewer = false;

static std::shared_ptr<ReplayClip> makeClip() {
    auto clip = std::make_shared<ReplayClip>();
    clip->title = L"2026-11-10  14:22:05";
    const int n = 1200;
    clip->duration = n * 0.05f;
    for (int i = 0; i < n; ++i) {
        ReplayFrame fr;
        fr.t = i * 0.05f;
        float a = i * 0.012f;
        fr.px = 120.f * std::cos(a) + 30.f * std::cos(a * 3.f);
        fr.pz = 80.f * std::sin(a) + 20.f * std::sin(a * 2.f);
        fr.py = 64.f;
        fr.yaw = a * 57.29f + 90.f;
        for (int k = 0; k < 6; ++k) {
            ReplayEntity e;
            e.id = 100 + k;
            e.player = k < 2;
            e.name = k < 2 ? (k == 0 ? L"Steve" : L"Alex") : L"";
            e.x = fr.px + 14.f * std::cos(a * 0.5f + k) + k * 5.f;
            e.z = fr.pz + 12.f * std::sin(a * 0.6f + k * 2.f) - k * 4.f;
            fr.entities.push_back(e);
        }
        clip->frames.push_back(fr);
    }
    // synthetic terrain: rolling hills with water, sand, grass, forest, rock and snow
    for (int cz = -12; cz < 12; ++cz) {
        for (int cx = -14; cx < 14; ++cx) {
            ReplayTerrainChunk ch;
            ch.cx = cx;
            ch.cz = cz;
            for (int lz = 0; lz < 16; ++lz) {
                for (int lx = 0; lx < 16; ++lx) {
                    float wx = static_cast<float>(cx * 16 + lx), wz = static_cast<float>(cz * 16 + lz);
                    float h = 66.f + 16.f * std::sin(wx * 0.019f) * std::cos(wz * 0.016f) + 7.f * std::sin(wx * 0.06f + wz * 0.045f) +
                              2.5f * std::sin(wx * 0.23f) * std::sin(wz * 0.21f);
                    int hi = static_cast<int>(std::floor(h));
                    uint8_t r, g, b;
                    if (h < 62.f) {
                        float depth = std::min(12.f, 62.f - h);
                        float f = 1.f - depth * 0.035f;
                        r = static_cast<uint8_t>(52 * f), g = static_cast<uint8_t>(98 * f), b = static_cast<uint8_t>(214 * f);
                        hi = 62;
                    } else if (h < 64.f) {
                        r = 219, g = 207, b = 142;
                    } else if (h < 84.f) {
                        bool tree = std::sin(wx * 0.9f + wz * 1.3f) * std::sin(wx * 0.37f - wz * 0.5f) > 0.78f;
                        if (tree) {
                            r = 52, g = 120, b = 36;
                            hi += 5;
                        } else {
                            r = 88, g = 148, b = 56;
                        }
                    } else if (h < 92.f) {
                        r = 125, g = 125, b = 125;
                    } else {
                        r = 249, g = 254, b = 254;
                    }
                    size_t i = static_cast<size_t>(lz * 16 + lx);
                    ch.rgb[i * 3] = r;
                    ch.rgb[i * 3 + 1] = g;
                    ch.rgb[i * 3 + 2] = b;
                    ch.height[i] = static_cast<int16_t>(hi);
                }
            }
            clip->terrain.push_back(ch);
        }
    }
    clip->markers.push_back({ 14.f, clip->frames[280].px, clip->frames[280].pz });
    clip->markers.push_back({ 36.f, clip->frames[720].px, clip->frames[720].pz });
    return clip;
}

static void buildFrame(Frame& f, Env& e, bool hudPreview) {
    f.replay.available = true;
    f.replay.recording = g_recording;
    f.replay.recordingSeconds = 83.f;
    f.replay.recordingMarkers = g_recording ? 2 : 0;
    f.replay.list = { { 11, L"2026-11-10  14:22:05", L"1:00  \u00B7  1.4 MB  \u00B7  2" }, { 12, L"2026-11-09  21:03:48", L"8:12  \u00B7  9.8 MB" } };
    f.replay.load = [](uint64_t) { return makeClip(); };
    f.replay.toggleRecording = [] {};
    f.replay.addMarker = [] {};
    f.replay.remove = [](uint64_t) {};
    f.version = L"v1.0.4";
    f.str.appName = L"ma4";
    f.logo = e.logo.Get();
    struct M {
        const char* name;
        const wchar_t* disp;
        const wchar_t* desc;
        bool hud;
        ModuleRow::Trailing tr;
    };
    static const M mods[] = {
        { "Zoom", L"Zoom", L"Zoom in with a key. Scroll to change the zoom level.", false, ModuleRow::Trailing::Toggle },
        { "Fullbright", L"Fullbright", L"See clearly in the dark by raising the gamma.", false, ModuleRow::Trailing::Toggle },
        { "FPS", L"FPS Counter", L"Shows the current frames per second.", true, ModuleRow::Trailing::Toggle },
        { "Keystrokes", L"Keystrokes", L"Displays the keys you press and your mouse clicks.", true, ModuleRow::Trailing::Toggle },
        { "CPS", L"CPS Counter", L"Counts the clicks per second.", true, ModuleRow::Trailing::Toggle },
        { "Ping", L"Ping Display", L"Displays your latency to the server.", true, ModuleRow::Trailing::Toggle },
        { "ToggleSprint", L"Toggle Sprint / Sneak", L"Sprint and sneak without holding the keys.", false, ModuleRow::Trailing::Toggle },
        { "Freelook", L"Freelook", L"Look around without turning your player.", false, ModuleRow::Trailing::HoldKey },
        { "MotionBlur", L"Motion Blur", L"Smooths fast camera motion.", false, ModuleRow::Trailing::Toggle },
        { "Armor", L"Armor HUD", L"Shows your armor durability.", true, ModuleRow::Trailing::Toggle },
        { "Coords", L"Coordinates", L"Displays your position in the world.", true, ModuleRow::Trailing::Toggle },
        { "Hitboxes", L"Hitboxes", L"Draws entity hitboxes.", false, ModuleRow::Trailing::Open },
        { "Clock", L"Clock", L"Shows the current time.", true, ModuleRow::Trailing::Toggle },
        { "Waila", L"WAILA", L"Shows what block or entity you are looking at.", true, ModuleRow::Trailing::Toggle },
        { "Direction", L"Direction HUD", L"A compass that shows your facing direction.", true, ModuleRow::Trailing::Toggle },
    };
    uint64_t idc = 100;
    for (auto& m : mods) {
        ModuleRow r;
        r.id = idc++;
        r.key = m.name;
        r.name = m.disp;
        r.desc = m.desc;
        r.inHud = m.hud;
        r.inGame = true;
        r.trailing = m.tr;
        r.enabled = (idc % 3) != 0;
        r.keyText = L"V";
        f.modules.push_back(r);
    }
    for (int i = 0; i < 3; ++i) {
        ModuleRow r;
        r.id = 900 + i;
        r.key = "plugin" + std::to_string(i);
        static const wchar_t* names[] = { L"Auto Sprint Plus", L"Damage Indicator", L"Mini Map" };
        r.name = names[i];
        r.desc = L"Community plugin";
        r.author = L"someone";
        r.market = true;
        r.inGame = false;
        r.trailing = i == 1 ? ModuleRow::Trailing::Installed : ModuleRow::Trailing::Install;
        f.modules.push_back(r);
    }

    auto row = [](SettingRow::Kind k, uint64_t id, std::wstring title, std::wstring sub = L"") {
        SettingRow r;
        r.kind = k;
        r.id = id;
        r.title = std::move(title);
        r.subtitle = std::move(sub);
        return r;
    };

    f.detail = [&, hudPreview](uint64_t id, ModuleDetail& d) {
        d.name = L"Keystrokes";
        d.desc = L"Displays the keys you press and your mouse clicks on screen.";
        d.key = "Keystrokes";
        d.enabled = fake.enabled;
        d.setEnabled = [](bool v) { fake.enabled = v; };
        if (hudPreview) {
            d.hasPreview = true;
            d.previewSize = Vec2(220.f, 52.f);
            d.drawPreview = [](Rect const& area) {
                Ui& ui = Ui::get();
                Rect pill(area.centerX() - ui.S(80.f), area.centerY() - ui.S(22.f), area.centerX() + ui.S(80.f), area.centerY() + ui.S(22.f));
                ui.rrect(pill, ui.S(14.f), Col(0.11f, 0.11f, 0.12f, 0.78f));
                ui.rrectStroke(pill, ui.S(14.f), Col(1.f, 1.f, 1.f, 0.12f), 1.f);
                ui.text(pill, L"144 FPS", TextStyle(Font::Semibold, ui.S(20.f), Col(1.f, 1.f, 1.f, 1.f), HAlign::Center, VAlign::Center));
            };
        }
        SettingsSection key;
        {
            auto r = row(SettingRow::Kind::Key, 11, L"Keybind", L"The key used to toggle this module.");
            r.keycode = &fake.keyA;
            r.keyText = L"Z";
            key.rows.push_back(r);
        }
        d.sections.push_back(std::move(key));
        SettingsSection main;
        main.header = L"APPEARANCE";
        {
            auto r = row(SettingRow::Kind::Toggle, 21, L"Background", L"Draw a rounded background behind the text.");
            r.boolean = &fake.boolA;
            main.rows.push_back(r);
        }
        {
            auto r = row(SettingRow::Kind::Toggle, 22, L"Outline");
            r.boolean = &fake.boolB;
            main.rows.push_back(r);
        }
        {
            auto r = row(SettingRow::Kind::Slider, 23, L"Corner Radius", L"How round the background corners are.");
            r.number = &fake.sliderB;
            r.lo = 0.f;
            r.hi = 20.f;
            r.step = 1.f;
            main.rows.push_back(r);
        }
        {
            auto r = row(SettingRow::Kind::Slider, 24, L"Size");
            r.number = &fake.sliderA;
            r.lo = 0.f;
            r.hi = 1.f;
            r.step = 0.05f;
            main.rows.push_back(r);
        }
        {
            auto r = row(SettingRow::Kind::Choice, 25, L"Alignment");
            r.choice = &fake.choiceA;
            r.options = { L"Center", L"Left", L"Right" };
            main.rows.push_back(r);
        }
        {
            auto r = row(SettingRow::Kind::Choice, 26, L"Language", L"Pick a language for the interface.");
            r.choice = &fake.choiceB;
            r.options = { L"System Default", L"English (US)", L"日本語", L"Español", L"Français", L"Deutsch", L"Русский", L"Português" };
            main.rows.push_back(r);
        }
        {
            auto r = row(SettingRow::Kind::Color, 27, L"Text Color");
            r.color = fake.colorA;
            r.setColor = [](Col c, bool) { fake.colorA = c; };
            main.rows.push_back(r);
        }
        {
            auto r = row(SettingRow::Kind::Color, 28, L"Background Color");
            r.color = fake.colorB;
            r.setColor = [](Col c, bool) { fake.colorB = c; };
            main.rows.push_back(r);
        }
        {
            auto r = row(SettingRow::Kind::Text, 29, L"Prefix");
            r.str = &fake.textA;
            main.rows.push_back(r);
        }
        d.sections.push_back(std::move(main));
        d.reset = [] {};
        (void)id;
        return true;
    };

    // client settings
    {
        SettingsSection a;
        a.header = L"GENERAL";
        auto r1 = row(SettingRow::Kind::Key, 301, L"Menu Key", L"The key used to open the menu");
        r1.keycode = &fake.keyA;
        r1.keyText = L"M";
        a.rows.push_back(r1);
        auto r2 = row(SettingRow::Kind::Choice, 302, L"Language", L"Language selection");
        r2.choice = &fake.choiceB;
        r2.options = { L"System Default", L"English (US)", L"日本語", L"Español" };
        a.rows.push_back(r2);
        f.clientSettings.push_back(std::move(a));

        SettingsSection b;
        b.header = L"APPEARANCE";
        auto r3 = row(SettingRow::Kind::Color, 303, L"Accent Color", L"Tint used for controls");
        r3.color = fake.colorA;
        r3.setColor = [](Col c, bool) { fake.colorA = c; };
        b.rows.push_back(r3);
        auto r4 = row(SettingRow::Kind::Toggle, 304, L"Menu Blur", L"Whether blur is enabled or disabled for the menu");
        r4.boolean = &fake.boolC;
        b.rows.push_back(r4);
        auto r5 = row(SettingRow::Kind::Slider, 305, L"Blur Intensity", L"The intensity of the menu blur");
        r5.number = &fake.sliderC;
        r5.lo = 0.f;
        r5.hi = 300.f;
        r5.step = 1.f;
        b.rows.push_back(r5);
        f.clientSettings.push_back(std::move(b));
    }
    f.closeWindow = [] {};
    f.openHudEditor = [] {};
}

static void renderScenario(Env& e, const wchar_t* out, bool light, bool bright, Section sec, bool detail, int popover,
                           bool colorSheet, float mouseX, float mouseY, float scroll, int frames = 90, bool preview = true,
                           bool rtl = false) {
    Shell shell;
    Frame f;
    buildFrame(f, e, preview);
    shell.reset(true);
    shell.selectSection(sec);

    Ui& ui = Ui::get();
    ui.releaseDeviceResources();
    Gfx g;
    g.ctx = e.ctx.Get();
    g.brush = e.brush.Get();
    g.dwrite = e.dwrite.Get();
    ComPtr<ID2D1Factory> fac;
    e.ctx->GetFactory(fac.GetAddressOf());
    g.d2d = fac.Get();
    for (int i = 0; i < 5; ++i) g.fonts[i] = e.fonts[i].Get();

    Rect screen(0, 0, static_cast<float>(e.w), static_cast<float>(e.h));
    float scale = Shell::computeScale(screen);
    Palette pal = Palette::make(light ? Appearance::Light : Appearance::Dark, Col(light ? 0.f : 10.f / 255.f, light ? 122.f / 255.f : 132.f / 255.f, 1.f, 1.f));

    if (detail) shell.pushDetail(102);
    if (g_replayViewer) {
        shell.pushReplay(11);
    }
    bool didPop = false, didSheet = false;
    for (int i = 0; i < frames; ++i) {
        e.ctx->BeginDraw();
        e.ctx->SetTransform(D2D1::Matrix3x2F::Identity());
        drawScene(e, bright);
        Input in;
        in.mouse = Vec2(mouseX, mouseY);
        in.dt = 1.f / 60.f;
        in.wheel = (i == frames - 40) ? scroll : 0.f;
        ui.beginFrame(g, in, pal, scale, rtl);
        if (popover >= 0 && !didPop && i == frames - 30) {
            ModuleDetail d;
            f.detail(102, d);
            for (auto& sec2 : d.sections) {
                for (auto& r : sec2.rows) {
                    if (r.kind == SettingRow::Kind::Choice && r.id == static_cast<uint64_t>(popover)) {
                        shell.openPopover(r, Rect(screen.getWidth() * 0.62f, screen.getHeight() * 0.58f, screen.getWidth() * 0.78f, screen.getHeight() * 0.62f));
                        didPop = true;
                    }
                }
            }
        }
        if (colorSheet && !didSheet && i == frames - 30) {
            ModuleDetail d;
            f.detail(102, d);
            for (auto& sec2 : d.sections) {
                for (auto& r : sec2.rows) {
                    if (r.kind == SettingRow::Kind::Color && !didSheet) {
                        shell.openColorSheet(r);
                        didSheet = true;
                    }
                }
            }
        }
        if (g_replayViewer && i == frames - 50) shell.viewerSeek(34.f, false);
        shell.frame(ui, screen, f, true);
        ui.endFrame();
        HRESULT hr = e.ctx->EndDraw();
        if (FAILED(hr)) {
            std::printf("EndDraw failed 0x%08lx at frame %d\n", static_cast<unsigned long>(hr), i);
            break;
        }
    }
    writePng(e, out);
}

static void loadLogo(Env& e, const wchar_t* path) {
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(e.wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, dec.GetAddressOf()))) return;
    ComPtr<IWICBitmapFrameDecode> fr;
    dec->GetFrame(0, fr.GetAddressOf());
    ComPtr<IWICFormatConverter> conv;
    e.wic->CreateFormatConverter(conv.GetAddressOf());
    conv->Initialize(fr.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    ComPtr<ID2D1Bitmap> bmp;
    e.ctx->CreateBitmapFromWicBitmap(conv.Get(), nullptr, bmp.GetAddressOf());
    if (bmp) bmp.As(&e.logo);
}

int wmain(int argc, wchar_t** argv) {
    Env e;
    initEnv(e);
    std::wstring outDir = argc > 1 ? argv[1] : L".";
    if (argc > 2) loadLogo(e, argv[2]);
    auto p = [&](const wchar_t* n) { return outDir + L"\\" + n; };

    renderScenario(e, p(L"01_list_dark.png").c_str(), false, false, Section::All, false, -1, false, 700, 380, 0);
    renderScenario(e, p(L"02_detail_dark.png").c_str(), false, false, Section::All, true, -1, false, 1500, 900, 0);
    renderScenario(e, p(L"03_detail_scrolled_dark.png").c_str(), false, false, Section::All, true, -1, false, 1500, 900, 520);
    renderScenario(e, p(L"04_settings_dark.png").c_str(), false, false, Section::Settings, false, -1, false, 1500, 900, 0);
    renderScenario(e, p(L"05_list_light.png").c_str(), true, true, Section::All, false, -1, false, 1500, 900, 0);
    renderScenario(e, p(L"06_detail_light.png").c_str(), true, true, Section::All, true, -1, false, 1500, 900, 0);
    renderScenario(e, p(L"07_popover_dark.png").c_str(), false, false, Section::All, true, 26, false, 1500, 900, 360);
    renderScenario(e, p(L"08_colorsheet_dark.png").c_str(), false, false, Section::All, true, -1, true, 1500, 900, 360);
    renderScenario(e, p(L"09_list_hud_bright.png").c_str(), false, true, Section::Hud, false, -1, false, 1500, 900, 0);
    renderScenario(e, p(L"10_list_rtl.png").c_str(), false, false, Section::All, false, -1, false, 1500, 900, 0, 90, true, true);
    renderScenario(e, p(L"11_replay_root.png").c_str(), false, false, Section::Replay, false, -1, false, 1500, 900, 0);
    g_recording = true;
    renderScenario(e, p(L"12_replay_recording.png").c_str(), false, false, Section::Replay, false, -1, false, 1500, 900, 0);
    g_recording = false;
    g_replayViewer = true;
    renderScenario(e, p(L"13_replay_viewer.png").c_str(), false, false, Section::Replay, false, -1, false, 1500, 900, 0, 120);
    renderScenario(e, p(L"14_replay_viewer_light.png").c_str(), true, true, Section::Replay, false, -1, false, 1500, 900, 0, 120);
    return 0;
}
