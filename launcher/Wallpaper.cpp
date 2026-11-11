#include "Wallpaper.h"

#include <algorithm>
#include <cmath>

namespace {
    D2D1_BITMAP_PROPERTIES1 targetProps() {
        return D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                                       D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    }

    ComPtr<ID2D1Bitmap1> makeTarget(ID2D1DeviceContext* ctx, int w, int h) {
        ComPtr<ID2D1Bitmap1> bmp;
        auto props = targetProps();
        ctx->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)), nullptr, 0, props, bmp.GetAddressOf());
        return bmp;
    }

    // draws `src` blurred over the whole target
    void drawBlurred(ID2D1DeviceContext* ctx, ID2D1Bitmap1* src, float sigma) {
        ComPtr<ID2D1Effect> fx;
        if (FAILED(ctx->CreateEffect(CLSID_D2D1GaussianBlur, fx.GetAddressOf()))) {
            ctx->DrawBitmap(src);
            return;
        }
        fx->SetInput(0, src);
        fx->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, sigma);
        // HARD: the edge pixels are repeated instead of fading to transparent
        fx->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
        ctx->DrawImage(fx.Get());
    }

    ComPtr<ID2D1LinearGradientBrush> linear(ID2D1DeviceContext* ctx, D2D1_POINT_2F a, D2D1_POINT_2F b,
                                            std::initializer_list<D2D1_GRADIENT_STOP> stops) {
        ComPtr<ID2D1GradientStopCollection> coll;
        ComPtr<ID2D1LinearGradientBrush> br;
        std::vector<D2D1_GRADIENT_STOP> v(stops);
        if (SUCCEEDED(ctx->CreateGradientStopCollection(v.data(), static_cast<UINT32>(v.size()), coll.GetAddressOf()))) {
            ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(a, b), coll.Get(), br.GetAddressOf());
        }
        return br;
    }
} // namespace

bool Wallpaper::setPicture(ID2D1DeviceContext* ctx, IWICImagingFactory* wic, std::wstring const& path) {
    image_.Reset();
    dirty_ = true;
    if (path.empty() || !ctx || !wic) return true;

    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, dec.GetAddressOf()))) {
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, frame.GetAddressOf()))) return false;

    UINT iw = 0, ih = 0;
    frame->GetSize(&iw, &ih);
    if (!iw || !ih) return false;

    // a wallpaper never needs more than a 4K picture: scale bigger ones down so they stay small on the GPU
    ComPtr<IWICBitmapSource> source = frame;
    const UINT longSide = std::max(iw, ih);
    if (longSide > 3840) {
        const double k = 3840.0 / static_cast<double>(longSide);
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(wic->CreateBitmapScaler(scaler.GetAddressOf())) &&
            SUCCEEDED(scaler->Initialize(frame.Get(), std::max(1u, static_cast<UINT>(iw * k)), std::max(1u, static_cast<UINT>(ih * k)),
                                         WICBitmapInterpolationModeFant))) {
            source = scaler;
        }
    }

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(conv.GetAddressOf())) ||
        FAILED(conv->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom))) {
        return false;
    }
    ComPtr<ID2D1Bitmap1> bmp;
    D2D1_BITMAP_PROPERTIES1 props =
        D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(ctx->CreateBitmapFromWicBitmap(conv.Get(), &props, bmp.GetAddressOf()))) return false;
    image_ = bmp;
    return true;
}

void Wallpaper::release() {
    image_.Reset();
    source_.Reset();
    sharp_.Reset();
    backdrop_.Reset();
    w_ = h_ = 0;
    dirty_ = true;
}

void Wallpaper::drawAurora(ID2D1DeviceContext* ctx, float w, float h) {
    auto stop = [](float p, float r, float g, float b, float a) { return D2D1_GRADIENT_STOP { p, D2D1::ColorF(r, g, b, a) }; };

    // deep indigo base
    if (auto base = linear(ctx, D2D1::Point2F(0, 0), D2D1::Point2F(w, h),
                           { stop(0.f, 0.05f, 0.07f, 0.22f, 1.f), stop(0.55f, 0.16f, 0.09f, 0.34f, 1.f), stop(1.f, 0.30f, 0.10f, 0.30f, 1.f) })) {
        ctx->FillRectangle(D2D1::RectF(0, 0, w, h), base.Get());
    }

    struct Blob {
        float cx, cy, r;
        float cr, cg, cb, ca;
    };
    const float m = std::max(w, h);
    const Blob blobs[] = {
        { 0.18f * w, 0.22f * h, 0.62f * m, 1.00f, 0.32f, 0.55f, 0.62f }, // pink
        { 0.82f * w, 0.16f * h, 0.58f * m, 0.12f, 0.50f, 1.00f, 0.60f }, // blue
        { 0.62f * w, 0.98f * h, 0.64f * m, 0.10f, 0.88f, 0.72f, 0.40f }, // teal
        { 0.06f * w, 0.92f * h, 0.52f * m, 0.58f, 0.30f, 1.00f, 0.50f }, // violet
        { 0.96f * w, 0.70f * h, 0.42f * m, 1.00f, 0.60f, 0.25f, 0.32f }, // amber
    };
    for (auto const& b : blobs) {
        D2D1_GRADIENT_STOP stops[3] = { stop(0.f, b.cr, b.cg, b.cb, b.ca), stop(0.5f, b.cr, b.cg, b.cb, b.ca * 0.34f),
                                        stop(1.f, b.cr, b.cg, b.cb, 0.f) };
        ComPtr<ID2D1GradientStopCollection> coll;
        ComPtr<ID2D1RadialGradientBrush> br;
        if (SUCCEEDED(ctx->CreateGradientStopCollection(stops, 3, coll.GetAddressOf())) &&
            SUCCEEDED(ctx->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(D2D1::Point2F(b.cx, b.cy), D2D1::Point2F(0, 0), b.r, b.r), coll.Get(),
                br.GetAddressOf()))) {
            ctx->FillRectangle(D2D1::RectF(0, 0, w, h), br.Get());
        }
    }
}

void Wallpaper::drawSource(ID2D1DeviceContext* ctx, float w, float h) {
    if (!image_) {
        drawAurora(ctx, w, h);
        return;
    }
    // "aspect fill": cover the window, centred
    D2D1_SIZE_F is = image_->GetSize();
    const float k = std::max(w / is.width, h / is.height);
    const float dw = is.width * k, dh = is.height * k;
    const float x = (w - dw) * 0.5f, y = (h - dh) * 0.5f;
    ctx->DrawBitmap(image_.Get(), D2D1::RectF(x, y, x + dw, y + dh), 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
}

void Wallpaper::update(ID2D1DeviceContext* ctx, int width, int height, float dim, float blurPx, float backdropPx) {
    if (!ctx || width < 8 || height < 8) return;
    const bool resized = width != w_ || height != h_ || !sharp_ || !backdrop_;
    if (!resized && !dirty_ && std::fabs(dim - dim_) < 0.002f && std::fabs(blurPx - blur_) < 0.05f &&
        std::fabs(backdropPx - backdropBlur_) < 0.05f) {
        return;
    }

    ComPtr<ID2D1Image> prevTarget;
    ctx->GetTarget(prevTarget.GetAddressOf());

    if (resized) {
        source_ = makeTarget(ctx, width, height);
        sharp_ = makeTarget(ctx, width, height);
        backdrop_ = makeTarget(ctx, width, height);
        w_ = width;
        h_ = height;
    }
    if (!source_ || !sharp_ || !backdrop_) return;

    const float w = static_cast<float>(width), h = static_cast<float>(height);

    // 1. the picture (or aurora) scaled to the window
    ctx->SetTarget(source_.Get());
    ctx->BeginDraw();
    ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    ctx->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 1.f));
    drawSource(ctx, w, h);
    ctx->EndDraw();

    // 2. user blur + dim
    ctx->SetTarget(sharp_.Get());
    ctx->BeginDraw();
    ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    if (blurPx > 0.4f) {
        drawBlurred(ctx, source_.Get(), blurPx);
    } else {
        ctx->DrawBitmap(source_.Get());
    }
    if (dim > 0.002f) {
        ComPtr<ID2D1SolidColorBrush> black;
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, std::clamp(dim, 0.f, 0.9f)), black.GetAddressOf());
        if (black) ctx->FillRectangle(D2D1::RectF(0, 0, w, h), black.Get());
    }
    ctx->EndDraw();

    // 3. what the glass sees: the same picture, heavily blurred
    ctx->SetTarget(backdrop_.Get());
    ctx->BeginDraw();
    ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    drawBlurred(ctx, sharp_.Get(), std::max(2.f, backdropPx));
    ctx->EndDraw();

    ctx->SetTarget(prevTarget.Get());
    dim_ = dim;
    blur_ = blurPx;
    backdropBlur_ = backdropPx;
    dirty_ = false;
}
