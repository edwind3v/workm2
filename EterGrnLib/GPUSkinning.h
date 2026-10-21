#pragma once

#include <granny.h>
#include "../EterLib/GrpBase.h"
#include <cstdint>

// Mesh-local palette indices match Granny's mesh binding, not skeleton indices.
// Keep all four weights, including zero-weight slots, in the original byte form.
struct TGPUSkinVertex
{
    float position[3];
    BYTE weights[4];
    BYTE indices[4];
    float normal[3];
    float uv[2];
    float uv1[2];
};
static_assert(sizeof(TGPUSkinVertex) == 48);

namespace GPUSkinning
{
constexpr unsigned MaxBones = 72;
bool Enabled();
bool Available(IDirect3DDevice9* device);
void Destroy();
void ReportFallback(const char* reason);

struct PaletteCache
{
    std::vector<D3DXVECTOR4> columns;
    std::uint64_t revision = 0;
    const granny_int32x* binding = nullptr;
};

// Restores the fixed-function vertex declaration, shader, stream, and texture
// projection state before rigid meshes, effects, or UI can use the device.
class DrawScope
{
public:
    DrawScope(IDirect3DDevice9* device, IDirect3DVertexBuffer9* vertices,
        const D3DXMATRIX* compositeBones, const granny_int32x* boneIndices,
        unsigned boneCount, PaletteCache* paletteCache = nullptr, std::uint64_t poseRevision = 0);
    ~DrawScope();
    explicit operator bool() const { return m_bound; }
    DrawScope(const DrawScope&) = delete;
    DrawScope& operator=(const DrawScope&) = delete;
private:
    bool m_bound = false;
    DWORD m_fvf = 0;
    DWORD m_textureFlags[2]{};
    IDirect3DVertexDeclaration9* m_declaration = nullptr;
};
}
