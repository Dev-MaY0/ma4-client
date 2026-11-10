#include "pch.h"
#include "FPSCounter.h"
#include "client/Ma4.h"

FPSCounter::FPSCounter()
    : TextModule("FPS", LocalizeString::get("client.textmodule.fpsCounter.name"),
                 LocalizeString::get("client.textmodule.fpsCounter.desc"), HUD) {
    this->prefix = TextValue(L"FPS: ");
}

std::wstringstream FPSCounter::text(bool isDefault, bool inEditor) {
    std::wstringstream wss;
    wss << Ma4::get().getTimings().getFPS();
    return wss;
}
