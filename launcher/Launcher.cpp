// ma4 Launcher - an iOS 26 lock-screen style window (wallpaper, big clock, "Ma4" title, Liquid Glass buttons) that
// starts Minecraft Bedrock and loads ma4.dll into it. It reuses the Direct2D toolkit of the in-game menu
// (src/client/screen/ios), so the controls, springs and glass look exactly like the menu.
//
//   Ma4Launcher.exe                          the launcher
//   Ma4Launcher.exe --render out.png [W H] [--sheet] [--state idle|running|ready|error|busy] [--wallpaper file]
//                                            draws one frame to a PNG (screenshots / no window)
//   Ma4Launcher.exe --inject <pid> <dll>     loads a DLL into a process (what the Play button does last)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>

#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Injector.h"
#include "LauncherResource.h"
#include "LauncherSettings.h"
#include "Wallpaper.h"
#include "client/screen/ios/IosUi.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

using ios::Col;
using ios::Rect;
using ios::Ui;

namespace {

#ifndef MA4_VERSION_TEXT
#define MA4_VERSION_TEXT "v0.0.0"
#endif
    // the launcher and the DLL share one version: CMake reads it from src/client/Ma4.h
    std::wstring const& versionText() {
        static const std::wstring v = [] {
            std::string s = MA4_VERSION_TEXT;
            return std::wstring(s.begin(), s.end());
        }();
        return v;
    }
    constexpr float kDefaultW = 1040.f, kDefaultH = 660.f; // points (1 pt = 1 px at 96 dpi)
    constexpr float kMinW = 800.f, kMinH = 640.f;
    constexpr UINT WM_WAKE = WM_APP + 1;

    // ---------------------------------------------------------------------------------------------- strings
    struct Strings {
        const wchar_t *play, *inject, *injected, *launching, *waiting, *injecting, *settings, *wallpaper, *choose, *reset, *dim, *blur,
            *clock, *h12, *h24, *closeAfter, *dllLabel, *browse, *notRunning, *running, *ready, *subtitle, *errNoDll, *errLaunch,
            *errTimeout, *errAcl, *errInject, *notFound, *imageFilter;
    };

    const Strings kJa = {
        L"\u8D77\u52D5", L"\u9069\u7528", L"\u9069\u7528\u6E08\u307F", L"\u8D77\u52D5\u4E2D\u2026", L"\u30B2\u30FC\u30E0\u3092\u5F85\u3063\u3066\u3044\u307E\u3059\u2026", L"\u8AAD\u307F\u8FBC\u307F\u4E2D\u2026", L"\u8A2D\u5B9A", L"\u58C1\u7D19", L"\u753B\u50CF\u3092\u9078\u629E\u2026", L"\u5143\u306B\u623B\u3059", L"\u6697\u3055", L"\u307C\u304B\u3057",
        L"\u6642\u8A08", L"12\u6642\u9593", L"24\u6642\u9593", L"\u8D77\u52D5\u5F8C\u306B\u30E9\u30F3\u30C1\u30E3\u30FC\u3092\u9589\u3058\u308B", L"ma4.dll \u306E\u5834\u6240", L"\u53C2\u7167\u2026", L"Minecraft\u306F\u8D77\u52D5\u3057\u3066\u3044\u307E\u305B\u3093", L"Minecraft\u304C\u8D77\u52D5\u3057\u3066\u3044\u307E\u3059",
        L"Ma4\u306F\u6709\u52B9\u3067\u3059", L"Minecraft \u7D71\u5408\u7248", L"ma4.dll \u304C\u898B\u3064\u304B\u308A\u307E\u305B\u3093\u3002\u8A2D\u5B9A\u3067\u5834\u6240\u3092\u6307\u5B9A\u3057\u3066\u304F\u3060\u3055\u3044", L"Minecraft\u3092\u8D77\u52D5\u3067\u304D\u307E\u305B\u3093\u3067\u3057\u305F",
        L"Minecraft\u306E\u8D77\u52D5\u3092\u5F85\u3061\u304D\u308C\u307E\u305B\u3093\u3067\u3057\u305F", L"ma4.dll \u306E\u30A2\u30AF\u30BB\u30B9\u6A29\u3092\u8A2D\u5B9A\u3067\u304D\u307E\u305B\u3093\u3067\u3057\u305F", L"ma4.dll \u3092\u8AAD\u307F\u8FBC\u3081\u307E\u305B\u3093\u3067\u3057\u305F", L"\u898B\u3064\u304B\u308A\u307E\u305B\u3093",
        L"\u753B\u50CF\u30D5\u30A1\u30A4\u30EB",
    };
    const Strings kEn = {
        L"Play", L"Inject", L"Active", L"Launching...", L"Waiting for the game...", L"Loading ma4...", L"Settings", L"Wallpaper", L"Choose Image...",
        L"Reset", L"Dim", L"Blur", L"Clock", L"12-hour", L"24-hour", L"Close launcher after launching", L"ma4.dll location", L"Browse...",
        L"Minecraft is not running", L"Minecraft is running", L"Ma4 is active", L"Minecraft Bedrock Edition",
        L"ma4.dll was not found. Choose it in Settings", L"Could not start Minecraft", L"Minecraft took too long to start",
        L"Could not set the access rights of ma4.dll", L"Could not load ma4.dll", L"not found", L"Image files",
    };

    Strings const& tr() {
        static const Strings& s = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE ? kJa : kEn;
        return s;
    }

    // ---------------------------------------------------------------------------------------------- helpers
    std::wstring dateString(SYSTEMTIME const& st) {
        wchar_t weekday[64] = {}, monthDay[64] = {}, pattern[64] = {};
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"dddd", weekday, 64, nullptr);
        if (!GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SMONTHDAY, pattern, 64)) wcscpy_s(pattern, L"MMMM d");
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, pattern, monthDay, 64, nullptr);
        const LANGID lang = GetUserDefaultUILanguage();
        const bool cjk = PRIMARYLANGID(lang) == LANG_JAPANESE || PRIMARYLANGID(lang) == LANG_CHINESE || PRIMARYLANGID(lang) == LANG_KOREAN;
        return cjk ? std::wstring(monthDay) + L" " + weekday : std::wstring(weekday) + L", " + monthDay;
    }

    bool systemIs24h() {
        wchar_t fmt[80] = {};
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIMEFORMAT, fmt, 80);
        return wcschr(fmt, L'H') != nullptr;
    }

    std::wstring clockString(SYSTEMTIME const& st, bool h24) {
        int hour = st.wHour;
        if (!h24) {
            hour %= 12;
            if (hour == 0) hour = 12;
        }
        wchar_t buf[16];
        swprintf(buf, 16, L"%d:%02d", hour, st.wMinute);
        return buf;
    }

    // ---------------------------------------------------------------------------------------------- Direct2D environment
    struct Gpu {
        ComPtr<ID3D11Device> d3d;
        ComPtr<IDXGISwapChain1> swap;
        ComPtr<ID2D1Factory1> factory;
        ComPtr<ID2D1Device> device;
        ComPtr<ID2D1DeviceContext> ctx;
        ComPtr<IDWriteFactory> dwrite;
        ComPtr<IWICImagingFactory> wic;
        ComPtr<ID2D1SolidColorBrush> brush;
        ComPtr<IDWriteTextFormat> fonts[5];
        ComPtr<ID2D1Bitmap1> target; // back buffer (window) or offscreen bitmap (--render)
        ComPtr<ID2D1Bitmap1> logo;

        bool init() {
            D3D_FEATURE_LEVEL fl;
            HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                           D3D11_SDK_VERSION, d3d.GetAddressOf(), &fl, nullptr);
            if (FAILED(hr)) {
                hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                       D3D11_SDK_VERSION, d3d.GetAddressOf(), &fl, nullptr);
            }
            if (FAILED(hr)) return false;
            ComPtr<IDXGIDevice> dxgi;
            if (FAILED(d3d.As(&dxgi))) return false;
            if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr,
                                         reinterpret_cast<void**>(factory.GetAddressOf())))) {
                return false;
            }
            if (FAILED(factory->CreateDevice(dxgi.Get(), device.GetAddressOf()))) return false;
            if (FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx.GetAddressOf()))) return false;
            ctx->SetDpi(96.f, 96.f);
            if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf())))) {
                return false;
            }
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
                                        reinterpret_cast<void**>(wic.GetAddressOf())))) {
                return false;
            }
            if (FAILED(ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), brush.GetAddressOf()))) return false;

            const std::wstring family = ios::defaultFontFamily();
            const DWRITE_FONT_WEIGHT weights[5] = { DWRITE_FONT_WEIGHT_LIGHT, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_WEIGHT_MEDIUM,
                                                    DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_WEIGHT_BOLD };
            for (int i = 0; i < 5; ++i) {
                if (FAILED(dwrite->CreateTextFormat(family.c_str(), nullptr, weights[i], DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.f,
                                                    L"en-us", fonts[i].GetAddressOf()))) {
                    return false;
                }
            }
            loadLogo();
            return true;
        }

        void loadLogo() {
            HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_LOGO), RT_RCDATA);
            if (!res) return;
            HGLOBAL mem = LoadResource(nullptr, res);
            const DWORD size = SizeofResource(nullptr, res);
            const void* data = mem ? LockResource(mem) : nullptr;
            if (!data || !size) return;
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapDecoder> dec;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter> conv;
            if (FAILED(wic->CreateStream(stream.GetAddressOf())) ||
                FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(static_cast<const BYTE*>(data)), size)) ||
                FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, dec.GetAddressOf())) ||
                FAILED(dec->GetFrame(0, frame.GetAddressOf())) || FAILED(wic->CreateFormatConverter(conv.GetAddressOf())) ||
                FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                return;
            }
            ComPtr<ID2D1Bitmap> bmp;
            if (SUCCEEDED(ctx->CreateBitmapFromWicBitmap(conv.Get(), nullptr, bmp.GetAddressOf()))) bmp.As(&logo);
        }

        bool createSwapChain(HWND hwnd, UINT w, UINT h) {
            ComPtr<IDXGIDevice> dxgi;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory2> fac;
            if (FAILED(d3d.As(&dxgi)) || FAILED(dxgi->GetAdapter(adapter.GetAddressOf())) || FAILED(adapter->GetParent(IID_PPV_ARGS(fac.GetAddressOf())))) {
                return false;
            }
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width = w;
            desc.Height = h;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            if (FAILED(fac->CreateSwapChainForHwnd(d3d.Get(), hwnd, &desc, nullptr, nullptr, swap.GetAddressOf()))) return false;
            fac->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
            return bindBackBuffer();
        }

        bool bindBackBuffer() {
            target.Reset();
            ctx->SetTarget(nullptr);
            ComPtr<IDXGISurface> surface;
            if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf())))) return false;
            D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.f, 96.f);
            if (FAILED(ctx->CreateBitmapFromDxgiSurface(surface.Get(), &props, target.GetAddressOf()))) return false;
            ctx->SetTarget(target.Get());
            return true;
        }

        bool resize(UINT w, UINT h) {
            if (!swap || !w || !h) return false;
            target.Reset();
            ctx->SetTarget(nullptr);
            if (FAILED(swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return false;
            return bindBackBuffer();
        }

        bool createOffscreen(UINT w, UINT h) {
            D2D1_BITMAP_PROPERTIES1 props =
                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
            if (FAILED(ctx->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, props, target.GetAddressOf()))) return false;
            ctx->SetTarget(target.Get());
            return true;
        }

        bool savePng(wchar_t const* path) {
            if (!target) return false;
            const D2D1_SIZE_U sz = target->GetPixelSize();
            D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            ComPtr<ID2D1Bitmap1> cpu;
            if (FAILED(ctx->CreateBitmap(sz, nullptr, 0, props, cpu.GetAddressOf())) || FAILED(cpu->CopyFromBitmap(nullptr, target.Get(), nullptr))) {
                return false;
            }
            D2D1_MAPPED_RECT mr;
            if (FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ, &mr))) return false;
            bool ok = false;
            ComPtr<IWICBitmap> wb;
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapEncoder> enc;
            ComPtr<IWICBitmapFrameEncode> frame;
            if (SUCCEEDED(wic->CreateBitmapFromMemory(sz.width, sz.height, GUID_WICPixelFormat32bppPBGRA, mr.pitch, mr.pitch * sz.height, mr.bits,
                                                      wb.GetAddressOf())) &&
                SUCCEEDED(wic->CreateStream(stream.GetAddressOf())) && SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
                SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.GetAddressOf())) &&
                SUCCEEDED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(frame.GetAddressOf(), nullptr)) &&
                SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(sz.width, sz.height))) {
                WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
                ok = SUCCEEDED(frame->SetPixelFormat(&pf)) && SUCCEEDED(frame->WriteSource(wb.Get(), nullptr)) && SUCCEEDED(frame->Commit()) &&
                     SUCCEEDED(enc->Commit());
            }
            cpu->Unmap();
            return ok;
        }
    };

    // ---------------------------------------------------------------------------------------------- launch state
    enum class Phase { Idle, Launching, Waiting, Injecting, Done, Error };
    enum class Err { None, NoDll, Launch, Timeout, Acl, Inject };

    struct Shared {
        std::atomic<int> phase { static_cast<int>(Phase::Idle) };
        std::atomic<int> err { static_cast<int>(Err::None) };
        std::atomic<unsigned long> pid { 0 }; // running Minecraft
        std::atomic<bool> injected { false };
        std::atomic<bool> cancel { false };
        std::atomic<bool> running { false }; // a launch worker is active
        std::atomic<unsigned long long> errorTick { 0 };
        std::mutex m;
        std::wstring detail;        // technical detail of the last error
        std::wstring dllConfigured; // copy of settings.dllPath for the poller thread
    };

    // ---------------------------------------------------------------------------------------------- the application
    class App {
    public:
        HWND hwnd = nullptr;
        Gpu gpu;
        LauncherSettings settings;
        Shared shared;
        Wallpaper wallpaper;
        std::atomic<bool> dirty { true };

        // input (latched between frames so a quick click is never lost)
        float mouseX = -1.f, mouseY = -1.f;
        bool down = false, pressedEdge = false, upPending = false, mouseInside = false;

        bool sheetOpen = false;
        float sheetAmount = 0.f;
        bool firstFrame = true;
        bool pictureDirty = true;
        std::chrono::steady_clock::time_point lastFrame = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point lastRender = std::chrono::steady_clock::now();
        std::thread worker, poller;
        std::atomic<bool> quitPoll { false };
        std::vector<Rect> hot; // interactive rectangles of the last frame (everything else drags the window)
        std::vector<Rect> hotNext;
        bool demo = false;     // --render: fake process state
        int dpi = 96;

        // ------------------------------------------------------------------------------------------ lifecycle
        bool init() {
            settings.load();
            syncDllPath();
            if (!gpu.init()) return false;
            pictureDirty = true;
            return true;
        }

        void syncDllPath() {
            std::lock_guard<std::mutex> lock(shared.m);
            shared.dllConfigured = settings.dllPath;
        }

        void startPoller() {
            poller = std::thread([this] {
                while (!quitPoll.load()) {
                    if (!shared.running.load()) {
                        DWORD pid = inject::findMinecraft();
                        std::wstring configured;
                        { std::lock_guard<std::mutex> lock(shared.m); configured = shared.dllConfigured; }
                        std::wstring dll = inject::resolveDll(configured);
                        bool inj = pid && !dll.empty() && inject::isModuleLoaded(pid, inject::fileNameOf(dll));
                        if (pid != shared.pid.load() || inj != shared.injected.load()) {
                            shared.pid = pid;
                            shared.injected = inj;
                            requestFrame();
                        }
                    }
                    for (int i = 0; i < 15 && !quitPoll.load(); ++i) Sleep(100);
                }
            });
        }

        void shutdown() {
            quitPoll = true;
            shared.cancel = true;
            if (poller.joinable()) poller.join();
            if (worker.joinable()) worker.join();
            settings.save();
        }

        void requestFrame() {
            dirty = true;
            if (hwnd) PostMessageW(hwnd, WM_WAKE, 0, 0);
        }

        // ------------------------------------------------------------------------------------------ launching
        void play() {
            if (shared.running.load()) return;
            if (worker.joinable()) worker.join();
            shared.running = true;
            shared.cancel = false;
            shared.err = static_cast<int>(Err::None);
            shared.phase = static_cast<int>(Phase::Launching);
            const std::wstring configured = settings.dllPath;
            const bool closeWhenDone = settings.closeAfterLaunch;
            worker = std::thread([this, configured, closeWhenDone] {
                auto fail = [&](Err e, std::wstring const& detail) {
                    { std::lock_guard<std::mutex> lock(shared.m); shared.detail = detail; }
                    shared.errorTick = GetTickCount64();
                    shared.err = static_cast<int>(e);
                    shared.phase = static_cast<int>(Phase::Error);
                    shared.running = false;
                    requestFrame();
                };

                const std::wstring dll = inject::resolveDll(configured);
                if (dll.empty()) return fail(Err::NoDll, L"");

                DWORD pid = inject::findMinecraft();
                bool startedByUs = false;
                if (!pid) {
                    shared.phase = static_cast<int>(Phase::Launching);
                    requestFrame();
                    std::wstring error;
                    DWORD newPid = 0;
                    if (!inject::launchMinecraft(newPid, error)) return fail(Err::Launch, error);
                    startedByUs = true;
                    pid = newPid;
                    for (int i = 0; i < 300 && !pid && !shared.cancel.load(); ++i) { // up to 30 s for the process to appear
                        Sleep(100);
                        pid = inject::findMinecraft();
                    }
                    if (!pid) return fail(Err::Launch, error);
                }
                shared.pid = pid;

                if (startedByUs || !inject::isRendererReady(pid)) {
                    shared.phase = static_cast<int>(Phase::Waiting);
                    requestFrame();
                    const auto t0 = std::chrono::steady_clock::now();
                    while (!inject::isRendererReady(pid)) {
                        if (shared.cancel.load()) { shared.running = false; return; }
                        if (!inject::findMinecraft()) return fail(Err::Launch, L"The game closed");
                        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(120)) return fail(Err::Timeout, L"");
                        Sleep(250);
                    }
                    // the renderer exists: let the engine finish setting itself up before the client hooks it
                    for (int i = 0; i < 28 && !shared.cancel.load(); ++i) Sleep(100);
                }

                shared.phase = static_cast<int>(Phase::Injecting);
                requestFrame();
                std::wstring error;
                if (!inject::isModuleLoaded(pid, inject::fileNameOf(dll))) {
                    if (!inject::grantAppPackageAccess(dll, error)) return fail(Err::Acl, error);
                    if (!inject::injectDll(pid, dll, error)) return fail(Err::Inject, error);
                }
                shared.injected = true;
                shared.phase = static_cast<int>(Phase::Done);
                shared.running = false;
                requestFrame();
                if (closeWhenDone && hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            });
        }

        // ------------------------------------------------------------------------------------------ settings actions
        void chooseWallpaper() {
            const Strings& s = tr();
            ComPtr<IFileOpenDialog> dlg;
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dlg.GetAddressOf())))) return;
            COMDLG_FILTERSPEC filters[] = { { s.imageFilter, L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.heic;*.jxr" } };
            dlg->SetFileTypes(1, filters);
            DWORD opts = 0;
            dlg->GetOptions(&opts);
            dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
            if (FAILED(dlg->Show(hwnd))) return;
            ComPtr<IShellItem> item;
            PWSTR raw = nullptr;
            if (FAILED(dlg->GetResult(item.GetAddressOf())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || !raw) return;
            std::wstring src = raw;
            CoTaskMemFree(raw);
            setWallpaperFrom(src);
        }

        bool setWallpaperFrom(std::wstring const& src) {
            std::error_code ec;
            const std::filesystem::path dir = LauncherSettings::folder();
            // keep our own copy: the original may be moved or deleted later
            for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
                if (e.path().stem() == L"wallpaper") std::filesystem::remove(e.path(), ec);
            }
            std::filesystem::path dst = dir / (L"wallpaper" + std::filesystem::path(src).extension().wstring());
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) dst = src;
            if (!wallpaper.setPicture(gpu.ctx.Get(), gpu.wic.Get(), dst.wstring())) {
                wallpaper.setPicture(gpu.ctx.Get(), gpu.wic.Get(), settings.wallpaper);
                return false;
            }
            settings.wallpaper = dst.wstring();
            settings.save();
            requestFrame();
            return true;
        }

        void resetWallpaper() {
            std::error_code ec;
            if (!settings.wallpaper.empty()) std::filesystem::remove(settings.wallpaper, ec);
            settings.wallpaper.clear();
            wallpaper.setPicture(gpu.ctx.Get(), gpu.wic.Get(), L"");
            settings.save();
            requestFrame();
        }

        void chooseDll() {
            ComPtr<IFileOpenDialog> dlg;
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dlg.GetAddressOf())))) return;
            COMDLG_FILTERSPEC filters[] = { { L"ma4.dll", L"*.dll" } };
            dlg->SetFileTypes(1, filters);
            DWORD opts = 0;
            dlg->GetOptions(&opts);
            dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
            if (FAILED(dlg->Show(hwnd))) return;
            ComPtr<IShellItem> item;
            PWSTR raw = nullptr;
            if (FAILED(dlg->GetResult(item.GetAddressOf())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || !raw) return;
            settings.dllPath = raw;
            CoTaskMemFree(raw);
            syncDllPath();
            settings.save();
            requestFrame();
        }

        // ------------------------------------------------------------------------------------------ frame
        [[nodiscard]] bool needsFrame() {
            if (dirty.load()) return true;
            if (Ui::get().animating()) return true;
            return std::chrono::steady_clock::now() - lastRender > std::chrono::milliseconds(1000); // the clock
        }

        /// Draws one frame into the current target. `w`, `h` in pixels.
        void drawFrame(float w, float h, float dt, float scale) {
            auto* ctx = gpu.ctx.Get();
            const Strings& s = tr();

            // ---- wallpaper bitmaps first (they render into their own targets)
            if (pictureDirty) {
                wallpaper.setPicture(ctx, gpu.wic.Get(), settings.wallpaper);
                pictureDirty = false;
            }
            wallpaper.update(ctx, static_cast<int>(w), static_cast<int>(h), settings.dim, settings.blur * scale, 30.f * scale);

            ctx->BeginDraw();
            ctx->SetTransform(D2D1::Matrix3x2F::Identity());
            ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            ctx->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 1.f));

            ios::Gfx g;
            g.ctx = ctx;
            g.brush = gpu.brush.Get();
            g.dwrite = gpu.dwrite.Get();
            g.d2d = gpu.factory.Get();
            for (int i = 0; i < 5; ++i) g.fonts[i] = gpu.fonts[i].Get();

            ios::Input in;
            in.mouse = Vec2(mouseX, mouseY);
            in.down = down || upPending;
            in.pressed = pressedEdge;
            in.dt = dt;

            ios::Palette pal = ios::Palette::make(ios::Appearance::Dark, Col(0.04f, 0.52f, 1.f, 1.f));
            Ui& ui = Ui::get();
            ui.beginFrame(g, in, pal, scale, false);
            const Rect win(0.f, 0.f, w, h);
            ui.setBackdrop(wallpaper.backdrop(), win);
            auto S = [&](float v) { return ui.S(v); };
            hotNext.clear();

            // ---- entrance
            static const uint64_t kIdAppear = Ui::hashId("launcher.appear");
            if (firstFrame) {
                ui.springSet(kIdAppear, 0.f);
                firstFrame = false;
            }
            const float appear = std::clamp(ui.spring(kIdAppear, 1.f, 0.9f, 0.86f), 0.f, 1.2f);
            const float appearA = ios::clamp01(appear);

            // ---- wallpaper with a little parallax, like the iOS lock screen
            static const uint64_t kIdPx = Ui::hashId("launcher.px"), kIdPy = Ui::hashId("launcher.py");
            const float tx = mouseInside ? (mouseX / std::max(1.f, w) - 0.5f) * 2.f : 0.f;
            const float ty = mouseInside ? (mouseY / std::max(1.f, h) - 0.5f) * 2.f : 0.f;
            const float px = ui.spring(kIdPx, tx, 0.9f, 1.f), py = ui.spring(kIdPy, ty, 0.9f, 1.f);
            if (wallpaper.sharp()) {
                const float mx = w * 0.022f, my = h * 0.022f;
                ctx->DrawBitmap(wallpaper.sharp(), D2D1::RectF(-mx - px * mx, -my - py * my, w + mx - px * mx, h + my - py * my), 1.f,
                                D2D1_INTERPOLATION_MODE_LINEAR);
            }
            // readability: darken the top and the bottom a little, like the lock screen scrims
            ui.scrollEdge(Rect(0, 0, w, h * 0.42f), true, Col(0.f, 0.f, 0.f, 0.34f), 1.f);
            ui.scrollEdge(Rect(0, h * 0.58f, w, h), false, Col(0.f, 0.f, 0.f, 0.38f), 1.f);

            const bool sheetBlocks = sheetOpen || sheetAmount > 0.01f;
            ui.lockInput(sheetBlocks);

            // ---- state of the game
            if (static_cast<Phase>(shared.phase.load()) == Phase::Error && !demo &&
                GetTickCount64() - shared.errorTick.load() > 9000ull) {
                shared.phase = static_cast<int>(Phase::Idle);
            }
            const Phase phase = static_cast<Phase>(shared.phase.load());
            const bool busy = phase == Phase::Launching || phase == Phase::Waiting || phase == Phase::Injecting;
            const bool mcRunning = shared.pid.load() != 0;
            const bool injected = shared.injected.load() && mcRunning;

            // ---- date, clock, title
            SYSTEMTIME st;
            GetLocalTime(&st);
            const bool h24 = settings.clock24 < 0 ? systemIs24h() : settings.clock24 == 1;
            const std::wstring dateStr = dateString(st);
            const std::wstring timeStr = clockString(st, h24);

            const float clockPx = std::clamp(h * 0.27f, S(96.f), S(250.f));
            float y = h * 0.095f;
            {
                ui.pushScale(Rect(0, y, w, y + S(300.f)), 0.94f + 0.06f * std::min(1.f, appear));

                ios::TextStyle ds(ios::Font::Semibold, S(23.f), Col(1.f, 1.f, 1.f, 0.92f * appearA), ios::HAlign::Center, ios::VAlign::Center);
                ui.text(Rect(0, y + S(2.f), w, y + S(2.f) + S(32.f)), dateStr, ds);
                y += S(36.f);

                // the clock is glass-like: white fading towards the bottom, with a soft shadow underneath
                ios::TextStyle cs(ios::Font::Bold, clockPx, Col(1.f, 1.f, 1.f, 1.f), ios::HAlign::Center, ios::VAlign::Center);
                cs.ellipsis = false;
                Rect cr(0, y - clockPx * 0.04f, w, y - clockPx * 0.04f + clockPx * 1.12f);
                ios::TextStyle shadowStyle = cs;
                shadowStyle.color = Col(0.f, 0.f, 0.f, 0.20f * appearA);
                ui.text(Rect(cr.left, cr.top + S(5.f), cr.right, cr.bottom + S(5.f)), timeStr, shadowStyle);
                auto grad = ui.linearBrush({ 0.f, cr.top + clockPx * 0.18f }, { 0.f, cr.bottom - clockPx * 0.12f },
                                           { D2D1_GRADIENT_STOP { 0.f, D2D1::ColorF(1.f, 1.f, 1.f, 0.98f * appearA) },
                                             D2D1_GRADIENT_STOP { 1.f, D2D1::ColorF(1.f, 1.f, 1.f, 0.70f * appearA) } });
                if (grad) {
                    ui.textBrush(cr, timeStr, cs, grad.Get());
                } else {
                    cs.color.a = appearA;
                    ui.text(cr, timeStr, cs);
                }
                y = cr.bottom + S(2.f);

                // ---- title row: logo + "Ma4"
                const float logoD = S(64.f);
                ios::TextStyle ts(ios::Font::Bold, S(58.f), Col(1.f, 1.f, 1.f, 0.97f * appearA), ios::HAlign::Leading, ios::VAlign::Center);
                ts.ellipsis = false;
                const float tw = ui.textWidth(L"Ma4", ts);
                const float gap = S(16.f);
                const float total = logoD + gap + tw;
                const float lx = (w - total) * 0.5f;
                Rect logoR(lx, y, lx + logoD, y + logoD);
                if (gpu.logo) {
                    ui.shadow(logoR, logoD * 0.5f, S(16.f), S(6.f), Col(0.f, 0.f, 0.f, 0.34f * appearA));
                    ui.bitmap(gpu.logo.Get(), logoR, appearA);
                } else {
                    ui.circle({ logoR.centerX(), logoR.centerY() }, logoD * 0.5f, Col(1.f, 1.f, 1.f, 0.2f));
                }
                ios::TextStyle tsShadow = ts;
                tsShadow.color = Col(0.f, 0.f, 0.f, 0.22f * appearA);
                Rect tr(lx + logoD + gap, y - S(4.f), lx + total + S(8.f), y + logoD + S(4.f));
                ui.text(Rect(tr.left, tr.top + S(3.f), tr.right, tr.bottom + S(3.f)), L"Ma4", tsShadow);
                ui.text(tr, L"Ma4", ts);
                y += logoD + S(8.f);

                std::wstring sub = std::wstring(s.subtitle) + L"  \u00B7  " + versionText();
                ui.text(Rect(0, y, w, y + S(24.f)), sub,
                        ios::TextStyle(ios::Font::Medium, S(16.f), Col(1.f, 1.f, 1.f, 0.72f * appearA), ios::HAlign::Center, ios::VAlign::Center));
                y += S(40.f);
                ui.popScale();
            }

            // ---- status chip
            {
                std::wstring text;
                Col dot = Col(0.62f, 0.62f, 0.66f, 1.f);
                Err err = static_cast<Err>(shared.err.load());
                switch (phase) {
                case Phase::Launching: text = s.launching; dot = pal.orange; break;
                case Phase::Waiting: text = s.waiting; dot = pal.orange; break;
                case Phase::Injecting: text = s.injecting; dot = pal.orange; break;
                case Phase::Error:
                    dot = pal.red;
                    switch (err) {
                    case Err::NoDll: text = s.errNoDll; break;
                    case Err::Launch: text = s.errLaunch; break;
                    case Err::Timeout: text = s.errTimeout; break;
                    case Err::Acl: text = s.errAcl; break;
                    default: text = s.errInject; break;
                    }
                    break;
                default:
                    if (injected) {
                        text = s.ready;
                        dot = pal.green;
                    } else if (mcRunning) {
                        text = s.running;
                        dot = pal.yellow;
                    } else {
                        text = s.notRunning;
                    }
                    break;
                }
                ios::TextStyle cs(ios::Font::Medium, S(15.f), Col(1.f, 1.f, 1.f, 0.95f), ios::HAlign::Leading, ios::VAlign::Center);
                const float tw = std::min(ui.textWidth(text, cs), w - S(120.f));
                const float ch = S(36.f), cwid = tw + S(52.f);
                Rect chip(w * 0.5f - cwid * 0.5f, y, w * 0.5f + cwid * 0.5f, y + ch);
                ios::GlassStyle gs;
                gs.shadow = 0.7f;
                gs.opacity = appearA;
                ui.glass(chip, ch * 0.5f, gs);
                const float pulse = busy ? 0.6f + 0.4f * std::sin(static_cast<float>(GetTickCount64() % 1200) / 1200.f * 6.2831853f) : 1.f;
                ui.circle({ chip.left + S(19.f), chip.centerY() }, S(5.f), Col(dot.r, dot.g, dot.b, pulse * appearA));
                ui.text(Rect(chip.left + S(32.f), chip.top, chip.right - S(14.f), chip.bottom), text,
                        ios::TextStyle(ios::Font::Medium, S(15.f), Col(1.f, 1.f, 1.f, 0.95f * appearA), ios::HAlign::Leading, ios::VAlign::Center));
                if (busy) requestFrameSoon();
            }

            // ---- buttons: settings (round) + Play (prominent capsule)
            {
                const float bh = S(60.f), gearD = bh, playW = S(240.f), gap = S(16.f);
                const float total = gearD + gap + playW;
                const float rise = (1.f - std::min(1.f, appear)) * S(40.f);
                const float by = h - S(44.f) - bh + rise;
                Rect gearR((w - total) * 0.5f, by, (w - total) * 0.5f + gearD, by + bh);
                Rect playR(gearR.right + gap, by, gearR.right + gap + playW, by + bh);
                hotNext.push_back(gearR);
                hotNext.push_back(playR);

                if (ui.glassIconButton(Ui::hashId("launcher.settings"), gearR, ios::Glyph::Gear, Col(0.f, 0.f, 0.f, 0.f), 0.46f)) {
                    sheetOpen = true;
                }

                // Play / Inject / Active
                const uint64_t id = Ui::hashId("launcher.play");
                const bool enabled = !busy && !injected;
                auto cr = ui.click(id, playR, enabled);
                const float pr = ui.pressSpring(id ^ 0x6B55ull, cr.pressed && enabled);
                const float hov = ui.anim(id ^ 0x6B66ull, (cr.hovered && enabled) ? 1.f : 0.f, 0.08f);
                ui.pushScale(playR, std::clamp(1.f + 0.06f * pr, 0.92f, 1.14f));
                ios::GlassStyle gs;
                gs.press = ios::clamp01(pr);
                gs.lift = hov;
                gs.backing = 0.f;
                gs.tint = ios::withAlpha(injected ? pal.green : pal.accent, 0.93f);
                gs.opacity = (busy ? 0.78f : 1.f) * appearA;
                ui.glass(playR, bh * 0.5f, gs);
                const wchar_t* label = busy ? (phase == Phase::Injecting ? s.injecting : (phase == Phase::Waiting ? s.waiting : s.launching))
                                       : injected ? s.injected
                                       : mcRunning ? s.inject
                                                   : s.play;
                ios::TextStyle ps(ios::Font::Bold, S(21.f), Col(1.f, 1.f, 1.f, appearA), ios::HAlign::Center, ios::VAlign::Center);
                const float lw = std::min(ui.textWidth(label, ps), playW - S(90.f));
                const float gl = S(22.f);
                const float cx = playR.centerX();
                const float groupL = cx - (gl + S(10.f) + lw) * 0.5f;
                if (!busy) {
                    ios::Glyph glyph = injected ? ios::Glyph::Check : ios::Glyph::Play;
                    ui.glyph(glyph, Rect(groupL, playR.centerY() - gl * 0.5f, groupL + gl, playR.centerY() + gl * 0.5f), Col(1.f, 1.f, 1.f, appearA), 1.1f);
                    ios::TextStyle psLeading = ps;
                    psLeading.h = ios::HAlign::Leading;
                    ui.text(Rect(groupL + gl + S(10.f), playR.top, playR.right, playR.bottom), label, psLeading);
                } else {
                    ui.text(playR, label, ps);
                }
                ui.popScale();
                if (cr.clicked) play();
            }

            // ---- window buttons (top right)
            {
                const float d = S(40.f), m = S(18.f);
                Rect closeR(w - m - d, m, w - m, m + d);
                Rect minR(closeR.left - S(12.f) - d, m, closeR.left - S(12.f), m + d);
                hotNext.push_back(closeR);
                hotNext.push_back(minR);
                if (ui.glassIconButton(Ui::hashId("launcher.min"), minR, ios::Glyph::Minus, Col(0.f, 0.f, 0.f, 0.f), 0.40f)) {
                    ShowWindow(hwnd, SW_MINIMIZE);
                }
                if (ui.glassIconButton(Ui::hashId("launcher.close"), closeR, ios::Glyph::XMark, Col(0.f, 0.f, 0.f, 0.f), 0.40f)) {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
            }

            // ---- settings sheet
            ui.lockInput(false);
            drawSheet(ui, win, scale);

            ui.endFrame();
            hot = hotNext;
            ctx->EndDraw();
        }

        void requestFrameSoon() { dirty = true; }

        // ------------------------------------------------------------------------------------------ settings sheet
        void drawSheet(Ui& ui, Rect const& win, float /*scale*/) {
            static const uint64_t kIdSheet = Ui::hashId("launcher.sheet");
            const Strings& s = tr();
            auto S = [&](float v) { return ui.S(v); };
            const ios::Palette& pal = ui.pal;

            const float open = ui.spring(kIdSheet, sheetOpen ? 1.f : 0.f, 0.46f, 0.84f);
            sheetAmount = open;
            const float a = ios::clamp01(open);
            if (a <= 0.003f && !sheetOpen) return;

            const float w = win.getWidth(), h = win.getHeight();
            ui.fillRect(win, Col(0.f, 0.f, 0.f, 0.46f * a));
            hotNext.push_back(win);

            const float sw = std::min(S(520.f), w - S(32.f));
            const float sh = std::min(S(600.f), h - S(28.f));
            const float sx = (w - sw) * 0.5f;
            const float sy = h - sh - S(14.f) + (1.f - open) * (sh + S(60.f));
            Rect sheet(sx, sy, sx + sw, sy + sh);
            const float R = S(38.f);

            ui.lockInput(open < 0.92f);
            if (ui.in.pressed && sheetOpen && open > 0.9f && !sheet.contains(ui.mouse())) sheetOpen = false;

            {
                ios::GlassStyle gs;
                gs.shadow = 1.8f;
                gs.backing = 1.f;
                ui.glass(sheet, R, gs);
                ui.fillSmooth(sheet, R, Col(0.08f, 0.08f, 0.10f, 0.52f));
            }

            const float pad = S(16.f);
            const float x0 = sheet.left + pad, x1 = sheet.right - pad;
            float y = sheet.top + S(10.f);
            ui.rrect(Rect(sheet.centerX() - S(18.f), y, sheet.centerX() + S(18.f), y + S(5.f)), S(2.5f), Col(1.f, 1.f, 1.f, 0.34f));
            y += S(16.f);

            // header
            {
                ui.text(Rect(x0, y, x1, y + S(40.f)), s.settings, ios::TextStyle(ios::Font::Bold, S(20.f), pal.label, ios::HAlign::Center, ios::VAlign::Center));
                Rect closeR(x1 - S(36.f), y + S(2.f), x1, y + S(38.f));
                if (ui.glassIconButton(Ui::hashId("sheet.close"), closeR, ios::Glyph::XMark, Col(0.f, 0.f, 0.f, 0.f), 0.40f)) sheetOpen = false;
                y += S(52.f);
            }

            auto card = [&](float top, float height) {
                Rect c(x0, top, x1, top + height);
                ui.rrect(c, S(20.f), Col(1.f, 1.f, 1.f, 0.075f));
                ui.rrectStroke(c, S(20.f), Col(1.f, 1.f, 1.f, 0.10f), std::max(1.f, S(0.75f)));
                return c;
            };
            auto divider = [&](Rect const& c, float yy) { ui.hairlineH(c.left + S(16.f), c.right, yy, pal.separator); };
            auto labelStyle = [&]() { return ios::TextStyle(ios::Font::Regular, S(16.f), pal.label, ios::HAlign::Leading, ios::VAlign::Center); };
            auto valueStyle = [&]() { return ios::TextStyle(ios::Font::Regular, S(15.f), pal.secondaryLabel, ios::HAlign::Trailing, ios::VAlign::Center); };

            // ---- wallpaper card
            {
                const float previewH = S(112.f), sliderRow = S(62.f);
                Rect c = card(y, previewH + sliderRow * 2.f);
                // preview
                const float aspect = h > 0.f ? w / h : 1.6f;
                const float th = S(88.f), tw = th * aspect;
                Rect thumb(c.left + S(14.f), c.top + S(12.f), c.left + S(14.f) + std::min(tw, S(190.f)), c.top + S(12.f) + std::min(tw, S(190.f)) / aspect);
                ui.shadow(thumb, S(12.f), S(8.f), S(3.f), Col(0.f, 0.f, 0.f, 0.3f));
                if (wallpaper.sharp()) {
                    ui.pushClipRRect(thumb, S(12.f));
                    ui.bitmap(wallpaper.sharp(), thumb);
                    ui.popClipRRect();
                }
                ui.rrectStroke(thumb, S(12.f), Col(1.f, 1.f, 1.f, 0.22f), std::max(1.f, S(0.75f)));

                const float bx = thumb.right + S(14.f);
                Rect choose(bx, c.top + S(14.f), c.right - S(14.f), c.top + S(14.f) + S(38.f));
                Rect reset(bx, choose.bottom + S(10.f), c.right - S(14.f), choose.bottom + S(10.f) + S(38.f));
                if (ui.glassButton(Ui::hashId("sheet.choose"), choose, s.choose, ios::GlassButtonKind::Prominent, 15.f)) pendingChoose = true;
                if (ui.glassButton(Ui::hashId("sheet.reset"), reset, s.reset, ios::GlassButtonKind::Regular, 15.f, !settings.wallpaper.empty())) pendingReset = true;

                float yy = c.top + previewH;
                divider(c, yy);
                // dim
                {
                    ui.text(Rect(c.left + S(16.f), yy + S(6.f), c.right - S(80.f), yy + S(30.f)), s.dim, labelStyle());
                    wchar_t buf[16];
                    swprintf(buf, 16, L"%d%%", static_cast<int>(std::lround(settings.dim * 100.f)));
                    ui.text(Rect(c.right - S(80.f), yy + S(6.f), c.right - S(16.f), yy + S(30.f)), buf, valueStyle());
                    Rect track(c.left + S(16.f), yy + S(32.f), c.right - S(16.f), yy + S(56.f));
                    float v = settings.dim;
                    if (ui.slider(Ui::hashId("sheet.dim"), track, v, 0.f, 0.7f, 0.01f)) {
                        settings.dim = v;
                        requestFrame();
                    }
                    yy += sliderRow;
                }
                divider(c, yy);
                // blur
                {
                    ui.text(Rect(c.left + S(16.f), yy + S(6.f), c.right - S(80.f), yy + S(30.f)), s.blur, labelStyle());
                    wchar_t buf[16];
                    swprintf(buf, 16, L"%d", static_cast<int>(std::lround(settings.blur)));
                    ui.text(Rect(c.right - S(80.f), yy + S(6.f), c.right - S(16.f), yy + S(30.f)), buf, valueStyle());
                    Rect track(c.left + S(16.f), yy + S(32.f), c.right - S(16.f), yy + S(56.f));
                    float v = settings.blur;
                    if (ui.slider(Ui::hashId("sheet.blur"), track, v, 0.f, 40.f, 1.f)) {
                        settings.blur = v;
                        requestFrame();
                    }
                }
                y = c.bottom + S(14.f);
            }

            // ---- clock + launch card
            {
                const float rowH = S(56.f);
                Rect c = card(y, rowH * 2.f);
                ui.text(Rect(c.left + S(16.f), c.top, c.left + S(160.f), c.top + rowH), s.clock, labelStyle());
                Rect seg(c.right - S(16.f) - S(190.f), c.top + (rowH - S(32.f)) * 0.5f, c.right - S(16.f), c.top + (rowH + S(32.f)) * 0.5f);
                const int cur = settings.clock24 < 0 ? (systemIs24h() ? 1 : 0) : settings.clock24;
                int sel = ui.segmented(Ui::hashId("sheet.clock"), seg, { s.h12, s.h24 }, cur);
                if (sel != cur) {
                    settings.clock24 = sel;
                    requestFrame();
                }
                divider(c, c.top + rowH);
                ui.text(Rect(c.left + S(16.f), c.top + rowH, c.right - S(16.f) - S(ios::kSwitchW) - S(12.f), c.top + rowH * 2.f), s.closeAfter, labelStyle());
                Rect sw2(c.right - S(16.f) - S(ios::kSwitchW), c.top + rowH + (rowH - S(ios::kSwitchH)) * 0.5f, c.right - S(16.f),
                         c.top + rowH + (rowH + S(ios::kSwitchH)) * 0.5f);
                bool v = settings.closeAfterLaunch;
                if (ui.toggle(Ui::hashId("sheet.close.after"), sw2, v)) {
                    settings.closeAfterLaunch = v;
                    settings.save();
                }
                y = c.bottom + S(14.f);
            }

            // ---- dll card
            {
                Rect c = card(y, S(76.f));
                const std::wstring resolved = inject::resolveDll(settings.dllPath);
                ui.text(Rect(c.left + S(16.f), c.top + S(10.f), c.right - S(130.f), c.top + S(34.f)), s.dllLabel,
                        ios::TextStyle(ios::Font::Medium, S(16.f), pal.label, ios::HAlign::Leading, ios::VAlign::Center));
                ui.text(Rect(c.left + S(16.f), c.top + S(36.f), c.right - S(130.f), c.top + S(62.f)), resolved.empty() ? std::wstring(s.notFound) : resolved,
                        ios::TextStyle(ios::Font::Regular, S(12.5f), resolved.empty() ? pal.red : pal.secondaryLabel, ios::HAlign::Leading, ios::VAlign::Center));
                Rect browse(c.right - S(16.f) - S(100.f), c.top + (S(76.f) - S(38.f)) * 0.5f, c.right - S(16.f), c.top + (S(76.f) + S(38.f)) * 0.5f);
                if (ui.glassButton(Ui::hashId("sheet.dll"), browse, s.browse, ios::GlassButtonKind::Regular, 15.f)) pendingDll = true;
                y = c.bottom + S(12.f);
            }

            ui.text(Rect(x0, y, x1, y + S(20.f)), std::wstring(L"Ma4 Launcher  ") + versionText(),
                    ios::TextStyle(ios::Font::Regular, S(12.f), pal.tertiaryLabel, ios::HAlign::Center, ios::VAlign::Center));
        }

        bool pendingChoose = false, pendingReset = false, pendingDll = false;

        // ------------------------------------------------------------------------------------------ message-loop side
        void handlePending() {
            if (pendingChoose) {
                pendingChoose = false;
                chooseWallpaper();
            }
            if (pendingReset) {
                pendingReset = false;
                resetWallpaper();
            }
            if (pendingDll) {
                pendingDll = false;
                chooseDll();
            }
        }

        void render() {
            if (!gpu.target) return;
            RECT rc;
            GetClientRect(hwnd, &rc);
            const float w = static_cast<float>(rc.right - rc.left), h = static_cast<float>(rc.bottom - rc.top);
            if (w < 8.f || h < 8.f) return;

            const auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - lastFrame).count();
            lastFrame = now;
            dt = std::clamp(dt, 0.001f, 0.05f);
            dpi = static_cast<int>(GetDpiForWindow(hwnd));

            dirty = false;
            drawFrame(w, h, dt, static_cast<float>(dpi) / 96.f);
            pressedEdge = false;
            upPending = false;
            lastRender = now;

            HRESULT hr = gpu.swap->Present(1, 0);
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                // the GPU went away (driver update, sleep): rebuild everything
                Ui::get().releaseDeviceResources();
                wallpaper.release();
                gpu = Gpu {};
                if (gpu.init() && gpu.createSwapChain(hwnd, static_cast<UINT>(w), static_cast<UINT>(h))) {
                    pictureDirty = true;
                    firstFrame = true;
                } else {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
            }
            handlePending();
        }
    };

    App* g_app = nullptr;

    // ---------------------------------------------------------------------------------------------- window
    bool overHot(App const& app, int x, int y) {
        const Vec2 p(static_cast<float>(x), static_cast<float>(y));
        for (auto const& r : app.hot) {
            if (r.contains(p)) return true;
        }
        return false;
    }

    LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        App* app = g_app;
        switch (msg) {
        case WM_NCCALCSIZE:
            if (wp) return 0; // the whole window is client area: we draw the "frame" ourselves
            break;
        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT rc;
            GetWindowRect(hwnd, &rc);
            const int border = std::max(6, static_cast<int>(6 * GetDpiForWindow(hwnd) / 96));
            const bool l = pt.x < rc.left + border, r = pt.x >= rc.right - border, t = pt.y < rc.top + border, b = pt.y >= rc.bottom - border;
            if (t && l) return HTTOPLEFT;
            if (t && r) return HTTOPRIGHT;
            if (b && l) return HTBOTTOMLEFT;
            if (b && r) return HTBOTTOMRIGHT;
            if (l) return HTLEFT;
            if (r) return HTRIGHT;
            if (t) return HTTOP;
            if (b) return HTBOTTOM;
            if (app && !overHot(*app, pt.x - rc.left, pt.y - rc.top)) return HTCAPTION; // drag the window by the wallpaper
            return HTCLIENT;
        }
        case WM_NCLBUTTONDBLCLK:
            return 0; // no maximise by double click
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            const float k = static_cast<float>(GetDpiForWindow(hwnd)) / 96.f;
            mmi->ptMinTrackSize.x = static_cast<LONG>(kMinW * k);
            mmi->ptMinTrackSize.y = static_cast<LONG>(kMinH * k);
            return 0;
        }
        case WM_DPICHANGED: {
            auto* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (app) app->requestFrame();
            return 0;
        }
        case WM_SIZE:
            if (app && app->gpu.swap && wp != SIZE_MINIMIZED) {
                app->gpu.resize(LOWORD(lp), HIWORD(lp));
                app->wallpaper.invalidate();
                app->requestFrame();
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            if (app) app->dirty = true;
            return 0;
        }
        case WM_MOUSEMOVE:
            if (app) {
                if (!app->mouseInside) {
                    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                    TrackMouseEvent(&tme);
                }
                app->mouseInside = true;
                app->mouseX = static_cast<float>(GET_X_LPARAM(lp));
                app->mouseY = static_cast<float>(GET_Y_LPARAM(lp));
                app->requestFrame();
            }
            return 0;
        case WM_MOUSELEAVE:
            if (app) {
                app->mouseInside = false;
                app->mouseX = app->mouseY = -1000.f;
                app->requestFrame();
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (app) {
                SetCapture(hwnd);
                app->mouseX = static_cast<float>(GET_X_LPARAM(lp));
                app->mouseY = static_cast<float>(GET_Y_LPARAM(lp));
                app->down = true;
                app->pressedEdge = true;
                app->requestFrame();
            }
            return 0;
        case WM_LBUTTONUP:
            if (app) {
                if (GetCapture() == hwnd) ReleaseCapture();
                app->mouseX = static_cast<float>(GET_X_LPARAM(lp));
                app->mouseY = static_cast<float>(GET_Y_LPARAM(lp));
                app->down = false;
                if (app->pressedEdge) app->upPending = true; // the press has not been drawn yet: show it for one frame first
                app->requestFrame();
            }
            return 0;
        case WM_KEYDOWN:
            if (app) {
                if (wp == VK_ESCAPE && app->sheetOpen) {
                    app->sheetOpen = false;
                    app->requestFrame();
                } else if ((wp == VK_RETURN || wp == VK_SPACE) && !app->sheetOpen) {
                    app->play();
                }
            }
            return 0;
        case WM_WAKE:
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    int runWindow(HINSTANCE inst) {
        App app;
        g_app = &app;
        if (!app.init()) {
            MessageBoxW(nullptr, L"Could not start Direct2D.", L"Ma4 Launcher", MB_ICONERROR);
            return 1;
        }

        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
        wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0));
        wc.lpszClassName = L"Ma4LauncherWindow";
        RegisterClassExW(&wc);

        const UINT sysDpi = GetDpiForSystem();
        const float k = static_cast<float>(sysDpi) / 96.f;
        int ww = static_cast<int>(kDefaultW * k), wh = static_cast<int>(kDefaultH * k);
        RECT work;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        ww = std::min<int>(ww, work.right - work.left - 40);
        wh = std::min<int>(wh, work.bottom - work.top - 40);
        const int x = work.left + ((work.right - work.left) - ww) / 2, y = work.top + ((work.bottom - work.top) - wh) / 2;

        HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Ma4", WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_SYSMENU, x, y, ww, wh,
                                    nullptr, nullptr, inst, nullptr);
        if (!hwnd) return 1;
        app.hwnd = hwnd;

        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        int corner = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        MARGINS margins = { 1, 1, 1, 1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        RECT rc;
        GetClientRect(hwnd, &rc);
        if (!app.gpu.createSwapChain(hwnd, static_cast<UINT>(rc.right - rc.left), static_cast<UINT>(rc.bottom - rc.top))) {
            MessageBoxW(nullptr, L"Could not create the swap chain.", L"Ma4 Launcher", MB_ICONERROR);
            return 1;
        }

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        app.startPoller();

        MSG msg;
        for (;;) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    app.shutdown();
                    g_app = nullptr;
                    return static_cast<int>(msg.wParam);
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!IsIconic(hwnd) && app.needsFrame()) {
                app.render(); // blocks on vsync
            } else {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 250, QS_ALLINPUT);
            }
        }
    }

    // ---------------------------------------------------------------------------------------------- command line
    void attachConsole() {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
    }

    int runInject(int argc, wchar_t** argv) {
        attachConsole();
        if (argc < 4) {
            std::wprintf(L"usage: Ma4Launcher.exe --inject <pid> <dll>\n");
            return 2;
        }
        const DWORD pid = static_cast<DWORD>(std::wcstoul(argv[2], nullptr, 10));
        std::wstring dll = std::filesystem::absolute(argv[3]).wstring();
        std::wstring error;
        if (!inject::grantAppPackageAccess(dll, error)) {
            std::wprintf(L"access rights: %s\n", error.c_str());
            return 1;
        }
        if (!inject::injectDll(pid, dll, error)) {
            std::wprintf(L"inject failed: %s\n", error.c_str());
            return 1;
        }
        std::wprintf(L"injected %s into %lu\n", dll.c_str(), static_cast<unsigned long>(pid));
        return 0;
    }

    int runRender(int argc, wchar_t** argv) {
        attachConsole();
        if (argc < 3) {
            std::wprintf(L"usage: Ma4Launcher.exe --render out.png [W H] [--sheet] [--state idle|running|ready|error|busy] [--wallpaper file]\n");
            return 2;
        }
        std::wstring out = argv[2];
        int w = 1040, h = 660;
        bool sheet = false;
        std::wstring state = L"idle", picture;
        float blurArg = -1.f, dimArg = -1.f;
        for (int i = 3; i < argc; ++i) {
            std::wstring a = argv[i];
            if (a == L"--sheet") sheet = true;
            else if (a == L"--state" && i + 1 < argc) state = argv[++i];
            else if (a == L"--wallpaper" && i + 1 < argc) picture = argv[++i];
            else if (a == L"--blur" && i + 1 < argc) blurArg = std::wcstof(argv[++i], nullptr);
            else if (a == L"--dim" && i + 1 < argc) dimArg = std::wcstof(argv[++i], nullptr);
            else if (i + 1 < argc && iswdigit(a[0]) && iswdigit(argv[i + 1][0])) {
                w = std::wcstol(argv[i], nullptr, 10);
                h = std::wcstol(argv[i + 1], nullptr, 10);
                ++i;
            }
        }

        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        App app;
        g_app = &app;
        if (!app.init() || !app.gpu.createOffscreen(static_cast<UINT>(w), static_cast<UINT>(h))) return 1;
        app.demo = true;
        if (!picture.empty()) {
            app.settings.wallpaper = picture; // used as is, nothing is copied or saved
        }
        if (blurArg >= 0.f) app.settings.blur = blurArg;
        if (dimArg >= 0.f) app.settings.dim = dimArg;
        if (state == L"running") app.shared.pid = 4242;
        if (state == L"ready") { app.shared.pid = 4242; app.shared.injected = true; }
        if (state == L"error") { app.shared.phase = static_cast<int>(Phase::Error); app.shared.err = static_cast<int>(Err::NoDll); }
        if (state == L"busy") app.shared.phase = static_cast<int>(Phase::Waiting);

        for (int i = 0; i < 90; ++i) {
            if (i == 20 && sheet) app.sheetOpen = true;
            app.drawFrame(static_cast<float>(w), static_cast<float>(h), 1.f / 60.f, 1.f);
        }
        const bool ok = app.gpu.savePng(out.c_str());
        std::wprintf(ok ? L"wrote %s\n" : L"failed to write %s\n", out.c_str());
        g_app = nullptr;
        return ok ? 0 : 1;
    }

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int rc = 0;
    if (argv && argc >= 2 && std::wstring(argv[1]) == L"--inject") {
        rc = runInject(argc, argv);
    } else if (argv && argc >= 2 && std::wstring(argv[1]) == L"--render") {
        rc = runRender(argc, argv);
    } else {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        rc = runWindow(inst);
    }
    if (argv) LocalFree(argv);
    return rc;
}
