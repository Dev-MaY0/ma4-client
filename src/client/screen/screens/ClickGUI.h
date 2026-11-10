#pragma once
#include "../Screen.h"
#include "../../render/asset/Asset.h"
#include "client/screen/TextBox.h"
#include "client/screen/ios/IosShell.h"
#include "client/localization/LocalizeString.h"
#include <memory>
#include <string>
#include <vector>

/// The ma4 mod menu: an iOS "Settings app" styled window (see client/screen/ios).
/// This class is the adapter between the game (modules, settings, plugins) and the UI toolkit.
class ClickGUI : public Screen {
public:
    ClickGUI();

    void onRender(class Event& ev);
    void onCleanup(Event& ev);
    void onInit(Event& ev);
    void onKey(Event& ev);
    void onClick(Event& ev);

    std::string getName() override { return "ClickGUI"; }

    void jumpToModule(std::string const& name);
    void requestModuleListRebuild() { shouldRebuildMarket = true; }
    void refreshLocalization() {}

protected:
    void onEnable(bool ignoreAnims) override;
    void onDisable() override;

private:
    struct MarketEntry {
        std::string id;
        std::wstring name;
        std::wstring author;
        std::wstring desc;
        bool installed = false;
    };

    void rebuildMarketIfNeeded();
    ios::Strings makeStrings();
    void fillModules(ios::Frame& f);
    void fillClientSettings(ios::Frame& f);
    void fillReplay(ios::Frame& f);
    bool fillDetail(uint64_t id, ios::ModuleDetail& out);

    ios::Shell shell;
    std::vector<MarketEntry> market;
    size_t lastModuleCount = 0;
    bool marketBuilt = false;
    bool shouldRebuildMarket = false;
    bool shellVisible = false;
    float pendingWheel = 0.f;
    std::optional<std::string> pendingJump;
};
