#include "pch.h"
#include "ReplayTerrain.h"

#include "mc/common/world/level/BlockSource.h"
#include "mc/common/world/level/block/Block.h"
#include "mc/common/world/level/block/BlockLegacy.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

// ===================================================================================================================
// block name -> map colour
// ===================================================================================================================
namespace terrain {
    namespace {
        struct Rgb {
            uint8_t r, g, b;
        };

        bool has(std::string const& n, char const* token) {
            return n.find(token) != std::string::npos;
        }

        bool endsWith(std::string const& n, char const* suffix) {
            size_t len = std::strlen(suffix);
            return n.size() >= len && n.compare(n.size() - len, len, suffix) == 0;
        }

        bool anyOf(std::string const& n, std::initializer_list<char const*> tokens) {
            for (auto* t : tokens) {
                if (has(n, t)) return true;
            }
            return false;
        }

        struct Named {
            char const* token;
            Rgb rgb;
        };

        // first match wins -> keep more specific tokens first
        const Named kSolid[] = {
            { "grass_block", { 88, 148, 56 } },
            { "grass_path", { 148, 122, 65 } },
            { "dirt_path", { 148, 122, 65 } },
            { "podzol", { 91, 63, 24 } },
            { "mycelium", { 111, 98, 102 } },
            { "moss", { 89, 109, 45 } },
            { "coarse_dirt", { 119, 85, 59 } },
            { "rooted_dirt", { 144, 103, 76 } },
            { "farmland", { 110, 75, 42 } },
            { "dirt", { 134, 96, 67 } },
            { "mud", { 60, 57, 60 } },
            { "clay", { 160, 166, 179 } },
            { "red_sandstone", { 181, 97, 31 } },
            { "sandstone", { 216, 203, 155 } },
            { "red_sand", { 190, 102, 33 } },
            { "sand", { 219, 207, 142 } },
            { "gravel", { 131, 127, 126 } },
            { "powder_snow", { 249, 254, 254 } },
            { "snow", { 249, 254, 254 } },
            { "blue_ice", { 116, 167, 253 } },
            { "packed_ice", { 141, 180, 250 } },
            { "ice", { 145, 183, 253 } },
            { "deepslate", { 80, 80, 86 } },
            { "cobblestone", { 127, 127, 127 } },
            { "granite", { 149, 103, 86 } },
            { "diorite", { 188, 188, 188 } },
            { "andesite", { 136, 136, 136 } },
            { "tuff", { 108, 109, 102 } },
            { "calcite", { 223, 224, 220 } },
            { "dripstone", { 131, 107, 93 } },
            { "netherrack", { 97, 38, 38 } },
            { "crimson_nylium", { 151, 33, 33 } },
            { "warped_nylium", { 22, 119, 121 } },
            { "nether_wart_block", { 114, 2, 2 } },
            { "warped_wart_block", { 22, 119, 121 } },
            { "soul_sand", { 81, 62, 50 } },
            { "soul_soil", { 75, 57, 46 } },
            { "basalt", { 73, 72, 77 } },
            { "blackstone", { 42, 36, 41 } },
            { "end_stone", { 219, 222, 158 } },
            { "obsidian", { 21, 18, 30 } },
            { "bedrock", { 85, 85, 85 } },
            { "magma", { 142, 63, 31 } },
            { "glowstone", { 171, 131, 84 } },
            { "ore", { 125, 125, 125 } },
            { "stone", { 125, 125, 125 } },
        };

        const Named kColorWords[] = {
            { "light_blue", { 58, 175, 217 } }, { "light_gray", { 142, 142, 134 } }, { "magenta", { 189, 68, 179 } },
            { "orange", { 240, 118, 19 } },     { "yellow", { 248, 197, 39 } },      { "lime", { 112, 185, 25 } },
            { "pink", { 237, 141, 172 } },      { "gray", { 62, 68, 71 } },          { "cyan", { 21, 137, 145 } },
            { "purple", { 121, 42, 172 } },     { "blue", { 53, 57, 157 } },         { "brown", { 114, 71, 40 } },
            { "green", { 84, 109, 27 } },       { "red", { 160, 39, 34 } },          { "black", { 20, 21, 25 } },
            { "white", { 233, 236, 236 } },
        };

        const Named kTerracotta[] = {
            { "light_blue", { 113, 108, 137 } }, { "light_gray", { 135, 107, 98 } }, { "magenta", { 149, 88, 108 } },
            { "orange", { 161, 83, 37 } },       { "yellow", { 186, 133, 35 } },     { "lime", { 103, 117, 53 } },
            { "pink", { 161, 78, 78 } },         { "gray", { 57, 41, 35 } },         { "cyan", { 87, 91, 91 } },
            { "purple", { 118, 70, 86 } },       { "blue", { 74, 59, 91 } },         { "brown", { 77, 51, 35 } },
            { "green", { 76, 83, 42 } },         { "red", { 143, 61, 46 } },         { "black", { 37, 22, 16 } },
            { "white", { 209, 178, 161 } },
        };

        const Named kTrees[] = {
            { "dark_oak", { 66, 43, 20 } },   { "pale_oak", { 226, 218, 205 } }, { "oak", { 162, 130, 78 } },
            { "spruce", { 114, 84, 48 } },    { "birch", { 192, 175, 121 } },    { "jungle", { 160, 115, 81 } },
            { "acacia", { 168, 90, 50 } },    { "mangrove", { 117, 54, 48 } },   { "cherry", { 226, 178, 172 } },
            { "bamboo", { 193, 173, 80 } },   { "crimson", { 101, 48, 70 } },    { "warped", { 43, 104, 99 } },
        };

        const Named kLogs[] = {
            { "dark_oak", { 60, 46, 26 } },  { "pale_oak", { 220, 214, 205 } }, { "oak", { 109, 85, 50 } },
            { "spruce", { 58, 37, 16 } },    { "birch", { 216, 215, 210 } },    { "jungle", { 85, 67, 25 } },
            { "acacia", { 103, 96, 86 } },   { "mangrove", { 84, 38, 34 } },    { "cherry", { 53, 33, 40 } },
            { "bamboo", { 193, 173, 80 } },  { "crimson", { 92, 25, 29 } },     { "warped", { 58, 142, 140 } },
        };

        const Named kMisc[] = {
            { "brick", { 150, 97, 83 } },
            { "quartz", { 235, 229, 222 } },
            { "prismarine", { 99, 171, 158 } },
            { "purpur", { 169, 125, 169 } },
            { "iron_block", { 220, 220, 220 } },
            { "gold_block", { 246, 208, 61 } },
            { "diamond_block", { 98, 219, 214 } },
            { "emerald_block", { 42, 203, 87 } },
            { "lapis_block", { 31, 67, 140 } },
            { "redstone_block", { 170, 24, 5 } },
            { "coal_block", { 16, 15, 15 } },
            { "copper", { 192, 107, 79 } },
            { "hay_block", { 166, 139, 12 } },
            { "pumpkin", { 198, 118, 24 } },
            { "melon", { 111, 145, 31 } },
            { "cactus", { 85, 127, 43 } },
            { "sponge", { 195, 192, 74 } },
            { "bookshelf", { 162, 130, 78 } },
            { "glass", { 175, 213, 219 } },
            { "shroomlight", { 240, 146, 70 } },
            { "sea_lantern", { 172, 199, 190 } },
            { "bone_block", { 229, 225, 207 } },
            { "slime", { 111, 192, 91 } },
            { "honey", { 251, 185, 52 } },
            { "netherite", { 66, 61, 63 } },
            { "ancient_debris", { 94, 66, 58 } },
            { "crafting_table", { 162, 130, 78 } },
            { "cobweb", { 220, 220, 220 } },
        };

        template<size_t N>
        bool lookup(std::string const& n, Named const (&table)[N], Classified& c) {
            for (auto const& e : table) {
                if (has(n, e.token)) {
                    c.rgb[0] = e.rgb.r;
                    c.rgb[1] = e.rgb.g;
                    c.rgb[2] = e.rgb.b;
                    return true;
                }
            }
            return false;
        }

        bool isPlantLike(std::string const& n) {
            if (has(n, "grass") && !has(n, "grass_block") && !has(n, "grass_path")) return true;
            if (has(n, "flower") && !has(n, "leaves") && !has(n, "azalea")) return true;
            if (has(n, "azalea") && !has(n, "leaves") && !has(n, "block")) return true;
            if (has(n, "mushroom") && !has(n, "block") && !has(n, "stem")) return true;
            if (has(n, "coral") && !has(n, "coral_block")) return true;
            if (has(n, "fungus") || has(n, "roots") || has(n, "sprouts")) return true;
            return anyOf(n, { "fern", "tulip", "dandelion", "poppy", "orchid", "allium", "bluet", "cornflower",
                              "lily_of", "waterlily", "rose", "peony", "lilac", "sapling", "propagule", "vine",
                              "sugar_cane", "reeds", "kelp", "seagrass", "torch", "fire", "rail", "button", "lever",
                              "pressure_plate", "tripwire", "redstone_wire", "carrots", "potatoes", "wheat",
                              "beetroot", "sweet_berry", "candle", "lantern", "chain", "banner", "skull", "head",
                              "sign", "ladder", "pointed_dripstone", "amethyst_cluster", "amethyst_bud",
                              "glow_lichen", "lichen", "leaf_litter", "pitcher", "torchflower", "spore_blossom",
                              "double_plant", "tallgrass", "flower_pot", "item_frame", "bell", "lightning_rod",
                              "end_rod", "tall_" });
        }
    } // namespace

    Classified classifyName(std::string n) {
        Classified c;
        if (n.rfind("minecraft:", 0) == 0) n.erase(0, 10);

        // --- invisible / thin things: look through them
        if (n.empty() || n == "air" || endsWith(n, "_air") || n == "light_block" || n == "structure_void" ||
            n == "barrier" || n == "moving_block" || n == "unknown" || n == "invisible_bedrock") {
            c.kind = 0;
            return c;
        }
        if (isPlantLike(n)) {
            c.kind = 0;
            return c;
        }

        // --- liquids
        if (n == "water" || n == "flowing_water") {
            c.kind = 2;
            c.rgb[0] = 52, c.rgb[1] = 98, c.rgb[2] = 214;
            return c;
        }
        if (n == "lava" || n == "flowing_lava") {
            c.kind = 2;
            c.rgb[0] = 232, c.rgb[1] = 98, c.rgb[2] = 20;
            return c;
        }

        // --- terrain materials
        if (lookup(n, kSolid, c)) return c;

        // --- dyed blocks
        if (anyOf(n, { "wool", "concrete", "terracotta", "glazed", "stained_glass", "carpet", "shulker_box" })) {
            if (has(n, "terracotta") && !has(n, "glazed")) {
                if (lookup(n, kTerracotta, c)) return c;
                c.rgb[0] = 152, c.rgb[1] = 94, c.rgb[2] = 67;
                return c;
            }
            if (lookup(n, kColorWords, c)) return c;
        }

        // --- leaves
        if (has(n, "leaves")) {
            if (has(n, "cherry")) {
                c.rgb[0] = 232, c.rgb[1] = 165, c.rgb[2] = 190;
            } else if (has(n, "azalea")) {
                c.rgb[0] = 101, c.rgb[1] = 124, c.rgb[2] = 47;
            } else if (has(n, "spruce")) {
                c.rgb[0] = 54, c.rgb[1] = 90, c.rgb[2] = 54;
            } else if (has(n, "birch")) {
                c.rgb[0] = 96, c.rgb[1] = 140, c.rgb[2] = 60;
            } else if (has(n, "mangrove")) {
                c.rgb[0] = 90, c.rgb[1] = 130, c.rgb[2] = 40;
            } else {
                c.rgb[0] = 52, c.rgb[1] = 120, c.rgb[2] = 36;
            }
            return c;
        }

        // --- wood
        if (anyOf(n, { "log", "_wood", "stem", "hyphae" })) {
            if (lookup(n, kLogs, c)) return c;
        }
        if (anyOf(n, { "planks", "slab", "stairs", "fence", "door", "trapdoor", "plate", "wood" })) {
            if (lookup(n, kTrees, c)) return c;
        }

        if (lookup(n, kMisc, c)) return c;

        // --- unknown: stable pastel derived from the name so different blocks stay distinguishable
        uint32_t h = 2166136261u;
        for (unsigned char ch : n) {
            h ^= ch;
            h *= 16777619u;
        }
        c.rgb[0] = static_cast<uint8_t>(90 + (h & 0x7F));
        c.rgb[1] = static_cast<uint8_t>(90 + ((h >> 8) & 0x7F));
        c.rgb[2] = static_cast<uint8_t>(90 + ((h >> 16) & 0x7F));
        return c;
    }
} // namespace terrain

// ===================================================================================================================
// sampler
// ===================================================================================================================
namespace {
    int64_t chunkKey(int cx, int cz) {
        return (static_cast<int64_t>(cx) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(cz));
    }

    int floorDiv16(int v) {
        return v >> 4; // arithmetic shift == floor division for negatives
    }

    // chunk offsets sorted by distance, nearest chunks are filled first
    std::vector<std::pair<int, int>> const& offsetsByDistance() {
        static const std::vector<std::pair<int, int>> offsets = [] {
            std::vector<std::pair<int, int>> v;
            const int R = 8;
            for (int dz = -R; dz <= R; ++dz) {
                for (int dx = -R; dx <= R; ++dx) {
                    if (dx * dx + dz * dz <= R * R + R) v.emplace_back(dx, dz);
                }
            }
            std::sort(v.begin(), v.end(), [](auto const& a, auto const& b) {
                return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
            });
            return v;
        }();
        return offsets;
    }
} // namespace

void TerrainRecorder::reset() {
    chunks.clear();
}

TerrainRecorder::BlockInfo const& TerrainRecorder::classify(SDK::BlockLegacy* legacy) {
    static const BlockInfo skip {};
    if (!legacy) return skip;

    auto it = infoCache.find(legacy);
    if (it != infoCache.end()) return it->second;

    BlockInfo info;
    auto result = terrain::classifyName(legacy->namespacedId.getString());
    info.kind = result.kind == 0 ? Kind::Skip : (result.kind == 2 ? Kind::Liquid : Kind::Solid);
    std::memcpy(info.rgb, result.rgb, 3);
    if (infoCache.size() > 4096) infoCache.clear();
    return infoCache.emplace(legacy, info).first->second;
}

bool TerrainRecorder::sampleColumn(SDK::BlockSource* region, int x, int z, int yTop, int yBottom, uint8_t rgb[3],
                                   int16_t& height) {
    for (int y = yTop; y >= yBottom; --y) {
        SDK::Block* block = region->getBlock(BlockPos(x, y, z));
        if (!block) continue;
        BlockInfo const info = classify(block->legacyBlock);
        if (info.kind == Kind::Skip) continue;

        if (info.kind == Kind::Liquid) {
            int depth = 1;
            for (int yy = y - 1; yy >= yBottom && depth < 14; --yy) {
                SDK::Block* below = region->getBlock(BlockPos(x, yy, z));
                if (!below || classify(below->legacyBlock).kind != Kind::Liquid) break;
                ++depth;
            }
            float f = 1.f - static_cast<float>(std::min(depth, 12)) * 0.035f;
            for (int i = 0; i < 3; ++i) rgb[i] = static_cast<uint8_t>(std::clamp(info.rgb[i] * f, 0.f, 255.f));
        } else {
            std::memcpy(rgb, info.rgb, 3);
        }
        height = static_cast<int16_t>(y);
        return true;
    }
    return false;
}

bool TerrainRecorder::sampleColumnGuarded(SDK::BlockSource* region, int x, int z, int yTop, int yBottom,
                                           uint8_t rgb[3], int16_t& height) {
    // No objects with destructors in this function: it only forwards to sampleColumn and survives an access violation.
    __try {
        return sampleColumn(region, x, z, yTop, yBottom, rgb, height);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        disabled = true;
        return false;
    }
}

void TerrainRecorder::update(SDK::BlockSource* region, Vec3 const& playerPos, Config const& cfg) {
    if (!region || disabled) return;

    const int px = static_cast<int>(std::floor(playerPos.x));
    const int py = static_cast<int>(std::floor(playerPos.y));
    const int pz = static_cast<int>(std::floor(playerPos.z));
    const int pcx = floorDiv16(px), pcz = floorDiv16(pz);
    const int yTop = std::clamp(py + 24, -64, 319);
    const int yBottom = std::clamp(py - 48, -64, 319);
    const int radius = std::clamp(cfg.radiusChunks, 1, 8);
    const auto now = std::chrono::steady_clock::now();

    int budget = std::max(16, cfg.columnBudget);
    for (auto const& [dx, dz] : offsetsByDistance()) {
        if (dx * dx + dz * dz > radius * radius + radius) break; // sorted: everything after is farther
        const int cx = pcx + dx, cz = pcz + dz;
        auto [it, inserted] = chunks.try_emplace(chunkKey(cx, cz));
        Build& b = it->second;
        if (inserted) {
            b.data.cx = cx;
            b.data.cz = cz;
        }

        if (b.cursor >= 256) {
            // finished; a few columns may have been unloaded the first time, try again a couple of times
            if (b.unknown > 40 && b.passes < 3 && now >= b.retryAt) {
                b.cursor = 0;
                b.unknown = 0;
                ++b.passes;
            } else {
                continue;
            }
        }

        while (b.cursor < 256 && budget > 0) {
            const int lx = b.cursor & 15, lz = b.cursor >> 4;
            uint8_t rgb[3];
            int16_t h;
            if (sampleColumnGuarded(region, cx * 16 + lx, cz * 16 + lz, yTop, yBottom, rgb, h)) {
                std::memcpy(&b.data.rgb[b.cursor * 3], rgb, 3);
                b.data.height[b.cursor] = h;
            } else if (b.data.height[b.cursor] == ios::ReplayTerrainChunk::kUnknown) {
                ++b.unknown;
            }
            ++b.cursor;
            --budget;
            if (disabled) return;
        }
        if (b.cursor >= 256) b.retryAt = now + std::chrono::seconds(4);
        if (budget <= 0) break;
    }
}

std::vector<ios::ReplayTerrainChunk> TerrainRecorder::snapshot() const {
    std::vector<ios::ReplayTerrainChunk> out;
    out.reserve(chunks.size());
    for (auto const& [key, b] : chunks) {
        bool any = false;
        for (auto h : b.data.height) {
            if (h != ios::ReplayTerrainChunk::kUnknown) {
                any = true;
                break;
            }
        }
        if (any) out.push_back(b.data);
    }
    std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) {
        return a.cz != b.cz ? a.cz < b.cz : a.cx < b.cx;
    });
    return out;
}
