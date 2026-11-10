#include "pch.h"
// Ma4Recode.cpp : Defines the entry point for the application.
//
#include <cstdint>

#include "Ma4.h"
#include "BuildTimestamp.h"
#include "localization/LocalizeString.h"

#include "client/screen/TextBox.h"

#include "feature/module/ModuleManager.h"
#include "feature/command/CommandManager.h"
#include "script/PluginManager.h"

#include "config/ConfigManager.h"
#include "misc/ClientMessageQueue.h"
#include "misc/ServerDetection.h"
#include "misc/TempStorage.h"
#include "input/Keyboard.h"
#include "memory/hook/Hooks.h"
#include "event/Eventing.h"
#include "event/events/KeyUpdateEvent.h"
#include "event/events/RendererInitEvent.h"
#include "event/events/RendererCleanupEvent.h"
#include "event/events/FocusLostEvent.h"
#include "event/events/AppSuspendedEvent.h"
#include "event/events/UpdateEvent.h"
#include "event/events/CharEvent.h"
#include "event/events/ClickEvent.h"
#include "event/events/BobMovementEvent.h"
#include "event/events/LeaveGameEvent.h"

#include "mc/Addresses.h"

#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/client/game/FontRepository.h"
#include "util/HttpClient.h"

#include <mc/common/client/gui/ScreenView.h>
#include <mc/common/client/gui/controls/VisualTree.h>
#include <mc/common/client/gui/controls/UIControl.h>

#include "event/events/MouseReleaseEvent.h"
#include "mc/common/client/game/GameCore.h"

#include "render/Renderer.h"
#include "screen/ScreenManager.h"
#include "render/asset/Assets.h"
#include "resource/Resource.h"
#include "feature/module/modules/game/Freelook.h"
#include "feature/module/modules/misc/DiscordPresence.h"
#include "util/GameAllocator.h"

using namespace std;

namespace {
    alignas(Eventing) char eventing[sizeof(Eventing)] = {};
    alignas(Ma4) char ma4Buf[sizeof(Ma4)] = {};
    alignas(Renderer) char rendererBuf[sizeof(Renderer)] = {};
    alignas(ModuleManager) char mmgrBuf[sizeof(ModuleManager)] = {};
    alignas(ClientMessageQueue) char messageSinkBuf[sizeof(ClientMessageQueue)] = {};
    alignas(CommandManager) char commandMgrBuf[sizeof(CommandManager)] = {};
    alignas(ConfigManager) char configMgrBuf[sizeof(ConfigManager)] = {};
    alignas(SettingGroup) char mainSettingGroup[sizeof(SettingGroup)] = {};
    alignas(Ma4Hooks) char hooks[sizeof(Ma4Hooks)] = {};
    alignas(ScreenManager) char scnMgrBuf[sizeof(ScreenManager)] = {};
    alignas(Assets) char assetsBuf[sizeof(Assets)] = {};
    alignas(PluginManager) char scriptMgrBuf[sizeof(PluginManager)] = {};
    alignas(Keyboard) char keyboardBuf[sizeof(Keyboard)] = {};
    alignas(Notifications) char notificaitonsBuf[sizeof(Notifications)] = {};

    bool hasInjected = false;

    struct DllMainCall {
        HINSTANCE hinstDLL;
        DWORD fdwReason;
        LPVOID reserved;
    };

    DWORD WINAPI ejectThread(LPVOID module) {
        std::this_thread::sleep_for(250ms);
        FreeLibraryAndExitThread(static_cast<HMODULE>(module), 0);
    }

    void centerCursorInGameWindow() {
        auto* gameCore = SDK::GameCore::get();
        if (!gameCore || !gameCore->hwnd) return;

        RECT clientRect {};
        if (!GetClientRect(gameCore->hwnd, &clientRect)) return;

        POINT center {
            (clientRect.left + clientRect.right) / 2,
            (clientRect.top + clientRect.bottom) / 2,
        };
        if (!ClientToScreen(gameCore->hwnd, &center)) return;

        SetCursorPos(center.x, center.y);
    }

}

#define MVSIG(...) \
    ([]() -> std::pair<SigImpl*, SigImpl*> {\
/*if (SDK::internalVers == SDK::VLATEST) */return {&Signatures::__VA_ARGS__, &Signatures::__VA_ARGS__}; }\
/*if (SDK::internalVers == SDK::V1_20_40) { return {&Signatures_1_20_40::__VA_ARGS__, &Signatures::__VA_ARGS__}; }*/ \
/*if (SDK::internalVers == SDK::V1_20_30) { return {&Signatures_1_20_30::__VA_ARGS__, &Signatures::__VA_ARGS__}; }*/ \
/*if (SDK::internalVers == SDK::V1_19_51) { return {&Signatures_1_19_51::__VA_ARGS__, &Signatures::__VA_ARGS__}; }*/ \
/*return {&Signatures_1_18_12::__VA_ARGS__, &Signatures::__VA_ARGS__}; }*/\
)()

#define MA4_EXPORT extern "C" __declspec(dllexport)

MA4_EXPORT const char* Ma4GetDllVersion() noexcept {
    return Ma4::version.data();
}

MA4_EXPORT uint32_t Ma4GetSupportedMinecraftVersionCount() noexcept {
    return Ma4::supportedMinecraftVersions.size();
}

MA4_EXPORT const char* Ma4GetSupportedMinecraftVersion(uint32_t index) noexcept {
    if (index >= Ma4::supportedMinecraftVersions.size()) {
        return nullptr;
    }

    return Ma4::supportedMinecraftVersions[index].data();
}

DWORD __stdcall startThreadImpl(HINSTANCE dll) {
    BEGIN_ERROR_HANDLER
    // Needed for Logger
    new (messageSinkBuf) ClientMessageQueue;
    new (eventing) Eventing();
    new (ma4Buf) Ma4;
    new (notificaitonsBuf) Notifications;

    std::filesystem::create_directory(util::GetMa4Path());
    std::filesystem::create_directory(util::GetMa4Path() / "Assets");
    Ma4Temp::cleanup();
    Logger::Setup();

#ifdef MA4_CRASH_REPORTING
    DebugExceptionHandler::Install();
#endif

#if defined(MA4_NIGHTLY)
    Logger::Info("ma4 Client [NIGHTLY] {}", Ma4::version);
#elif defined(MA4_DEBUG)
    Logger::Info("ma4 Client [DEBUG] {}", Ma4::version);
#else
    Logger::Info("ma4 Client {}", Ma4::version);
#endif

    char path[MAX_PATH] {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);

    DWORD handle;
    DWORD size = GetFileVersionInfoSizeA(path, &handle);

    if (size == 0) {
        Logger::Fatal("Failed to get file version size");
    }

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoA(path, handle, size, data.data())) {
        Logger::Fatal("Failed to get file version");
    }

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT len = 0;

    if (VerQueryValueA(data.data(), "\\", reinterpret_cast<LPVOID*>(&fileInfo), &len)) {
        const auto major = HIWORD(fileInfo->dwFileVersionMS);
        const auto minor = LOWORD(fileInfo->dwFileVersionMS);
        const auto build = HIWORD(fileInfo->dwFileVersionLS);

        Ma4::get().gameVersion = std::format("{}.{}.{}", major, minor, build);
    }

    Logger::Info("Minecraft {}", Ma4::get().gameVersion);
    Logger::Info("Using {} memory allocator", GameAllocator::backendName());
    if (util::IsRunningUnderWine()) Logger::Info("Running under Wine");

    Logger::Info("Loading assets");
    Ma4::get().dllInst = dll;
    // ... init assets
    Ma4::get().initL10n();

    Logger::Info("Resolving signatures..");

    int sigCount = 0;
    int deadCount = 0;

    if (Ma4::supportsMinecraftVersion(Ma4::get().gameVersion)) {
        // not needed as it will always just be latest
        // SDK::internalVers = vers;
    } else {
        std::stringstream ss;
        ss << "ma4 Client does not support your version: " << Ma4::get().gameVersion
           << ". ma4 only supports the following versions:\n\n";

        for (const auto key : Ma4::supportedMinecraftVersions) {
            ss << key << "\n";
        }

        Logger::Warn(ss.str());
    }

    std::vector<std::pair<SigImpl*, SigImpl*>> sigList = {
        MVSIG(MainWindow__windowProcCallback),
        MVSIG(LevelRenderer_renderLevel),
        MVSIG(Options_getGamma),
        MVSIG(Options_getPerspective),
        MVSIG(Options_setPerspective),
        MVSIG(Options_getHideHand),
        MVSIG(ClientInstance_grabCursor),
        MVSIG(ClientInstance_releaseCursor),
        MVSIG(MultiPlayerLevel__subTick),
        MVSIG(ChatScreenController_sendChatMessage),
        MVSIG(Mouse_processInputQueue),
        MVSIG(MinecraftGame_onDeviceLost),
        MVSIG(RenderController_getOverlayColor),
        MVSIG(ScreenView_setupAndRender),
        MVSIG(MinecraftGame__update),
        MVSIG(AppPlatform_GameCorePC_pickImage),
        MVSIG(GpuInfo),
        MVSIG(RakPeer_GetAveragePing),
        MVSIG(ClientInputUpdateSystemInternal_tickUpdateClientInput),
        MVSIG(LocalPlayer_applyTurnDelta),
        MVSIG(ItemStackBase_getHoverName),
        MVSIG(I18n_getI18n),
        MVSIG(ItemStackVtable),
        MVSIG(ItemStackBase_destructor),
        MVSIG(Vtable::Level),
        MVSIG(Tessellator_begin),
        MVSIG(Tessellator_vertex),
        MVSIG(MeshHelpers_renderMeshImmediately),
        MVSIG(BaseActorRenderContext_BaseActorRenderContext),
        MVSIG(ItemRenderer_renderGuiItemNew),
        MVSIG(ActorRenderDispatcher_render),
        MVSIG(MolangVariable__findOrAddVariableIndex),
        MVSIG(MolangVariableMap__getOrAddMolangVariable),
        MVSIG(LevelRendererPlayer_renderOutlineSelection),
        MVSIG(Dimension_getSkyColor),
        MVSIG(Dimension_getTimeOfDay),
        MVSIG(Dimension_tick),
        MVSIG(Misc::thirdPersonNametag),
        MVSIG(ItemStackBase_getDamageValue),
        MVSIG(MinecraftPackets_createPacket),
        MVSIG(Actor_attack),
        MVSIG(Actor_getNameTag),
        MVSIG(Actor_setNameTag),
        MVSIG(GuiMessageVector_emplaceBack),
        MVSIG(_updatePlayer),
        MVSIG(GameArguments__onUri),
        MVSIG(RenderMaterialGroup__common),
        MVSIG(GuiData_displayClientMessage),
        MVSIG(ClientInstanceScreenModel_forwardSoundSubtitle),
        MVSIG(BaseActorRenderer_renderText),
        MVSIG(AppPlatformGDK_releaseMouse),
        MVSIG(Misc::Platform_GameCore),
        MVSIG(Misc::mouseDevice),
    };

    new (configMgrBuf) ConfigManager();
    if (!Ma4::getConfigManager().loadMaster()) {
        Logger::Fatal("Could not load master config!");
    } else {
        Logger::Info("Loaded master config");
    }
    new (mainSettingGroup) SettingGroup("global");

    // The Language setting is a special case because we need it to apply names to other global settings.
    Ma4::get().initLanguageSetting();
    Ma4::getConfigManager().applyLanguageConfig("language");

    Ma4::get().initSettings();
    Ma4::getConfigManager().applyGlobalConfig();

    new (mmgrBuf) ModuleManager;
    new (commandMgrBuf) CommandManager;
    new (scnMgrBuf) ScreenManager(); // needs to be initialized before renderer

    new (scriptMgrBuf) PluginManager();
    new (rendererBuf) Renderer();
    new (assetsBuf) Assets();

    for (auto& entry : sigList) {
        if (!entry.first->mod) continue;
        auto res = entry.first->resolve();
        if (!res) {
#if MA4_DEBUG
            Logger::Warn("Signature {} failed to resolve!", entry.first->name);
#endif
            deadCount++;
        } else {
            entry.second->result = entry.first->result;
            entry.second->scan_result = entry.first->scan_result;
            sigCount++;
        }
    }
#if MA4_DEBUG
    Logger::Info("Resolved {} signatures ({} dead)", sigCount, deadCount);
#endif

    MH_Initialize();
    new (hooks) Ma4Hooks();

    new (keyboardBuf) Keyboard();

    Logger::Info("Waiting for game to load..");

    while (!SDK::ClientInstance::get()) {
        std::this_thread::sleep_for(10ms);
    }

    Ma4::get().initialize(dll);

    Logger::Info("Initialized ma4 Client");
    return 0ul;
    END_ERROR_HANDLER
}

DWORD __stdcall startThread(LPVOID context) {
#ifdef MA4_CRASH_REPORTING
    return static_cast<DWORD>(DebugExceptionHandler::RunWithSehGuard(
        [](void* param) -> std::uintptr_t {
            return startThreadImpl(static_cast<HINSTANCE>(param));
        },
        context, "Caught SEH exception in ma4 startup thread"));
#else
    return startThreadImpl(static_cast<HINSTANCE>(context));
#endif
}

BOOL WINAPI DllMainImpl(HINSTANCE hinstDLL, // handle to DLL module
                        DWORD fdwReason,    // reason for calling function
                        LPVOID reserved)    // reserved
{
    BEGIN_ERROR_HANDLER
    if (GetModuleHandleA("Minecraft.Windows.exe") != GetModuleHandleA(NULL)) return TRUE;

    if (fdwReason == DLL_PROCESS_ATTACH) {
        hasInjected = true;

        DisableThreadLibraryCalls(hinstDLL);
        CloseHandle(CreateThread(nullptr, 0, startThread, hinstDLL, 0, nullptr));
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (reserved != nullptr) {
            hasInjected = false;
            return TRUE;
        }

        // Remove singletons

        Ma4::getHooks().disable();

        // Wait for all running hooks accross different threads to stop executing
        std::this_thread::sleep_for(200ms);

        if (!Ma4::get().isEjectReadyForRenderThread()) {
            Ma4::getConfigManager().saveCurrentConfig();
        }

        Ma4::getKeyboard().~Keyboard();
        Ma4::getModuleManager().~ModuleManager();
        Ma4::getClientMessageQueue().~ClientMessageQueue();
        Ma4::getCommandManager().~CommandManager();
        Ma4::getSettings().~SettingGroup();
        Ma4::getHooks().~Ma4Hooks();
        Ma4::getEventing().~Eventing();
        Ma4::getRenderer().~Renderer();
        Ma4::getAssets().~Assets();
        Ma4::getScreenManager().~ScreenManager();
        Ma4::getPluginManager().~PluginManager();
        Ma4::getNotifications().~Notifications();
        Ma4::get().~Ma4();
        Ma4Temp::cleanup();

        MH_Uninitialize();

        hasInjected = false;
        Logger::Info("ma4 Client detached.");

#ifdef MA4_CRASH_REPORTING
        DebugExceptionHandler::Uninstall();
#endif
    }
    return TRUE; // Successful DLL_PROCESS_ATTACH.
    END_ERROR_HANDLER
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, // handle to DLL module
                    DWORD fdwReason,    // reason for calling function
                    LPVOID reserved)    // reserved
{
#ifdef MA4_CRASH_REPORTING
    DllMainCall call { hinstDLL, fdwReason, reserved };
    return static_cast<BOOL>(DebugExceptionHandler::RunWithSehGuard(
        [](void* context) -> std::uintptr_t {
            auto* call = static_cast<DllMainCall*>(context);
            return DllMainImpl(call->hinstDLL, call->fdwReason, call->reserved);
        },
        &call, "Caught SEH exception in ma4 DllMain"));
#else
    return DllMainImpl(hinstDLL, fdwReason, reserved);
#endif
}

Ma4& Ma4::get() noexcept {
    return *std::launder(reinterpret_cast<Ma4*>(ma4Buf));
}

ModuleManager& Ma4::getModuleManager() noexcept {
    return *std::launder(reinterpret_cast<ModuleManager*>(mmgrBuf));
}

CommandManager& Ma4::getCommandManager() noexcept {
    return *std::launder(reinterpret_cast<CommandManager*>(commandMgrBuf));
}

ConfigManager& Ma4::getConfigManager() noexcept {
    return *std::launder(reinterpret_cast<ConfigManager*>(configMgrBuf));
}

ClientMessageQueue& Ma4::getClientMessageQueue() noexcept {
    return *std::launder(reinterpret_cast<ClientMessageQueue*>(messageSinkBuf));
}

SettingGroup& Ma4::getSettings() noexcept {
    return *std::launder(reinterpret_cast<SettingGroup*>(mainSettingGroup));
}

Ma4Hooks& Ma4::getHooks() noexcept {
    return *std::launder(reinterpret_cast<Ma4Hooks*>(hooks));
}

Eventing& Ma4::getEventing() noexcept {
    return *std::launder(reinterpret_cast<Eventing*>(eventing));
}

Renderer& Ma4::getRenderer() noexcept {
    return *std::launder(reinterpret_cast<Renderer*>(rendererBuf));
}

ScreenManager& Ma4::getScreenManager() noexcept {
    return *std::launder(reinterpret_cast<ScreenManager*>(scnMgrBuf));
}

Assets& Ma4::getAssets() noexcept {
    return *std::launder(reinterpret_cast<Assets*>(assetsBuf));
}

PluginManager& Ma4::getPluginManager() noexcept {
    return *std::launder(reinterpret_cast<PluginManager*>(scriptMgrBuf));
}

Keyboard& Ma4::getKeyboard() noexcept {
    return *std::launder(reinterpret_cast<Keyboard*>(keyboardBuf));
}

Notifications& Ma4::getNotifications() noexcept {
    return *std::launder(reinterpret_cast<Notifications*>(notificaitonsBuf));
}

std::optional<float> Ma4::getMenuBlur() {
    if (std::get<BoolValue>(this->menuBlurEnabled)) {
        return std::get<FloatValue>(this->menuBlur);
    }
    return std::nullopt;
}

std::vector<std::string> Ma4::getMa4Users() {
    return ma4Users;
}

int Ma4::getSelectedLanguage() {
    if (!l10nData) return 0;
    return l10nData->resolveLanguageSetting(clientLanguage.getSelectedKey());
}

void Ma4::queueEject() noexcept {
    SetWindowTextW(SDK::GameCore::get()->hwnd, L"Minecraft");
    if (this->shouldEject.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
}

bool Ma4::isEjectQueued() const noexcept {
    return this->shouldEject.load(std::memory_order_acquire);
}

bool Ma4::isEjectReadyForRenderThread() const noexcept {
    return this->shouldEject.load(std::memory_order_acquire) &&
           this->mainThreadEjectCleanupComplete.load(std::memory_order_acquire);
}

void Ma4::completeEjectFromRenderThread() noexcept {
    if (!this->isEjectReadyForRenderThread()) {
        return;
    }

    if (Ma4::getRenderer().hasInitialized()) {
        this->releaseDeferredD2DResources();
        Ma4::getAssets().unloadAll();
        Ma4::getRenderer().shutdownForEject();
    }

    Ma4::getHooks().disable();

    if (!this->unloadStarted.exchange(true, std::memory_order_acq_rel)) {
        CloseHandle(CreateThread(nullptr, 0, ejectThread, dllInst, 0, nullptr));
    }
}

SDK::Font* Ma4::getFont() {
    switch (this->mcRendFont.getSelectedKey()) {
    case 0:
        return SDK::ClientInstance::get()->minecraftGame->getFontRepository()->getMinecraftFont();
    case 1:
        return SDK::ClientInstance::get()->minecraftGame->getFontRepository()->getSmoothFont();
    default:
        Logger::Fatal("Unknown font selected: {}", this->mcRendFont.getSelectedKey());
        throw std::runtime_error("Unknown font");
    }
}

void Ma4::initialize(HINSTANCE hInst) {
    this->dllInst = hInst;

    if (!controllerInput.start()) {
        Logger::Warn("Controller keybind input could not be initialized.");
    }

    Ma4::getPluginManager().init();
    Logger::Info("Script manager initialized.");

    Ma4::getEventing().listen<UpdateEvent, &Ma4::onUpdate>(this, 2);
    Ma4::getEventing().listen<KeyUpdateEvent, &Ma4::onKey>(this, 2);
    Ma4::getEventing().listen<RendererInitEvent, &Ma4::onRendererInit>(this, 2);
    Ma4::getEventing().listen<RendererCleanupEvent, &Ma4::onRendererCleanup>(this, 2);
    Ma4::getEventing().listen<AppSuspendedEvent, &Ma4::onSuspended>(this, 2);
    Ma4::getEventing().listen<CharEvent, &Ma4::onChar>(this, 2);
    Ma4::getEventing().listen<ClickEvent, &Ma4::onClick>(this, 2);
    Ma4::getEventing().listen<BobMovementEvent, &Ma4::onBobView>(this, 2);
    Ma4::getEventing().listen<LeaveGameEvent, &Ma4::onLeaveGame>(this, 2);
    Ma4::getEventing().listen<RenderLayerEvent, &Ma4::onRenderLayer>(this, 2);
    Ma4::getEventing().listen<RenderOverlayEvent, &Ma4::onRenderOverlay>(this, 2);
    Ma4::getEventing().listen<TickEvent, &Ma4::onTick>(this, 2);
    Ma4::getEventing().listen<MouseReleaseEvent, &Ma4::onMouseRelease>(this, 2);

    Logger::Info("Initialized Hooks");
    getHooks().enable();
    Logger::Info("Enabled Hooks");

    // doesn't work, maybe it's stored somewhere else too
    // if (SDK::internalVers < SDK::V1_20) {
    //    patchKey();
    //}
}

void Ma4::threadsafeInit() {
    this->gameThreadId = std::this_thread::get_id();
    // TODO: ma4 beta only
    // if (SDK::ClientInstance::get()->minecraftGame->xuid.size() > 0) wnd->postXUID();

    std::string vstr(this->version);

#if defined(MA4_NIGHTLY)
    auto ws = util::StrToWStr("ma4 Client [NIGHTLY] " + gameVersion + " " + vstr + "/" + getBuildTimestamp());
#elif defined(MA4_DEBUG)
    auto ws = util::StrToWStr("ma4 Client [DEBUG] " + gameVersion + " " + vstr + "/" + getBuildTimestamp());
#else
    auto ws = util::StrToWStr("ma4 Client " + vstr);
#endif

    SetWindowTextW(SDK::GameCore::get()->hwnd, ws.c_str());
    Ma4::getPluginManager().loadPrerunScripts();
    Logger::Info("Loaded startup scripts");

    Ma4::getConfigManager().applyModuleConfig();
    if (auto presence = Ma4::getModuleManager().find("DiscordPresence")) {
        std::static_pointer_cast<DiscordPresence>(presence)->applyFirstRunDefault();
    }

    Ma4::getRenderer().setShouldInit();

    Ma4::getCommandManager().prefix = Ma4::get().getCommandPrefix();
    Ma4::getNotifications().push(LocalizeString::get("client.intro.welcome"));
    Ma4::getNotifications().push(
        util::FormatWString(LocalizeString::get("client.intro.menubutton"),
                            { util::StrToWStr(util::KeyToString(Ma4::get().getMenuKey().value)) }));
}

static void setModuleBlocked(std::string_view moduleName, bool shouldBlock) {
    std::vector<std::wstring> blockedList;
    Ma4::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
        if (mod->name() == moduleName && mod->isBlocked() != shouldBlock) {
            if (shouldBlock) {
                blockedList.push_back(mod->getDisplayName());
            }
            mod->setBlocked(shouldBlock);
        }
    });

    if (!blockedList.empty()) {
        std::wstring str;
        for (size_t i = 0; i < blockedList.size(); i++) {
            str += blockedList[i];
            if (i != blockedList.size() - 1) {
                str += L", ";
            }
        }
        Ma4::getNotifications().push(
            util::FormatWString(LocalizeString::get("client.modules.blockedOnServer.name"), { str }));
    }
}

void Ma4::updateModuleBlocking() {
    auto* connectionInfo = SDK::RemoteConnectorComposite::getConnectionInfo();
    const auto* server = ServerDetection::identify(connectionInfo);
    const bool isHiveOrGalaxite =
        server && (server->id == ServerDetection::ServerId::Hive || server->id == ServerDetection::ServerId::Galaxite);
    const bool isCubeCraft = server && server->id == ServerDetection::ServerId::CubeCraft;

    static_assert(std::is_base_of_v<Module, Freelook>);

    setModuleBlocked("Freelook", isHiveOrGalaxite);
    setModuleBlocked("Gyro", isCubeCraft);
}

std::string Ma4::getBuildTimestamp() {
#if defined(MA4_BUILD_TIMESTAMP)
    return MA4_BUILD_TIMESTAMP;
#else
    return Ma4Build::getTimestamp();
#endif
}

std::wstring Ma4::GetCurrentModuleFilePath(HMODULE hModule) {
    std::vector<wchar_t> buffer(MAX_PATH);

    DWORD result = GetModuleFileNameW(hModule, buffer.data(), static_cast<DWORD>(buffer.size()));

    if (result > 0 && result < buffer.size()) {
        return std::wstring(buffer.data());
    } else if (result >= buffer.size()) {
        buffer.resize(result + 1);
        result = GetModuleFileNameW(hModule, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (result > 0 && result < buffer.size()) {
            return std::wstring(buffer.data());
        }
    }

    return std::wstring(L"couldn't get file path");
}

void Ma4::initSettings() {
    {
        auto set = std::make_shared<Setting>("menuKey", LocalizeString::get("client.settings.menuKey.name"),
                                             LocalizeString::get("client.settings.menuKey.desc"));
        set->value = &this->menuKey;
        set->callback = [this](Setting& set) {
            Ma4::getScreenManager().get<HUDEditor>().key = this->getMenuKey();
        };
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("ejectKey", LocalizeString::get("client.settings.ejectKey.name"),
                                             LocalizeString::get("client.settings.ejectKey.desc"));
        set->value = &this->ejectKey;
        this->getSettings().addSetting(set);
    }
    {
        auto set =
            std::make_shared<Setting>("menuBlurEnabled", LocalizeString::get("client.settings.menuBlurEnabled.name"),
                                      LocalizeString::get("client.settings.menuBlurEnabled.desc"));
        set->value = &this->menuBlurEnabled;
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("useDX11", LocalizeString::get("client.settings.useDX11.name"),
                                             LocalizeString::get("client.settings.useDX11.desc"));
        set->value = &this->useDX11;
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("forceDisableVSync",
                                             LocalizeString::get("client.settings.forceDisableVSync.name"),
                                             LocalizeString::get("client.settings.forceDisableVSync.desc"));
        set->value = &this->forceDisableVSync;
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("commandPrefix", LocalizeString::get("client.settings.commandPrefix.name"),
                                             LocalizeString::get("client.settings.commandPrefix.desc"));
        set->value = &this->commandPrefix;
        set->visible = false;
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("menuIntensity", LocalizeString::get("client.settings.menuIntensity.name"),
                                             LocalizeString::get("client.settings.menuIntensity.desc"));
        set->value = &this->menuBlur;
        set->min = FloatValue(1.f);
        set->max = FloatValue(30.f);
        set->interval = FloatValue(1.f);
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("accentColor", LocalizeString::get("client.settings.accentColor.name"),
                                             LocalizeString::get("client.settings.accentColor.desc"));
        set->value = &this->accentColor;
        this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>("appearance", LocalizeString::get("client.settings.appearance.name"),
                                             LocalizeString::get("client.settings.appearance.desc"));
        set->enumData = &this->appearanceData;
        set->value = set->enumData->getValue();
        set->enumData->addEntry({ 0, LocalizeString::get("client.settings.appearance.dark.name") });
        set->enumData->addEntry({ 1, LocalizeString::get("client.settings.appearance.light.name") });
        this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>("uiScale", LocalizeString::get("client.settings.uiScale.name"),
                                             LocalizeString::get("client.settings.uiScale.desc"));
        set->value = &this->uiScale;
        set->min = FloatValue(0.6f);
        set->max = FloatValue(1.6f);
        set->interval = FloatValue(0.05f);
        this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>("minViewBob", L"Minimal View Bob (UNSTABLE)",
                                             L"Only bob the item in hand, not the camera");
        set->value = &this->minimalViewBob;
        this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>("minecraftRenderer",
                                             LocalizeString::get("client.settings.minecraftRenderer.name"),
                                             LocalizeString::get("client.settings.minecraftRenderer.desc"));
        set->value = &this->minecraftRenderer;
        this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>("textShadow", LocalizeString::get("client.settings.textShadow.name"),
                                             LocalizeString::get("client.settings.textShadow.desc"));
        set->value = &this->textShadow;
        this->getSettings().addSetting(set);
    }

#ifdef MA4_DEBUG
    {
        auto set = std::make_shared<Setting>("debugTextRects", L"Debug Text Rects",
                                             L"Draw text bounds and highlight likely text overflow.");
        set->value = &this->debugTextRects;
        this->getSettings().addSetting(set);
    }
#endif

    {
        auto set = std::make_shared<Setting>("secondaryFont", LocalizeString::get("client.settings.secondaryFont.name"),
                                             LocalizeString::get("client.settings.secondaryFont.desc"));
        set->value = &this->secondaryFont;
        this->getSettings().addSetting(set);
    }

    {
        auto set =
            std::make_shared<Setting>("mcRendererFont", LocalizeString::get("client.settings.mcRendererFont.name"),
                                      LocalizeString::get("client.settings.mcRendererFont.desc"));
        set->enumData = &this->mcRendFont;
        set->value = set->enumData->getValue();
        set->enumData->addEntry({ 0, LocalizeString::get("client.settings.mcRendererFont.default.name"),
                                  LocalizeString::get("client.settings.mcRendererFont.default.desc") });
        set->enumData->addEntry({ 1, LocalizeString::get("client.settings.mcRendererFont.notoSans.name"),
                                  LocalizeString::get("client.settings.mcRendererFont.notoSans.desc") });
        this->getSettings().addSetting(set);
    }

    {
        // auto set = std::make_shared<Setting>("broadcastClientUsage", "ma4 Client Presence", "If you leave this on,
        // others with Ma4 will see that you are using Ma4 and you will see other people who use Ma4.");
        // set->value = &this->broadcastUsage;
        // this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>(""
                                             "centerCursor",
                                             LocalizeString::get("client.settings.centerCursor.name"),
                                             LocalizeString::get("client.settings.centerCursor.desc"));
        set->value = &this->centerCursorMenus;
        this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>("snapLines", LocalizeString::get("client.settings.snapLines.name"),
                                             LocalizeString::get("client.settings.snapLines.desc"));
        set->value = &this->snapLines;
        this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>("rgbSpeed", LocalizeString::get("client.settings.rgbSpeed.name"),
                                             LocalizeString::get("client.settings.rgbSpeed.desc"));
        set->value = &this->rgbSpeed;
        set->min = FloatValue(0.f);
        set->max = FloatValue(3.f);
        set->interval = FloatValue(0.1f);
        this->getSettings().addSetting(set);
    }
}

void Ma4::queueForUIRender(std::function<void(SDK::MinecraftUIRenderContext* ctx)> callback) {
    if (isEjectQueued()) return;
    this->uiRenderQueue.push(callback);
}

void Ma4::queueForClientThread(std::function<void()> callback) {
    if (isEjectQueued()) return;
    this->clientThreadQueue.push(callback);
}

void Ma4::queueForDXRender(std::function<void(ID2D1DeviceContext* ctx)> callback) {
    if (isEjectQueued()) return;
    this->dxRenderQueue.push(callback);
}

void Ma4::deferD2DResourceRelease(IUnknown* resource) noexcept {
    if (!resource) {
        return;
    }

    std::lock_guard lock(this->deferredD2DReleaseMutex);
    this->deferredD2DReleases.push_back(resource);
}

void Ma4::releaseDeferredD2DResources() noexcept {
    std::vector<IUnknown*> resources;
    {
        std::lock_guard lock(this->deferredD2DReleaseMutex);
        resources.swap(this->deferredD2DReleases);
    }

    for (auto* resource : resources) {
        resource->Release();
    }
}

void Ma4::initL10n() {
    l10nData = LocalizeData();
}

namespace {
    constexpr auto chakraCoreUrl = L"https://raw.githubusercontent.com/Imrglop/Latite-Releases/main/bin/ChakraCore.dll";
}

void Ma4::downloadChakraCore() {
    if (downloadingAssets) return;
    downloadingAssets = true;

    auto download = http::request(L"GET", chakraCoreUrl);
    if (!download || download->status != 200) {
        Logger::Warn("Could not download ChakraCore: {}",
                     download ? std::format("HTTP {}", download->status) : download.error());
        downloadingAssets = false;
        return;
    }

    auto assetPath = util::GetMa4Path() / "Assets" / "ChakraCore.dll";
    std::filesystem::create_directories(assetPath.parent_path());
    std::ofstream output(assetPath, std::ios::binary);
    output.write(reinterpret_cast<char const*>(download->body.data()), download->body.size());
    if (!output) Logger::Warn("Could not write ChakraCore.dll to {}", assetPath.string());

    downloadingAssets = false;
}

void Ma4::initLanguageSetting() {
    auto set = std::make_shared<Setting>("language", LocalizeString::get("client.settings.language.name"),
                                         LocalizeString::get("client.settings.language.desc"));
    set->enumData = &this->clientLanguage;
    set->value = set->enumData->getValue();
    set->userUpdateCallback = [](Setting&) {
        Ma4::get().onLanguageChanged();
    };

    set->enumData->addEntry({ LocalizeData::systemDefaultLanguageSettingValue,
                              LocalizeString::get("client.settings.language.systemDefault.name") });

    for (int i = 0; auto& lang : l10nData->getLanguages()) {
        set->enumData->addEntry({ i + 1, util::StrToWStr(lang->name) });
        i++;
    }
    this->getSettings().addSetting(set);
}

void Ma4::onLanguageChanged() {
    Ma4::getRenderer().refreshTextFormats();
    Ma4::getSettings().refreshLocalization();

    Ma4::getModuleManager().forEach([](std::shared_ptr<Module> mod) {
        mod->refreshLocalization();
    });

    Ma4::getCommandManager().refreshLocalization();
    Ma4::getScreenManager().get<ClickGUI>().requestModuleListRebuild();
    Ma4::getScreenManager().get<ClickGUI>().refreshLocalization();
}

void Ma4::onUpdate(Event& evGeneric) {
    auto& ev = reinterpret_cast<UpdateEvent&>(evGeneric);
    timings.update();
    auto now = std::chrono::system_clock::now();
    static auto lastSend = now;

    if (this->shouldEject.load(std::memory_order_acquire)) {
        if (!this->mainThreadEjectCleanupComplete.load(std::memory_order_acquire)) {
            Ma4::getScreenManager().shutdownForEject();
            Ma4::getConfigManager().saveCurrentConfig();
            Ma4::getModuleManager().shutdownForEject();
            Ma4::getPluginManager().unloadAll();
            this->controllerInput.stop();
            this->mainThreadEjectCleanupComplete.store(true, std::memory_order_release);
        }

        if (!Ma4::getRenderer().hasInitialized()) {
            this->completeEjectFromRenderThread();
        }
        return;
    }

    while (!this->clientThreadQueue.empty()) {
        auto& latest = this->clientThreadQueue.front();
        latest();
        this->clientThreadQueue.pop();
    }

    auto* connectionInfo = SDK::RemoteConnectorComposite::getConnectionInfo();

    if (!connectionInfo || (connectionInfo->unresolvedUrl.empty() && connectionInfo->hostIpAddress.empty() &&
                            connectionInfo->thirdPartyServerInfo.creatorName.empty())) {
        // updateModuleBlocking();
        getModuleManager().forEach([](std::shared_ptr<Module> mod) {
            mod->setBlocked(false);
        });
    }

    if (std::get<BoolValue>(centerCursorMenus) && SDK::ClientInstance::get()->minecraftGame->isCursorGrabbed()) {
        centerCursorInGameWindow();
    }

    ma4Users = ma4UsersDirty;

    if (!hasInit) {
        threadsafeInit();
        hasInit = true;
    }
    controllerInput.update();
    getKeyboard().findTextInput();
    Ma4::getPluginManager().runScriptingOperations();

    static bool lastDX11 = std::get<BoolValue>(this->useDX11);
    if (std::get<BoolValue>(useDX11) != lastDX11) {
        if (lastDX11) {
            Ma4::getClientMessageQueue().display(
                util::WFormat(LocalizeString::get("client.settings.dx11EnabledMsg.name")));
        } else {
            Ma4::getRenderer().setShouldReinit();
        }
        lastDX11 = std::get<BoolValue>(useDX11);
    }

    rgbHue += SDK::ClientInstance::get()->minecraft->timer->alpha * 0.005f * std::get<FloatValue>(rgbSpeed);
    if (rgbHue > 1.f) {
        rgbHue = 0.f;
    }
}

void Ma4::onKey(Event& evGeneric) {
    auto& ev = reinterpret_cast<KeyUpdateEvent&>(evGeneric);
    if (ev.getKey() == std::get<KeyValue>(ejectKey) && ev.isDown()) {
        this->queueEject();
        Logger::Info("Uninject key pressed");

        ev.setCancelled();
        return;
    }

    if (ev.isDown()) {
        for (auto& tb : textBoxes) {
            if (tb->isSelected()) {
                tb->onKeyDown(ev.getKey());
            }
        }
    }
}

void Ma4::onClick(Event& evGeneric) {
    auto& ev = reinterpret_cast<ClickEvent&>(evGeneric);
    timings.onClick(ev.getMouseButton(), ev.isDown());
}

void Ma4::onChar(Event& evGeneric) {
    auto& ev = reinterpret_cast<CharEvent&>(evGeneric);
    for (auto tb : textBoxes) {
        if (tb->isSelected()) {
            if (ev.isChar()) {
                tb->onChar(ev.getChar());
            } else {
                auto ch = ev.getChar();
                switch (ch) {
                case 0x1:
                    util::SetClipboardText(tb->getText());
                    break;
                case 0x2:
                    tb->setSelected(false);
                    break;
                case 0x3:
                    tb->reset();
                    break;
                default:
                    break;
                }
            }
            ev.setCancelled(true);
        }
    }
}

void Ma4::onRendererInit(Event&) {
    getAssets().unloadAll(); // should be safe even if we didn't load resources yet
    getAssets().loadAll();

    this->hudBlurBitmap = getRenderer().getCopiedBitmap();
    getRenderer().getDeviceContext()->CreateEffect(CLSID_D2D1GaussianBlur, gaussianBlurEffect.GetAddressOf());

    gaussianBlurEffect->SetInput(0, hudBlurBitmap.Get());
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
                                 std::get<FloatValue>(this->hudBlurIntensity));
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);

    getRenderer().getDeviceContext()->CreateBitmapBrush(hudBlurBitmap.Get(), this->hudBlurBrush.GetAddressOf());
}

void Ma4::onRendererCleanup(Event& ev) {
    this->hudBlurBitmap = nullptr;
    this->gaussianBlurEffect = nullptr;
    this->hudBlurBrush = nullptr;
}

void Ma4::onSuspended(Event& ev) {
    Ma4::getConfigManager().saveCurrentConfig();
    Logger::Info("Saved config");
}

void Ma4::onBobView(Event& ev) {
    if (std::get<BoolValue>(this->minimalViewBob)) {
        reinterpret_cast<Cancellable&>(ev).setCancelled(true);
    }
}

void Ma4::onLeaveGame(Event& ev) {
    getRenderer().clearTextCache();
}

void Ma4::onRenderLayer(Event& evG) {
    if (isEjectQueued()) return;

    auto& ev = reinterpret_cast<RenderLayerEvent&>(evG);
    while (!this->uiRenderQueue.empty()) {
        auto& latest = this->uiRenderQueue.front();
        latest(ev.getUIRenderContext());
        this->uiRenderQueue.pop();
    }
}

void Ma4::onRenderOverlay(Event& evG) {
    if (isEjectQueued()) return;

    auto& ev = reinterpret_cast<RenderOverlayEvent&>(evG);

    this->releaseDeferredD2DResources();

    if (getRenderer().getFontFamily2() != std::get<TextValue>(secondaryFont).str) {
        getRenderer().updateSecondaryFont(std::get<TextValue>(secondaryFont).str);
    }

    while (!this->dxRenderQueue.empty()) {
        auto& latest = this->dxRenderQueue.front();
        latest(ev.getDeviceContext());
        this->dxRenderQueue.pop();
    }
}

void Ma4::onPacketReceive(Event& evG) {
    // disabled
    auto& ev = reinterpret_cast<PacketReceiveEvent&>(evG);
}

void Ma4::onTick(Event& ev) {
    updateModuleBlocking();
}

void Ma4::onMouseRelease(Event& ev) {
    if (std::get<BoolValue>(centerCursorMenus)) {
        centerCursorInGameWindow();
    }
}

void Ma4::loadLanguageConfig(std::shared_ptr<Setting> languageSetting) {
    this->getSettings().forEach([&](std::shared_ptr<Setting> set) {
        if (set->name() == languageSetting->name()) {
            std::visit(
                [&](auto&& obj) {
                    *set->value = obj;
                    set->update();
                },
                languageSetting->resolvedValue);
        }
    });
}

void Ma4::loadConfig(SettingGroup& gr) {
    gr.forEach([&](std::shared_ptr<Setting> set) {
        this->getSettings().forEach([&](std::shared_ptr<Setting> modSet) {
            if (modSet->name() == set->name()) {
                std::visit(
                    [&](auto&& obj) {
                        *modSet->value = obj;
                        modSet->update();
                    },
                    set->resolvedValue);
            }
        });
    });
}
