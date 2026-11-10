#pragma once
// Game-side glue for the iOS UI toolkit: builds the Gfx / Palette / scale from the client renderer and settings.
#include "IosUi.h"

namespace ios {

    /// Direct2D / DirectWrite objects of the client renderer.
    Gfx gameGfx();
    /// Palette matching the "Appearance" and accent colour client settings.
    Palette gamePalette();
    /// Pixel scale of one iOS point for the current screen and the "Menu Scale" setting.
    float gameScale();
    /// Starts a frame on the shared Ui instance using the client renderer.
    Ui& beginGameFrame(Input const& input = Input());

} // namespace ios
