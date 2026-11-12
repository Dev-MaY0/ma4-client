#pragma once
#include "../Screen.h"
#include "../../render/asset/Asset.h"
#include "client/screen/TextBox.h"
#include "client/screen/ios/IosShell.h"
#include "client/localization/LocalizeString.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

/// The ma4 mod menu: an iOS "Settings app" styled window (see client/screen/ios).
/// This class is the adapter between the game (modules, settings, plugins) and the UI toolkit.
class ClickGUI : public Screen {
public:
    ClickGUI();
    ~ClickGUI();

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

    /// Plugin registry entry as downloaded by the background thread.
    struct MarketInfo {
        std::string id;
        std::wstring name, author, desc;
    };
    struct MarketFetch {
        std::vector<MarketInfo> result;
        std::atomic<bool> done { false };
    };

    void startMarketFetch();
    void rebuildMarketIfNeeded();
    void renderFrame();
    void reportRenderFailure(char const* what);
    ios::Strings makeStrings();
    void fillModules(ios::Frame& f);
    void fillClientSettings(ios::Frame& f);
    void fillReplay(ios::Frame& f);
    bool fillDetail(uint64_t id, ios::ModuleDetail& out);

    ios::Shell shell;
    std::vector<MarketEntry> market;
    // The registry is a network download (one request for the index plus one per plugin). It used to run on the render
    // thread the first time the menu was drawn, which froze the game for seconds: it now runs on its own thread.
    std::vector<MarketInfo> marketInfo;
    std::shared_ptr<MarketFetch> marketFetch;
    std::thread marketThread;
    std::chrono::steady_clock::time_point marketNextTry {};
    bool marketRequested = false;
    size_t lastModuleCount = 0;
    bool marketBuilt = false;
    bool shouldRebuildMarket = false;
    int renderFailures = 0;     // consecutive frames that threw
    int renderFailuresTotal = 0;
    bool loggedOpen = false;    // the "menu opened" diagnostics line was written for this opening
    bool shellVisible = false;
    float pendingWheel = 0.f;
    std::optional<std::string> pendingJump;
};
