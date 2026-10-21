#include "StdAfx.h"
#include "GrpTextInstance.h"
#include "ShaderManager.h"
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
        ID3D11Query* start = nullptr;
        ID3D11Query* end = nullptr;
        ID3D11Query* disjoint = nullptr;
        bool pending = false;
    };
    struct STextStream
    {
        ID3D11Device* device = nullptr; // Lifetime belongs to CGraphicDevice.
        ID3D11Buffer* vertices = nullptr;
        ID3D11Buffer* indices = nullptr;
        unsigned capacity = MaxQuads, cursor = 0, quads = 0, draws = 0;
        bool active = false, viewportValid = false;
        D3D11_VIEWPORT viewport{};
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
        auto* context = CGraphicBase::GetContext();
        if (!context) return;
        for (auto& frame : Stream().gpuFrames)
        {
            if (!frame.pending) continue;
            UINT64 begin = 0, end = 0;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{};
            if (context->GetData(frame.end, &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                context->GetData(frame.start, &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                context->GetData(frame.disjoint, &timing, sizeof(timing), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
            frame.pending = false;
            if (!timing.Disjoint && timing.Frequency && end >= begin)
                CTextPerformance::Get().GPU(1000000.0 * static_cast<double>(end - begin) / timing.Frequency);
        }
    }

    void DrawQueuedText()
    {
        auto& stream = Stream();
        if (!stream.quads) return;
        auto& profile = CTextPerformance::Get();
        auto* context = CGraphicBase::GetContext();
        if (!context || !SHADERMANAGER.IsInitialized()) { stream.quads = stream.draws = 0; return; }
        SHADERMANAGER.PushState();
        SHADERMANAGER.BeginUI();
        SHADERMANAGER.SetTwoTextureBlend(false);
        SHADERMANAGER.SetAlphaTestEnabled(false);
        SHADERMANAGER.SetFogEnabled(false);
        SHADERMANAGER.SetLightingEnabled(false);
        SHADERMANAGER.SetPipelineState(PSTATE_BLENDENABLE, TRUE);
        SHADERMANAGER.SetPipelineState(PSTATE_SRCBLEND, BLEND_SRCALPHA);
        SHADERMANAGER.SetPipelineState(PSTATE_DESTBLEND, BLEND_INVSRCALPHA);
        SHADERMANAGER.SetDiffuseColor(1, 1, 1, 1);
        SHADERMANAGER.SetInputLayout(INPUT_LAYOUT_PDT);
        bool uploaded = false;
        if (stream.vertices && stream.indices)
        {
            if (stream.cursor + stream.quads > stream.capacity) stream.cursor = 0;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            profile.Add(CTextPerformance::Locks);
            const UINT bytes = stream.quads * 4 * sizeof(SPDTVertexRaw);
            if (SUCCEEDED(context->Map(stream.vertices, 0, stream.cursor ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(static_cast<BYTE*>(mapped.pData) + stream.cursor * 4 * sizeof(SPDTVertexRaw), stream.cpuVertices.data(), bytes);
                context->Unmap(stream.vertices, 0);
                uploaded = true;
                profile.Add(CTextPerformance::UploadBytes, bytes);
            }
            if (!uploaded) { stream.cursor = 0; profile.Add(CTextPerformance::StreamFailures); }
        }
        if (uploaded)
        {
            SHADERMANAGER.SetVertexBuffer(0, stream.vertices, sizeof(SPDTVertexRaw));
            SHADERMANAGER.SetIndexBuffer(stream.indices, DXGI_FORMAT_R16_UINT);
        }
        for (unsigned i = 0; i < stream.draws; ++i)
        {
            const auto& command = stream.commands[i];
            SHADERMANAGER.SetShaderResource(0, command.texture->GetSRV());
            if (uploaded)
                SHADERMANAGER.DrawIndexed(TOPOLOGY_TRIANGLELIST, command.first * 4, command.count * 4, command.first * 6, command.count * 2, stream.cursor * 4);
            else
            {
                profile.Add(CTextPerformance::FallbackDraws);
                SHADERMANAGER.DrawIndexedDynamic(TOPOLOGY_TRIANGLELIST, 0, command.count * 4, command.count * 2, stream.cpuIndices.data(), DXGI_FORMAT_R16_UINT, &stream.cpuVertices[command.first * 4], sizeof(SPDTVertexRaw));
            }
            profile.Add(CTextPerformance::Draws);
        }
        if (uploaded) stream.cursor += stream.quads;
        stream.quads = stream.draws = 0;
        SHADERMANAGER.PopState();
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

void CGraphicTextInstance::CreateWorldTextDeviceObjects(ID3D11Device* device)
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
    for (unsigned capacity = MaxQuads; capacity >= 256; capacity /= 2)
    {
        D3D11_BUFFER_DESC vb{};
        vb.ByteWidth = capacity * 4 * sizeof(SPDTVertexRaw);
        vb.Usage = D3D11_USAGE_DYNAMIC; vb.BindFlags = D3D11_BIND_VERTEX_BUFFER; vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        HRESULT result = device->CreateBuffer(&vb, nullptr, &stream.vertices);
        D3D11_BUFFER_DESC ib{};
        ib.ByteWidth = capacity * 6 * sizeof(WORD); ib.Usage = D3D11_USAGE_IMMUTABLE; ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = stream.cpuIndices.data();
        if (SUCCEEDED(result)) result = device->CreateBuffer(&ib, &initial, &stream.indices);
        if (SUCCEEDED(result)) { stream.capacity = capacity; break; }
        Release(stream.vertices); Release(stream.indices);
    }
    if (CTextPerformance::Get().Enabled())
        for (auto& frame : stream.gpuFrames)
        {
            D3D11_QUERY_DESC stamp{D3D11_QUERY_TIMESTAMP, 0}, timing{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            if (FAILED(device->CreateQuery(&stamp, &frame.start)) || FAILED(device->CreateQuery(&stamp, &frame.end)) || FAILED(device->CreateQuery(&timing, &frame.disjoint)))
            { Release(frame.start); Release(frame.end); Release(frame.disjoint); }
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
        Release(frame.start); Release(frame.end); Release(frame.disjoint);
        frame.pending = false;
    }
    stream.device = nullptr;
}

void CGraphicTextInstance::BeginWorldTextBatch()
{
    auto& stream = Stream();
    DrawQueuedText();
    stream.active = true;
    auto* context = CGraphicBase::GetContext();
    UINT count = 1;
    if (context) context->RSGetViewports(&count, &stream.viewport);
    stream.viewportValid = context && count == 1;
    if (CTextPerformance::Get().Enabled() && context)
    {
        PollGPU();
        for (unsigned i = 0; i < stream.gpuFrames.size(); ++i)
        {
            auto& frame = stream.gpuFrames[i];
            if (!frame.start || frame.pending) continue;
            context->Begin(frame.disjoint); context->End(frame.start);
            stream.gpuActive = static_cast<int>(i); break;
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
        auto* context = CGraphicBase::GetContext();
        if (context) { context->End(frame.end); context->End(frame.disjoint); frame.pending = true; }
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
                    SCachedTextGeometry::SGlyph glyph = { x, y,
                        x + info->width, y + info->height,
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

    const float originX = floorf(m_v3Position.x + 0.5f), originY = floorf(m_v3Position.y + 0.5f);
    const auto& geometry = *m_cachedGeometry;
    if (geometry.glyphs.empty()) return true;
    const float padding = m_isOutline ? 1.0f : 0.0f;
    // Visibility policy and picking lists are untouched; only wholly offscreen pixels are skipped.
    if (stream.viewportValid &&
        (m_v3Position.x + geometry.right + padding < stream.viewport.TopLeftX ||
         m_v3Position.y + geometry.bottom + padding < stream.viewport.TopLeftY ||
         m_v3Position.x + geometry.left - padding > stream.viewport.TopLeftX + stream.viewport.Width ||
         m_v3Position.y + geometry.top - padding > stream.viewport.TopLeftY + stream.viewport.Height))
    {
        profile.Add(CTextPerformance::Offscreen);
        return true;
    }
    if (m_isOutline)
    {
        static const float offsets[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
        for (const auto& glyph : geometry.glyphs)
            for (const auto& offset : offsets)
                AppendQuad(glyph.texture, originX + glyph.left + offset[0],
                    originY + glyph.top + offset[1], originX + glyph.right + offset[0],
                    originY + glyph.bottom + offset[1], m_v3Position.z, m_dwOutLineColor,
                    glyph.u0, glyph.v0, glyph.u1, glyph.v1);
    }
    for (const auto& glyph : geometry.glyphs)
        AppendQuad(glyph.texture, originX + glyph.left, originY + glyph.top,
            originX + glyph.right, originY + glyph.bottom, m_v3Position.z,
            m_dwColorInfoVector[glyph.colorIndex], glyph.u0, glyph.v0, glyph.u1, glyph.v1);
    return true;
}
#endif
