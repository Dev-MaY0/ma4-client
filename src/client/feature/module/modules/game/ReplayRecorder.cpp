#include "pch.h"
#include "ReplayRecorder.h"
#include "client/Ma4.h"
#include "client/event/events/KeyUpdateEvent.h"
#include "client/event/events/TickEvent.h"
#include "client/misc/ReplayStore.h"

ReplayRecorder::ReplayRecorder()
    : Module("Replay", LocalizeString::get("client.module.replay.name"), LocalizeString::get("client.module.replay.desc"),
             GAME, nokeybind) {
    addSetting("recordKey", LocalizeString::get("client.module.replay.recordKey.name"),
               LocalizeString::get("client.module.replay.recordKey.desc"), recordKey);
    addSetting("markerKey", LocalizeString::get("client.module.replay.markerKey.name"),
               LocalizeString::get("client.module.replay.markerKey.desc"), markerKey);
    auto maxSetting = addSliderSetting("maxMinutes", LocalizeString::get("client.module.replay.maxMinutes.name"),
                                       LocalizeString::get("client.module.replay.maxMinutes.desc"), maxMinutes,
                                       FloatValue(1.f), FloatValue(30.f), FloatValue(1.f));
    maxSetting->callback = [this](Setting&) {
        applyConfig();
    };
    auto entSetting = addSetting("recordEntities", LocalizeString::get("client.module.replay.recordEntities.name"),
                                 LocalizeString::get("client.module.replay.recordEntities.desc"), recordEntities);
    entSetting->callback = [this](Setting&) {
        applyConfig();
    };
    auto terSetting = addSetting("recordTerrain", LocalizeString::get("client.module.replay.recordTerrain.name"),
                                 LocalizeString::get("client.module.replay.recordTerrain.desc"), recordTerrain);
    terSetting->callback = [this](Setting&) {
        applyConfig();
    };
    auto radSetting = addSliderSetting("terrainRadius", LocalizeString::get("client.module.replay.terrainRadius.name"),
                                       LocalizeString::get("client.module.replay.terrainRadius.desc"), terrainRadius,
                                       FloatValue(1.f), FloatValue(8.f), FloatValue(1.f), "recordTerrain"_istrue);
    radSetting->callback = [this](Setting&) {
        applyConfig();
    };
    addSetting("autoRecord", LocalizeString::get("client.module.replay.autoRecord.name"),
               LocalizeString::get("client.module.replay.autoRecord.desc"), autoRecord);

    listen<KeyUpdateEvent>((EventListenerFunc)&ReplayRecorder::onKey);
    listen<TickEvent>((EventListenerFunc)&ReplayRecorder::onTick);
    applyConfig();
}

void ReplayRecorder::afterLoadConfig() {
    applyConfig();
}

void ReplayRecorder::applyConfig() {
    auto& cfg = ReplayStore::get().config;
    cfg.maxMinutes = std::clamp(std::get<FloatValue>(maxMinutes).value, 1.f, 30.f);
    cfg.recordEntities = std::get<BoolValue>(recordEntities).value;
    cfg.recordTerrain = std::get<BoolValue>(recordTerrain).value;
    cfg.terrainRadius = std::clamp(static_cast<int>(std::get<FloatValue>(terrainRadius).value), 1, 8);
}

void ReplayRecorder::onKey(Event& evG) {
    auto& ev = reinterpret_cast<KeyUpdateEvent&>(evG);
    if (!ev.isDown() || ev.inUI()) return;

    int key = ev.getKey();
    if (key != 0 && key == std::get<KeyValue>(recordKey).value) {
        applyConfig();
        ReplayStore::get().toggleRecording();
    } else if (key != 0 && key == std::get<KeyValue>(markerKey).value) {
        ReplayStore::get().addMarker();
    }
}

void ReplayRecorder::onTick(Event&) {
    if (!std::get<BoolValue>(autoRecord).value) return;
    auto& store = ReplayStore::get();
    if (!store.isRecording()) {
        applyConfig();
        store.startRecording();
    }
}
