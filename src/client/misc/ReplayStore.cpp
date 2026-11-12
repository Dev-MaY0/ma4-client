#include "pch.h"
#include "ReplayStore.h"
#include "client/Ma4.h"
#include "client/screen/ios/IosUi.h"
#include "client/event/events/LeaveGameEvent.h"
#include "client/event/events/TickEvent.h"
#include "mc/common/world/actor/Actor.h"
#include "mc/common/entity/component/ActorTypeComponent.h"
#include "mc/common/entity/component/RuntimeIDComponent.h"
#include "mc/common/world/Minecraft.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>

namespace {
    constexpr uint32_t kMagic = 0x5052344Du; // "M4RP"
    constexpr uint32_t kVersion = 2; // 2 = adds the terrain map section
    constexpr uint32_t kItemEntityType = 64;

    template<typename T>
    void put(std::vector<uint8_t>& buf, T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        size_t at = buf.size();
        buf.resize(at + sizeof(T));
        std::memcpy(buf.data() + at, &value, sizeof(T));
    }

    struct Reader {
        std::vector<uint8_t> const& data;
        size_t pos = 0;
        bool ok = true;

        template<typename T>
        T get() {
            T v {};
            if (pos + sizeof(T) > data.size()) {
                ok = false;
                return v;
            }
            std::memcpy(&v, data.data() + pos, sizeof(T));
            pos += sizeof(T);
            return v;
        }

        std::string str(size_t n) {
            if (pos + n > data.size()) {
                ok = false;
                return {};
            }
            std::string s(reinterpret_cast<char const*>(data.data() + pos), n);
            pos += n;
            return s;
        }
    };

    std::wstring niceTitle(std::filesystem::path const& p) {
        // ma4-replay-YYYYMMDD-HHMMSS. (wstring on purpose: path::string() throws for names that are not representable
        // in the ANSI code page, e.g. a replay somebody copied into the folder)
        std::wstring stem = p.stem().wstring();
        auto pos = stem.rfind(L"replay-");
        if (pos != std::wstring::npos && stem.size() >= pos + 7 + 15) {
            std::wstring d = stem.substr(pos + 7, 8);
            std::wstring t = stem.substr(pos + 7 + 9, 6);
            return d.substr(0, 4) + L"-" + d.substr(4, 2) + L"-" + d.substr(6, 2) + L"  " + t.substr(0, 2) + L":" +
                   t.substr(2, 2) + L":" + t.substr(4, 2);
        }
        return stem;
    }

    std::wstring niceSize(uintmax_t bytes) {
        char buf[48];
        if (bytes >= 1024ull * 1024ull) {
            std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else {
            std::snprintf(buf, sizeof(buf), "%.0f KB", std::max(1.0, static_cast<double>(bytes) / 1024.0));
        }
        return util::StrToWStr(buf);
    }

    std::wstring niceDuration(float secs) {
        int s = static_cast<int>(secs);
        wchar_t b[32];
        swprintf(b, 32, L"%d:%02d", s / 60, s % 60);
        return b;
    }
} // namespace

ReplayStore& ReplayStore::get() {
    // intentionally leaked: the event system may already be gone when static destructors run on unload
    static ReplayStore* instance = new ReplayStore();
    return *instance;
}

ReplayStore::ReplayStore() {
    Eventing::get().listen<TickEvent>(this, (EventListenerFunc)&ReplayStore::onTick, 0, true);
    Eventing::get().listen<LeaveGameEvent>(this, (EventListenerFunc)&ReplayStore::onLeave, 0, true);
}

bool ReplayStore::shouldListen() {
    return !Ma4::get().isEjectQueued();
}

std::filesystem::path ReplayStore::directory() {
    return util::GetMa4Path() / "Replays";
}

namespace {
    // Runs fn(ctx) and turns an access violation into `false`. Kept free of objects with destructors so that
    // __try is allowed here; everything that allocates lives in the callee.
    bool runGuarded(void (*fn)(void*), void* ctx) {
        __try {
            fn(ctx);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
} // namespace

float ReplayStore::recordingSeconds() const {
    std::lock_guard lock(mutex);
    if (!recording) return 0.f;
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime).count();
}

bool ReplayStore::startRecording() {
    std::lock_guard lock(mutex);
    if (recording) return true;
    auto* ci = SDK::ClientInstance::get();
    if (!ci || !ci->getLocalPlayer()) return false;

    current = ios::ReplayClip {};
    terrainRec.reset();
    current.frames.reserve(4096);
    startTime = std::chrono::steady_clock::now();
    entitiesFaulted = false;
    recording = true;
    Logger::Info("Replay: recording started (entities: {}, map: {})", config.recordEntities, config.recordTerrain);
    return true;
}

void ReplayStore::stopRecording(bool save) {
    std::lock_guard lock(mutex);
    if (!recording) return;
    recording = false;
    if (save && current.frames.size() >= 2) {
        current.duration = current.frames.back().t;
        current.terrain = terrainRec.snapshot();
        if (saveCurrent()) {
            Ma4::getNotifications().push(LocalizeString::get("client.ui.replay.saved").value());
        }
        listDirty = true;
    }
    current = ios::ReplayClip {};
    terrainRec.reset();
}

void ReplayStore::toggleRecording() {
    if (recording) {
        stopRecording(true);
    } else if (startRecording()) {
        Ma4::getNotifications().push(LocalizeString::get("client.ui.replay.started").value());
    } else {
        Ma4::getNotifications().push(LocalizeString::get("client.ui.replay.notInWorld").value());
    }
}

void ReplayStore::addMarker() {
    std::lock_guard lock(mutex);
    if (!recording) return;
    auto* ci = SDK::ClientInstance::get();
    auto* lp = ci ? ci->getLocalPlayer() : nullptr;
    ios::ReplayMarker m;
    m.t = recordingSeconds();
    if (lp) {
        auto pos = lp->getPos();
        m.x = pos.x;
        m.z = pos.z;
    }
    current.markers.push_back(m);
    Ma4::getNotifications().push(LocalizeString::get("client.ui.replay.markerAdded").value());
}

void ReplayStore::onLeave(Event&) {
    if (recording) stopRecording(true);
}

void ReplayStore::onTick(Event& evG) {
    if (!recording) return;

    // The recorder only reads game memory. If a read ever faults, stop recording instead of taking the game down.
    struct Ctx {
        ReplayStore* self;
        Event* ev;
    } ctx { this, &evG };
    bool ok = runGuarded(
        [](void* p) {
            auto* c = static_cast<Ctx*>(p);
            c->self->tickImpl(*c->ev);
        },
        &ctx);
    if (!ok) {
        recording = false;
        stoppedByError = true;
        Logger::Warn("Replay: reading the game faulted while recording - the recording was stopped");
    }
}

bool ReplayStore::sampleEntities(SDK::Level* level, SDK::Actor* lp, float px, float pz, ios::ReplayFrame& fr) {
    struct Candidate {
        float dist2;
        SDK::Actor* actor;
    };
    std::vector<Candidate> nearby;
    for (auto* actor : level->getRuntimeActorList()) {
        if (!actor || actor == lp || !actor->stateVector) continue;
        auto* type = actor->tryGetComponent<SDK::ActorTypeComponent>();
        if (!type || type->type == kItemEntityType) continue;
        auto p = actor->stateVector->pos;
        float dx = p.x - px, dz = p.z - pz;
        float d2 = dx * dx + dz * dz;
        if (d2 > config.radius * config.radius) continue;
        nearby.push_back({ d2, actor });
    }
    std::sort(nearby.begin(), nearby.end(), [](Candidate const& a, Candidate const& b) { return a.dist2 < b.dist2; });
    if (nearby.size() > config.entityLimit) nearby.resize(config.entityLimit);

    // the name tag function is signature based - only call it if the signature resolved
    const bool canReadNames = Signatures::Actor_getNameTag.result != 0;

    for (auto& c : nearby) {
        auto* actor = c.actor;
        auto* type = actor->tryGetComponent<SDK::ActorTypeComponent>();
        auto* runtimeId = actor->tryGetComponent<SDK::RuntimeIDComponent>();
        if (!type) continue;

        ios::ReplayEntity e;
        auto p = actor->stateVector->pos;
        e.id = runtimeId ? static_cast<uint32_t>(runtimeId->runtimeID & 0xFFFFFFFFull) : 0u;
        e.type = type->type;
        e.player = type->type == 319;
        e.x = p.x;
        e.y = p.y;
        e.z = p.z;
        e.yaw = actor->actorRotation ? actor->actorRotation->rotation.y : 0.f;
        if (e.player && canReadNames) {
            std::wstring name = util::StrToWStr(actor->getNameTag());
            if (name.size() > 24) name.resize(24);
            e.name = std::move(name);
        }
        fr.entities.push_back(std::move(e));
    }
    return true;
}

void ReplayStore::tickImpl(Event&) {
    std::lock_guard lock(mutex);
    if (!recording) return;

    auto* ci = SDK::ClientInstance::get();
    auto* lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp || !lp->stateVector || !lp->actorRotation) return;

    float t = recordingSeconds();
    if (t > config.maxMinutes * 60.f) {
        stopRecording(true);
        return;
    }

    ios::ReplayFrame fr;
    fr.t = t;
    Vec3 pos = lp->stateVector->pos;
    Vec2 rot = lp->actorRotation->rotation;
    fr.px = pos.x;
    fr.py = pos.y;
    fr.pz = pos.z;
    fr.pitch = rot.x;
    fr.yaw = rot.y;

    // same way Hitboxes / WAILA get the level
    SDK::Level* level = ci->minecraft ? ci->minecraft->getLevel() : nullptr;

    if (config.recordEntities && level && !entitiesFaulted) {
        struct Ctx {
            ReplayStore* self;
            SDK::Level* level;
            SDK::Actor* lp;
            float px, pz;
            ios::ReplayFrame* frame;
        } ctx { this, level, lp, pos.x, pos.z, &fr };
        bool ok = runGuarded(
            [](void* p) {
                auto* c = static_cast<Ctx*>(p);
                c->self->sampleEntities(c->level, c->lp, c->px, c->pz, *c->frame);
            },
            &ctx);
        if (!ok) {
            entitiesFaulted = true;
            fr.entities.clear();
            Logger::Warn("Replay: reading nearby entities faulted - entity recording is switched off for this recording");
        }
    }

    if (config.recordTerrain && !terrainRec.isDisabled()) {
        struct RegionCtx {
            SDK::ClientInstance* ci;
            SDK::BlockSource* region;
        } rctx { ci, nullptr };
        bool ok = runGuarded(
            [](void* p) {
                auto* c = static_cast<RegionCtx*>(p);
                c->region = c->ci->getRegion();
            },
            &rctx);
        if (ok && rctx.region) {
            TerrainRecorder::Config tc;
            tc.radiusChunks = config.terrainRadius;
            terrainRec.update(rctx.region, pos, tc);
            if (terrainRec.isDisabled()) {
                Logger::Warn("Replay: reading blocks faulted - the map is switched off for this recording");
            }
        } else if (!ok) {
            Logger::Warn("Replay: could not get the block source - the map is not recorded");
            config.recordTerrain = false;
        }
    }

    current.frames.push_back(std::move(fr));
}

bool ReplayStore::saveCurrent() {
    std::error_code ec;
    std::filesystem::create_directories(directory(), ec);

    std::time_t now = std::time(nullptr);
    std::tm tmv {};
    localtime_s(&tmv, &now);
    char name[96];
    std::strftime(name, sizeof(name), "ma4-replay-%Y%m%d-%H%M%S.ma4replay", &tmv);
    auto path = directory() / name;

    std::vector<uint8_t> buf;
    buf.reserve(1024 * 64);
    put<uint32_t>(buf, kMagic);
    put<uint32_t>(buf, kVersion);
    put<uint32_t>(buf, static_cast<uint32_t>(current.frames.size()));
    put<uint32_t>(buf, static_cast<uint32_t>(current.markers.size()));
    put<float>(buf, current.duration);
    for (auto& m : current.markers) {
        put<float>(buf, m.t);
        put<float>(buf, m.x);
        put<float>(buf, m.z);
    }
    for (auto& fr : current.frames) {
        put<float>(buf, fr.t);
        put<float>(buf, fr.px);
        put<float>(buf, fr.py);
        put<float>(buf, fr.pz);
        put<float>(buf, fr.yaw);
        put<float>(buf, fr.pitch);
        put<uint16_t>(buf, static_cast<uint16_t>(std::min<size_t>(fr.entities.size(), 0xFFFF)));
        for (size_t i = 0; i < fr.entities.size() && i < 0xFFFF; ++i) {
            auto& e = fr.entities[i];
            put<uint32_t>(buf, e.id);
            put<uint32_t>(buf, e.type);
            put<uint8_t>(buf, e.player ? 1 : 0);
            put<float>(buf, e.x);
            put<float>(buf, e.y);
            put<float>(buf, e.z);
            put<float>(buf, e.yaw);
            std::string nm = util::WStrToStr(e.name);
            if (nm.size() > 255) nm.resize(255);
            put<uint8_t>(buf, static_cast<uint8_t>(nm.size()));
            buf.insert(buf.end(), nm.begin(), nm.end());
        }
    }

    put<uint32_t>(buf, static_cast<uint32_t>(current.terrain.size()));
    for (auto const& chunk : current.terrain) {
        put<int32_t>(buf, chunk.cx);
        put<int32_t>(buf, chunk.cz);
        buf.insert(buf.end(), chunk.rgb.begin(), chunk.rgb.end());
        for (auto h : chunk.height) put<int16_t>(buf, h);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<char const*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    return out.good();
}

std::vector<ios::ReplayEntry> const& ReplayStore::list() {
    if (!listDirty) return cachedList;
    listDirty = false;
    cachedList.clear();
    files.clear();

    // Nothing in here may throw: it runs on the render thread every time the menu shows the list. The non-throwing
    // overloads are used throughout (a OneDrive placeholder or a file in use makes the throwing ones raise).
    std::error_code ec;
    std::vector<std::filesystem::path> paths;
    std::filesystem::directory_iterator dirIt(directory(), ec), dirEnd;
    for (; !ec && dirIt != dirEnd; dirIt.increment(ec)) {
        std::error_code entryEc;
        if (dirIt->is_regular_file(entryEc) && dirIt->path().extension() == L".ma4replay") paths.push_back(dirIt->path());
    }
    std::sort(paths.begin(), paths.end(), std::greater<>());

    for (auto const& p : paths) {
        ios::ReplayEntry e;
        e.id = ios::Ui::hashId(util::WStrToStr(p.filename().wstring()));
        e.title = niceTitle(p);

        float duration = 0.f;
        uint32_t markers = 0;
        {
            std::ifstream in(p, std::ios::binary);
            uint32_t header[4] = {};
            float dur = 0.f;
            if (in.read(reinterpret_cast<char*>(header), sizeof(header)) && in.read(reinterpret_cast<char*>(&dur), sizeof(dur)) &&
                header[0] == kMagic) {
                duration = dur;
                markers = header[3];
            }
        }
        std::wstring sub = niceDuration(duration) + L"  \u00B7  " + niceSize(std::filesystem::file_size(p, ec));
        if (markers > 0) sub += L"  \u00B7  \u2691 " + std::to_wstring(markers);
        e.subtitle = sub;
        cachedList.push_back(e);
        files.push_back({ e.id, p });
    }
    return cachedList;
}

std::shared_ptr<ios::ReplayClip> ReplayStore::load(uint64_t id) {
    list();
    for (auto const& f : files) {
        if (f.id != id) continue;

        std::ifstream in(f.path, std::ios::binary | std::ios::ate);
        if (!in) return nullptr;
        auto size = static_cast<size_t>(in.tellg());
        if (size < 20 || size > 512ull * 1024ull * 1024ull) return nullptr;
        std::vector<uint8_t> data(size);
        in.seekg(0);
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));

        Reader r { data };
        if (r.get<uint32_t>() != kMagic) return nullptr;
        uint32_t version = r.get<uint32_t>();
        if (version < 1 || version > kVersion) return nullptr;
        uint32_t frames = r.get<uint32_t>();
        uint32_t markers = r.get<uint32_t>();
        auto clip = std::make_shared<ios::ReplayClip>();
        clip->duration = r.get<float>();
        clip->title = niceTitle(f.path);
        if (frames > 4000000u || markers > 100000u) return nullptr;

        for (uint32_t i = 0; i < markers && r.ok; ++i) {
            ios::ReplayMarker m;
            m.t = r.get<float>();
            m.x = r.get<float>();
            m.z = r.get<float>();
            clip->markers.push_back(m);
        }
        clip->frames.reserve(frames);
        for (uint32_t i = 0; i < frames && r.ok; ++i) {
            ios::ReplayFrame fr;
            fr.t = r.get<float>();
            fr.px = r.get<float>();
            fr.py = r.get<float>();
            fr.pz = r.get<float>();
            fr.yaw = r.get<float>();
            fr.pitch = r.get<float>();
            uint16_t count = r.get<uint16_t>();
            for (uint16_t k = 0; k < count && r.ok; ++k) {
                ios::ReplayEntity e;
                e.id = r.get<uint32_t>();
                e.type = r.get<uint32_t>();
                e.player = r.get<uint8_t>() != 0;
                e.x = r.get<float>();
                e.y = r.get<float>();
                e.z = r.get<float>();
                e.yaw = r.get<float>();
                uint8_t n = r.get<uint8_t>();
                e.name = util::StrToWStr(r.str(n));
                fr.entities.push_back(std::move(e));
            }
            if (r.ok) clip->frames.push_back(std::move(fr));
        }
        if (version >= 2 && r.ok) {
            uint32_t chunkCount = r.get<uint32_t>();
            if (chunkCount <= 200000u) {
                clip->terrain.reserve(chunkCount);
                for (uint32_t i = 0; i < chunkCount && r.ok; ++i) {
                    ios::ReplayTerrainChunk chunk;
                    chunk.cx = r.get<int32_t>();
                    chunk.cz = r.get<int32_t>();
                    if (r.pos + chunk.rgb.size() + chunk.height.size() * sizeof(int16_t) > data.size()) break;
                    std::memcpy(chunk.rgb.data(), data.data() + r.pos, chunk.rgb.size());
                    r.pos += chunk.rgb.size();
                    for (auto& h : chunk.height) h = r.get<int16_t>();
                    if (r.ok) clip->terrain.push_back(std::move(chunk));
                }
            }
        }
        if (clip->frames.size() < 2) return nullptr;
        return clip;
    }
    return nullptr;
}

void ReplayStore::remove(uint64_t id) {
    list();
    for (auto const& f : files) {
        if (f.id == id) {
            std::error_code ec;
            std::filesystem::remove(f.path, ec);
            break;
        }
    }
    listDirty = true;
}
