#include "pch.h"
#include "ReplayTerrain.h"
#include "util/Logger.h"

#include "mc/common/world/level/BlockSource.h"
#include "mc/common/world/level/block/Block.h"
#include "mc/common/world/level/block/BlockLegacy.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_set>

// ===================================================================================================================
// block name -> map colour
//
// The game does not tell us a colour, so it is derived from the block id. Vanilla blocks are matched by tables
// (most specific token first). Blocks of other namespaces - the add-on / custom blocks servers such as Hive ship - and
// vanilla blocks added after this table was written fall through to word based guesses (material and colour words in
// the id) and, as a last resort, a stable colour derived from the id so different blocks stay distinguishable.
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

        // `w` as a whole underscore separated word ("jack_o_lantern" has the words jack, o, lantern)
        bool hasWord(std::string const& n, char const* w) {
            const size_t len = std::strlen(w);
            size_t pos = 0;
            while ((pos = n.find(w, pos)) != std::string::npos) {
                const bool left = pos == 0 || n[pos - 1] == '_';
                const bool right = pos + len == n.size() || n[pos + len] == '_';
                if (left && right) return true;
                pos += 1;
            }
            return false;
        }

        bool anyWord(std::string const& n, std::initializer_list<char const*> words) {
            for (auto* w : words) {
                if (hasWord(n, w)) return true;
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
            { "dirt_path", { 148, 122, 65 } },
            { "grass_path", { 148, 122, 65 } },
            { "podzol", { 91, 63, 24 } },
            { "mycelium", { 111, 98, 102 } },
            { "pale_moss", { 150, 160, 120 } },
            { "moss", { 89, 109, 45 } },
            { "coarse_dirt", { 119, 85, 59 } },
            { "dirt_with_roots", { 144, 103, 76 } },
            { "rooted_dirt", { 144, 103, 76 } },
            { "farmland", { 110, 75, 42 } },
            { "muddy_mangrove_roots", { 70, 58, 40 } },
            { "mangrove_roots", { 116, 90, 56 } },
            { "mud_brick", { 137, 104, 79 } },
            { "packed_mud", { 142, 106, 79 } },
            { "mud", { 60, 57, 60 } },
            { "dirt", { 134, 96, 67 } },
            { "hardened_clay", { 152, 94, 67 } },
            { "clay", { 160, 166, 179 } },
            { "red_sandstone", { 181, 97, 31 } },
            { "sandstone", { 216, 203, 155 } },
            { "red_sand", { 190, 102, 33 } },
            { "soul_sand", { 81, 62, 50 } },
            { "soul_soil", { 75, 57, 46 } },
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
            { "smooth_basalt", { 72, 72, 78 } },
            { "basalt", { 73, 72, 77 } },
            { "crying_obsidian", { 32, 10, 60 } },
            { "obsidian", { 21, 18, 30 } },
            { "bedrock", { 85, 85, 85 } },
            { "nether_gold_ore", { 111, 54, 53 } },
            { "nether_quartz_ore", { 111, 54, 53 } },
            { "quartz_ore", { 111, 54, 53 } },
            { "red_nether_brick", { 69, 7, 9 } },
            { "nether_brick", { 44, 21, 26 } },
            { "netherrack", { 97, 38, 38 } },
            { "crimson_nylium", { 151, 33, 33 } },
            { "warped_nylium", { 22, 119, 121 } },
            { "nether_wart_block", { 114, 2, 2 } },
            { "warped_wart_block", { 22, 119, 121 } },
            { "blackstone", { 42, 36, 41 } },
            { "end_stone", { 219, 222, 158 } },
            { "magma", { 142, 63, 31 } },
            { "glowstone", { 171, 131, 84 } },
            { "ore", { 125, 125, 125 } },
            { "stone", { 125, 125, 125 } },
        };

        const Named kColorWords[] = {
            { "light_blue", { 58, 175, 217 } }, { "light_gray", { 142, 142, 134 } }, { "magenta", { 189, 68, 179 } },
            { "orange", { 240, 118, 19 } },     { "yellow", { 248, 197, 39 } },      { "lime", { 112, 185, 25 } },
            { "pink", { 237, 141, 172 } },      { "gray", { 62, 68, 71 } },          { "grey", { 62, 68, 71 } },
            { "cyan", { 21, 137, 145 } },       { "purple", { 121, 42, 172 } },      { "blue", { 53, 57, 157 } },
            { "brown", { 114, 71, 40 } },       { "green", { 84, 109, 27 } },        { "red", { 160, 39, 34 } },
            { "black", { 20, 21, 25 } },        { "white", { 233, 236, 236 } },
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
            { "red_mushroom_block", { 200, 46, 45 } },
            { "brown_mushroom_block", { 149, 111, 81 } },
            { "mushroom_stem", { 207, 206, 198 } },
            { "dead_", { 130, 123, 119 } },
            { "tube_coral", { 49, 87, 206 } },
            { "brain_coral", { 207, 91, 159 } },
            { "bubble_coral", { 165, 26, 162 } },
            { "fire_coral", { 167, 26, 32 } },
            { "horn_coral", { 216, 199, 66 } },
            { "chiseled_bookshelf", { 162, 130, 78 } },
            { "bookshelf", { 162, 130, 78 } },
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
            { "raw_copper", { 154, 105, 79 } },
            { "raw_iron", { 166, 135, 107 } },
            { "raw_gold", { 221, 169, 46 } },
            { "oxidized", { 82, 162, 132 } },
            { "weathered", { 108, 153, 110 } },
            { "exposed", { 161, 125, 103 } },
            { "copper", { 192, 107, 79 } },
            { "amethyst", { 133, 97, 191 } },
            { "sculk", { 20, 40, 46 } },
            { "tinted_glass", { 44, 38, 46 } },
            { "froglight", { 247, 232, 190 } },
            { "ochre", { 247, 232, 190 } },
            { "verdant", { 205, 238, 200 } },
            { "pearlescent", { 245, 228, 240 } },
            { "honeycomb", { 229, 148, 29 } },
            { "honey", { 251, 185, 52 } },
            { "dried_kelp", { 50, 58, 38 } },
            { "tnt", { 219, 68, 26 } },
            { "lodestone", { 147, 149, 152 } },
            { "end_portal_frame", { 91, 120, 97 } },
            { "beacon", { 116, 221, 215 } },
            { "resin", { 219, 92, 41 } },
            { "creaking_heart", { 96, 72, 56 } },
            { "hay_block", { 166, 139, 12 } },
            { "pumpkin", { 198, 118, 24 } },
            { "jack_o_lantern", { 214, 152, 52 } },
            { "melon", { 111, 145, 31 } },
            { "cactus", { 85, 127, 43 } },
            { "sponge", { 195, 192, 74 } },
            { "glass", { 175, 213, 219 } },
            { "shroomlight", { 240, 146, 70 } },
            { "sea_lantern", { 172, 199, 190 } },
            { "bone_block", { 229, 225, 207 } },
            { "slime", { 111, 192, 91 } },
            { "netherite", { 66, 61, 63 } },
            { "ancient_debris", { 94, 66, 58 } },
            { "enchanting_table", { 120, 40, 50 } },
            { "note_block", { 104, 70, 48 } },
            { "jukebox", { 104, 70, 48 } },
            { "composter", { 112, 84, 52 } },
            { "barrel", { 134, 100, 60 } },
            { "scaffolding", { 172, 146, 80 } },
            { "campfire", { 140, 98, 54 } },
            { "cake", { 252, 246, 238 } },
            { "bed", { 160, 39, 34 } },
            { "furnace", { 120, 120, 120 } },
            { "smoker", { 112, 102, 92 } },
            { "dispenser", { 120, 120, 120 } },
            { "dropper", { 120, 120, 120 } },
            { "observer", { 100, 100, 100 } },
            { "piston", { 150, 130, 90 } },
            { "hopper", { 70, 70, 74 } },
            { "cauldron", { 70, 70, 74 } },
            { "anvil", { 68, 68, 68 } },
            { "grindstone", { 140, 140, 140 } },
            { "stonecutter", { 125, 120, 115 } },
            { "decorated_pot", { 140, 80, 60 } },
            { "target", { 226, 170, 163 } },
            { "crafting_table", { 162, 130, 78 } },
            { "table", { 150, 120, 78 } },
            { "lectern", { 162, 130, 78 } },
            { "loom", { 150, 120, 80 } },
            { "command_block", { 160, 120, 90 } },
            { "cobweb", { 220, 220, 220 } },
            { "web", { 220, 220, 220 } },
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

        template<size_t N>
        bool lookupWord(std::string const& n, Named const (&table)[N], Classified& c) {
            for (auto const& e : table) {
                if (hasWord(n, e.token)) {
                    c.rgb[0] = e.rgb.r;
                    c.rgb[1] = e.rgb.g;
                    c.rgb[2] = e.rgb.b;
                    return true;
                }
            }
            return false;
        }

        void set(Classified& c, int r, int g, int b) {
            c.rgb[0] = static_cast<uint8_t>(r);
            c.rgb[1] = static_cast<uint8_t>(g);
            c.rgb[2] = static_cast<uint8_t>(b);
        }

        // blocks that are invisible or have no surface of their own, in every namespace
        bool isInvisible(std::string const& n) {
            if (n.empty() || n == "air" || n == "light_block" || n == "light" || n == "structure_void" || n == "barrier" ||
                n == "border_block" || n == "moving_block" || n == "unknown" || n == "invisible_bedrock" || n == "allow" ||
                n == "deny" || n == "info_update" || n == "info_update2" || n == "client_request_placeholder_block" ||
                n == "reserved6" || n == "update_block" || n == "camera" || n == "bubble_column" || n == "end_gateway" ||
                n == "end_portal" || n == "portal" || n == "nether_portal" || n.rfind("light_block", 0) == 0) {
                return true;
            }
            return endsWith(n, "_air");
        }

        // vanilla blocks that are thin or plant-like: the map looks through them to the ground below
        bool isThinVanilla(std::string const& n) {
            if (n == "short_grass" || n == "tall_grass" || n == "grass" || n == "tallgrass" || n == "double_plant" ||
                n == "dead_bush" || n == "deadbush" || n == "fire" || n == "soul_fire" || n == "lantern" ||
                n == "soul_lantern" || n == "chain" || n == "iron_chain" || n == "bell" || n == "bamboo" ||
                n == "bamboo_sapling" || n == "nether_wart" || n == "melon_stem" || n == "pumpkin_stem" ||
                n == "attached_melon_stem" || n == "attached_pumpkin_stem" || n == "hanging_roots" ||
                n == "pale_hanging_moss" || n == "sweet_berry_bush" || n == "turtle_egg" || n == "cocoa" ||
                n == "brewing_stand" || n == "flower_pot" || n == "end_rod" || n == "lightning_rod" ||
                n == "chorus_plant" || n == "chorus_flower" || n == "item_frame" || n == "glow_frame") {
                return true;
            }
            if (anyWord(n, { "sapling", "propagule", "flower", "flowers", "tulip", "orchid", "allium", "bluet", "cornflower",
                             "dandelion", "poppy", "oxeye", "daisy", "rose", "peony", "lilac", "lily", "pitcher", "blossom",
                             "eyeblossom", "wildflowers", "torchflower", "vine", "vines", "kelp", "seagrass", "reeds",
                             "cane", "waterlily", "fern", "sprouts", "roots", "fungus", "lichen", "litter", "dripleaf", "bush", "torch",
                             "button", "rail", "tripwire", "wire", "repeater", "comparator", "lever", "carrots", "potatoes",
                             "wheat", "beetroot", "beetroots", "ladder", "sign", "banner", "skull", "head", "bud", "cluster",
                             "frogspawn", "spawn", "pickle", "pointed", "plate" })) {
                // "dripstone cluster" does not exist, but a "roots" block that is a full block does
                if (hasWord(n, "roots") && (has(n, "mangrove") || has(n, "dirt"))) return false;
                return true;
            }
            if (hasWord(n, "mushroom") && !hasWord(n, "block") && !hasWord(n, "stem")) return true;
            if (hasWord(n, "coral") && !hasWord(n, "block")) return true;
            if (hasWord(n, "azalea") && !hasWord(n, "leaves") && !hasWord(n, "block")) return true;
            if (hasWord(n, "candle") && !hasWord(n, "cake")) return true;
            if (hasWord(n, "glow") && hasWord(n, "lichen")) return true;
            return false;
        }

        // add-on blocks are mostly full blocks: only skip what is clearly not one
        bool isThinAddon(std::string const& n) {
            return anyWord(n, { "torch", "flame", "sapling", "flower", "flowers", "vine", "vines", "fern", "sign", "banner",
                                "chain", "rope", "lantern", "candle", "button", "lever", "rail", "ladder", "particle",
                                "marker", "hitbox", "collision", "invisible", "air" });
        }

        void fallbackColor(std::string const& n, Classified& c) {
            c.guessed = true;
            // 1. a colour word in the id ("red_tile", "ruby_ore" has none): terracotta tones for stone-like blocks
            if (lookupWord(n, kColorWords, c)) return;

            // 2. a material word
            if (anyWord(n, { "leaf", "leaves", "foliage", "hedge", "bush" })) return set(c, 52, 120, 36);
            if (anyWord(n, { "grass", "turf", "lawn" })) return set(c, 88, 148, 56);
            if (anyWord(n, { "planks", "plank", "wood", "log", "timber", "board", "parquet" })) return set(c, 162, 130, 78);
            if (anyWord(n, { "brick", "bricks", "tile", "tiles" })) return set(c, 150, 97, 83);
            if (anyWord(n, { "dirt", "soil", "earth", "mud" })) return set(c, 134, 96, 67);
            if (anyWord(n, { "sand", "beach", "desert" })) return set(c, 219, 207, 142);
            if (anyWord(n, { "snow", "ice", "frost", "frozen" })) return set(c, 230, 240, 250);
            if (anyWord(n, { "gold", "golden" })) return set(c, 246, 208, 61);
            if (anyWord(n, { "iron", "steel", "metal", "chrome", "silver" })) return set(c, 200, 200, 200);
            if (anyWord(n, { "copper", "bronze" })) return set(c, 192, 107, 79);
            if (anyWord(n, { "glass", "crystal" })) return set(c, 175, 213, 219);
            if (anyWord(n, { "lava", "magma", "ember" })) return set(c, 232, 98, 20);
            if (anyWord(n, { "water", "sea", "ocean" })) return set(c, 52, 98, 214);
            if (anyWord(n, { "concrete", "cement", "asphalt", "road", "pavement", "slab", "stone", "rock", "cobble", "granite",
                             "marble", "ore", "block" })) {
                return set(c, 125, 125, 125);
            }

            // 3. a stable colour derived from the id: different unknown blocks stay distinguishable
            uint32_t h = 2166136261u;
            for (unsigned char ch : n) {
                h ^= ch;
                h *= 16777619u;
            }
            c.unknown = true;
            set(c, 100 + static_cast<int>(h & 0x6F), 100 + static_cast<int>((h >> 8) & 0x6F), 100 + static_cast<int>((h >> 16) & 0x6F));
        }
    } // namespace

    Classified classifyName(std::string n) {
        Classified c;
        std::string ns;
        if (size_t colon = n.find(':'); colon != std::string::npos) {
            ns = n.substr(0, colon);
            n.erase(0, colon + 1);
        }
        for (auto& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (auto& ch : ns) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const bool vanilla = ns.empty() || ns == "minecraft";

        // --- invisible / thin things: look through them
        if (isInvisible(n)) {
            c.kind = 0;
            return c;
        }
        if (vanilla ? isThinVanilla(n) : isThinAddon(n)) {
            c.kind = 0;
            return c;
        }

        // --- liquids
        if (n == "water" || n == "flowing_water") {
            c.kind = 2;
            set(c, 52, 98, 214);
            return c;
        }
        if (n == "lava" || n == "flowing_lava") {
            c.kind = 2;
            set(c, 232, 98, 20);
            return c;
        }

        // --- terrain materials
        if (lookup(n, kSolid, c)) return c;

        // --- dyed blocks
        if (anyOf(n, { "wool", "concrete", "terracotta", "glazed", "stained_glass", "carpet", "shulker_box", "bed" })) {
            if (has(n, "terracotta") && !has(n, "glazed")) {
                if (lookupWord(n, kTerracotta, c)) return c;
                set(c, 152, 94, 67);
                return c;
            }
            if (lookupWord(n, kColorWords, c)) return c;
        }

        // --- leaves
        if (has(n, "leaves")) {
            if (has(n, "cherry")) {
                set(c, 232, 165, 190);
            } else if (has(n, "azalea")) {
                set(c, 101, 124, 47);
            } else if (has(n, "spruce")) {
                set(c, 54, 90, 54);
            } else if (has(n, "birch")) {
                set(c, 96, 140, 60);
            } else if (has(n, "mangrove")) {
                set(c, 90, 130, 40);
            } else if (has(n, "pale_oak")) {
                set(c, 120, 140, 110);
            } else {
                set(c, 52, 120, 36);
            }
            return c;
        }

        // --- wood
        if (anyOf(n, { "log", "_wood", "stem", "hyphae", "bamboo_block" })) {
            if (lookup(n, kLogs, c)) return c;
        }
        if (anyOf(n, { "planks", "slab", "stairs", "fence", "door", "trapdoor", "wood", "mosaic" })) {
            if (lookup(n, kTrees, c)) return c;
        }

        if (lookup(n, kMisc, c)) return c;

        // --- vanilla names that none of the tables know (new blocks) and every add-on block
        fallbackColor(n, c);
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
            const int R = 12;
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
    // block types are registered again per world / server (add-on blocks come and go): never trust old addresses
    infoCache.clear();
}

TerrainRecorder::BlockInfo const& TerrainRecorder::classify(SDK::BlockLegacy* legacy) {
    static const BlockInfo skip {};
    if (!legacy) return skip;

    // The cache is keyed by address. The game frees and re-creates block types when a world or server is joined, so an
    // address can later belong to a different block: the hash of the id is compared on every hit.
    const int64_t hash = legacy->namespacedId.hash;
    auto it = infoCache.find(legacy);
    if (it != infoCache.end() && it->second.nameHash == hash) return it->second;

    BlockInfo info;
    const std::string id = legacy->namespacedId.getString();
    auto result = terrain::classifyName(id);
    info.kind = result.kind == 0 ? Kind::Skip : (result.kind == 2 ? Kind::Liquid : Kind::Solid);
    std::memcpy(info.rgb, result.rgb, 3);
    info.nameHash = hash;

    // blocks the tables did not know: write them to the log (once each) so the tables can be extended
    if (result.guessed && loggedGuesses.size() < 96 && loggedGuesses.insert(id).second) {
        Logger::Info("Replay map: no colour table entry for block '{}' -> {} colour ({}, {}, {})", id,
                     result.unknown ? "derived" : "guessed", static_cast<int>(result.rgb[0]), static_cast<int>(result.rgb[1]),
                     static_cast<int>(result.rgb[2]));
    }

    if (infoCache.size() > 4096) infoCache.clear();
    return infoCache.insert_or_assign(legacy, info).first->second;
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
    const int yTop = std::clamp(py + 32, -64, 319);
    const int yBottom = std::clamp(py - 48, -64, 319);
    const int radius = std::clamp(cfg.radiusChunks, 1, 12);
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
            if (b.unknown > 8 && b.passes < 8 && now >= b.retryAt) {
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
                if (b.data.height[b.cursor] == ios::ReplayTerrainChunk::kUnknown) ++b.known;
            } else if (b.data.height[b.cursor] == ios::ReplayTerrainChunk::kUnknown) {
                ++b.unknown;
                // a chunk that has produced nothing after a couple of rows is almost certainly not loaded yet:
                // skip the rest of this pass instead of scanning 200+ empty columns (it is retried later)
                if (b.known == 0 && b.cursor >= 31) {
                    b.unknown = 256;
                    b.cursor = 255;
                }
            }
            ++b.cursor;
            --budget;
            if (disabled) return;
        }
        if (b.cursor >= 256) b.retryAt = now + std::chrono::seconds(3 * (b.passes + 1));
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
