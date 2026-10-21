#include "StdAfx.h"
#include "GPUSkinning.h"
#include "GPUSkinningShader.h"
#include "../EterLib/StateManager.h"
#include "../EterBase/MapPerformance.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "d3dcompiler.lib")

namespace GPUSkinning
{
namespace
{
template<class T> void Release(T*& value)
{
    if (value) { value->Release(); value = nullptr; }
}
struct DrawConstants
{
    D3DXMATRIX wvp, worldView;
    D3DXVECTOR4 normalColumns[3];
    D3DXMATRIX texture[2];
    D3DXVECTOR4 initial, fog, flags, textureModes, reserved;
    D3DXVECTOR4 lights[4][3];
    D3DXVECTOR4 compilerLiterals[4];
};
static_assert(sizeof(DrawConstants) == 40 * sizeof(D3DXVECTOR4));

struct Pipeline
{
    IDirect3DDevice9* device = nullptr; // Owned by CGraphicDevice.
    IDirect3DVertexShader9* shader = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    bool attempted = false, commonValid = false;
    unsigned long long commonRevision = 0;
    DWORD textureFlags[2]{};
    DrawConstants common;
    D3DXMATRIX view, projection;
};
Pipeline& State() { static Pipeline state; return state; }

float FloatState(D3DRENDERSTATETYPE state)
{
    const DWORD bits = STATEMANAGER.GetRenderState(state);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool BuildCommonConstants(DrawConstants& out)
{
    if (STATEMANAGER.GetRenderState(D3DRS_CLIPPLANEENABLE) ||
        STATEMANAGER.GetRenderState(D3DRS_VERTEXBLEND) != D3DVBF_DISABLE ||
        STATEMANAGER.GetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE))
    {
        ReportFallback("custom_shader_or_vertex_state");
        return false;
    }
    for (unsigned stage = 2; stage < 8; ++stage)
    {
        DWORD operation;
        STATEMANAGER.GetTextureStageState(stage, D3DTSS_COLOROP, &operation);
        if (operation != D3DTOP_DISABLE)
        {
            ReportFallback("more_than_two_texture_stages");
            return false;
        }
    }

    D3DXMATRIX view;
    STATEMANAGER.GetTransform(D3DTS_VIEW, &view);

    for (unsigned stage = 0; stage < 2; ++stage)
    {
        DWORD coordinates, flags;
        STATEMANAGER.GetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, &coordinates);
        STATEMANAGER.GetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, &flags);
        const DWORD generation = coordinates & 0xffff0000;
        float mode = 0;
        if (generation == D3DTSS_TCI_CAMERASPACENORMAL) mode = 2;
        else if (generation == D3DTSS_TCI_CAMERASPACEPOSITION) mode = 3;
        else if (generation == D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR) mode = 4;
        else if (generation || (coordinates & 0xffff) > 1)
        {
            ReportFallback("unsupported_texture_coordinates");
            return false;
        }
        else mode = static_cast<float>(coordinates & 0xffff);
        out.textureModes[stage] = mode;
        out.textureModes[stage + 2] = (flags & D3DTTFF_PROJECTED) ?
            static_cast<float>(flags & 0xff) : 0.0f;
        if (flags == D3DTTFF_DISABLE) D3DXMatrixIdentity(&out.texture[stage]);
        else STATEMANAGER.GetTransform(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + stage),
            &out.texture[stage]);
    }

    const bool lighting = STATEMANAGER.GetRenderState(D3DRS_LIGHTING) != FALSE;
    out.flags = D3DXVECTOR4(
        static_cast<float>(STATEMANAGER.GetRenderState(D3DRS_NORMALIZENORMALS) != FALSE),
        static_cast<float>(STATEMANAGER.GetRenderState(D3DRS_RANGEFOGENABLE) != FALSE),
        static_cast<float>(lighting), 0);
    out.fog = D3DXVECTOR4(FloatState(D3DRS_FOGSTART), FloatState(D3DRS_FOGEND),
        FloatState(D3DRS_FOGDENSITY), STATEMANAGER.GetRenderState(D3DRS_FOGENABLE) ?
        static_cast<float>(STATEMANAGER.GetRenderState(D3DRS_FOGVERTEXMODE)) : 0.0f);
    out.initial = D3DXVECTOR4(1, 1, 1, 1);
    if (!lighting) return true;
    if (STATEMANAGER.GetRenderState(D3DRS_SPECULARENABLE) ||
        (STATEMANAGER.GetRenderState(D3DRS_COLORVERTEX) &&
            (STATEMANAGER.GetRenderState(D3DRS_DIFFUSEMATERIALSOURCE) != D3DMCS_MATERIAL ||
             STATEMANAGER.GetRenderState(D3DRS_AMBIENTMATERIALSOURCE) != D3DMCS_MATERIAL ||
             STATEMANAGER.GetRenderState(D3DRS_EMISSIVEMATERIALSOURCE) != D3DMCS_MATERIAL)))
    {
        ReportFallback("vertex_specular_or_color_material");
        return false;
    }
    D3DMATERIAL9 material;
    STATEMANAGER.GetMaterial(&material);
    const D3DXCOLOR ambient(STATEMANAGER.GetRenderState(D3DRS_AMBIENT));
    out.initial = D3DXVECTOR4(material.Emissive.r + material.Ambient.r * ambient.r,
        material.Emissive.g + material.Ambient.g * ambient.g,
        material.Emissive.b + material.Ambient.b * ambient.b, material.Diffuse.a);
    unsigned active = 0;
    for (unsigned index = 0; index < 8; ++index)
    {
        if (!STATEMANAGER.GetLightEnable(index)) continue;
        D3DLIGHT9 light;
        STATEMANAGER.GetLight(index, &light);
        if (active == 4 || (light.Type != D3DLIGHT_DIRECTIONAL && light.Type != D3DLIGHT_POINT))
        {
            ReportFallback("light_count_or_spotlight");
            return false;
        }
        auto& values = out.lights[active++];
        D3DXVECTOR3 vector;
        if (light.Type == D3DLIGHT_DIRECTIONAL)
        {
            out.initial.x += light.Ambient.r * material.Ambient.r;
            out.initial.y += light.Ambient.g * material.Ambient.g;
            out.initial.z += light.Ambient.b * material.Ambient.b;
            const D3DXVECTOR3 direction(-light.Direction.x, -light.Direction.y, -light.Direction.z);
            D3DXVec3TransformNormal(&vector, &direction, &view);
            D3DXVec3Normalize(&vector, &vector);
            values[0] = D3DXVECTOR4(vector.x, vector.y, vector.z, 1);
        }
        else
        {
            if (light.Ambient.r != 0 || light.Ambient.g != 0 || light.Ambient.b != 0)
            {
                ReportFallback("point_light_ambient");
                return false;
            }
            const D3DXVECTOR3 position(light.Position.x, light.Position.y, light.Position.z);
            D3DXVec3TransformCoord(&vector, &position, &view);
            values[0] = D3DXVECTOR4(vector.x, vector.y, vector.z, 2);
        }
        values[1] = D3DXVECTOR4(light.Diffuse.r * material.Diffuse.r,
            light.Diffuse.g * material.Diffuse.g, light.Diffuse.b * material.Diffuse.b, 0);
        values[2] = D3DXVECTOR4(light.Attenuation0, light.Attenuation1, light.Attenuation2, light.Range);
    }
    return true;
}

bool BuildConstants(DrawConstants& out)
{
    IDirect3DVertexShader9* shader = nullptr;
    IDirect3DPixelShader9* pixelShader = nullptr;
    STATEMANAGER.GetVertexShader(&shader);
    STATEMANAGER.GetPixelShader(&pixelShader);
    if (shader || pixelShader)
    {
        ReportFallback("custom_shader_or_vertex_state");
        return false;
    }
    auto& cache = State();
    DWORD flags[2];
    for (unsigned stage = 0; stage < 2; ++stage)
        STATEMANAGER.GetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, &flags[stage]);
    const auto revision = STATEMANAGER.GetVertexProcessingRevision();
    if (!cache.commonValid || cache.commonRevision != revision ||
        memcmp(cache.textureFlags, flags, sizeof(flags)) != 0)
    {
        cache.commonValid = false;
        memset(&cache.common, 0, sizeof(cache.common));
        if (!BuildCommonConstants(cache.common)) return false;
        STATEMANAGER.GetTransform(D3DTS_VIEW, &cache.view);
        STATEMANAGER.GetTransform(D3DTS_PROJECTION, &cache.projection);
        memcpy(cache.textureFlags, flags, sizeof(flags));
        cache.commonRevision = revision;
        cache.commonValid = true;
        MapPerformance::Count(MapPerformance::Counter::GPUSkinCommonBuilds);
    }
    else MapPerformance::Count(MapPerformance::Counter::GPUSkinCommonReuse);
    out = cache.common;
    D3DXMATRIX world, normalMatrix;
    STATEMANAGER.GetTransform(D3DTS_WORLD, &world);
    // Preserve the original multiplication order and inverse transpose,
    // including nonuniform scale and generated/reflection texture coordinates.
    D3DXMatrixMultiply(&out.worldView, &world, &cache.view);
    D3DXMatrixMultiply(&out.wvp, &out.worldView, &cache.projection);
    // Shadow/unlit passes with ordinary UVs do not consume normals. Retain
    // the full path for lighting, generated normals, and reflection mapping.
    const auto needsNormal = [](float mode) { return mode == 2.0f || mode == 4.0f; };
    if (out.flags.z == 0 && !needsNormal(out.textureModes.x) && !needsNormal(out.textureModes.y))
    {
        out.normalColumns[0] = D3DXVECTOR4(1, 0, 0, 0);
        out.normalColumns[1] = D3DXVECTOR4(0, 1, 0, 0);
        out.normalColumns[2] = D3DXVECTOR4(0, 0, 1, 0);
        MapPerformance::Count(MapPerformance::Counter::GPUSkinUnlitMatrices);
        return true;
    }
    if (!D3DXMatrixInverse(&normalMatrix, nullptr, &out.worldView))
    {
        ReportFallback("singular_world_view");
        return false;
    }
    D3DXMatrixTranspose(&normalMatrix, &normalMatrix);
    for (unsigned column = 0; column < 3; ++column)
        out.normalColumns[column] = D3DXVECTOR4(normalMatrix(0, column),
            normalMatrix(1, column), normalMatrix(2, column), 0);
    return true;
}
}

bool Enabled()
{
    static const bool enabled = [] {
        const char* setting = std::getenv("M2_GPU_SKINNING");
        return !setting || std::strcmp(setting, "0") != 0;
    }();
    return enabled;
}

void ReportFallback(const char* reason)
{
    // Reasons have static lifetime. Log each distinct reason once, not per actor.
    static std::array<const char*, 32> reported{};
    for (const auto* previous : reported)
        if (previous && std::strcmp(previous, reason) == 0) return;
    for (auto& slot : reported)
        if (!slot) { slot = reason; TraceError("GPU_SKINNING_FALLBACK reason=%s", reason); break; }
}

void Destroy()
{
    auto& state = State();
    Release(state.shader);
    Release(state.declaration);
    state = {};
}

bool Available(IDirect3DDevice9* device)
{
    if (!Enabled() || !device) return false;
    auto& state = State();
    if (state.device != device) { Destroy(); state.device = device; }
    if (state.attempted) return state.shader && state.declaration;
    state.attempted = true;
    D3DCAPS9 caps{};
    D3DDEVICE_CREATION_PARAMETERS creation{};
    if (FAILED(device->GetDeviceCaps(&caps)) || FAILED(device->GetCreationParameters(&creation)) ||
        caps.VertexShaderVersion < D3DVS_VERSION(3, 0) || caps.MaxVertexShaderConst < 256 ||
        !(caps.DeclTypes & D3DDTCAPS_UBYTE4) || !(caps.DeclTypes & D3DDTCAPS_UBYTE4N) ||
        (creation.BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) || device->GetSoftwareVertexProcessing())
    {
        ReportFallback("hardware_capabilities");
        return false;
    }
    ID3DBlob* bytecode = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT result = D3DCompile(GPUSkinningShaderSource, sizeof(GPUSkinningShaderSource) - 1,
        "GPU_skinning", nullptr, nullptr, "main", "vs_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &bytecode, &errors);
    if (FAILED(result))
        TraceError("GPU_SKINNING_SHADER_FAILED hr=0x%08x message=%s", static_cast<unsigned>(result),
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no compiler diagnostics");
    if (SUCCEEDED(result)) result = device->CreateVertexShader(
        static_cast<const DWORD*>(bytecode->GetBufferPointer()), &state.shader);
    Release(bytecode); Release(errors);
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0},
        {0, 16, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
        {0, 20, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 32, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 40, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        D3DDECL_END()
    };
    if (SUCCEEDED(result)) result = device->CreateVertexDeclaration(elements, &state.declaration);
    if (FAILED(result))
    {
        Release(state.shader); Release(state.declaration);
        ReportFallback("shader_or_declaration_creation");
        return false;
    }
    TraceError("GPU_SKINNING_READY max_bones=%u max_weights=4 path=DX9_vertex_shader toggle=M2_GPU_SKINNING",
        MaxBones);
    return true;
}

DrawScope::DrawScope(IDirect3DDevice9* device, IDirect3DVertexBuffer9* vertices,
    const D3DXMATRIX* compositeBones, const granny_int32x* boneIndices, unsigned boneCount,
    PaletteCache* paletteCache, std::uint64_t poseRevision)
{
    if (!vertices || !compositeBones || !boneIndices || !boneCount || boneCount > MaxBones ||
        !Available(device)) return;
    MapPerformance::Scope profile("actor.gpu_skin_submit");
    DrawConstants constants;
    // BuildConstants copies the completely initialized common block.
    if (!BuildConstants(constants)) return;
    D3DXVECTOR4 palette[MaxBones * 3];
    D3DXVECTOR4* columns = palette;
    const bool reuse = paletteCache && poseRevision && paletteCache->revision == poseRevision &&
        paletteCache->binding == boneIndices && paletteCache->columns.size() == boneCount * 3;
    if (paletteCache)
    {
        if (!reuse) paletteCache->columns.resize(boneCount * 3);
        columns = paletteCache->columns.data();
    }
    if (reuse) MapPerformance::Count(MapPerformance::Counter::GPUSkinPaletteReuse);
    else for (unsigned bone = 0; bone < boneCount; ++bone)
    {
        const auto& matrix = compositeBones[boneIndices[bone]];
        for (unsigned column = 0; column < 3; ++column)
            columns[bone * 3 + column] = D3DXVECTOR4(matrix(0, column), matrix(1, column),
                matrix(2, column), matrix(3, column));
    }
    if (paletteCache)
    {
        paletteCache->revision = poseRevision;
        paletteCache->binding = boneIndices;
    }
    STATEMANAGER.GetFVF(&m_fvf);
    STATEMANAGER.GetVertexDeclaration(&m_declaration);
    STATEMANAGER.SaveStreamSource(0, vertices, sizeof(TGPUSkinVertex));
    STATEMANAGER.SetVertexDeclaration(State().declaration);
    STATEMANAGER.SetVertexShader(State().shader);
    // Per-instance matrices change each draw. Lighting/fog/UV state normally
    // stays identical for thousands of monsters, so upload it independently.
    STATEMANAGER.SetVertexShaderConstant(0, &constants, 11);
    STATEMANAGER.SetVertexShaderConstant(11, &constants.texture[0], 25);
    STATEMANAGER.SetVertexShaderConstant(40, columns, boneCount * 3);
    for (unsigned stage = 0; stage < 2; ++stage)
    {
        STATEMANAGER.GetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, &m_textureFlags[stage]);
        // Projection is a pixel-pipeline operation; preserve it. Texture matrix
        // multiplication and coordinate generation have moved into the VS.
        if (!(m_textureFlags[stage] & D3DTTFF_PROJECTED))
            STATEMANAGER.SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    }
    m_bound = true;
}

DrawScope::~DrawScope()
{
    if (!m_bound) return;
    STATEMANAGER.SetVertexShader(nullptr);
    if (m_fvf) STATEMANAGER.SetFVF(m_fvf);
    else STATEMANAGER.SetVertexDeclaration(m_declaration);
    STATEMANAGER.RestoreStreamSource(0);
    for (unsigned stage = 0; stage < 2; ++stage)
        STATEMANAGER.SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, m_textureFlags[stage]);
}
}
