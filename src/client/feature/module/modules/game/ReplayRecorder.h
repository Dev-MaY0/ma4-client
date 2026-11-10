#pragma once
#include "../../Module.h"

// Hotkeys / options for the replay recorder (the recorder itself lives in ReplayStore).
class ReplayRecorder : public Module {
public:
    ReplayRecorder();

    void afterLoadConfig() override;

private:
    void applyConfig();
    void onKey(Event& ev);
    void onTick(Event& ev);

    ValueType recordKey = KeyValue(0);
    ValueType markerKey = KeyValue(0);
    ValueType maxMinutes = FloatValue(10.f);
    ValueType recordEntities = BoolValue(true);
    ValueType recordTerrain = BoolValue(true);
    ValueType terrainRadius = FloatValue(6.f);
    ValueType autoRecord = BoolValue(false);
};
