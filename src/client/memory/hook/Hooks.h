#pragma once
#include "Hook.h"
#include "client/manager/StaticManager.h"

#include "hooks/GeneralHooks.h"
#include "hooks/LevelRendererHooks.h"
#include "hooks/OptionHooks.h"
#include "hooks/DXHooks.h"
#include "hooks/MinecraftGameHooks.h"
#include "hooks/RenderControllerHooks.h"
#include "hooks/ScreenViewHooks.h"
#include "hooks/PacketHooks.h"
#include "hooks/PlayerHooks.h"
#include "hooks/CustomSkinPickerHooks.h"

class Ma4Hooks final : public StaticManager<HookGroup, GenericHooks, LevelRendererHooks, OptionHooks, DXHooks,
                                               PlayerHooks, MinecraftGameHooks, RenderControllerHooks, ScreenViewHooks,
                                               PacketHooks, CustomSkinPickerHooks> {
public:
    Ma4Hooks();
    ~Ma4Hooks();

    void enable();
    void disable();

private:
};
