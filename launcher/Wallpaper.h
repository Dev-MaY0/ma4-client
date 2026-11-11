#pragma once
// ma4 Launcher - wallpaper pipeline. Everything is rendered into window sized bitmaps so that drawing a frame is one
// bitmap draw and the glass can use a real blurred backdrop:
//   source  = picture scaled to cover the window (or the built-in aurora)
//   sharp   = source, optionally blurred, then dimmed            -> drawn behind the UI
//   backdrop = sharp, heavily blurred                              -> seen through the glass controls

#include "client/screen/ios/IosUi.h"

#include <string>

class Wallpaper {
public:
    /// Loads a picture file (any format WIC can decode). An empty path selects the built-in wallpaper.
    bool setPicture(ID2D1DeviceContext* ctx, IWICImagingFactory* wic, std::wstring const& path);
    [[nodiscard]] bool hasPicture() const { return image_ != nullptr; }

    /// Rebuilds the window sized bitmaps when the size, picture or look changed. Call outside BeginDraw / EndDraw.
    void update(ID2D1DeviceContext* ctx, int width, int height, float dim, float blurPx, float backdropPx);

    [[nodiscard]] ID2D1Bitmap1* sharp() const { return sharp_.Get(); }
    [[nodiscard]] ID2D1Bitmap1* backdrop() const { return backdrop_.Get(); }
    void invalidate() { dirty_ = true; }
    void release();

private:
    void drawSource(ID2D1DeviceContext* ctx, float w, float h);
    void drawAurora(ID2D1DeviceContext* ctx, float w, float h);

    ComPtr<ID2D1Bitmap1> image_;
    ComPtr<ID2D1Bitmap1> source_, sharp_, backdrop_;
    int w_ = 0, h_ = 0;
    float dim_ = -1.f, blur_ = -1.f, backdropBlur_ = -1.f;
    bool dirty_ = true;
};
