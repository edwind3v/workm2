#include "StdAfx.h"
#include "MapPerformance.h"
#include "Debug.h"
#include <cstdarg>
#include <cstring>

namespace MapPerformance
{
namespace
{
constexpr unsigned EventCapacity = 192;
constexpr unsigned ReportEventLimit = 16;
constexpr unsigned CounterCount = static_cast<unsigned>(Counter::Count);
constexpr unsigned StageCapacity = 128;
constexpr unsigned StageLookupCapacity = 512;
constexpr unsigned FrameStageLimit = 12;
constexpr unsigned SummaryStageLimit = 32;

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

bool Enabled()
{
    static const bool enabled = [] {
        char value[16]{};
        const DWORD length = GetEnvironmentVariableA("M2_MAP_PROFILE", value, sizeof(value));
        return !(length == 1 && value[0] == '0');
    }();
    return enabled;
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
    TraceError("[MAP_PROFILE] %s frame=%llu pose_samples=%llu pose_reuse=%llu skin_uploads=%llu skin_reuse=%llu skinned_vertices=%llu binding_reuse=%llu pose_pool_reuse=%llu material_binds=%llu actor_draw_calls=%llu particle_quads=%llu particle_batches=%llu particle_node_moves=%llu effect_sorts=%llu buffer_pool_reuse=%llu",
        kind, static_cast<unsigned long long>(frame),
        static_cast<unsigned long long>(counters[13]), static_cast<unsigned long long>(counters[14]), static_cast<unsigned long long>(counters[15]), static_cast<unsigned long long>(counters[16]), static_cast<unsigned long long>(counters[17]), static_cast<unsigned long long>(counters[18]), static_cast<unsigned long long>(counters[19]), static_cast<unsigned long long>(counters[20]), static_cast<unsigned long long>(counters[21]), static_cast<unsigned long long>(counters[22]), static_cast<unsigned long long>(counters[23]), static_cast<unsigned long long>(counters[24]), static_cast<unsigned long long>(counters[25]), static_cast<unsigned long long>(counters[26]));
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
    TraceError("[MAP_PROFILE] WINDOW first_frame=%llu last_frame=%llu first_tick=%u last_tick=%u rendered_frames=%u over_16_7ms_rendered=%u map=\"%s\" map_changes=%u stage_drops=%u",
        static_cast<unsigned long long>(state.firstFrame), static_cast<unsigned long long>(state.lastFrame),
        state.firstTick, state.lastTick, state.renderedFrames, state.over17Rendered, state.lastMap, state.mapChanges, state.stageDrops);
    DumpCrowd("CROWD_SUMMARY", state.lastFrame, state.counters);
    unsigned order[StageCapacity];
    bool selected[StageCapacity]{};
    for (unsigned i = 0; i < state.stageCount; ++i) order[i] = i;
    std::sort(order, order + state.stageCount, [](unsigned a, unsigned b) { return state.stages[a].work.self > state.stages[b].work.self; });
    for (unsigned i = 0; i < (std::min)(state.stageCount, 12u); ++i) selected[order[i]] = true;
    std::sort(order, order + state.stageCount, [](unsigned a, unsigned b) { return state.stages[a].work.total > state.stages[b].work.total; });
    for (unsigned i = 0; i < (std::min)(state.stageCount, 6u); ++i) selected[order[i]] = true;
    for (unsigned i = 0; i < state.stageCount; ++i)
        if (strncmp(state.stageLabels[i], "pack.", 5) == 0) selected[i] = true;
    std::sort(order, order + state.stageCount, [](unsigned a, unsigned b) { return state.stages[a].work.self > state.stages[b].work.self; });
    unsigned written = 0;
    for (unsigned rank = 0; rank < state.stageCount && written < SummaryStageLimit; ++rank)
    {
        const unsigned index = order[rank];
        const StageSummary& summary = state.stages[index];
        const StageWork& work = summary.work;
        if (!selected[index] || !work.calls) continue;
        ++written;
        TraceError("[MAP_PROFILE] STAGE_SUMMARY first_frame=%llu last_frame=%llu stage=%s calls=%llu frames=%u total_ms=%.3f self_ms=%.3f avg_frame_ms=%.3f max_frame_ms=%.3f max_call_ms=%.3f over_16_7ms_frames=%u over_16_7ms_self_ms=%.3f over_33_3ms_frames=%u over_33_3ms_self_ms=%.3f",
            static_cast<unsigned long long>(state.firstFrame), static_cast<unsigned long long>(state.lastFrame), state.stageLabels[index],
            static_cast<unsigned long long>(work.calls), summary.frames, Milliseconds(work.total), Milliseconds(work.self),
            Milliseconds(work.total) / state.frames, Milliseconds(summary.maxFrame), Milliseconds(work.maxCall),
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
    if (!Enabled() || state.active)
        return;
    const unsigned now = GetTickCount();
    if (!state.announced)
    {
        TraceError("[MAP_PROFILE] START version=5 movement_streaming=1 crowd_rendering=1 asset_preparation=1 enabled=1 disable=M2_MAP_PROFILE=0 hitch_ms=16.7 event_ms=1 report=worst_per_second summary_seconds=5 inclusive_events=1 work_excludes_frame_sleep=1 stage_capacity=128 stage_all_calls=1 frame_stage_limit=12 summary_stage_limit=32 pack_breakdown=1");
        state.announced = true;
        state.lastReport = state.lastSummary = now;
    }
    Capture& frame = state.frame;
    frame.id = ++state.frameId;
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
    if (m_started)
        state.frame.rendered = true;
}

void Frame::Finish(bool foreground)
{
    if (!m_started)
        return;
    Capture& frame = state.frame;
    frame.work = Milliseconds(Tick() - state.start);
    frame.foreground = foreground;
    state.active = m_started = false;
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
}

void Count(Counter counter, unsigned amount)
{
    if (state.active)
        state.frame.counters[static_cast<unsigned>(counter)] += amount;
}

void SetWorld(const char* map, float x, float y, float z, int sectorX, int sectorY)
{
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
    if (!Enabled())
        return;
    DumpPending();
    DumpSummary();
}
}
