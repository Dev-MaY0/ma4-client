#pragma once
// Data model shared between the replay recorder (game side) and the replay viewer page of the iOS shell.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ios {

    struct ReplayEntity {
        uint32_t id = 0;
        uint32_t type = 0;
        bool player = false;
        float x = 0.f, y = 0.f, z = 0.f;
        float yaw = 0.f;
        std::wstring name;
    };

    struct ReplayFrame {
        float t = 0.f; // seconds since the recording started
        float px = 0.f, py = 0.f, pz = 0.f;
        float yaw = 0.f, pitch = 0.f;
        std::vector<ReplayEntity> entities;
    };

    struct ReplayMarker {
        float t = 0.f;
        float x = 0.f, z = 0.f;
    };

    /// A 16x16 block column map of the surface around the player (top block colour + height per column).
    struct ReplayTerrainChunk {
        static constexpr int16_t kUnknown = -32768;

        int32_t cx = 0, cz = 0; // chunk coordinates (block >> 4)
        std::array<uint8_t, 256 * 3> rgb {};
        std::array<int16_t, 256> height {};

        ReplayTerrainChunk() { height.fill(kUnknown); }
    };

    struct ReplayClip {
        std::wstring title;
        float duration = 0.f;
        std::vector<ReplayFrame> frames;
        std::vector<ReplayMarker> markers;
        std::vector<ReplayTerrainChunk> terrain;
    };

    struct ReplayEntry {
        uint64_t id = 0;
        std::wstring title;
        std::wstring subtitle;
    };

    /// Everything the replay pages need from the host. Rebuilt every frame.
    struct ReplayHost {
        bool available = false;
        bool recording = false;
        float recordingSeconds = 0.f;
        int recordingMarkers = 0;
        std::vector<ReplayEntry> list;
        std::function<void()> toggleRecording;
        std::function<void()> addMarker;
        std::function<void(uint64_t)> remove;
        std::function<std::shared_ptr<ReplayClip>(uint64_t)> load;
    };

} // namespace ios
