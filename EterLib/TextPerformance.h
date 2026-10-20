#pragma once

#ifdef ENABLE_FIX_MOBS_LAG
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Opt-in, fixed-storage diagnostics. Timings are CPU wall time unless marked GPU.
class CTextPerformance
{
public:
    enum EStage { Distance, Selection, Projection, Arrangement, Rendering, StageCount };
    enum ECounter { EligibleCharacters, EligibleItems, Labels, Offscreen, CacheHits,
        CacheBuilds, CapacityGrowths, Draws, Locks, UploadBytes, StreamFailures,
        FallbackDraws, TransientMapNodes, OverlapChecks, CounterCount };

    static CTextPerformance& Get() { static CTextPerformance value; return value; }
    bool Enabled() const { return m_enabled; }
    static bool TutorialRenderer()
    {
        static const bool enabled = [] {
            const char* mode = std::getenv("METIN2_TEXT_RENDERER");
            return mode && std::strcmp(mode, "tutorial") == 0;
        }();
        return enabled;
    }
    double Now() const
    {
        LARGE_INTEGER value;
        QueryPerformanceCounter(&value);
        return static_cast<double>(value.QuadPart) * m_microsecondsPerTick;
    }
    void Add(ECounter counter, unsigned long long value = 1)
    { if (m_enabled) m_counters[counter] += value; }
    void Time(EStage stage, double start)
    { if (m_enabled) m_times[stage] += Now() - start; }
    void BeginFrame()
    {
        if (!m_enabled) return;
        const double now = Now();
        if (m_previousFrame != 0.0 && m_frameCount < m_frameIntervals.size())
            m_frameIntervals[m_frameCount++] = now - m_previousFrame;
        m_previousFrame = now;
    }
    void GPU(double microseconds)
    {
        if (m_enabled && m_gpuCount < m_gpuTimes.size()) m_gpuTimes[m_gpuCount++] = microseconds;
    }
    void CacheBytes(std::size_t bytes) { if (m_enabled) m_cacheBytes = bytes; }
    void EndFrame()
    {
        if (!m_enabled) return;
        ++m_samples;
        const double now = Now();
        if (m_reportTime == 0.0) m_reportTime = now;
        if (now - m_reportTime < 5000000.0 && m_frameCount < m_frameIntervals.size()) return;
        std::sort(m_frameIntervals.begin(), m_frameIntervals.begin() + m_frameCount);
        std::sort(m_gpuTimes.begin(), m_gpuTimes.begin() + m_gpuCount);
        if (!m_file)
        {
            m_file = std::fopen("text_performance.txt", "a");
            if (m_file)
                std::fprintf(m_file, "\nTEXT_PROFILE build=%s %s mode=%s ptr_bits=%u config=%s\n",
                    __DATE__, __TIME__, TutorialRenderer() ? "tutorial" : "cached_batch",
                    static_cast<unsigned>(sizeof(void*) * 8),
#ifdef _DEBUG
                    "Debug"
#else
                    "Release"
#endif
                );
        }
        if (m_file)
        {
            std::fprintf(m_file, "frames=%u frame_ms_p50=%.3f p95=%.3f p99=%.3f max=%.3f gpu_pass_ms_p95=%.3f gpu_samples=%u cache_index_bytes=%llu\n",
                m_samples, Percentile(m_frameIntervals, m_frameCount, 50),
                Percentile(m_frameIntervals, m_frameCount, 95), Percentile(m_frameIntervals, m_frameCount, 99),
                Percentile(m_frameIntervals, m_frameCount, 100), Percentile(m_gpuTimes, m_gpuCount, 95),
                m_gpuCount, static_cast<unsigned long long>(m_cacheBytes));
            static const char* stages[] = { "distance", "selection", "projection", "arrangement", "render" };
            for (unsigned i = 0; i < StageCount; ++i)
                std::fprintf(m_file, "%s_cpu_ms_avg=%.4f ", stages[i], m_times[i] / (1000.0 * m_samples));
            std::fprintf(m_file, "\n");
            static const char* counters[] = { "eligible_characters", "eligible_items", "labels", "offscreen",
                "cache_hits", "cache_builds", "capacity_growths", "draws", "locks", "upload_bytes",
                "stream_failures", "fallback_draws", "transient_map_nodes", "overlap_checks" };
            for (unsigned i = 0; i < CounterCount; ++i)
                std::fprintf(m_file, "%s=%llu ", counters[i], m_counters[i]);
            std::fprintf(m_file, "\n");
            std::fflush(m_file);
        }
        m_frameCount = m_gpuCount = m_samples = 0;
        m_counters.fill(0);
        m_times.fill(0.0);
        m_reportTime = Now();
    }

private:
    CTextPerformance()
    {
        const char* setting = std::getenv("METIN2_TEXT_PROFILE");
        m_enabled = setting && setting[0] == '1';
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        m_microsecondsPerTick = 1000000.0 / static_cast<double>(frequency.QuadPart);
    }
    ~CTextPerformance() { if (m_file) std::fclose(m_file); }
    static double Percentile(const std::array<double, 2048>& values, unsigned count, unsigned percentile)
    {
        if (!count) return 0.0;
        const unsigned index = std::min<unsigned>(count - 1, (count * percentile + 99) / 100 - 1);
        return values[index] / 1000.0;
    }
    bool m_enabled = false;
    FILE* m_file = nullptr;
    double m_microsecondsPerTick = 0.0, m_previousFrame = 0.0, m_reportTime = 0.0;
    unsigned m_frameCount = 0, m_gpuCount = 0, m_samples = 0;
    std::size_t m_cacheBytes = 0;
    std::array<double, 2048> m_frameIntervals{}, m_gpuTimes{};
    std::array<double, StageCount> m_times{};
    std::array<unsigned long long, CounterCount> m_counters{};
};

class CTextProfileScope
{
public:
    explicit CTextProfileScope(CTextPerformance::EStage stage) : m_stage(stage)
    { if (CTextPerformance::Get().Enabled()) m_start = CTextPerformance::Get().Now(); }
    ~CTextProfileScope() { if (m_start != 0.0) CTextPerformance::Get().Time(m_stage, m_start); }
private:
    CTextPerformance::EStage m_stage;
    double m_start = 0.0;
};
#endif
