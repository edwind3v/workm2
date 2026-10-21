#pragma once

#include <cstdint>

// Temporary movement diagnostics. Enabled by default; M2_MAP_PROFILE=0 disables
// capture at process startup. No resource or world-loading behavior is changed.
namespace MapPerformance
{
enum class Counter { Resources, ObjectsCreated, ObjectsRemoved, TerrainsLoaded, AreasLoaded, MetadataHits, MetadataMisses, ReadAheadHits, TerrainPrepared, StreamingFallbacks, MotionLibraryHits, MotionLibraryBuilds, ObjectsPreparedAhead, Count };

class Scope
{
public:
    explicit Scope(const char* label, const char* detail = nullptr);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    void Reset(const char* label, const char* detail = nullptr);
    void Detail(const char* format, ...);
    void Stop();
private:
    Scope* m_parent = nullptr;
    const char* m_label = nullptr;
    std::int64_t m_start = 0;
    std::int64_t m_children = 0;
    char m_detail[224];
};

class Frame
{
public:
    Frame();
    ~Frame();
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    void Finish(bool foreground);
    void Rendered();
private:
    bool m_started = false;
};

void Count(Counter counter);
void SetWorld(const char* map, float x, float y, float z, int sectorX, int sectorY);
void Flush();

template<class F>
auto Measure(const char* label, const char* detail, F&& action) -> decltype(action())
{
    Scope scope(label, detail);
    return action();
}
}
