#pragma once
// Layout constants and small drawing helpers shared by IosShell.cpp and IosShellReplay.cpp.
#include "IosUi.h"

#include <algorithm>

namespace ios {
    namespace shell {
        constexpr float kNavH = 60.f;   // height reserved for the floating glass navigation buttons
        constexpr float kBtnD = 40.f;   // floating glass nav button diameter
        constexpr float kMargin = 22.f; // page side margin
        constexpr float kGroupR = 24.f; // grouped card corner radius (iOS 26: large continuous corners)
        constexpr float kRowH = 44.f;
        constexpr uint64_t kIdPush = 0x0A110002ull;

        /// Opaque page colour: what content fades into under floating controls (scroll edge effect).
        inline Col solidBase(Ui& ui) {
            return ui.pal.grouped;
        }

        /// Inset grouped card: continuous corners and a hairline edge.
        inline void drawCard(Ui& ui, Rect const& card) {
            ui.fillSmooth(card, ui.S(kGroupR), ui.pal.cell);
            ui.strokeSmooth(card, ui.S(kGroupR), ui.pal.cardBorder, std::max(1.f, ui.S(0.5f)));
        }
    } // namespace shell
    using namespace shell;
} // namespace ios
