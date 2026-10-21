#pragma once

#include <cstdint>

// Detailed diagnostics sample one frame in 17 by default. M2_MAP_PROFILE=full
// captures every frame; =0 keeps only lightweight frame pacing; =compare
// alternates full/light capture. No world-loading behavior is changed.
namespace MapPerformance
{
enum class Counter { Resources, ObjectsCreated, ObjectsRemoved, TerrainsLoaded, AreasLoaded, MetadataHits, MetadataMisses, ReadAheadHits, TerrainPrepared, StreamingFallbacks, MotionLibraryHits, MotionLibraryBuilds, ObjectsPreparedAhead, PoseSamples, PoseReuse, SkinUploads, SkinReuse, SkinnedVertices, BindingReuse, PosePoolReuse, MaterialBinds, ActorDrawCalls, ParticleQuads, ParticleBatches, ParticleMoves, EffectSorts, BufferPoolReuse, GPUSkinDraws, GPUSkinVertices, CPUSkinFallbacks, ModelDrawsSaved, RigidBatchDraws, RigidBatchSaved, GPUSkinCommonBuilds, GPUSkinCommonReuse, RigidBatchMatrixFallbacks, RestPoseReuse, MeshMatricesReuse, MeshMatricesBuilds, ParticleStreamDraws, ParticleStreamFallbacks, ParticleBasisReuse, GPUSkinPaletteReuse, GPUSkinUnlitMatrices, TextInstanceQuads, TextInstanceDraws, TextExpandedDraws, TextUploadBytes, ParticleMultiDraws, ParticleMultiSaved, ParticleMultiFallbacks, ConstantPoseReuse, RevisionMatricesReuse, ParticleCompactUpdates, ActorLODNoWork, Count };

// Detailed diagnostics sample one Process frame in 17 and aggregate by asset.
// These are CPU wall times, not GPU timer measurements. No per-draw log I/O.
enum class DetailCounter { ModelAttempts, ModelSucceeded, ModelFailed, ModelEmpty,
    ModelTriangles, ModelGPUSucceeded, ModelSkipped, ParticleFailed, ParticleUnbatched,
    EffectMeshAttempts, EffectMeshFailed, EffectMeshTriangles, LiveParticlesUpdate,
    LiveParticlesRender, ParticleUpdates, ParticleRenders, AttachedSystems, Count };

class RenderPassScope
{
public:
    explicit RenderPassScope(const char* pass);
    ~RenderPassScope();
    RenderPassScope(const RenderPassScope&) = delete;
    RenderPassScope& operator=(const RenderPassScope&) = delete;
private:
    const char* m_previous;
};

class DetailScope
{
public:
    // Asset/model strings are copied into a bounded registry on first use.
    // Effect child scopes inherit the parent effect's script filename.
    DetailScope(const char* kind, const char* phase, const char* asset = nullptr,
        const char* name = nullptr, int mesh = -1, int meshType = -1,
        int materialType = -1, int vertices = 0, const char* meshName = nullptr);
    ~DetailScope();
    DetailScope(const DetailScope&) = delete;
    DetailScope& operator=(const DetailScope&) = delete;
private:
    unsigned m_row = static_cast<unsigned>(-1);
    std::int64_t m_start = 0;
    unsigned m_before[static_cast<unsigned>(Counter::Count)];
    std::uint64_t m_detailBefore[static_cast<unsigned>(DetailCounter::Count)];
    const char* m_previousEffect = nullptr;
    bool m_effectContext = false;
};

void RecordModelDrawResult(bool succeeded, bool gpu, int triangles);
void RecordModelDrawSkipped();
void RecordParticleDrawResult(bool succeeded, bool unbatched);
void RecordEffectMeshDrawResult(bool succeeded, unsigned triangles);
void RecordParticleSystem(unsigned liveParticles, bool rendering, bool attached);
bool SampledFrame();

class Scope
{
public:
    // Labels must have static lifetime; file/actor details are copied separately.
    explicit Scope(const char* label = nullptr, const char* detail = nullptr);
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
    unsigned m_stage = static_cast<unsigned>(-1);
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
    bool m_rendered = false;
    std::int64_t m_processStart = 0;
};

void Count(Counter counter, unsigned amount = 1);
void SetWorld(const char* map, float x, float y, float z, int sectorX, int sectorY);
void SetCrowdSize(unsigned alive, unsigned dead);
void Flush();

template<class F>
auto Measure(const char* label, const char* detail, F&& action) -> decltype(action())
{
    Scope scope(label, detail);
    return action();
}

template<class F>
auto MeasureIf(bool enabled, const char* label, F&& action) -> decltype(action())
{
    if (enabled) { Scope scope(label); return action(); }
    return action();
}
}
