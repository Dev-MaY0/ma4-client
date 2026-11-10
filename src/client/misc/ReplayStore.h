#pragma once
// Replay data recorder / file store.
//
// While recording, the local player and the entities around it are sampled every game tick and kept in memory.
// Stopping the recording writes a compact binary ".ma4replay" file into the ma4 data folder. The iOS style
// "Replay" page lists those files and plays them back in a top-down viewer.
//
// This is a read-only data recorder: it does not re-create the world and does not touch any game state.

#include "client/event/Listener.h"
#include "client/misc/ReplayTerrain.h"
#include "client/screen/ios/IosReplay.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class ReplayStore final : public Listener {
public:
    struct Config {
        float maxMinutes = 10.f;
        bool recordEntities = true;
        bool recordTerrain = true;
        int terrainRadius = 3; // chunks
        float radius = 64.f;
        size_t entityLimit = 40;
    } config;

    static ReplayStore& get();

    [[nodiscard]] bool isRecording() const { return recording; }
    [[nodiscard]] float recordingSeconds() const;
    [[nodiscard]] int markerCount() const { return static_cast<int>(current.markers.size()); }

    /// Returns false when no world is loaded.
    bool startRecording();
    void stopRecording(bool save = true);
    void toggleRecording();
    void addMarker();

    std::vector<ios::ReplayEntry> const& list();
    std::shared_ptr<ios::ReplayClip> load(uint64_t id);
    void remove(uint64_t id);
    void markListDirty() { listDirty = true; }

    [[nodiscard]] bool shouldListen() override;

    [[nodiscard]] static std::filesystem::path directory();

private:
    ReplayStore();
    ReplayStore(ReplayStore const&) = delete;

    void onTick(class Event& ev);
    void onLeave(class Event& ev);
    bool saveCurrent();

    struct FileInfo {
        uint64_t id = 0;
        std::filesystem::path path;
    };

    bool recording = false;
    std::chrono::steady_clock::time_point startTime;
    ios::ReplayClip current;
    TerrainRecorder terrainRec;

    bool listDirty = true;
    std::vector<ios::ReplayEntry> cachedList;
    std::vector<FileInfo> files;
};
