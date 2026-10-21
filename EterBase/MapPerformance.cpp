#include "StdAfx.h"
#include "MapPerformance.h"
#include "Debug.h"
#include <cstdarg>
#include <cstring>
#include <array>

namespace MapPerformance
{
namespace
{
constexpr unsigned EventCapacity = 192;
constexpr unsigned ReportEventLimit = 16;
constexpr unsigned CounterCount = static_cast<unsigned>(Counter::Count);
constexpr unsigned StageCapacity = 192;
constexpr unsigned StageLookupCapacity = 1024;
constexpr unsigned FrameStageLimit = 12;
constexpr unsigned SummaryStageLimit = 48;

std::int64_t Tick()
{
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

double Milliseconds(std::int64_t ticks)
{
    static const double scale = [] {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        return 1000.0 / static_cast<double>(frequency.QuadPart);
    }();
    return static_cast<double>(ticks) * scale;
}

enum class ProfileMode { Light, Sampled, Full, Compare };
ProfileMode Mode()
{
    static const ProfileMode mode = [] {
        char value[32]{};
        GetEnvironmentVariableA("M2_MAP_PROFILE", value, sizeof(value));
        if (!strcmp(value, "0") || !strcmp(value, "light")) return ProfileMode::Light;
        if (!strcmp(value, "1") || !strcmp(value, "full")) return ProfileMode::Full;
        if (!strcmp(value, "compare")) return ProfileMode::Compare;
        return ProfileMode::Sampled;
    }();
    return mode;
}
bool Enabled() { return Mode() != ProfileMode::Light; }

struct Pacing
{
    bool announced = false, compareFull = true, world = false;
    char frameMap[128]{}, map[128]{};
    unsigned startTick = 0, endTick = 0, processes = 0, rendered = 0, profiled = 0, intervals = 0;
    unsigned over17 = 0, over33 = 0, over50 = 0;
    bool crowdValid = false, crowdWindowValid = false;
    unsigned alive = 0, dead = 0, aliveMin = 0, aliveMax = 0, deadMax = 0;
    std::int64_t lastRender = 0;
    double work = 0, renderWork = 0, maxWork = 0, maxInterval = 0;
    std::array<double, 1024> durations{};
};
thread_local Pacing pacing;

const char* CurrentModeName()
{
    if (Mode() == ProfileMode::Compare) return pacing.compareFull ? "full" : "light";
    if (Mode() == ProfileMode::Full) return "full";
    return Mode() == ProfileMode::Light ? "light" : "sampled";
}
bool FullCapture()
{
    return Mode() == ProfileMode::Full || (Mode() == ProfileMode::Compare && pacing.compareFull);
}

void DumpPacing(unsigned now, bool advanceComparison)
{
    if (!pacing.processes || !pacing.startTick) return;
    std::sort(pacing.durations.begin(), pacing.durations.begin() + pacing.intervals);
    const auto percentile = [](unsigned percent) {
        if (!pacing.intervals) return 0.0;
        const unsigned rank = (pacing.intervals * percent + 99) / 100;
        return pacing.durations[(std::min)(pacing.intervals - 1, rank ? rank - 1 : 0)];
    };
    const unsigned elapsed = now - pacing.startTick;
    TraceError("[FRAME_PACING] pid=%u mode=%s map=\"%s\" elapsed_ms=%u process_frames=%u rendered_frames=%u profiled_frames=%u fps=%.2f interval_samples=%u interval_p50_ms=%.3f interval_p95_ms=%.3f interval_p99_ms=%.3f interval_max_ms=%.3f intervals_over_16_7=%u intervals_over_33_3=%u intervals_over_50=%u cpu_process_avg_ms=%.3f cpu_render_avg_ms=%.3f cpu_process_max_ms=%.3f alive_min=%u alive_max=%u dead_max=%u",
        GetCurrentProcessId(), CurrentModeName(), pacing.map, elapsed, pacing.processes, pacing.rendered, pacing.profiled,
        elapsed ? 1000.0 * pacing.rendered / elapsed : 0.0, pacing.intervals,
        percentile(50), percentile(95), percentile(99), pacing.maxInterval,
        pacing.over17, pacing.over33, pacing.over50, pacing.work / pacing.processes,
        pacing.rendered ? pacing.renderWork / pacing.rendered : 0.0, pacing.maxWork,
        pacing.crowdWindowValid ? pacing.aliveMin : 0,
        pacing.crowdWindowValid ? pacing.aliveMax : 0,
        pacing.crowdWindowValid ? pacing.deadMax : 0);
    if (advanceComparison && Mode() == ProfileMode::Compare) pacing.compareFull = !pacing.compareFull;
    pacing.processes = pacing.rendered = pacing.profiled = pacing.intervals = 0;
    pacing.over17 = pacing.over33 = pacing.over50 = 0;
    pacing.work = pacing.renderWork = pacing.maxWork = pacing.maxInterval = 0;
    pacing.startTick = now;
    pacing.lastRender = 0;
    pacing.crowdWindowValid = false;
}

void FinishPacing(std::int64_t processStart, bool rendered, bool foreground, bool profiled)
{
    const auto end = Tick();
    const unsigned now = GetTickCount();
    if (!pacing.world || !foreground)
    {
        DumpPacing(pacing.endTick, false);
        pacing.startTick = 0; pacing.lastRender = 0;
        return;
    }
    if (strcmp(pacing.map, pacing.frameMap))
    {
        DumpPacing(pacing.endTick, false);
        strncpy_s(pacing.map, pacing.frameMap, _TRUNCATE);
        pacing.startTick = 0; pacing.lastRender = 0;
    }
    if (!pacing.startTick) pacing.startTick = now;
    pacing.endTick = now;
    if (pacing.crowdValid)
    {
        if (!pacing.crowdWindowValid)
        {
            pacing.aliveMin = pacing.aliveMax = pacing.alive; pacing.deadMax = pacing.dead;
            pacing.crowdWindowValid = true;
        }
        else
        {
            pacing.aliveMin = (std::min)(pacing.aliveMin, pacing.alive);
            pacing.aliveMax = (std::max)(pacing.aliveMax, pacing.alive);
            pacing.deadMax = (std::max)(pacing.deadMax, pacing.dead);
        }
    }
    const double work = Milliseconds(end - processStart);
    ++pacing.processes; pacing.profiled += profiled;
    pacing.work += work; pacing.maxWork = (std::max)(pacing.maxWork, work);
    if (rendered)
    {
        ++pacing.rendered; pacing.renderWork += work;
        if (pacing.lastRender)
        {
            const double duration = Milliseconds(end - pacing.lastRender);
            pacing.maxInterval = (std::max)(pacing.maxInterval, duration);
            pacing.over17 += duration > 16.7; pacing.over33 += duration > 33.3; pacing.over50 += duration > 50;
            if (pacing.intervals < pacing.durations.size()) pacing.durations[pacing.intervals++] = duration;
        }
        pacing.lastRender = end;
    }
    if (now - pacing.startTick >= 5000) DumpPacing(now, true);
}

struct Event
{
    const char* label = nullptr;
    double total = 0;
    double self = 0;
    char detail[224]{};
};

struct StageWork
{
    std::uint64_t calls = 0;
    std::int64_t total = 0, self = 0, maxCall = 0;
};

struct StageSummary
{
    StageWork work;
    unsigned frames = 0, over17Frames = 0, over33Frames = 0;
    std::int64_t over17Self = 0, over33Self = 0, maxFrame = 0;
};

struct StageLookup
{
    const char* label = nullptr;
    unsigned index = StageCapacity;
};

struct Capture
{
    std::uint64_t id = 0;
    unsigned tick = 0;
    double work = 0;
    double gap = 0;
    bool world = false;
    bool foreground = false;
    bool rendered = false;
    char map[128]{};
    float x = 0, y = 0, z = 0;
    int sectorX = -1, sectorY = -1;
    unsigned counters[CounterCount]{};
    unsigned eventCount = 0;
    unsigned dropped = 0;
    unsigned stageDrops = 0;
    StageWork stages[StageCapacity];
    Event events[EventCapacity];
};

struct State
{
    bool active = false;
    bool announced = false;
    Scope* current = nullptr;
    std::int64_t start = 0;
    std::int64_t lastEnd = 0;
    std::uint64_t frameId = 0;
    Capture frame;
    Capture pending;
    unsigned lastReport = 0;
    unsigned lastSummary = 0;
    unsigned frames = 0, over17 = 0, over33 = 0, over100 = 0;
    unsigned suppressedHitches = 0;
    std::uint64_t counters[CounterCount]{};
    double totalWork = 0, maxWork = 0, maxGap = 0;
    double lastFlush = 0;
    unsigned stageCount = 0;
    const char* stageLabels[StageCapacity]{};
    StageLookup stageLookup[StageLookupCapacity];
    StageSummary stages[StageCapacity];
    unsigned stageDrops = 0, renderedFrames = 0, over17Rendered = 0;
    std::uint64_t firstFrame = 0, lastFrame = 0;
    unsigned firstTick = 0, lastTick = 0, mapChanges = 0;
    char lastMap[128]{};
};

// Capture is owned by the Process() thread. Worker resource loads do not write
// into it or contend with it. Fixed arrays avoid profiler allocations per frame.
thread_local State state;

constexpr unsigned DetailCapacity = 1024;
constexpr unsigned DetailLookupCapacity = 2048;
constexpr unsigned DetailCounterCount = static_cast<unsigned>(DetailCounter::Count);
constexpr unsigned DetailSampleInterval = 17;

struct DetailWork
{
    std::uint64_t calls = 0;
    unsigned frames = 0;
    std::int64_t total = 0, maxCall = 0, maxFrame = 0;
    std::uint64_t counters[CounterCount]{};
    std::uint64_t detail[DetailCounterCount]{};
};

struct DetailRow
{
    char kind[16]{}, phase[32]{}, pass[32]{}, asset[260]{}, name[96]{}, meshName[96]{};
    int mesh = -1, meshType = -1, materialType = -1, vertices = 0;
    unsigned hash = 0;
    std::uint64_t frameId = 0;
    DetailWork frame, window;
};

struct DetailState
{
    bool sampled = false;
    const char* pass = "unspecified";
    const char* effectAsset = nullptr;
    unsigned count = 0, touchedCount = 0, dropped = 0;
    unsigned sampledUpdates = 0, sampledRenders = 0;
    unsigned lookup[DetailLookupCapacity]{}; // row + 1, zero is empty
    unsigned touched[DetailCapacity]{};
    DetailRow rows[DetailCapacity];
    std::uint64_t running[DetailCounterCount]{};
};

thread_local DetailState detailState;

bool DetailEnabled()
{
    static const bool enabled = [] {
        char value[8]{};
        return !(GetEnvironmentVariableA("M2_CROWD_DETAIL", value, sizeof(value)) == 1 && value[0] == '0');
    }();
    return enabled;
}

unsigned FindDetail(const char* kind, const char* phase, const char* asset, const char* name,
    int mesh, int meshType, int materialType, int vertices)
{
    const char* pass = detailState.pass;
    unsigned hash = 2166136261u;
    for (const char* word : {kind, phase, pass, asset, name})
    {
        for (const char* c = word; *c; ++c) hash = (hash ^ static_cast<unsigned char>(*c)) * 16777619u;
        hash = (hash ^ 255u) * 16777619u;
    }
    for (int value : {mesh, meshType, materialType}) hash = (hash ^ static_cast<unsigned>(value)) * 16777619u;
    unsigned slot = hash & (DetailLookupCapacity - 1);
    for (unsigned probe = 0; probe < DetailLookupCapacity; ++probe)
    {
        unsigned& entry = detailState.lookup[slot];
        if (!entry)
        {
            if (detailState.count == DetailCapacity) break;
            const unsigned index = detailState.count++;
            DetailRow& row = detailState.rows[index];
            row = DetailRow{};
            row.hash = hash;
            strncpy_s(row.kind, kind, _TRUNCATE);
            strncpy_s(row.phase, phase, _TRUNCATE);
            strncpy_s(row.pass, pass, _TRUNCATE);
            strncpy_s(row.asset, asset, _TRUNCATE);
            strncpy_s(row.name, name, _TRUNCATE);
            row.mesh = mesh; row.meshType = meshType; row.materialType = materialType; row.vertices = vertices;
            entry = index + 1;
            return index;
        }
        const DetailRow& row = detailState.rows[entry - 1];
        if (row.hash == hash && row.mesh == mesh && row.meshType == meshType && row.materialType == materialType &&
            strcmp(row.kind, kind) == 0 && strcmp(row.phase, phase) == 0 && strcmp(row.pass, pass) == 0 &&
            strncmp(row.asset, asset, sizeof(row.asset) - 1) == 0 && strncmp(row.name, name, sizeof(row.name) - 1) == 0)
            return entry - 1;
        slot = (slot + 1) & (DetailLookupCapacity - 1);
    }
    ++detailState.dropped;
    return DetailCapacity;
}

void FinishDetailFrame(const Capture& frame)
{
    if (!detailState.sampled) return;
    detailState.sampled = false;
    if (!frame.world) return;
    ++detailState.sampledUpdates;
    detailState.sampledRenders += frame.rendered;
    for (unsigned i = 0; i < detailState.touchedCount; ++i)
    {
        DetailRow& row = detailState.rows[detailState.touched[i]];
        DetailWork& sum = row.window;
        const DetailWork& work = row.frame;
        if (!work.calls) continue;
        ++sum.frames;
        sum.calls += work.calls;
        sum.total += work.total;
        sum.maxCall = (std::max)(sum.maxCall, work.maxCall);
        sum.maxFrame = (std::max)(sum.maxFrame, work.total);
        for (unsigned c = 0; c < CounterCount; ++c) sum.counters[c] += work.counters[c];
        for (unsigned c = 0; c < DetailCounterCount; ++c) sum.detail[c] += work.detail[c];
    }
}

void DumpDetails()
{
    if (!detailState.sampledUpdates) return;
    TraceError("[MAP_PROFILE] DETAIL_WINDOW first_frame=%llu last_frame=%llu sample_every=%u sampled_updates=%u sampled_renders=%u rows=%u dropped=%u timing=CPU_wall inclusive=1 map=\"%s\"",
        static_cast<unsigned long long>(state.firstFrame), static_cast<unsigned long long>(state.lastFrame),
        DetailSampleInterval, detailState.sampledUpdates, detailState.sampledRenders,
        detailState.count, detailState.dropped, state.lastMap);
    unsigned order[DetailCapacity];
    bool selected[DetailCapacity]{};
    for (unsigned i = 0; i < detailState.count; ++i) order[i] = i;
    std::sort(order, order + detailState.count, [](unsigned a, unsigned b) {
        return detailState.rows[a].window.total > detailState.rows[b].window.total;
    });
    unsigned models = 0, effects = 0, elements = 0;
    for (unsigned i = 0; i < detailState.count; ++i)
    {
        const unsigned index = order[i];
        const DetailRow& row = detailState.rows[index];
        if (!row.window.calls) continue;
        if (strcmp(row.kind, "model") == 0) { if (models++ < 6) selected[index] = true; }
        else if (strcmp(row.phase, "update") == 0 || strcmp(row.phase, "render") == 0)
        { if (effects++ < 8) selected[index] = true; }
        else { if (elements++ < 8) selected[index] = true; }
    }
    std::sort(order, order + detailState.count, [](unsigned a, unsigned b) {
        return detailState.rows[a].window.counters[static_cast<unsigned>(Counter::ActorDrawCalls)] >
            detailState.rows[b].window.counters[static_cast<unsigned>(Counter::ActorDrawCalls)];
    });
    models = 0;
    for (unsigned i = 0; i < detailState.count && models < 6; ++i)
    {
        const DetailRow& row = detailState.rows[order[i]];
        if (strcmp(row.kind, "model") == 0 && row.window.calls)
        { selected[order[i]] = true; ++models; }
    }
    for (unsigned i = 0; i < detailState.count; ++i)
    {
        if (!selected[i]) continue;
        const DetailRow& row = detailState.rows[i];
        const DetailWork& work = row.window;
        auto count = [&](Counter counter) { return static_cast<unsigned long long>(work.counters[static_cast<unsigned>(counter)]); };
        auto detail = [&](DetailCounter counter) { return static_cast<unsigned long long>(work.detail[static_cast<unsigned>(counter)]); };
        // Identity and counters use separate lines to stay below TraceError's
        // fixed formatting buffer, even for long packed asset paths.
        TraceError("[MAP_PROFILE] DETAIL_ASSET first_frame=%llu id=%u kind=%s phase=%s pass=%s asset=\"%s\" name=\"%s\" mesh=%d mesh_name=\"%s\" mesh_type=%d material_type=%d vertices=%d",
            static_cast<unsigned long long>(state.firstFrame), i, row.kind, row.phase, row.pass,
            row.asset, row.name, row.mesh, row.meshName, row.meshType, row.materialType, row.vertices);
        TraceError("[MAP_PROFILE] DETAIL_TIME first_frame=%llu id=%u calls=%llu sampled_frames=%u total_ms=%.3f max_call_ms=%.3f max_frame_ms=%.3f",
            static_cast<unsigned long long>(state.firstFrame), i, static_cast<unsigned long long>(work.calls), work.frames,
            Milliseconds(work.total), Milliseconds(work.maxCall), Milliseconds(work.maxFrame));
        if (strcmp(row.kind, "model") == 0)
            TraceError("[MAP_PROFILE] DETAIL_MODEL first_frame=%llu id=%u groups=%llu saved=%llu material_binds=%llu device_attempts=%llu succeeded=%llu failed=%llu empty=%llu triangles=%llu gpu_succeeded=%llu skipped=%llu",
                static_cast<unsigned long long>(state.firstFrame), i,
                count(Counter::ActorDrawCalls), count(Counter::ModelDrawsSaved), count(Counter::MaterialBinds),
                detail(DetailCounter::ModelAttempts), detail(DetailCounter::ModelSucceeded), detail(DetailCounter::ModelFailed),
                detail(DetailCounter::ModelEmpty), detail(DetailCounter::ModelTriangles), detail(DetailCounter::ModelGPUSucceeded), detail(DetailCounter::ModelSkipped));
        else
            TraceError("[MAP_PROFILE] DETAIL_EFFECT first_frame=%llu id=%u quads=%llu batches=%llu unbatched=%llu particle_failed=%llu mesh_draws=%llu mesh_failed=%llu mesh_triangles=%llu particle_updates=%llu particle_renders=%llu live_update=%llu live_render=%llu attached_system_calls=%llu",
                static_cast<unsigned long long>(state.firstFrame), i,
                count(Counter::ParticleQuads), count(Counter::ParticleBatches), detail(DetailCounter::ParticleUnbatched),
                detail(DetailCounter::ParticleFailed), detail(DetailCounter::EffectMeshAttempts), detail(DetailCounter::EffectMeshFailed),
                detail(DetailCounter::EffectMeshTriangles), detail(DetailCounter::ParticleUpdates), detail(DetailCounter::ParticleRenders),
                detail(DetailCounter::LiveParticlesUpdate), detail(DetailCounter::LiveParticlesRender), detail(DetailCounter::AttachedSystems));
    }
    detailState.count = detailState.sampledUpdates = detailState.sampledRenders = detailState.dropped = 0;
    std::fill(std::begin(detailState.lookup), std::end(detailState.lookup), 0u);
}

unsigned FindStage(const char* label)
{
    // Pointer lookup is constant time after first use. Equal literal text from
    // separate translation units is merged once, outside the timed scope.
    const auto address = reinterpret_cast<std::uintptr_t>(label);
    unsigned slot = static_cast<unsigned>((address >> 3) ^ (address >> 12)) & (StageLookupCapacity - 1);
    for (unsigned probe = 0; probe < StageLookupCapacity; ++probe)
    {
        StageLookup& entry = state.stageLookup[slot];
        if (entry.label == label) return entry.index;
        if (!entry.label)
        {
            unsigned index = 0;
            while (index < state.stageCount && strcmp(state.stageLabels[index], label) != 0) ++index;
            if (index == state.stageCount)
            {
                if (index == StageCapacity) return StageCapacity;
                state.stageLabels[state.stageCount++] = label;
            }
            entry.label = label;
            entry.index = index;
            return index;
        }
        slot = (slot + 1) & (StageLookupCapacity - 1);
    }
    return StageCapacity;
}

void DumpFrameStages(const Capture& capture)
{
    unsigned order[StageCapacity];
    for (unsigned i = 0; i < state.stageCount; ++i) order[i] = i;
    std::sort(order, order + state.stageCount, [&](unsigned a, unsigned b) {
        return capture.stages[a].self > capture.stages[b].self;
    });
    for (unsigned rank = 0; rank < (std::min)(state.stageCount, FrameStageLimit); ++rank)
    {
        const unsigned index = order[rank];
        const StageWork& work = capture.stages[index];
        if (!work.calls) continue;
        TraceError("[MAP_PROFILE] FRAME_STAGE frame=%llu stage=%s calls=%llu total_ms=%.3f self_ms=%.3f max_call_ms=%.3f",
            static_cast<unsigned long long>(capture.id), state.stageLabels[index],
            static_cast<unsigned long long>(work.calls), Milliseconds(work.total), Milliseconds(work.self), Milliseconds(work.maxCall));
    }
}

template<class T>
void DumpCrowd(const char* kind, std::uint64_t frame, const T* counters)
{
    TraceError("[MAP_PROFILE] %s frame=%llu pose_samples=%llu pose_reuse=%llu skin_uploads=%llu skin_reuse=%llu skinned_vertices=%llu binding_reuse=%llu pose_pool_reuse=%llu material_binds=%llu actor_draw_calls=%llu particle_quads=%llu particle_batches=%llu particle_node_moves=%llu effect_sorts=%llu buffer_pool_reuse=%llu gpu_skin_draws=%llu gpu_skin_vertices=%llu cpu_skin_fallbacks=%llu model_draws_saved=%llu",
        kind, static_cast<unsigned long long>(frame),
        static_cast<unsigned long long>(counters[13]), static_cast<unsigned long long>(counters[14]), static_cast<unsigned long long>(counters[15]), static_cast<unsigned long long>(counters[16]), static_cast<unsigned long long>(counters[17]), static_cast<unsigned long long>(counters[18]), static_cast<unsigned long long>(counters[19]), static_cast<unsigned long long>(counters[20]), static_cast<unsigned long long>(counters[21]), static_cast<unsigned long long>(counters[22]), static_cast<unsigned long long>(counters[23]), static_cast<unsigned long long>(counters[24]), static_cast<unsigned long long>(counters[25]), static_cast<unsigned long long>(counters[26]),
        static_cast<unsigned long long>(counters[27]), static_cast<unsigned long long>(counters[28]), static_cast<unsigned long long>(counters[29]), static_cast<unsigned long long>(counters[30]));
    TraceError("[MAP_PROFILE] DRAW_OPT kind=%s frame=%llu rigid_batch_draws=%llu rigid_draws_saved=%llu skin_common_builds=%llu skin_common_reuse=%llu rigid_matrix_fallbacks=%llu",
        kind, static_cast<unsigned long long>(frame), static_cast<unsigned long long>(counters[31]),
        static_cast<unsigned long long>(counters[32]), static_cast<unsigned long long>(counters[33]),
        static_cast<unsigned long long>(counters[34]), static_cast<unsigned long long>(counters[35]));
    TraceError("[MAP_PROFILE] CROWD_REUSE kind=%s frame=%llu rest_pose_reuse=%llu mesh_matrices_reuse=%llu mesh_matrices_builds=%llu particle_stream_draws=%llu particle_stream_fallbacks=%llu particle_basis_reuse=%llu",
        kind, static_cast<unsigned long long>(frame),
        static_cast<unsigned long long>(counters[36]), static_cast<unsigned long long>(counters[37]),
        static_cast<unsigned long long>(counters[38]), static_cast<unsigned long long>(counters[39]),
        static_cast<unsigned long long>(counters[40]), static_cast<unsigned long long>(counters[41]));
    TraceError("[MAP_PROFILE] CROWD_GPU kind=%s frame=%llu skin_palette_reuse=%llu skin_unlit_matrices=%llu text_instance_quads=%llu text_instance_draws=%llu text_expanded_draws=%llu text_upload_bytes=%llu",
        kind, static_cast<unsigned long long>(frame),
        static_cast<unsigned long long>(counters[42]), static_cast<unsigned long long>(counters[43]),
        static_cast<unsigned long long>(counters[44]), static_cast<unsigned long long>(counters[45]),
        static_cast<unsigned long long>(counters[46]), static_cast<unsigned long long>(counters[47]));
    TraceError("[MAP_PROFILE] EFFECT_BATCH kind=%s frame=%llu multi_draws=%llu particle_draws_saved=%llu multi_fallbacks=%llu",
        kind, static_cast<unsigned long long>(frame), static_cast<unsigned long long>(counters[48]),
        static_cast<unsigned long long>(counters[49]), static_cast<unsigned long long>(counters[50]));
    TraceError("[MAP_PROFILE] ANIMATION_REUSE kind=%s frame=%llu constant_pose_reuse=%llu revision_matrices_reuse=%llu",
        kind, static_cast<unsigned long long>(frame), static_cast<unsigned long long>(counters[51]),
        static_cast<unsigned long long>(counters[52]));
    TraceError("[MAP_PROFILE] CPU_CROWD kind=%s frame=%llu particle_compact_updates=%llu actor_lod_no_work=%llu",
        kind, static_cast<unsigned long long>(frame), static_cast<unsigned long long>(counters[53]),
        static_cast<unsigned long long>(counters[54]));
}

void DumpPending()
{
    Capture& p = state.pending;
    if (!p.world)
        return;
    TraceError("[MAP_PROFILE] HITCH frame=%llu tick=%u work_ms=%.3f outside_process_ms=%.3f foreground=%u rendered=%u map=\"%s\" pos=(%.0f,%.0f,%.0f) sector=(%d,%d) events=%u dropped=%u other_hitches=%u stage_drops=%u",
        static_cast<unsigned long long>(p.id), p.tick, p.work, p.gap,
        p.foreground ? 1u : 0u, p.rendered ? 1u : 0u, p.map,
        p.x, p.y, p.z, p.sectorX, p.sectorY, p.eventCount, p.dropped, state.suppressedHitches, p.stageDrops);
    TraceError("[MAP_PROFILE] COUNTS frame=%llu resource_loads=%u object_creates=%u object_destroys=%u terrain_loads=%u area_loads=%u",
        static_cast<unsigned long long>(p.id), p.counters[0], p.counters[1], p.counters[2], p.counters[3], p.counters[4]);
    TraceError("[MAP_PROFILE] OPT frame=%llu metadata_hits=%u metadata_misses=%u read_ahead_hits=%u terrain_prepared=%u required_terrain_fallbacks=%u motion_library_hits=%u motion_library_builds=%u objects_prepared_ahead=%u",
        static_cast<unsigned long long>(p.id), p.counters[5], p.counters[6], p.counters[7], p.counters[8], p.counters[9], p.counters[10], p.counters[11], p.counters[12]);
    std::sort(p.events, p.events + p.eventCount, [](const Event& a, const Event& b) { return a.total > b.total; });
    const unsigned count = (std::min)(p.eventCount, ReportEventLimit);
    for (unsigned i = 0; i < count; ++i)
    {
        const Event& e = p.events[i];
        TraceError("[MAP_PROFILE] EVENT frame=%llu total_ms=%.3f self_ms=%.3f stage=%s detail=\"%s\"",
            static_cast<unsigned long long>(p.id), e.total, e.self, e.label, e.detail);
    }
    DumpCrowd("CROWD", p.id, p.counters);
    DumpFrameStages(p);
    p.world = false;
    state.suppressedHitches = 0;
}

void DumpSummary()
{
    if (!state.frames)
        return;
    TraceError("[MAP_PROFILE] SUMMARY frames=%u avg_work_ms=%.3f max_work_ms=%.3f max_outside_process_ms=%.3f over_16_7ms=%u over_33_3ms=%u over_100ms=%u resource_loads=%llu object_creates=%llu object_destroys=%llu terrain_loads=%llu area_loads=%llu previous_flush_ms=%.3f",
        state.frames, state.totalWork / state.frames, state.maxWork, state.maxGap,
        state.over17, state.over33, state.over100,
        static_cast<unsigned long long>(state.counters[0]), static_cast<unsigned long long>(state.counters[1]),
        static_cast<unsigned long long>(state.counters[2]), static_cast<unsigned long long>(state.counters[3]),
        static_cast<unsigned long long>(state.counters[4]), state.lastFlush);
    TraceError("[MAP_PROFILE] OPT_SUMMARY metadata_hits=%llu metadata_misses=%llu read_ahead_hits=%llu terrain_prepared=%llu required_terrain_fallbacks=%llu motion_library_hits=%llu motion_library_builds=%llu objects_prepared_ahead=%llu",
        static_cast<unsigned long long>(state.counters[5]), static_cast<unsigned long long>(state.counters[6]),
        static_cast<unsigned long long>(state.counters[7]), static_cast<unsigned long long>(state.counters[8]),
        static_cast<unsigned long long>(state.counters[9]), static_cast<unsigned long long>(state.counters[10]),
        static_cast<unsigned long long>(state.counters[11]), static_cast<unsigned long long>(state.counters[12]));
    TraceError("[MAP_PROFILE] WINDOW first_frame=%llu last_frame=%llu first_tick=%u last_tick=%u rendered_frames=%u over_16_7ms_rendered=%u map=\"%s\" map_changes=%u stage_drops=%u timing_scope=profiled_frames fps_source=FRAME_PACING",
        static_cast<unsigned long long>(state.firstFrame), static_cast<unsigned long long>(state.lastFrame),
        state.firstTick, state.lastTick, state.renderedFrames, state.over17Rendered, state.lastMap, state.mapChanges, state.stageDrops);
    DumpCrowd("CROWD_SUMMARY", state.lastFrame, state.counters);
    DumpDetails();
    unsigned order[StageCapacity];
    bool selected[StageCapacity]{};
    for (unsigned i = 0; i < state.stageCount; ++i) order[i] = i;
    std::sort(order, order + state.stageCount, [](unsigned a, unsigned b) { return state.stages[a].work.self > state.stages[b].work.self; });
    for (unsigned i = 0; i < (std::min)(state.stageCount, 12u); ++i) selected[order[i]] = true;
    std::sort(order, order + state.stageCount, [](unsigned a, unsigned b) { return state.stages[a].work.total > state.stages[b].work.total; });
    for (unsigned i = 0; i < (std::min)(state.stageCount, 6u); ++i) selected[order[i]] = true;
    for (unsigned i = 0; i < state.stageCount; ++i)
        if (strncmp(state.stageLabels[i], "pack.", 5) == 0 ||
            strncmp(state.stageLabels[i], "characters.", 11) == 0 ||
            strncmp(state.stageLabels[i], "instance.", 9) == 0 ||
            strncmp(state.stageLabels[i], "actor.", 6) == 0) selected[i] = true;
    std::sort(order, order + state.stageCount, [](unsigned a, unsigned b) { return state.stages[a].work.self > state.stages[b].work.self; });
    unsigned written = 0;
    for (unsigned rank = 0; rank < state.stageCount && written < SummaryStageLimit; ++rank)
    {
        const unsigned index = order[rank];
        const StageSummary& summary = state.stages[index];
        const StageWork& work = summary.work;
        if (!selected[index] || !work.calls) continue;
        ++written;
        TraceError("[MAP_PROFILE] STAGE_SUMMARY first_frame=%llu last_frame=%llu stage=%s calls=%llu frames=%u total_ms=%.3f self_ms=%.3f avg_frame_ms=%.3f max_frame_ms=%.3f max_call_ms=%.3f avg_active_frame_ms=%.3f over_16_7ms_frames=%u over_16_7ms_self_ms=%.3f over_33_3ms_frames=%u over_33_3ms_self_ms=%.3f",
            static_cast<unsigned long long>(state.firstFrame), static_cast<unsigned long long>(state.lastFrame), state.stageLabels[index],
            static_cast<unsigned long long>(work.calls), summary.frames, Milliseconds(work.total), Milliseconds(work.self),
            Milliseconds(work.total) / state.frames, Milliseconds(summary.maxFrame), Milliseconds(work.maxCall), Milliseconds(work.total) / summary.frames,
            summary.over17Frames, Milliseconds(summary.over17Self), summary.over33Frames, Milliseconds(summary.over33Self));
    }
    state.frames = state.over17 = state.over33 = state.over100 = 0;
    state.totalWork = state.maxWork = state.maxGap = 0;
    for (auto& counter : state.counters)
        counter = 0;
    for (auto& stage : state.stages) stage = {};
    state.stageDrops = state.renderedFrames = state.over17Rendered = state.mapChanges = 0;
    state.firstFrame = state.lastFrame = 0;
    state.firstTick = state.lastTick = 0;
    state.lastMap[0] = '\0';
}
}

RenderPassScope::RenderPassScope(const char* pass) : m_previous(detailState.pass)
{
    detailState.pass = pass;
}

RenderPassScope::~RenderPassScope() { detailState.pass = m_previous; }

DetailScope::DetailScope(const char* kind, const char* phase, const char* asset,
    const char* name, int mesh, int meshType, int materialType, int vertices, const char* meshName)
{
    if (!state.active || !detailState.sampled) return;
    if (strcmp(kind, "effect") == 0)
    {
        m_effectContext = true;
        m_previousEffect = detailState.effectAsset;
        if (asset) detailState.effectAsset = asset;
        asset = detailState.effectAsset;
    }
    m_row = FindDetail(kind, phase, asset ? asset : "<unknown>", name ? name : "",
        mesh, meshType, materialType, vertices);
    if (m_row >= DetailCapacity) return;
    DetailRow& row = detailState.rows[m_row];
    if (meshName && !row.meshName[0]) strncpy_s(row.meshName, meshName, _TRUNCATE);
    if (row.frameId != state.frame.id)
    {
        row.frameId = state.frame.id;
        row.frame = DetailWork{};
        detailState.touched[detailState.touchedCount++] = m_row;
    }
    std::copy(std::begin(state.frame.counters), std::end(state.frame.counters), m_before);
    std::copy(std::begin(detailState.running), std::end(detailState.running), m_detailBefore);
    m_start = Tick();
}

DetailScope::~DetailScope()
{
    if (m_row < DetailCapacity)
    {
        const auto elapsed = Tick() - m_start;
        DetailWork& work = detailState.rows[m_row].frame;
        ++work.calls;
        work.total += elapsed;
        work.maxCall = (std::max)(work.maxCall, elapsed);
        for (unsigned c = 0; c < CounterCount; ++c) work.counters[c] += state.frame.counters[c] - m_before[c];
        for (unsigned c = 0; c < DetailCounterCount; ++c) work.detail[c] += detailState.running[c] - m_detailBefore[c];
    }
    if (m_effectContext) detailState.effectAsset = m_previousEffect;
}

void RecordModelDrawResult(bool succeeded, bool gpu, int triangles)
{
    if (!detailState.sampled) return;
    auto add = [](DetailCounter counter, std::uint64_t amount = 1) {
        detailState.running[static_cast<unsigned>(counter)] += amount;
    };
    add(DetailCounter::ModelAttempts);
    add(succeeded ? DetailCounter::ModelSucceeded : DetailCounter::ModelFailed);
    if (triangles <= 0) add(DetailCounter::ModelEmpty);
    if (succeeded && triangles > 0) add(DetailCounter::ModelTriangles, triangles);
    if (succeeded && gpu) add(DetailCounter::ModelGPUSucceeded);
}

void RecordModelDrawSkipped()
{
    if (detailState.sampled) ++detailState.running[static_cast<unsigned>(DetailCounter::ModelSkipped)];
}

void RecordParticleDrawResult(bool succeeded, bool unbatched)
{
    if (!detailState.sampled) return;
    if (!succeeded) ++detailState.running[static_cast<unsigned>(DetailCounter::ParticleFailed)];
    if (unbatched) ++detailState.running[static_cast<unsigned>(DetailCounter::ParticleUnbatched)];
}

void RecordEffectMeshDrawResult(bool succeeded, unsigned triangles)
{
    if (!detailState.sampled) return;
    ++detailState.running[static_cast<unsigned>(DetailCounter::EffectMeshAttempts)];
    if (!succeeded) ++detailState.running[static_cast<unsigned>(DetailCounter::EffectMeshFailed)];
    else detailState.running[static_cast<unsigned>(DetailCounter::EffectMeshTriangles)] += triangles;
}

void RecordParticleSystem(unsigned liveParticles, bool rendering, bool attached)
{
    if (!detailState.sampled) return;
    ++detailState.running[static_cast<unsigned>(rendering ? DetailCounter::ParticleRenders : DetailCounter::ParticleUpdates)];
    detailState.running[static_cast<unsigned>(rendering ? DetailCounter::LiveParticlesRender : DetailCounter::LiveParticlesUpdate)] += liveParticles;
    if (attached) ++detailState.running[static_cast<unsigned>(DetailCounter::AttachedSystems)];
}

Scope::Scope(const char* label, const char* detail)
{
    Reset(label, detail);
}

Scope::~Scope() { Stop(); }

void Scope::Reset(const char* label, const char* detail)
{
    Stop();
    if (!state.active || !label)
        return;
    m_label = label;
    m_parent = state.current;
    m_children = 0;
    m_detail[0] = '\0';
    m_stage = FindStage(label);
    if (detail)
        strncpy_s(m_detail, detail, _TRUNCATE);
    state.current = this;
    m_start = Tick();
}

void Scope::Detail(const char* format, ...)
{
    if (!m_label)
        return;
    va_list args;
    va_start(args, format);
    vsnprintf_s(m_detail, sizeof(m_detail), _TRUNCATE, format, args);
    va_end(args);
}

void Scope::Stop()
{
    if (!m_label)
        return;
    const std::int64_t elapsed = Tick() - m_start;
    state.current = m_parent;
    if (m_parent)
        m_parent->m_children += elapsed;
    const double total = Milliseconds(elapsed);
    const auto self = (std::max)(std::int64_t(0), elapsed - m_children);
    if (m_stage < StageCapacity)
    {
        StageWork& work = state.frame.stages[m_stage];
        ++work.calls;
        work.total += elapsed;
        work.self += self;
        work.maxCall = (std::max)(work.maxCall, elapsed);
    }
    else ++state.frame.stageDrops;
    if (total >= 1.0)
    {
        Capture& frame = state.frame;
        Event event;
        event.label = m_label;
        event.total = total;
        event.self = Milliseconds(self);
        strncpy_s(event.detail, m_detail, _TRUNCATE);
        if (frame.eventCount < EventCapacity)
            frame.events[frame.eventCount++] = event;
        else
        {
            // Keep the most expensive events, including late-closing parents.
            auto smallest = std::min_element(frame.events, frame.events + EventCapacity,
                [](const Event& a, const Event& b) { return a.total < b.total; });
            if (event.total > smallest->total)
                *smallest = event;
            ++frame.dropped;
        }
    }
    m_label = nullptr;
}

Frame::Frame()
{
    if (state.active) return;
    m_processStart = Tick();
    pacing.world = false;
    pacing.crowdValid = false;
    detailState.sampled = false;
    ++state.frameId;
    if (!pacing.announced)
    {
        pacing.announced = true;
        TraceError("[FRAME_PACING] START version=1 pid=%u mode=%s default=sampled sample_stride=17 compare_block_ms=5000 work_includes_profiler=1 intervals=render_finish_to_finish",
            GetCurrentProcessId(), Mode() == ProfileMode::Compare ? "compare" : CurrentModeName());
    }
    if (!Enabled() || state.active || (Mode() == ProfileMode::Compare && !pacing.compareFull) ||
        (!FullCapture() && state.frameId % DetailSampleInterval != 1))
        return;
    const unsigned now = GetTickCount();
    if (!state.announced)
    {
        TraceError("[MAP_PROFILE] START version=11 movement_streaming=1 crowd_rendering=1 asset_preparation=1 enabled=1 disable=M2_MAP_PROFILE=0 hitch_ms=16.7 event_ms=1 report=worst_per_second summary_seconds=5 inclusive_events=1 work_excludes_frame_sleep=1 stage_capacity=192 stage_all_calls=profiled_frames frame_stage_limit=12 summary_stage_limit=48 pack_breakdown=1 crowd_detail=%u detail_sample_every=17 detail_disable=M2_CROWD_DETAIL=0 rigid_batching=1 skin_common_cache=1 crowd_pose_matrix_reuse=opt_in particle_preparation=opt_in particle_stream=opt_in text_instancing=1 skin_palette_cache=1 particle_multi_texture=1 constant_motion=1 particle_cpu_compact=1 particle_storage=stable_vectors actor_collision_vectors=1 fps_label=1 timing_frames=profile_mode update_breakdown_sample_every=17 fps_source=FRAME_PACING pid=%u profile_mode=%s", DetailEnabled() ? 1u : 0u, GetCurrentProcessId(), CurrentModeName());
        state.announced = true;
        state.lastReport = state.lastSummary = now;
    }
    Capture& frame = state.frame;
    frame.id = state.frameId;
    detailState.sampled = DetailEnabled() && frame.id % DetailSampleInterval == 1;
    detailState.touchedCount = 0;
    detailState.effectAsset = nullptr;
    detailState.pass = "unspecified";
    if (detailState.sampled) std::fill(std::begin(detailState.running), std::end(detailState.running), 0ull);
    frame.tick = now;
    frame.world = frame.foreground = frame.rendered = false;
    frame.map[0] = '\0';
    frame.eventCount = frame.dropped = 0;
    frame.stageDrops = 0;
    for (auto& stage : frame.stages) stage = {};
    for (auto& counter : frame.counters)
        counter = 0;
    state.current = nullptr;
    state.start = Tick();
    frame.gap = state.lastEnd ? Milliseconds(state.start - state.lastEnd) : 0;
    state.active = m_started = true;
}

Frame::~Frame() { Finish(false); }

void Frame::Rendered()
{
    m_rendered = true;
    if (m_started)
        state.frame.rendered = true;
}

void Frame::Finish(bool foreground)
{
    if (!m_processStart) return;
    if (!m_started)
    {
        FinishPacing(m_processStart, m_rendered, foreground, false);
        // The gap is relative to the preceding Process, not the preceding
        // sampled Process; otherwise sampling fabricates idle-gap hitches.
        state.lastEnd = Tick();
        m_processStart = 0;
        return;
    }
    Capture& frame = state.frame;
    frame.work = Milliseconds(Tick() - state.start);
    frame.foreground = foreground;
    state.active = m_started = false;
    FinishDetailFrame(frame);
    if (frame.world)
    {
        if (!state.frames)
        {
            state.firstFrame = frame.id;
            state.firstTick = frame.tick;
        }
        else if (strcmp(state.lastMap, frame.map) != 0) ++state.mapChanges;
        state.lastFrame = frame.id;
        state.lastTick = frame.tick;
        strncpy_s(state.lastMap, frame.map, _TRUNCATE);
        ++state.frames;
        state.totalWork += frame.work;
        state.maxWork = (std::max)(state.maxWork, frame.work);
        state.maxGap = (std::max)(state.maxGap, frame.gap);
        state.over17 += frame.work >= 16.7;
        state.over33 += frame.work >= 33.3;
        state.over100 += frame.work >= 100.0;
        for (unsigned i = 0; i < CounterCount; ++i)
            state.counters[i] += frame.counters[i];
        state.renderedFrames += frame.rendered;
        state.over17Rendered += frame.rendered && frame.work >= 16.7;
        state.stageDrops += frame.stageDrops;
        for (unsigned i = 0; i < state.stageCount; ++i)
        {
            const StageWork& work = frame.stages[i];
            if (!work.calls) continue;
            StageSummary& summary = state.stages[i];
            ++summary.frames;
            summary.work.calls += work.calls;
            summary.work.total += work.total;
            summary.work.self += work.self;
            summary.work.maxCall = (std::max)(summary.work.maxCall, work.maxCall);
            summary.maxFrame = (std::max)(summary.maxFrame, work.total);
            if (frame.work >= 16.7) { ++summary.over17Frames; summary.over17Self += work.self; }
            if (frame.work >= 33.3) { ++summary.over33Frames; summary.over33Self += work.self; }
        }
        if (frame.work >= 16.7 || (foreground && frame.gap >= 100.0))
        {
            if (state.pending.world)
                ++state.suppressedHitches;
            // Prefer actual Process work over an idle/message-pump gap, which
            // may simply be frame pacing or returning from another window.
            if (!state.pending.world || frame.work > state.pending.work)
                state.pending = frame;
        }
    }
    const unsigned now = GetTickCount();
    const auto flushStart = Tick();
    bool wroteLog = false;
    if (now - state.lastReport >= 1000)
    {
        wroteLog = state.pending.world;
        DumpPending();
        state.lastReport = now;
    }
    if (now - state.lastSummary >= 5000)
    {
        wroteLog = wroteLog || state.frames != 0;
        DumpSummary();
        state.lastSummary = now;
    }
    state.lastEnd = Tick();
    if (wroteLog)
        state.lastFlush = Milliseconds(state.lastEnd - flushStart);
    FinishPacing(m_processStart, m_rendered, foreground, true);
    m_processStart = 0;
}

void Count(Counter counter, unsigned amount)
{
    if (state.active)
        state.frame.counters[static_cast<unsigned>(counter)] += amount;
}

void SetWorld(const char* map, float x, float y, float z, int sectorX, int sectorY)
{
    pacing.world = true;
    strncpy_s(pacing.frameMap, map, _TRUNCATE);
    if (!state.active)
        return;
    Capture& frame = state.frame;
    frame.world = true;
    strncpy_s(frame.map, map, _TRUNCATE);
    frame.x = x; frame.y = y; frame.z = z;
    frame.sectorX = sectorX; frame.sectorY = sectorY;
}

void Flush()
{
    DumpPacing(pacing.endTick, false);
    if (!Enabled())
        return;
    DumpPending();
    DumpSummary();
}

bool SampledFrame() { return state.active && detailState.sampled; }
void SetCrowdSize(unsigned alive, unsigned dead)
{
    pacing.alive = alive; pacing.dead = dead; pacing.crowdValid = true;
}
}
