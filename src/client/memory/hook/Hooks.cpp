#include "pch.h"
#include "Hooks.h"

#include "hooks/GeneralHooks.h"
#include "hooks/LevelRendererHooks.h"
#include "hooks/OptionHooks.h"
#include "hooks/DXHooks.h"
#include "hooks/MinecraftGameHooks.h"
#include "hooks/RenderControllerHooks.h"
#include "hooks/ScreenViewHooks.h"
#include "hooks/PacketHooks.h"
#include "MinHook.h"
#include <vhook/vtable_hook.h>

using namespace std::chrono_literals;

Ma4Hooks::Ma4Hooks() {
}

Ma4Hooks::~Ma4Hooks() {
    MH_Uninitialize();
}

void Ma4Hooks::enable() {
    MH_EnableHook(MH_ALL_HOOKS);
}

void Ma4Hooks::disable() {
    MH_DisableHook(MH_ALL_HOOKS);
    vh::unhook_all();
}
