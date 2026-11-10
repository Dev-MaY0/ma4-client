#pragma once
// Surface map sampler for the replay recorder.
//
// Around the player the top visible block of every column is sampled (a few hundred columns per tick, nearest chunks
// first) and turned into a colour + height. The result is stored per 16x16 chunk and later drawn as a top-down
// "map" behind the replay path. Read-only: it only asks the game's BlockSource which block is where.

#include "client/screen/ios/IosReplay.h"
#include "util/LMath.h"

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace SDK {
    class BlockSource;
    class BlockLegacy;
} // namespace SDK

class TerrainRecorder {
public:
    struct Config {
        int radiusChunks = 3;    // chunks around the player that are mapped
        int columnBudget = 160;  // columns sampled per tick
    };

    void reset();
    /// Samples a slice of the surface around `playerPos`. Call once per game tick.
    void update(SDK::BlockSource* region, Vec3 const& playerPos, Config const& cfg);
    /// Chunks that contain at least one known column.
    [[nodiscard]] std::vector<ios::ReplayTerrainChunk> snapshot() const;
    [[nodiscard]] size_t chunkCount() const { return chunks.size(); }

private:
    enum class Kind : uint8_t {
        Skip,
        Solid,
        Liquid
    };
    struct BlockInfo {
        Kind kind = Kind::Skip;
        uint8_t rgb[3] = { 128, 128, 128 };
    };
    struct Build {
        ios::ReplayTerrainChunk data;
        uint16_t cursor = 0;
        uint16_t unknown = 0;
        uint8_t passes = 0;
        std::chrono::steady_clock::time_point retryAt;
    };

    BlockInfo const& classify(SDK::BlockLegacy* legacy);
    bool sampleColumn(SDK::BlockSource* region, int x, int z, int yTop, int yBottom, uint8_t rgb[3], int16_t& height);

    std::unordered_map<int64_t, Build> chunks;
    std::unordered_map<const void*, BlockInfo> infoCache;
};

/// Block id ("grass_block", "oak_planks", ...) -> map colour. Exposed for tests / tooling.
namespace terrain {
    struct Classified {
        int kind = 1; // 0 skip, 1 solid, 2 liquid
        uint8_t rgb[3] = { 128, 128, 128 };
    };
    Classified classifyName(std::string name);
} // namespace terrain
