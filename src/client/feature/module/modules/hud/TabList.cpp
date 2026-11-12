#include "pch.h"
#include "TabList.h"
#include "client/Ma4.h"
#include "client/event/events/RenderNameTagEvent.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/event/events/RendererCleanupEvent.h"
#include "client/event/events/TickEvent.h"
#include "client/misc/NameTagCache.h"
#include "client/screen/ScreenManager.h"
#include "client/screen/ios/IosBridge.h"
#include "client/screen/ios/IosCards.h"
#include "client/screen/ios/IosUi.h"
#include "mc/common/world/actor/player/SerializedSkinRef.h"
#include "mc/common/world/level/Level.h"
#include "util/Logger.h"
#include "util/DrawContext.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

TabList::TabList()
    : Module("PlayerList", LocalizeString::get("client.module.tabList.name"),
             LocalizeString::get("client.module.tabList.desc"), HUD, VK_TAB) {
    addSetting("textColor", LocalizeString::get("client.module.tabList.textColor.name"),
               LocalizeString::get("client.module.tabList.textColor.desc"), textCol);
    addSetting("bgColor", LocalizeString::get("client.module.tabList.bgColor.name"),
               LocalizeString::get("client.module.tabList.bgColor.desc"), bgCol);
    addSetting("hideModules", LocalizeString::get("client.module.tabList.hideModules.name"),
               LocalizeString::get("client.module.tabList.hideModules.desc"), hideModules);
    addSliderSetting("textSize", LocalizeString::get("client.textmodule.props.textSize.name"), L"", textSizeS,
                     FloatValue(2.f), FloatValue(100.f), FloatValue(2.f));
    listen<RenderOverlayEvent>(static_cast<EventListenerFunc>(&TabList::onRenderOverlay));
    listen<RenderNameTagEvent>(static_cast<EventListenerFunc>(&TabList::onRenderNameTag));
    listen<TickEvent>(static_cast<EventListenerFunc>(&TabList::onTick));
    listen<RendererCleanupEvent>(static_cast<EventListenerFunc>(&TabList::onCleanup), true);
}

bool TabList::skinKeyMatches(PlayerHeadSkinKey const& key, SDK::SerializedSkinRef const& skin,
                             SDK::SkinImage const& image) const {
    auto id = skin.getId();

    if (id && key.id != *id) return false;
    if (!id && !key.id.empty()) return false;

    if (key.width != image.width) return false;
    if (key.height != image.height) return false;

    if (key.bytes != reinterpret_cast<uintptr_t>(image.bytes.data())) return false;
    if (key.byteCount != image.bytes.size()) return false;

    return true;
}

TabList::PlayerHeadSkinKey TabList::makeSkinKey(SDK::SerializedSkinRef const& skin, SDK::SkinImage const& image) const {
    PlayerHeadSkinKey key {};
    if (auto id = skin.getId(); id) {
        key.id = *id;
    }
    key.width = image.width;
    key.height = image.height;
    key.bytes = reinterpret_cast<uintptr_t>(image.bytes.data());
    key.byteCount = image.bytes.size();
    return key;
}

std::string TabList::getRowName(SDK::PlayerListEntry& entry) const {
    if (auto formattedName = Ma4::get().getNameTagCache().getFormattedPlayerName(entry.name)) return *formattedName;
    return entry.name;
}

bool TabList::refreshActivePlayerNames(SDK::Level* level) {
    std::unordered_set<std::string> players;
    if (level && level->getPlayerList()) {
        players.reserve(level->getPlayerList()->size());
        for (auto& ent : *level->getPlayerList()) {
            players.insert(ent.second.name);
        }
    }

    bool changed = players.size() != cachedActivePlayerNames.size();
    if (!changed) {
        for (auto const& playerName : players) {
            if (!cachedActivePlayerNames.contains(playerName)) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        cachedActivePlayerNames = std::move(players);
        rowsDirty = true;
        pruneHeadCache();
    }

    return changed;
}

void TabList::syncNameTagCache(SDK::Level* level) {
    auto& nameTags = Ma4::get().getNameTagCache();
    if (level) {
        for (auto* actor : level->getRuntimeActorList()) {
            if (!actor || !actor->isPlayer()) continue;
            nameTags.recordActorNameTag(actor->getRuntimeID(), actor->getNameTag());
        }
    }

    bool playersChanged = refreshActivePlayerNames(level);
    uint64_t actorNameTagsRevision = nameTags.getActorNameTagsRevision();
    bool actorNameTagsChanged = cachedActorNameTagsRevision != actorNameTagsRevision;

    if (playersChanged || actorNameTagsChanged) {
        if (actorNameTagsChanged) {
            rowsDirty = true;
        }
        if (nameTags.updateFormattedPlayerNames(cachedActivePlayerNames)) {
            rowsDirty = true;
        }
        cachedActorNameTagsRevision = actorNameTagsRevision;
    }
}

void TabList::rebuildRows(SDK::Level* level) {
    cachedRows.clear();
    if (!level || !level->getPlayerList()) {
        rowsDirty = false;
        return;
    }

    auto& nameTags = Ma4::get().getNameTagCache();
    cachedRows.reserve(level->getPlayerList()->size());
    for (auto& ent : *level->getPlayerList()) {
        CachedPlayerRow row {};
        row.playerName = ent.second.name;
        row.displayName = getRowName(ent.second);
        row.strippedName = nameTags.stripFormatCodes(row.displayName);
        row.displayNameWide = util::StrToWStr(row.displayName);
        row.strippedNameWide = util::StrToWStr(row.strippedName);
        row.colorSortIndex = nameTags.getFirstColorSortIndex(row.displayName);
        cachedRows.push_back(std::move(row));
    }

    std::stable_sort(cachedRows.begin(), cachedRows.end(), [](CachedPlayerRow const& a, CachedPlayerRow const& b) {
        if (a.colorSortIndex != b.colorSortIndex) return a.colorSortIndex < b.colorSortIndex;
        return a.strippedName < b.strippedName;
    });

    rowsDirty = false;
}


void TabList::onCleanup(Event&) {
    cachedHeads.clear(); // Direct2D bitmaps belong to the renderer's device
}

void TabList::onDisable() {
    shownLastFrame = false;
}

void TabList::pruneHeadCache() {
    for (auto it = cachedHeads.begin(); it != cachedHeads.end();) {
        if (cachedActivePlayerNames.contains(it->first))
            ++it;
        else
            it = cachedHeads.erase(it);
    }
}

TabList::CachedHead* TabList::headFor(ID2D1DeviceContext* ctx, SDK::PlayerListEntry& entry, std::string const& cacheKey) {
    auto* image = entry.skin.getSkinImage();
    if (!image || !image->hasRgbaBytes()) return nullptr;

    auto now = std::chrono::steady_clock::now();
    auto& head = cachedHeads[cacheKey];
    if (head.hasSkinKey && !skinKeyMatches(head.skinKey, entry.skin, *image)) head = {}; // the skin changed
    if (head.bitmap && head.brush) return &head;
    if (now < head.nextAttempt) return nullptr;

    head.skinKey = makeSkinKey(entry.skin, *image);
    head.hasSkinKey = true;
    head.nextAttempt = now + std::chrono::seconds(2); // a failed attempt is not repeated every frame

    // classic layout: face at (8, 8), hat layer at (40, 8), 8x8 pixels at 64 px width (twice as much for 128 px skins)
    const uint32_t w = image->width, h = image->height;
    const uint32_t unit = std::max(1u, w / 64u);
    const uint32_t fs = 8u * unit;
    if (w < 48u * unit || h < 16u * unit) return nullptr;
    const uint8_t* px = image->bytes.data();
    auto at = [&](uint32_t x, uint32_t y) { return px + (static_cast<size_t>(y) * w + x) * 4u; };

    std::vector<uint32_t> out(static_cast<size_t>(fs) * fs);
    for (uint32_t y = 0; y < fs; ++y) {
        for (uint32_t x = 0; x < fs; ++x) {
            const uint8_t* face = at(8u * unit + x, 8u * unit + y);
            const uint8_t* hat = at(40u * unit + x, 8u * unit + y);
            const float ha = static_cast<float>(hat[3]) / 255.f;
            const auto mixc = [&](int i) { return static_cast<uint32_t>(std::clamp(face[i] * (1.f - ha) + hat[i] * ha, 0.f, 255.f)); };
            out[static_cast<size_t>(y) * fs + x] = (255u << 24) | (mixc(0) << 16) | (mixc(1) << 8) | mixc(2); // BGRA, opaque
        }
    }

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(ctx->CreateBitmap(D2D1::SizeU(fs, fs), out.data(), fs * 4u, props, head.bitmap.ReleaseAndGetAddressOf()))) return nullptr;
    D2D1_BITMAP_BRUSH_PROPERTIES1 bp =
        D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    ComPtr<ID2D1DeviceContext> dc;
    if (FAILED(ctx->QueryInterface(IID_PPV_ARGS(dc.GetAddressOf()))) ||
        FAILED(dc->CreateBitmapBrush(head.bitmap.Get(), &bp, nullptr, head.brush.ReleaseAndGetAddressOf()))) {
        head.bitmap.Reset();
        return nullptr;
    }
    head.faceSize = fs;
    return &head;
}

ColorValue TabList::getColorOrDefault(ValueType const& value, ColorValue const& fallback) const {
    if (!std::holds_alternative<ColorValue>(value)) return fallback;
    return std::get<ColorValue>(value);
}

float TabList::getFloatOrDefault(ValueType const& value, float fallback) const {
    if (!std::holds_alternative<FloatValue>(value)) return fallback;
    return std::get<FloatValue>(value).value;
}

void TabList::onRenderNameTag(Event& evG) {
    RenderNameTagEvent& ev = static_cast<RenderNameTagEvent&>(evG);
    std::string* tag = ev.getNametag();
    if (!tag || !Ma4::get().getNameTagCache().hasFormatCode(*tag)) return;

    SDK::ClientInstance* clientInstance = SDK::ClientInstance::get();
    if (!clientInstance || !clientInstance->minecraft) return;

    SDK::Level* level = clientInstance->minecraft->getLevel();
    if (!level || !level->getPlayerList()) return;

    if (cachedActivePlayerNames.empty()) {
        refreshActivePlayerNames(level);
    }
    if (Ma4::get().getNameTagCache().recordRenderedNameTag(*tag, cachedActivePlayerNames)) {
        rowsDirty = true;
    }
}

void TabList::onTick(Event& evG) {
    auto& ev = static_cast<TickEvent&>(evG);
    auto* level = ev.getLevel();
    syncNameTagCache(level);
}

void TabList::afterLoadConfig() {
    if (!std::holds_alternative<ColorValue>(textCol)) {
        Logger::Warn("TabList: textColor setting was invalid, restoring default");
        textCol = ColorValue(1.f, 1.f, 1.f, 1.f);
    }
    if (!std::holds_alternative<ColorValue>(bgCol)) {
        Logger::Warn("TabList: bgColor setting was invalid, restoring default");
        bgCol = ColorValue(0.f, 0.f, 0.f, 0.5f);
    }
    if (!std::holds_alternative<FloatValue>(textSizeS)) {
        Logger::Warn("TabList: textSize setting was invalid, restoring default");
        textSizeS = FloatValue(20.f);
    }
}


void TabList::onRenderOverlay(Event& evG) {
    auto& ev = static_cast<RenderOverlayEvent&>(evG);
    auto* d2dCtx = ev.getDeviceContext();
    auto& rend = Ma4::getRenderer();
    if (!d2dCtx || !rend.hasInitialized()) return;

    auto* client = SDK::ClientInstance::get();
    if (!client || !client->minecraft || !client->minecraftGame || !client->getLocalPlayer()) return;
    // only in the world: with a menu, the inventory or the chat open the cursor is free and the list stays away
    if (!client->minecraftGame->isCursorGrabbed()) return;
    if (Ma4::getScreenManager().getActiveScreen()) return;
    auto* lvl = client->minecraft->getLevel();
    if (!lvl || !lvl->getPlayerList()) return;

    const size_t count = lvl->getPlayerList()->size();
    if (cachedRows.size() != count || cachedActivePlayerNames.empty()) syncNameTagCache(lvl);
    if (rowsDirty) rebuildRows(lvl);

    // ---- title: the server, or the world
    std::wstring title;
    auto* connectionInfo = SDK::RemoteConnectorComposite::getConnectionInfo();
    if (connectionInfo && !connectionInfo->thirdPartyServerInfo.creatorName.empty()) {
        title = util::StrToWStr(connectionInfo->thirdPartyServerInfo.creatorName);
    } else {
        title = util::StrToWStr(lvl->getLevelName());
    }
    // the title may carry colour codes too (servers like to): show the plain text
    title = util::StrToWStr(Ma4::get().getNameTagCache().stripFormatCodes(util::WStrToStr(title)));
    if (title.empty()) title = L"Players";

    std::wstring subtitle = LocalizeString::get("client.module.tabList.players").value();
    if (size_t p = subtitle.find(L"{}"); p != std::wstring::npos) subtitle.replace(p, 2, std::to_wstring(cachedRows.size()));

    const ColorValue textColor = getColorOrDefault(textCol, ColorValue(1.f, 1.f, 1.f, 1.f));
    const ColorValue backgroundColor = getColorOrDefault(bgCol, ColorValue(0.f, 0.f, 0.f, 0.5f));

    ios::TabData data;
    data.title = std::move(title);
    data.subtitle = std::move(subtitle);
    data.textColor = d2d::Color(textColor.getMainColor());
    data.veil = d2d::Color(backgroundColor.getMainColor());
    auto ss = rend.getScreenSize();
    data.screenW = ss.width;
    data.screenH = ss.height;

    std::unordered_map<std::string, SDK::PlayerListEntry*> entries;
    entries.reserve(count);
    for (auto& ent : *lvl->getPlayerList()) entries.emplace(ent.second.name, &ent.second);

    data.rows.reserve(cachedRows.size());
    for (auto& row : cachedRows) {
        ios::TabRow r;
        r.text = row.displayNameWide;
        if (auto it = entries.find(row.playerName); it != entries.end() && it->second) {
            if (auto* head = headFor(d2dCtx, *it->second, row.playerName)) {
                r.face = head->brush.Get();
                r.faceSize = static_cast<float>(head->faceSize);
            }
        }
        data.rows.push_back(std::move(r));
    }

    // ---- one toolkit frame; the size setting scales the whole card
    const float textP = std::clamp(getFloatOrDefault(textSizeS, 20.f), 6.f, 100.f);
    const float sc = std::clamp(ios::gameScale() * 0.82f * (textP / 20.f), 0.45f, 3.f);
    ios::Input in;
    in.dt = rend.getDeltaTime() * 0.017f;
    auto& ui = ios::Ui::get();
    ui.beginFrame(ios::gameGfx(), in, ios::gamePalette(), sc, Ma4::get().getL10nData().isSelectedLanguageRightToLeft());
    static const uint64_t kPop = ios::Ui::hashId("ma4.tablist.pop");
    ios::drawTabList(ui, data, kPop, !shownLastFrame);
    shownLastFrame = true;
    ui.endFrame();
}
