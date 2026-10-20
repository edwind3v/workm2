#include "StdAfx.h"
#include "GrpTextInstance.h"
#include "StateManager.h"
#include "TextPerformance.h"

#ifdef ENABLE_FIX_MOBS_LAG
#include <array>
#include <cfloat>
#include <cstdint>
#include <list>
#include <map>
#include <tuple>

extern DWORD GetDefaultCodePage();

struct SCachedTextGeometry
{
    struct SGlyph
    {
        float left, top, right, bottom, u0, v0, u1, v1;
        CGraphicImageTexture* texture;
        std::size_t colorIndex;
    };
    CGraphicText::TRef fontOwner;
    std::vector<SGlyph> glyphs;
    DWORD revision = 0;
    float left = FLT_MAX, top = FLT_MAX, right = -FLT_MAX, bottom = -FLT_MAX;
};

namespace
{
    constexpr unsigned MaxQuads = 4096;
    constexpr std::size_t CacheBudget = 4 * 1024 * 1024;
    constexpr std::size_t MaxCacheEntries = 1024;
    using TGeometryKey = std::tuple<std::uintptr_t, DWORD, DWORD, BYTE, BYTE, bool, bool, float, std::string>;
    struct SCacheEntry
    {
        std::shared_ptr<SCachedTextGeometry> geometry;
        std::size_t bytes;
        std::list<TGeometryKey>::iterator recency;
    };
    struct SGeometryCache
    {
        std::map<TGeometryKey, SCacheEntry> entries;
        std::list<TGeometryKey> recency;
        std::size_t bytes = 0;
        void Clear() { entries.clear(); recency.clear(); bytes = 0; }
    };
    SGeometryCache& GeometryCache() { static SGeometryCache cache; return cache; }

    struct SDraw { CGraphicImageTexture* texture; unsigned first, count; };
    struct SGPUFrame
    {
        IDirect3DQuery9* start = nullptr;
        IDirect3DQuery9* end = nullptr;
        IDirect3DQuery9* frequency = nullptr;
        IDirect3DQuery9* disjoint = nullptr;
        bool pending = false;
    };
    struct STextStream
    {
        IDirect3DDevice9Ex* device = nullptr; // Lifetime belongs to CGraphicDevice.
        IDirect3DVertexBuffer9* vertices = nullptr;
        IDirect3DIndexBuffer9* indices = nullptr;
        unsigned capacity = MaxQuads, cursor = 0, quads = 0, draws = 0;
        bool active = false, viewportValid = false;
        D3DVIEWPORT9 viewport{};
        std::array<SPDTVertexRaw, MaxQuads * 4> cpuVertices{};
        std::array<WORD, MaxQuads * 6> cpuIndices{};
        std::array<SDraw, MaxQuads> commands{};
        std::array<SGPUFrame, 8> gpuFrames{};
        int gpuActive = -1;
    };
    STextStream& Stream() { static STextStream stream; return stream; }

    template<class T> void Release(T*& object)
    { if (object) { object->Release(); object = nullptr; } }

    void PollGPU()
    {
        for (auto& frame : Stream().gpuFrames)
        {
            if (!frame.pending) continue;
            UINT64 begin = 0, end = 0, frequency = 0;
            BOOL disjoint = TRUE;
            // No FLUSH flag and no waiting: profiling must not serialize frames.
            if (frame.end->GetData(&end, sizeof(end), 0) != S_OK ||
                frame.start->GetData(&begin, sizeof(begin), 0) != S_OK ||
                frame.frequency->GetData(&frequency, sizeof(frequency), 0) != S_OK ||
                frame.disjoint->GetData(&disjoint, sizeof(disjoint), 0) != S_OK) continue;
            frame.pending = false;
            if (!disjoint && frequency && end >= begin)
                CTextPerformance::Get().GPU(1000000.0 * static_cast<double>(end - begin) / frequency);
        }
    }

    void DrawQueuedText()
    {
        auto& stream = Stream();
        if (!stream.quads) return;
        auto& profile = CTextPerformance::Get();
        const DWORD fog = STATEMANAGER.GetRenderState(D3DRS_FOGENABLE);
        const DWORD lighting = STATEMANAGER.GetRenderState(D3DRS_LIGHTING);
        STATEMANAGER.SaveRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        STATEMANAGER.SaveRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        STATEMANAGER.SetRenderState(D3DRS_FOGENABLE, FALSE);
        STATEMANAGER.SetRenderState(D3DRS_LIGHTING, FALSE);
        STATEMANAGER.SetFVF(TPDTVertex::kFVF);
        STATEMANAGER.SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        STATEMANAGER.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        STATEMANAGER.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        STATEMANAGER.SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        STATEMANAGER.SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        STATEMANAGER.SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);

        bool uploaded = false;
        if (stream.vertices && stream.indices)
        {
            if (stream.cursor + stream.quads > stream.capacity) stream.cursor = 0;
            void* destination = nullptr;
            profile.Add(CTextPerformance::Locks);
            const UINT bytes = stream.quads * 4 * sizeof(SPDTVertexRaw);
            if (SUCCEEDED(stream.vertices->Lock(stream.cursor * 4 * sizeof(SPDTVertexRaw), bytes,
                &destination, stream.cursor ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD)))
            {
                std::memcpy(destination, stream.cpuVertices.data(), bytes);
                uploaded = SUCCEEDED(stream.vertices->Unlock());
                if (uploaded) profile.Add(CTextPerformance::UploadBytes, bytes);
            }
            if (!uploaded)
            {
                stream.cursor = 0;
                profile.Add(CTextPerformance::StreamFailures);
            }
        }
        if (uploaded)
        {
            STATEMANAGER.SetStreamSource(0, stream.vertices, sizeof(SPDTVertexRaw));
            STATEMANAGER.SetIndices(stream.indices, 0);
        }
        for (unsigned i = 0; i < stream.draws; ++i)
        {
            const auto& command = stream.commands[i];
            STATEMANAGER.SetTexture(0, command.texture->GetD3DTexture());
            HRESULT result;
            if (uploaded)
                result = STATEMANAGER.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, command.first * 4,
                    command.count * 4, command.first * 6, command.count * 2, stream.cursor * 4);
            else
            {
                // Resource pressure must not suppress names. Use bounded CPU data.
                profile.Add(CTextPerformance::FallbackDraws);
                result = STATEMANAGER.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0,
                    command.count * 4, command.count * 2, stream.cpuIndices.data(), D3DFMT_INDEX16,
                    &stream.cpuVertices[command.first * 4], sizeof(SPDTVertexRaw));
            }
            profile.Add(CTextPerformance::Draws);
            if (FAILED(result)) profile.Add(CTextPerformance::StreamFailures);
        }
        if (uploaded) stream.cursor += stream.quads;
        stream.quads = stream.draws = 0;
        STATEMANAGER.RestoreRenderState(D3DRS_SRCBLEND);
        STATEMANAGER.RestoreRenderState(D3DRS_DESTBLEND);
        STATEMANAGER.SetRenderState(D3DRS_FOGENABLE, fog);
        STATEMANAGER.SetRenderState(D3DRS_LIGHTING, lighting);
    }

    void AppendQuad(CGraphicImageTexture* texture, float left, float top, float right, float bottom,
        float z, DWORD color, float u0, float v0, float u1, float v1)
    {
        auto& stream = Stream();
        if (stream.quads == stream.capacity) DrawQueuedText();
        const unsigned first = stream.quads++;
        SPDTVertexRaw* vertices = &stream.cpuVertices[first * 4];
        vertices[0] = { left, top, z, color, u0, v0 };
        vertices[1] = { left, bottom, z, color, u0, v1 };
        vertices[2] = { right, top, z, color, u1, v0 };
        vertices[3] = { right, bottom, z, color, u1, v1 };
        // Merge adjacent compatible runs only; painter order is unchanged.
        if (stream.draws && stream.commands[stream.draws - 1].texture == texture)
            ++stream.commands[stream.draws - 1].count;
        else stream.commands[stream.draws++] = { texture, first, 1 };
    }
}

void CGraphicTextInstance::CreateWorldTextDeviceObjects(IDirect3DDevice9Ex* device)
{
    auto& stream = Stream();
    stream.device = device;
    for (unsigned i = 0; i < MaxQuads; ++i)
    {
        WORD* indices = &stream.cpuIndices[i * 6];
        indices[0] = i * 4; indices[1] = i * 4 + 1; indices[2] = i * 4 + 2;
        indices[3] = i * 4 + 2; indices[4] = i * 4 + 1; indices[5] = i * 4 + 3;
    }
    if (!device || (stream.vertices && stream.indices)) return;
    D3DCAPS9 caps{};
    device->GetDeviceCaps(&caps);
    for (unsigned capacity = MaxQuads; capacity >= 256; capacity /= 2)
    {
        if (capacity * 4 - 1 > caps.MaxVertexIndex || capacity * 2 > caps.MaxPrimitiveCount) continue;
        HRESULT result = device->CreateVertexBuffer(capacity * 4 * sizeof(SPDTVertexRaw),
            D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, TPDTVertex::kFVF, D3DPOOL_DEFAULT, &stream.vertices, nullptr);
        if (SUCCEEDED(result)) result = device->CreateIndexBuffer(capacity * 6 * sizeof(WORD),
            D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &stream.indices, nullptr);
        if (SUCCEEDED(result))
        {
            void* indices = nullptr;
            result = stream.indices->Lock(0, 0, &indices, 0);
            if (SUCCEEDED(result))
            {
                std::memcpy(indices, stream.cpuIndices.data(), capacity * 6 * sizeof(WORD));
                result = stream.indices->Unlock();
            }
        }
        if (SUCCEEDED(result)) { stream.capacity = capacity; break; }
        Release(stream.vertices); Release(stream.indices);
    }
    if (CTextPerformance::Get().Enabled())
    {
        for (auto& frame : stream.gpuFrames)
        {
            if (FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &frame.start)) ||
                FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &frame.end)) ||
                FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &frame.frequency)) ||
                FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &frame.disjoint)))
            {
                Release(frame.start); Release(frame.end); Release(frame.frequency); Release(frame.disjoint);
            }
        }
    }
}

void CGraphicTextInstance::DestroyWorldTextDeviceObjects()
{
    auto& stream = Stream();
    stream.active = false;
    stream.quads = stream.draws = stream.cursor = 0;
    stream.capacity = MaxQuads;
    stream.gpuActive = -1;
    // The device owner unbinds the stream and resets its state cache.
    Release(stream.vertices); Release(stream.indices);
    for (auto& frame : stream.gpuFrames)
    {
        Release(frame.start); Release(frame.end); Release(frame.frequency); Release(frame.disjoint);
        frame.pending = false;
    }
    stream.device = nullptr;
}

void CGraphicTextInstance::BeginWorldTextBatch()
{
    auto& stream = Stream();
    DrawQueuedText();
    stream.active = true;
    stream.viewportValid = stream.device && SUCCEEDED(stream.device->GetViewport(&stream.viewport));
    if (CTextPerformance::Get().Enabled())
    {
        PollGPU();
        for (unsigned i = 0; i < stream.gpuFrames.size(); ++i)
        {
            auto& frame = stream.gpuFrames[i];
            if (!frame.start || frame.pending) continue;
            if (SUCCEEDED(frame.disjoint->Issue(D3DISSUE_BEGIN)) &&
                SUCCEEDED(frame.frequency->Issue(D3DISSUE_END)) && SUCCEEDED(frame.start->Issue(D3DISSUE_END)))
                stream.gpuActive = static_cast<int>(i);
            break;
        }
    }
}

bool CGraphicTextInstance::IsWorldTextBatchActive() { return Stream().active; }

void CGraphicTextInstance::FlushWorldTextBatch() { DrawQueuedText(); }

void CGraphicTextInstance::EndWorldTextBatch()
{
    auto& stream = Stream();
    DrawQueuedText();
    stream.active = false;
    if (stream.gpuActive >= 0)
    {
        auto& frame = stream.gpuFrames[stream.gpuActive];
        const HRESULT end = frame.end->Issue(D3DISSUE_END);
        const HRESULT disjoint = frame.disjoint->Issue(D3DISSUE_END);
        frame.pending = SUCCEEDED(end) && SUCCEEDED(disjoint);
        stream.gpuActive = -1;
    }
}

void CGraphicTextInstance::ClearWorldTextCache()
{
    GeometryCache().Clear();
    CTextPerformance::Get().CacheBytes(0);
}

bool CGraphicTextInstance::QueueWorldText(CGraphicFontTexture* font, RECT* clip)
{
    auto& stream = Stream();
    if (!stream.active) return false;
    auto& profile = CTextPerformance::Get();
    profile.Add(CTextPerformance::Labels);
    // Interactive/special labels retain the existing renderer at an order barrier.
    if (CTextPerformance::TutorialRenderer() || clip || m_isCursor || !m_hyperlinkVector.empty()
#ifdef ENABLE_EMOJI_SYSTEM
        || !m_emojiVector.empty()
#endif
        ) return false;

    const DWORD revision = font->GetGeometryRevision();
    if (!m_cachedGeometry || m_cachedGeometry->revision != revision)
    {
        auto& cache = GeometryCache();
        const TGeometryKey key(reinterpret_cast<std::uintptr_t>(m_roText.GetPointer()), revision,
            GetDefaultCodePage(), m_hAlign, m_vAlign, m_isMultiLine, m_isSecret, m_fLimitWidth, m_stText);
        auto found = cache.entries.find(key);
        if (found != cache.entries.end())
        {
            m_cachedGeometry = found->second.geometry;
            cache.recency.splice(cache.recency.begin(), cache.recency, found->second.recency);
            profile.Add(CTextPerformance::CacheHits);
        }
        else
        {
            auto geometry = std::make_shared<SCachedTextGeometry>();
            geometry->fontOwner = m_roText.GetPointer();
            geometry->revision = revision;
            geometry->glyphs.reserve(m_pCharInfoVector.size());
            profile.Add(CTextPerformance::CapacityGrowths);
            float startX = 0.0f, startY = 1.0f;
            if (GetDefaultCodePage() == CP_ARABIC)
            {
                if (m_hAlign == HORIZONTAL_ALIGN_LEFT) startX -= m_textWidth;
                else if (m_hAlign == HORIZONTAL_ALIGN_CENTER) startX -= float(m_textWidth / 2);
            }
            else
            {
                if (m_hAlign == HORIZONTAL_ALIGN_RIGHT) startX -= m_textWidth;
                else if (m_hAlign == HORIZONTAL_ALIGN_CENTER) startX -= float(m_textWidth / 2);
            }
            if (m_vAlign == VERTICAL_ALIGN_BOTTOM) startY -= m_textHeight;
            else if (m_vAlign == VERTICAL_ALIGN_CENTER) startY -= float(m_textHeight) / 2.0f;
            float x = startX, y = startY, lineHeight = 0.0f;
            for (std::size_t i = 0; i < m_pCharInfoVector.size(); ++i)
            {
                const auto* info = m_pCharInfoVector[i];
                if (x + info->width > m_fLimitWidth)
                {
                    if (!m_isMultiLine) break;
                    x = startX; y += lineHeight; lineHeight = 0.0f;
                }
                lineHeight = std::max<float>(lineHeight, info->height);
                auto* texture = font->GetTexture(info->index);
                if (texture && info->width > 0 && info->height > 0)
                {
                    SCachedTextGeometry::SGlyph glyph = { x - 0.5f, y - 0.5f,
                        x - 0.5f + info->width, y - 0.5f + info->height,
                        info->left, info->top, info->right, info->bottom, texture, i };
                    geometry->glyphs.push_back(glyph);
                    geometry->left = std::min<float>(geometry->left, glyph.left);
                    geometry->right = std::max<float>(geometry->right, glyph.right);
                    geometry->top = std::min<float>(geometry->top, glyph.top);
                    geometry->bottom = std::max<float>(geometry->bottom, glyph.bottom);
                }
                x += info->advance;
            }
            profile.Add(CTextPerformance::CacheBuilds);
            m_cachedGeometry = geometry;
            const std::size_t bytes = sizeof(SCachedTextGeometry) + geometry->glyphs.capacity() *
                sizeof(SCachedTextGeometry::SGlyph) + m_stText.size() * 2 + sizeof(SCacheEntry) + sizeof(TGeometryKey) * 2;
            if (bytes <= CacheBudget)
            {
                while (!cache.recency.empty() && (cache.bytes + bytes > CacheBudget || cache.entries.size() >= MaxCacheEntries))
                {
                    auto evicted = cache.entries.find(cache.recency.back());
                    cache.bytes -= evicted->second.bytes;
                    cache.entries.erase(evicted);
                    cache.recency.pop_back();
                }
                cache.recency.push_front(key);
                cache.entries.emplace(key, SCacheEntry{ geometry, bytes, cache.recency.begin() });
                cache.bytes += bytes;
            }
            profile.CacheBytes(cache.bytes);
        }
    }
    else profile.Add(CTextPerformance::CacheHits);

    const auto& geometry = *m_cachedGeometry;
    if (geometry.glyphs.empty()) return true;
    const float padding = m_isOutline ? 1.0f : 0.0f;
    // Visibility policy and picking lists are untouched; only wholly offscreen pixels are skipped.
    if (stream.viewportValid &&
        (m_v3Position.x + geometry.right + padding < stream.viewport.X ||
         m_v3Position.y + geometry.bottom + padding < stream.viewport.Y ||
         m_v3Position.x + geometry.left - padding > stream.viewport.X + stream.viewport.Width ||
         m_v3Position.y + geometry.top - padding > stream.viewport.Y + stream.viewport.Height))
    {
        profile.Add(CTextPerformance::Offscreen);
        return true;
    }
    if (m_isOutline)
    {
        static const float offsets[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
        for (const auto& glyph : geometry.glyphs)
            for (const auto& offset : offsets)
                AppendQuad(glyph.texture, m_v3Position.x + glyph.left + offset[0],
                    m_v3Position.y + glyph.top + offset[1], m_v3Position.x + glyph.right + offset[0],
                    m_v3Position.y + glyph.bottom + offset[1], m_v3Position.z, m_dwOutLineColor,
                    glyph.u0, glyph.v0, glyph.u1, glyph.v1);
    }
    for (const auto& glyph : geometry.glyphs)
        AppendQuad(glyph.texture, m_v3Position.x + glyph.left, m_v3Position.y + glyph.top,
            m_v3Position.x + glyph.right, m_v3Position.y + glyph.bottom, m_v3Position.z,
            m_dwColorInfoVector[glyph.colorIndex], glyph.u0, glyph.v0, glyph.u1, glyph.v1);
    return true;
}
#endif
