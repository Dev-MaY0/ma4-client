#include "pch.h"
#include "ClickGUI.h"
#include "client/event/Eventing.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/event/events/RendererCleanupEvent.h"
#include "client/event/events/RendererInitEvent.h"
#include "client/event/events/KeyUpdateEvent.h"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/CharEvent.h"
#include "client/render/Renderer.h"
#include "client/Ma4.h"
#include "client/feature/module/Module.h"
#include "client/feature/module/ModuleManager.h"
#include "client/feature/module/HUDModule.h"
#include "util/DrawContext.h"
#include "../../render/asset/Assets.h"
#include "client/config/ConfigManager.h"
#include "client/screen/ios/IosUi.h"
#include "client/misc/ReplayStore.h"

#include "../ScreenManager.h"

#include <algorithm>
#include <array>
#include <cmath>

#ifdef min
#undef min
#undef max
#endif

namespace {
    std::wstring replaceAll(std::wstring text, std::wstring const& from, std::wstring const& to) {
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::wstring::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
        return text;
    }

    ios::Col toCol(StoredColor const& c) {
        return ios::Col(c.r, c.g, c.b, c.a);
    }

    bool isInternal(std::wstring const& desc) {
        return desc == L"<internal setting>";
    }

    /// Converts a Setting into an iOS list row. Returns false for values the menu cannot edit
    /// (positions, snapping data, ...).
    bool buildRow(std::shared_ptr<Setting> const& set, SettingGroup& group, ios::SettingRow& row) {
        if (!set->visible || !set->shouldRender(group)) return false;

        row.id = ios::Ui::idOf(set.get());
        row.title = set->getDisplayName();
        row.subtitle = isInternal(set->desc()) ? L"" : set->desc();
        row.changed = [set]() {
            set->update();
            set->userUpdate();
        };

        switch (static_cast<Setting::Type>(set->value->index())) {
        case Setting::Type::Bool:
            row.kind = ios::SettingRow::Kind::Toggle;
            row.boolean = &std::get<BoolValue>(*set->value).value;
            return true;

        case Setting::Type::Float: {
            row.kind = ios::SettingRow::Kind::Slider;
            row.number = &std::get<FloatValue>(*set->value).value;
            row.lo = std::holds_alternative<FloatValue>(set->min) ? std::get<FloatValue>(set->min).value : 0.f;
            row.hi = std::holds_alternative<FloatValue>(set->max) ? std::get<FloatValue>(set->max).value : 1.f;
            float interval =
                std::holds_alternative<FloatValue>(set->interval) ? std::get<FloatValue>(set->interval).value : 0.f;
            row.step = (std::isfinite(interval) && interval > 0.f) ? interval : 0.f;
            return true;
        }

        case Setting::Type::Key: {
            row.kind = ios::SettingRow::Kind::Key;
            auto& kv = std::get<KeyValue>(*set->value);
            row.keycode = &kv.value;
            row.keyText = kv.value == 0 ? L"" : util::StrToWStr(util::KeyToString(kv.value));
            row.clear = [set]() {
                std::get<KeyValue>(*set->value).value = 0;
                set->update();
                set->userUpdate();
            };
            return true;
        }

        case Setting::Type::Enum: {
            if (!set->enumData) return false;
            auto* entries = set->enumData->getEntries();
            if (!entries || entries->empty()) return false;
            row.kind = ios::SettingRow::Kind::Choice;
            auto& ev = std::get<EnumValue>(*set->value);
            ev.val = std::clamp(ev.val, 0, static_cast<int>(entries->size()) - 1);
            row.choice = &ev.val;
            for (auto& entry : *entries) row.options.push_back(entry.name());
            std::wstring selectedDesc = (*entries)[static_cast<size_t>(ev.val)].desc();
            if (!selectedDesc.empty()) {
                row.subtitle = row.subtitle.empty() ? selectedDesc : row.subtitle + L"\n" + selectedDesc;
            }
            return true;
        }

        case Setting::Type::Color: {
            row.kind = ios::SettingRow::Kind::Color;
            auto& cv = std::get<ColorValue>(*set->value);
            row.color = toCol(cv.color1);
            row.rgb = cv.isRGB;
            row.hasRgbToggle = true;
            row.setColor = [set](ios::Col c, bool rgb) {
                auto& colorValue = std::get<ColorValue>(*set->value);
                colorValue.isRGB = rgb;
                colorValue.color1 = { c.r, c.g, c.b, c.a };
                set->update();
                set->userUpdate();
            };
            return true;
        }

        case Setting::Type::Text:
            row.kind = ios::SettingRow::Kind::Text;
            row.str = &std::get<TextValue>(*set->value).str;
            row.maxChars = 128;
            return true;

        case Setting::Type::Action:
            row.kind = ios::SettingRow::Kind::Action;
            row.action = [set]() {
                if (set->action) set->action.value()();
            };
            return true;

        default:
            return false;
        }
    }

    int clientSectionFor(std::string const& name) {
        static const std::vector<std::pair<int, std::vector<std::string>>> table = {
            { 0, { "language", "menuKey", "ejectKey" } },
            { 1, { "accentColor", "appearance", "uiScale", "menuBlurEnabled", "menuIntensity", "secondaryFont",
                   "mcRendererFont", "textShadow", "rgbSpeed" } },
            { 2, { "snapLines", "centerCursor" } },
            { 3, { "useDX11", "forceDisableVSync", "minecraftRenderer", "minViewBob" } },
        };
        for (auto const& [idx, names] : table) {
            if (std::find(names.begin(), names.end(), name) != names.end()) return idx;
        }
        return 4;
    }
} // namespace

ClickGUI::ClickGUI() {
    shell.setTextEditFactory([](int maxChars) -> std::shared_ptr<ios::TextEdit> {
        auto box = std::make_shared<TextBox>(maxChars);
        Ma4::get().addTextBox(box.get());
        return box;
    });

    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&ClickGUI::onRender, 1, true);
    Eventing::get().listen<RendererCleanupEvent>(this, (EventListenerFunc)&ClickGUI::onCleanup, 1, true);
    Eventing::get().listen<RendererInitEvent>(this, (EventListenerFunc)&ClickGUI::onInit, 1, true);
    Eventing::get().listen<KeyUpdateEvent>(this, (EventListenerFunc)&ClickGUI::onKey, 1);
    Eventing::get().listen<ClickEvent>(this, (EventListenerFunc)&ClickGUI::onClick, 1);
}

void ClickGUI::jumpToModule(std::string const& name) {
    pendingJump = name;
}

ios::Strings ClickGUI::makeStrings() {
    ios::Strings s;
    s.appName = L"ma4";
    s.search = LocalizeString::get("client.ui.clickGui.search.name");
    s.searchSettings = LocalizeString::get("client.ui.clickGui.searchSettings.name");
    s.all = LocalizeString::get("client.ui.clickGui.tab.all.name");
    s.game = LocalizeString::get("client.ui.clickGui.tab.game.name");
    s.hud = LocalizeString::get("client.ui.clickGui.tab.hud.name");
    s.plugins = LocalizeString::get("client.ui.clickGui.tab.plugins.name");
    s.settings = LocalizeString::get("client.ui.clickGui.settings.name");
    s.hudEditor = LocalizeString::get("client.ui.clickGui.hudEditor.name");
    s.replay = LocalizeString::get("client.ui.clickGui.replay.name");
    s.modulesHeader = LocalizeString::get("client.ui.clickGui.header.modules");
    s.clientHeader = LocalizeString::get("client.ui.clickGui.header.client");
    s.modulesBack = LocalizeString::get("client.ui.clickGui.modules.name");
    s.reset = LocalizeString::get("client.ui.clickGui.reset.name");
    s.install = LocalizeString::get("client.ui.clickGui.install.name");
    s.installed = LocalizeString::get("client.ui.clickGui.installed.name");
    s.open = LocalizeString::get("client.ui.clickGui.open.name");
    s.done = LocalizeString::get("client.ui.clickGui.done.name");
    s.color = LocalizeString::get("client.ui.clickGui.colorPicker.name");
    s.rgb = LocalizeString::get("client.ui.clickGui.rgb.name");
    s.noResults = LocalizeString::get("client.ui.clickGui.noResults.name");
    s.byAuthor = replaceAll(LocalizeString::get("client.ui.clickGui.pluginAuthor.name"), L"{}", L"%s");
    s.recorder = LocalizeString::get("client.ui.replay.recorder");
    s.recordReady = LocalizeString::get("client.ui.replay.ready");
    s.recordingNow = LocalizeString::get("client.ui.replay.recording");
    s.startRecording = LocalizeString::get("client.ui.replay.start");
    s.stopRecording = LocalizeString::get("client.ui.replay.stop");
    s.addMarker = LocalizeString::get("client.ui.replay.addMarker");
    s.recordings = LocalizeString::get("client.ui.replay.recordings");
    s.noRecordings = LocalizeString::get("client.ui.replay.none");
    s.noRecordingsHint = LocalizeString::get("client.ui.replay.noneHint");
    s.markersCount = replaceAll(LocalizeString::get("client.ui.replay.markers"), L"{}", L"%d");
    s.deleteReplay = LocalizeString::get("client.ui.replay.delete");
    s.follow = LocalizeString::get("client.ui.replay.follow");
    s.addKeyframe = LocalizeString::get("client.ui.replay.addKeyframe");
    s.clearKeyframes = LocalizeString::get("client.ui.replay.clearKeyframes");
    s.cameraPath = LocalizeString::get("client.ui.replay.cameraPath");
    s.cameraPathHint = LocalizeString::get("client.ui.replay.cameraPathHint");
    s.mapToggle = LocalizeString::get("client.ui.replay.map");
    return s;
}

ClickGUI::~ClickGUI() {
    if (marketThread.joinable()) marketThread.join();
}

void ClickGUI::startMarketFetch() {
    marketRequested = true;
    auto state = std::make_shared<MarketFetch>();
    marketFetch = state;
    marketThread = std::thread([state] {
        std::vector<MarketInfo> out;
        try {
            for (auto& plug : Ma4::getPluginManager().fetchPluginsFromMarket(false)) {
                out.push_back({ plug.id, plug.name, plug.author, plug.desc });
            }
        } catch (...) {
        }
        state->result = std::move(out);
        state->done.store(true, std::memory_order_release);
    });
}

void ClickGUI::rebuildMarketIfNeeded() {
    // Never block the render thread on the network: download in the background and pick the result up here.
    if (marketFetch && marketFetch->done.load(std::memory_order_acquire)) {
        if (marketThread.joinable()) marketThread.join();
        marketInfo = std::move(marketFetch->result);
        marketFetch.reset();
        // an empty list means the registry was unreachable: try again in a while, not every frame
        marketNextTry = marketInfo.empty() ? std::chrono::steady_clock::now() + std::chrono::seconds(120)
                                           : std::chrono::steady_clock::time_point::max();
        shouldRebuildMarket = true;
    }
    if (!marketFetch && !marketThread.joinable() &&
        (!marketRequested || (marketInfo.empty() && std::chrono::steady_clock::now() >= marketNextTry))) {
        startMarketFetch();
    }

    size_t count = Ma4::getModuleManager().size();
    if (!marketBuilt || shouldRebuildMarket || count != lastModuleCount) {
        shouldRebuildMarket = false;
        marketBuilt = true;
        lastModuleCount = count;
        market.clear();

        // only the "installed" state is looked up again here (a local file check), never the registry
        for (auto& plug : marketInfo) {
            MarketEntry e;
            e.id = plug.id;
            e.name = plug.name;
            e.author = plug.author;
            e.desc = plug.desc;
            e.installed = PluginManager::isPluginInstalled(plug.id);
            market.push_back(std::move(e));
        }
    }
}

void ClickGUI::fillModules(ios::Frame& f) {
    for (auto& plug : market) {
        ios::ModuleRow r;
        r.id = ios::Ui::hashId(plug.id, 0xA11CEull);
        r.key = plug.id;
        r.name = plug.name;
        r.desc = plug.desc;
        r.author = plug.author;
        r.market = true;
        r.inGame = false;
        r.trailing = plug.installed ? ios::ModuleRow::Trailing::Installed : ios::ModuleRow::Trailing::Install;
        r.install = [this, id = plug.id]() {
            auto result = PluginManager::installScript(id);
            if (!result.has_value()) {
                Ma4::getNotifications().push(util::StrToWStr(result.error()));
            } else {
                shouldRebuildMarket = true;
            }
        };
        f.modules.push_back(std::move(r));
    }

    Ma4::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
        if (!mod->isVisible()) return;
        ios::ModuleRow r;
        r.id = ios::Ui::idOf(mod.get());
        r.key = mod->name();
        r.name = mod->getDisplayName();
        r.desc = mod->desc();
        r.inGame = mod->getCategory() != Module::HUD;
        r.inHud = mod->isHud();
        if (mod->shouldHoldToToggle()) {
            r.trailing = ios::ModuleRow::Trailing::HoldKey;
            r.keyText = util::StrToWStr(util::KeyToString(mod->getKeybind()));
        } else if (!mod->isToggleable()) {
            r.trailing = ios::ModuleRow::Trailing::Open;
            r.open = [mod]() {
                mod->setEnabled(true);
            };
        } else {
            r.trailing = ios::ModuleRow::Trailing::Toggle;
            r.enabled = mod->isEnabled();
            r.setEnabled = [mod](bool v) {
                mod->setEnabled(v);
            };
        }
        f.modules.push_back(std::move(r));
    });
}

void ClickGUI::fillReplay(ios::Frame& f) {
    auto& store = ReplayStore::get();
    if (store.consumeError()) {
        Ma4::getNotifications().push(LocalizeString::get("client.ui.replay.error").value());
    }
    f.replay.available = true;
    f.replay.recording = store.isRecording();
    f.replay.recordingSeconds = store.recordingSeconds();
    f.replay.recordingMarkers = store.markerCount();
    f.replay.list = store.list();
    f.replay.toggleRecording = []() {
        ReplayStore::get().toggleRecording();
    };
    f.replay.addMarker = []() {
        ReplayStore::get().addMarker();
    };
    f.replay.remove = [](uint64_t id) {
        ReplayStore::get().remove(id);
    };
    f.replay.load = [](uint64_t id) {
        return ReplayStore::get().load(id);
    };
}

void ClickGUI::fillClientSettings(ios::Frame& f) {
    auto& settings = Ma4::getSettings();
    std::array<ios::SettingsSection, 5> sections;
    sections[0].header = LocalizeString::get("client.ui.section.general");
    sections[1].header = LocalizeString::get("client.ui.section.appearance");
    sections[2].header = LocalizeString::get("client.ui.section.interaction");
    sections[3].header = LocalizeString::get("client.ui.section.performance");
    sections[4].header = LocalizeString::get("client.ui.section.other");

    settings.forEach([&](std::shared_ptr<Setting> set) {
        ios::SettingRow row;
        if (!buildRow(set, settings, row)) return;
        sections[static_cast<size_t>(clientSectionFor(set->name()))].rows.push_back(std::move(row));
    });

    for (auto& sec : sections) {
        if (!sec.rows.empty()) f.clientSettings.push_back(std::move(sec));
    }
}

bool ClickGUI::fillDetail(uint64_t id, ios::ModuleDetail& d) {
    std::shared_ptr<Module> found;
    Ma4::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
        if (ios::Ui::idOf(mod.get()) == id) found = mod;
    });
    if (!found) return false;
    auto mod = found;

    d.name = mod->getDisplayName();
    d.desc = mod->desc();
    d.key = mod->name();
    d.hasToggle = mod->isToggleable() && !mod->shouldHoldToToggle();
    d.enabled = mod->isEnabled();
    d.setEnabled = [mod](bool v) {
        mod->setEnabled(v);
    };

    ios::SettingsSection keySection;
    ios::SettingsSection mainSection;
    auto& group = *mod->settings;
    group.forEach([&](std::shared_ptr<Setting> set) {
        ios::SettingRow row;
        if (!buildRow(set, group, row)) return;
        if (set->name() == "key") {
            keySection.rows.push_back(std::move(row));
        } else {
            mainSection.rows.push_back(std::move(row));
        }
    });
    if (!keySection.rows.empty()) d.sections.push_back(std::move(keySection));
    if (!mainSection.rows.empty()) d.sections.push_back(std::move(mainSection));

    d.reset = [mod]() {
        mod->settings->forEach([&](std::shared_ptr<Setting> set) {
            *set->value = set->defaultValue;
        });
    };

    if (mod->isHud() && static_cast<HUDModule*>(mod.get())->isShowPreview()) {
        auto* hud = static_cast<HUDModule*>(mod.get());
        d.hasPreview = true;
        auto size = hud->getRectNonScaled().getSize();
        d.previewSize = Vec2(size.x, size.y);
        d.drawPreview = [hud](ios::Rect const& area) {
            D2DUtil dc;
            auto size = hud->getRectNonScaled().getSize();
            Vec2 drawPos = area.center(Vec2(size.x, size.y));
            D2D1::Matrix3x2F oldTransform;
            dc.ctx->GetTransform(&oldTransform);
            dc.ctx->SetTransform(D2D1::Matrix3x2F::Translation(drawPos.x, drawPos.y) * oldTransform);
            hud->render(dc, true, false);
            dc.ctx->SetTransform(oldTransform);
        };
    }
    return true;
}

void ClickGUI::onRender(Event&) {
    {
        auto scn = Ma4::getScreenManager().getActiveScreen();
        if (scn && scn->get().getName() != this->getName()) {
            // another screen (e.g. the HUD editor) took over
            if (shellVisible) {
                shell.onClosed();
                shellVisible = false;
            }
            return;
        }
    }
    if (!isActive() && !shellVisible) return;
    if (!isActive()) justClicked = { false, false, false };

    rebuildMarketIfNeeded();

    auto& rend = Ma4::getRenderer();
    auto ss = rend.getScreenSize();
    ios::Rect screen(0.f, 0.f, ss.width, ss.height);

    D2DUtil dc;
    dc.ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // blurred, dimmed game behind the window (like an iOS sheet)
    float open = shell.openAmount();
    if (auto blur = Ma4::get().getMenuBlur()) {
        if (blur.value() * open > 0.5f) dc.drawGaussianBlur(blur.value() * open);
    }
    dc.fillRectangle(d2d::Rect(0.f, 0.f, ss.width, ss.height), d2d::Color(0.f, 0.f, 0.f, 0.26f * open));

    ios::Gfx gfx;
    gfx.ctx = rend.getDeviceContext();
    gfx.brush = rend.getSolidBrush();
    gfx.dwrite = rend.getDWriteFactory();
    gfx.d2d = rend.getFactory();
    gfx.fonts[static_cast<int>(ios::Font::Light)] = rend.getTextFormat(Renderer::FontSelection::PrimaryLight);
    gfx.fonts[static_cast<int>(ios::Font::Regular)] = rend.getTextFormat(Renderer::FontSelection::PrimaryRegular);
    gfx.fonts[static_cast<int>(ios::Font::Medium)] = rend.getTextFormat(Renderer::FontSelection::PrimaryMedium);
    gfx.fonts[static_cast<int>(ios::Font::Semibold)] = rend.getTextFormat(Renderer::FontSelection::PrimarySemibold);
    gfx.fonts[static_cast<int>(ios::Font::Bold)] = rend.getTextFormat(Renderer::FontSelection::PrimaryBold);

    ios::Input in;
    in.mouse = SDK::ClientInstance::get()->cursorPos;
    in.down = mouseButtons[0];
    in.pressed = justClicked[0];
    in.rightPressed = justClicked[1];
    in.wheel = pendingWheel;
    in.dt = rend.getDeltaTime() * 0.017f;
    pendingWheel = 0.f;

    const bool rtl = Ma4::get().getL10nData().isSelectedLanguageRightToLeft();
    float uiScale = ios::Shell::computeScale(screen, Ma4::get().getUiScale());
    auto accent = d2d::Color(Ma4::get().getAccentColor().getMainColor());
    ios::Palette pal = ios::Palette::make(
        Ma4::get().isLightAppearance() ? ios::Appearance::Light : ios::Appearance::Dark, accent);

    auto& ui = ios::Ui::get();
    ui.beginFrame(gfx, in, pal, uiScale, rtl);

    ios::Frame f;
    f.str = makeStrings();
    f.version = util::StrToWStr(std::string(Ma4::version));
    f.logo = Ma4::getAssets().ma4Logo.getBitmap();
    fillModules(f);
    fillClientSettings(f);
    fillReplay(f);
    f.detail = [this](uint64_t id, ios::ModuleDetail& d) {
        return fillDetail(id, d);
    };
    f.closeWindow = [this]() {
        playClickSound();
        close();
    };
    f.openHudEditor = [this]() {
        playClickSound();
        close();
        Ma4::getScreenManager().showScreen<HUDEditor>(true);
    };

    if (pendingJump) {
        Ma4::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
            if (mod->name() == *pendingJump) shell.jumpToModule(ios::Ui::idOf(mod.get()));
        });
        pendingJump.reset();
    }

    shellVisible = shell.frame(ui, screen, f, isActive());
    ui.endFrame();

    cursor = Cursor::Arrow;
}

void ClickGUI::onInit(Event&) {
}

void ClickGUI::onCleanup(Event&) {
    shell.releaseDeviceResources();
    ios::Ui::get().releaseDeviceResources();
}

void ClickGUI::onKey(Event& evGeneric) {
    auto& ev = reinterpret_cast<KeyUpdateEvent&>(evGeneric);
    if (ev.getKey() == VK_F11) return;

    if (ev.isDown()) {
        if (ev.getKey() == VK_ESCAPE) {
            if (!shell.onEscape()) this->close();
            ev.setCancelled(true);
            return;
        }
        if (shell.capturingKey()) {
            shell.feedKey(ev.getKey());
            ev.setCancelled(true);
            return;
        }
    }

    ev.setCancelled(true);
}

void ClickGUI::onClick(Event& evGeneric) {
    auto& ev = reinterpret_cast<ClickEvent&>(evGeneric);
    if (ev.getMouseButton() > 0) {
        ev.setCancelled(true);
    }

    if (ev.getClickType() == ClickEvent::ClickType::Wheel) {
        pendingWheel -= static_cast<float>(ev.getWheelDelta()) / 3.f;
        ev.setCancelled(true);
        return;
    }
}

void ClickGUI::onEnable(bool ignoreAnims) {
    mouseButtons = {};
    justClicked = {};
    pendingWheel = 0.f;
    shell.reset(ignoreAnims);
    shellVisible = true;
}

void ClickGUI::onDisable() {
    shell.onClosed();
    Ma4::getConfigManager().saveCurrentConfig();
}
