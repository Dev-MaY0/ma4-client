#pragma once
#include "../../HUDModule.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SDK {
    class Level;
    class PlayerListEntry;
    class SerializedSkinRef;
    class SkinImage;
}

/// The player list shown while the Tab key is held, drawn as an iOS card: a glass panel with the server name and the
/// player count, and one rounded list per column with the player's face, and the name with its Minecraft colour codes.
/// It is drawn with Direct2D (SF-style font, rounded avatars), not with the game's UI renderer.
class TabList : public Module {
public:
    TabList();

    void onRenderOverlay(Event&);
    void onRenderNameTag(Event&);
    void onTick(Event&);
    void onCleanup(Event&);
    void onDisable() override;
    void afterLoadConfig() override;
    bool shouldHoldToToggle() override { return true; }
    bool shouldRenderWithTabList() const override { return true; }
    [[nodiscard]] bool shouldHideModules() const noexcept {
        const auto* setting = std::get_if<BoolValue>(&hideModules);
        return !setting || setting->value;
    }

private:
    struct PlayerHeadSkinKey {
        std::string id;
        uint32_t width = 0;
        uint32_t height = 0;
        uintptr_t bytes = 0;
        size_t byteCount = 0;
    };

    /// The face of a player (8x8 head + hat layer of the skin) as a Direct2D bitmap.
    struct CachedHead {
        PlayerHeadSkinKey skinKey;
        bool hasSkinKey = false;
        ComPtr<ID2D1Bitmap> bitmap;
        ComPtr<ID2D1BitmapBrush1> brush;
        uint32_t faceSize = 8;
        std::chrono::steady_clock::time_point nextAttempt {};
    };

    struct CachedPlayerRow {
        std::string playerName;
        std::string displayName;
        std::string strippedName;
        std::wstring displayNameWide;
        std::wstring strippedNameWide;
        int colorSortIndex = 16;
    };

    std::string getRowName(SDK::PlayerListEntry& entry) const;
    bool refreshActivePlayerNames(SDK::Level* level);
    void syncNameTagCache(SDK::Level* level);
    void rebuildRows(SDK::Level* level);
    void pruneHeadCache();
    CachedHead* headFor(ID2D1DeviceContext* ctx, SDK::PlayerListEntry& entry, std::string const& cacheKey);
    bool skinKeyMatches(PlayerHeadSkinKey const& key, SDK::SerializedSkinRef const& skin, SDK::SkinImage const& image) const;
    PlayerHeadSkinKey makeSkinKey(SDK::SerializedSkinRef const& skin, SDK::SkinImage const& image) const;
    ColorValue getColorOrDefault(ValueType const& value, ColorValue const& fallback) const;
    float getFloatOrDefault(ValueType const& value, float fallback) const;

    std::unordered_map<std::string, CachedHead> cachedHeads;
    std::unordered_set<std::string> cachedActivePlayerNames;
    std::vector<CachedPlayerRow> cachedRows;
    uint64_t cachedActorNameTagsRevision = 0;
    bool rowsDirty = true;
    bool shownLastFrame = false; // for the pop-in animation

    ValueType textSizeS = FloatValue(20.f);
    ValueType textCol = ColorValue(1.f, 1.f, 1.f, 1.f);
    ValueType bgCol = ColorValue(0.f, 0.f, 0.f, 0.5f);
    ValueType hideModules = BoolValue(true);
};
