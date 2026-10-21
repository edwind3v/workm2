#include "StdAfx.h"
#include "../eterBase/Random.h"
#include "../eterLib/StateManager.h"
#include "ParticleSystemData.h"
#include "ParticleSystemInstance.h"
#include "ParticleInstance.h"
#include "../EterBase/MapPerformance.h"
#include "../EterLib/Camera.h"
#include <cmath>
#include <cstdlib>
#include <array>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

namespace NParticleMultiTexture { void Destroy(); }

CDynamicPool<CParticleSystemInstance>	CParticleSystemInstance::ms_kPool;

using namespace NEffectUpdateDecorator;

void CParticleSystemInstance::DestroySystem()
{
	NParticleMultiTexture::Destroy();
	ms_kPool.Destroy();

	CParticleInstance::DestroySystem();
	//CRayParticleInstance::DestroySystem();
}

CParticleSystemInstance* CParticleSystemInstance::New()
{
	return ms_kPool.Alloc();
}

void CParticleSystemInstance::Delete(CParticleSystemInstance* pkPSInst)
{
	pkPSInst->Destroy();
	ms_kPool.Free(pkPSInst);
}

DWORD CParticleSystemInstance::GetEmissionCount() const
{
	return m_dwCurrentEmissionCount;
}

void CParticleSystemInstance::CreateParticles(float fElapsedTime)
{
	float fEmissionCount;
	m_pEmitterProperty->GetEmissionCountPerSecond(m_fLocalTime, &fEmissionCount);

	const float fCreatingValue = fEmissionCount * (fElapsedTime / 1.0f) + m_fEmissionResidue;
	int iCreatingCount = int(fCreatingValue);
	m_fEmissionResidue = fCreatingValue - iCreatingCount;

	const int icurEmissionCount = GetEmissionCount();
	const int iMaxEmissionCount = int(m_pEmitterProperty->GetMaxEmissionCount());
	const int iNextEmissionCount = int(icurEmissionCount + iCreatingCount);
	iCreatingCount -= max(0, iNextEmissionCount - iMaxEmissionCount);

	float fLifeTime = 0.0f;
	float fEmittingSize = 0.0f;
	D3DXVECTOR3 _v3TimePosition;
	D3DXVECTOR3 _v3Velocity;
	float fVelocity = 0.0f;
	D3DXVECTOR2 v2HalfSize;
	float fLieRotation = 0;
	if (iCreatingCount)
	{
		m_pEmitterProperty->GetParticleLifeTime(m_fLocalTime, &fLifeTime);
		if (fLifeTime==0.0f)
		{
			return;
		}

		m_pEmitterProperty->GetEmittingSize(m_fLocalTime, &fEmittingSize);

		m_pData->GetPosition(m_fLocalTime, _v3TimePosition);

		m_pEmitterProperty->GetEmittingDirectionX(m_fLocalTime, &_v3Velocity.x);
		m_pEmitterProperty->GetEmittingDirectionY(m_fLocalTime, &_v3Velocity.y);
		m_pEmitterProperty->GetEmittingDirectionZ(m_fLocalTime, &_v3Velocity.z);

		m_pEmitterProperty->GetEmittingVelocity(m_fLocalTime, &fVelocity);

		m_pEmitterProperty->GetParticleSizeX(m_fLocalTime, &v2HalfSize.x);
		m_pEmitterProperty->GetParticleSizeY(m_fLocalTime, &v2HalfSize.y);

		if (BILLBOARD_TYPE_LIE == m_pParticleProperty->m_byBillboardType && mc_pmatLocal)
		{
			const float fsx = mc_pmatLocal->_32;
			const float fcx = sqrtf(1.0f - fsx * fsx);

			if (fcx >= 0.00001f)
				fLieRotation = D3DXToDegree(atan2f(-mc_pmatLocal->_12, mc_pmatLocal->_22));
		}

	}

	CParticleInstance * pFirstInstance = nullptr;

	for (int i = 0; i < iCreatingCount; ++i)
	{
		CParticleInstance * pInstance;

		pInstance = CParticleInstance::New();
		pInstance->m_pParticleProperty = m_pParticleProperty;
		pInstance->m_pEmitterProperty = m_pEmitterProperty;

		// LifeTime
		pInstance->m_fLifeTime = fLifeTime;
		pInstance->m_fLastLifeTime = fLifeTime;

		// Position
		switch (m_pEmitterProperty->GetEmitterShape())
		{
			case CEmitterProperty::EMITTER_SHAPE_POINT:
				pInstance->m_v3Position.x = 0.0f;
				pInstance->m_v3Position.y = 0.0f;
				pInstance->m_v3Position.z = 0.0f;
				break;

			case CEmitterProperty::EMITTER_SHAPE_ELLIPSE:
				pInstance->m_v3Position.x = frandom(-500.0f, 500.0f);
				pInstance->m_v3Position.y = frandom(-500.0f, 500.0f);
				pInstance->m_v3Position.z = 0.0f;
				D3DXVec3Normalize(&pInstance->m_v3Position, &pInstance->m_v3Position);

				if (m_pEmitterProperty->isEmitFromEdge())
				{
					pInstance->m_v3Position *= (m_pEmitterProperty->m_fEmittingRadius + fEmittingSize);
				}
				else
				{
					pInstance->m_v3Position *= (frandom(0.0f, m_pEmitterProperty->m_fEmittingRadius) + fEmittingSize);
				}
				break;

			case CEmitterProperty::EMITTER_SHAPE_SQUARE:
				pInstance->m_v3Position.x = (frandom(-m_pEmitterProperty->m_v3EmittingSize.x/2.0f, m_pEmitterProperty->m_v3EmittingSize.x/2.0f) + fEmittingSize);
				pInstance->m_v3Position.y = (frandom(-m_pEmitterProperty->m_v3EmittingSize.y/2.0f, m_pEmitterProperty->m_v3EmittingSize.y/2.0f) + fEmittingSize);
				pInstance->m_v3Position.z = (frandom(-m_pEmitterProperty->m_v3EmittingSize.z/2.0f, m_pEmitterProperty->m_v3EmittingSize.z/2.0f) + fEmittingSize);
				break;

			case CEmitterProperty::EMITTER_SHAPE_SPHERE:
				pInstance->m_v3Position.x = frandom(-500.0f, 500.0f);
				pInstance->m_v3Position.y = frandom(-500.0f, 500.0f);
				pInstance->m_v3Position.z = frandom(-500.0f, 500.0f);
				D3DXVec3Normalize(&pInstance->m_v3Position, &pInstance->m_v3Position);

				if (m_pEmitterProperty->isEmitFromEdge())
				{
					pInstance->m_v3Position *= (m_pEmitterProperty->m_fEmittingRadius + fEmittingSize);
				}
				else
				{
					pInstance->m_v3Position *= (frandom(0.0f, m_pEmitterProperty->m_fEmittingRadius) + fEmittingSize);
				}
				break;
		}

		// Position
		D3DXVECTOR3 v3TimePosition=_v3TimePosition;

		pInstance->m_v3Position += v3TimePosition;

		if (mc_pmatLocal && !m_pParticleProperty->m_bAttachFlag)
		{
			D3DXVec3TransformCoord(&pInstance->m_v3Position,&pInstance->m_v3Position,mc_pmatLocal);
			D3DXVec3TransformCoord(&v3TimePosition, &v3TimePosition, mc_pmatLocal);
		}
		pInstance->m_v3StartPosition = v3TimePosition;
		//pInstance->m_v3LastPosition = pInstance->m_v3Position;

		// Direction & Velocity
		pInstance->m_v3Velocity.x = 0.0f;
		pInstance->m_v3Velocity.y = 0.0f;
		pInstance->m_v3Velocity.z = 0.0f;

		if (CEmitterProperty::EMITTER_ADVANCED_TYPE_INNER == m_pEmitterProperty->GetEmitterAdvancedType())
		{
			auto val = (pInstance->m_v3Position-v3TimePosition);
			D3DXVec3Normalize(&pInstance->m_v3Velocity, &val);
			pInstance->m_v3Velocity *= -100.0f;
		}
		else if (CEmitterProperty::EMITTER_ADVANCED_TYPE_OUTER == m_pEmitterProperty->GetEmitterAdvancedType())
		{
			if (m_pEmitterProperty->GetEmitterShape() == CEmitterProperty::EMITTER_SHAPE_POINT)
			{
				pInstance->m_v3Velocity.x = frandom(-100.0f, 100.0f);
				pInstance->m_v3Velocity.y = frandom(-100.0f, 100.0f);
				pInstance->m_v3Velocity.z = frandom(-100.0f, 100.0f);
			}
			else
			{
				auto val = (pInstance->m_v3Position-v3TimePosition);
				D3DXVec3Normalize(&pInstance->m_v3Velocity, &val);
				pInstance->m_v3Velocity *= 100.0f;
			}
		}

		D3DXVECTOR3 v3Velocity = _v3Velocity;
		if (mc_pmatLocal && !m_pParticleProperty->m_bAttachFlag)
		{
			D3DXVec3TransformNormal(&v3Velocity, &v3Velocity, mc_pmatLocal);
		}

		pInstance->m_v3Velocity += v3Velocity;
		if (m_pEmitterProperty->m_v3EmittingDirection.x > 0.0f)
			pInstance->m_v3Velocity.x += frandom(-m_pEmitterProperty->m_v3EmittingDirection.x/2.0f, m_pEmitterProperty->m_v3EmittingDirection.x/2.0f) * 1000.0f;
		if (m_pEmitterProperty->m_v3EmittingDirection.y > 0.0f)
			pInstance->m_v3Velocity.y += frandom(-m_pEmitterProperty->m_v3EmittingDirection.y/2.0f, m_pEmitterProperty->m_v3EmittingDirection.y/2.0f) * 1000.0f;
		if (m_pEmitterProperty->m_v3EmittingDirection.z > 0.0f)
			pInstance->m_v3Velocity.z += frandom(-m_pEmitterProperty->m_v3EmittingDirection.z/2.0f, m_pEmitterProperty->m_v3EmittingDirection.z/2.0f) * 1000.0f;

		pInstance->m_v3Velocity *= fVelocity;

		// Size
		pInstance->m_v2HalfSize = v2HalfSize;

		// Rotation
		pInstance->m_fRotation = m_pParticleProperty->m_wRotationRandomStartingBegin;
		pInstance->m_fRotation = frandom(m_pParticleProperty->m_wRotationRandomStartingBegin,m_pParticleProperty->m_wRotationRandomStartingEnd);
		if (BILLBOARD_TYPE_LIE == m_pParticleProperty->m_byBillboardType && mc_pmatLocal)
		{
			pInstance->m_fRotation += fLieRotation;
		}

		// Texture Animation
		pInstance->m_byFrameIndex = 0;
		pInstance->m_byTextureAnimationType = m_pParticleProperty->GetTextureAnimationType();

		if (m_pParticleProperty->GetTextureAnimationFrameCount() > 1)
		{
			if (CParticleProperty::TEXTURE_ANIMATION_TYPE_RANDOM_DIRECTION == m_pParticleProperty->GetTextureAnimationType())
			{
				if (random() & 1)
				{
					pInstance->m_byFrameIndex = 0;
					pInstance->m_byTextureAnimationType = CParticleProperty::TEXTURE_ANIMATION_TYPE_CW;
				}
				else
				{
					pInstance->m_byFrameIndex = m_pParticleProperty->GetTextureAnimationFrameCount() - 1;
					pInstance->m_byTextureAnimationType = CParticleProperty::TEXTURE_ANIMATION_TYPE_CCW;
				}
			}
			if (m_pParticleProperty->m_bTexAniRandomStartFrameFlag)
			{
				pInstance->m_byFrameIndex = random_range(0,m_pParticleProperty->GetTextureAnimationFrameCount()-1);
			}
		}

		// Simple Update
		{
			pInstance->m_v3LastPosition = pInstance->m_v3Position - (pInstance->m_v3Velocity * fElapsedTime);
			pInstance->m_v2Scale.x = m_pParticleProperty->m_TimeEventScaleX.front().m_Value;
			pInstance->m_v2Scale.y= m_pParticleProperty->m_TimeEventScaleY.front().m_Value;
			//pInstance->m_v2Scale = m_pParticleProperty->m_TimeEventScaleXY.front().m_Value;
#ifdef WORLD_EDITOR
			pInstance->m_Color.r = m_pParticleProperty->m_TimeEventColorRed.front().m_Value;
			pInstance->m_Color.g = m_pParticleProperty->m_TimeEventColorGreen.front().m_Value;
			pInstance->m_Color.b = m_pParticleProperty->m_TimeEventColorBlue.front().m_Value;
			pInstance->m_Color.a = m_pParticleProperty->m_TimeEventAlpha.front().m_Value;
#else
			pInstance->m_dcColor = m_pParticleProperty->m_TimeEventColor.front().m_Value;
#endif
		}

        if (CParticleInstance::CompactUpdateEnabled())
        {
            pInstance->InitializeCompactUpdate(pFirstInstance);
            if (!pFirstInstance) pFirstInstance = pInstance;
        }
		else if (!pFirstInstance)
		{
			m_pData->BuildDecorator(pInstance);
			pFirstInstance = pInstance;
		}
		else
		{
			pInstance->m_pDecorator = pFirstInstance->m_pDecorator->Clone(pFirstInstance,pInstance);
		}

		m_ParticleInstanceListVector[pInstance->m_byFrameIndex].push_back(pInstance);
		m_dwCurrentEmissionCount++;
	}
}

bool CParticleSystemInstance::OnUpdate(float fElapsedTime)
{
	MapPerformance::Scope updateProfile("effects.particles_update");
	MapPerformance::RecordParticleSystem(m_dwCurrentEmissionCount, false, m_pParticleProperty->m_bAttachFlag != 0);
	bool bMakeParticle = true;

	/////

	if (m_fLocalTime >= m_pEmitterProperty->GetCycleLength())
	{
		if (m_pEmitterProperty->isCycleLoop() && --m_iLoopCount!=0)
		{
			if (m_iLoopCount<0)
				m_iLoopCount = 0;
			m_fLocalTime = m_fLocalTime - m_pEmitterProperty->GetCycleLength();
		}
		else
		{
			bMakeParticle = false;
			m_iLoopCount=1;
			if (GetEmissionCount()==0)
				return false;
		}
	}

	/////

	int dwFrameIndex;
	const int dwFrameCount = m_pParticleProperty->GetTextureAnimationFrameCount();

	float fAngularVelocity;
	m_pEmitterProperty->GetEmittingAngularVelocity(m_fLocalTime,&fAngularVelocity);

	if (fAngularVelocity && !m_pParticleProperty->m_bAttachFlag)
	{
		const auto val = D3DXVECTOR3(0.0f,0.0f,1.0f);
		D3DXVec3TransformNormal(&m_pParticleProperty->m_v3ZAxis,&val,mc_pmatLocal);
	}

    unsigned compactUpdates = 0;
    for (dwFrameIndex = 0; dwFrameIndex < dwFrameCount; ++dwFrameIndex)
    {
        auto& particles = m_ParticleInstanceListVector[dwFrameIndex];
        size_t retained = 0;
        for (auto* particle : particles)
        {
            if (!particle->Update(fElapsedTime, fAngularVelocity))
            {
                particle->DeleteThis();
                --m_dwCurrentEmissionCount;
                continue;
            }
            compactUpdates += particle->m_compactUpdate;
            if (particle->m_byFrameIndex != dwFrameIndex)
            {
                m_ParticleInstanceListVector[dwFrameCount + particle->m_byFrameIndex].push_back(particle);
                MapPerformance::Count(MapPerformance::Counter::ParticleMoves);
            }
            else particles[retained++] = particle;
        }
        // Stable compaction preserves alpha blending and RNG/update order.
        particles.resize(retained);
    }
    MapPerformance::Count(MapPerformance::Counter::ParticleCompactUpdates, compactUpdates);
	if (isActive() && bMakeParticle)
		CreateParticles(fElapsedTime);

	for (dwFrameIndex = 0; dwFrameIndex < dwFrameCount; ++dwFrameIndex)
	{
        auto& particles = m_ParticleInstanceListVector[dwFrameIndex];
        auto& migrated = m_ParticleInstanceListVector[dwFrameIndex + dwFrameCount];
        particles.insert(particles.end(), migrated.begin(), migrated.end());
        migrated.clear();
	}

	return true;
}

namespace NParticleMultiTexture
{
constexpr unsigned MaxVertices = 2048;
struct Vertex { D3DXVECTOR3 position; DWORD diffuse; D3DXVECTOR2 uv; float texture; };
struct Command { unsigned first, count, texture; };
struct State
{
    IDirect3DDevice9* device = nullptr; // Owned by the graphic device.
    IDirect3DPixelShader9* shader = nullptr;
    IDirect3DVertexShader9* vertexShader = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    bool attempted = false, failed = false, logged = false, samplerValid = false;
    unsigned depth = 0, count = 0, commands = 0, textureCount = 0;
    DWORD source = 0, destination = 0;
    DWORD sampler[11]{};
    IDirect3DBaseTexture9* textures[4]{};
    Vertex vertices[MaxVertices];
    TPDTVertex fallback[MaxVertices];
    Command command[MaxVertices / 4];
};
State& Data() { static State state; return state; }
void Destroy()
{
    auto& state = Data();
    if (state.shader) state.shader->Release();
    if (state.vertexShader) state.vertexShader->Release();
    if (state.declaration) state.declaration->Release();
    state.vertexShader = nullptr; state.declaration = nullptr;
    state.shader = nullptr; state.device = nullptr;
    state.attempted = state.failed = state.logged = state.samplerValid = false;
    state.depth = state.count = state.commands = state.textureCount = 0;
}
bool Available()
{
    auto& state = Data();
    static const bool enabled = [] {
        const char* value = std::getenv("M2_PARTICLE_MULTI_TEXTURE");
        return !value || strcmp(value, "0") != 0;
    }();
    if (!enabled || !state.device || state.failed) return false;
    if (state.attempted) return state.shader && state.vertexShader && state.declaration;
    state.attempted = true;
    D3DCAPS9 caps{};
    if (FAILED(state.device->GetDeviceCaps(&caps)) || caps.PixelShaderVersion < D3DPS_VERSION(3, 0) ||
        caps.VertexShaderVersion < D3DVS_VERSION(3, 0) || state.device->GetSoftwareVertexProcessing() ||
        caps.MaxVertexIndex < MaxVertices - 1 || caps.MaxPrimitiveCount < MaxVertices / 2) return false;
    static constexpr char source[] = R"hlsl(
sampler2D Image0 : register(s0);
sampler2D Image1 : register(s1);
sampler2D Image2 : register(s2);
sampler2D Image3 : register(s3);
float4 main(float4 color : COLOR0, float2 uv : TEXCOORD0, float slot : TEXCOORD1) : COLOR0
{
    float4 image;
    if (slot < 0.5) image = tex2D(Image0, uv);
    else if (slot < 1.5) image = tex2D(Image1, uv);
    else if (slot < 2.5) image = tex2D(Image2, uv);
    else image = tex2D(Image3, uv);
    return image * color;
}
)hlsl";
    ID3DBlob* code = nullptr; ID3DBlob* errors = nullptr;
    HRESULT result = D3DCompile(source, sizeof(source) - 1, "ordered_particle_textures", nullptr, nullptr,
        "main", "ps_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (SUCCEEDED(result)) result = state.device->CreatePixelShader(
        static_cast<const DWORD*>(code->GetBufferPointer()), &state.shader);
    if (FAILED(result)) TraceError("[PARTICLE_MULTI] FALLBACK shader_hr=0x%08X message=%s",
        static_cast<unsigned>(result), errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    if (code) code->Release();
    if (errors) errors->Release();
    code = errors = nullptr;
    static constexpr char vertexSource[] = R"hlsl(
row_major float4x4 View : register(c0);
row_major float4x4 Projection : register(c4);
struct Input { float3 position : POSITION0; float4 color : COLOR0;
               float2 uv : TEXCOORD0; float slot : TEXCOORD1; };
struct Output { float4 position : POSITION0; float4 color : COLOR0;
                float2 uv : TEXCOORD0; float slot : TEXCOORD1; };
Output main(Input input) {
    Output output;
    output.position = mul(mul(float4(input.position, 1), View), Projection);
    output.color = input.color; output.uv = input.uv; output.slot = input.slot;
    return output;
}
)hlsl";
    if (SUCCEEDED(result)) result = D3DCompile(vertexSource, sizeof(vertexSource) - 1, "ordered_particle_vertices",
        nullptr, nullptr, "main", "vs_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (SUCCEEDED(result)) result = state.device->CreateVertexShader(
        static_cast<const DWORD*>(code->GetBufferPointer()), &state.vertexShader);
    if (code) code->Release();
    if (errors) errors->Release();
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 24, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        D3DDECL_END()
    };
    if (SUCCEEDED(result)) result = state.device->CreateVertexDeclaration(elements, &state.declaration);
    if (FAILED(result))
    {
        state.failed = true;
        TraceError("[PARTICLE_MULTI] FALLBACK pipeline_hr=0x%08X", static_cast<unsigned>(result));
    }
    return SUCCEEDED(result);
}

void Flush()
{
    auto& state = Data();
    if (!state.count) return;
    MapPerformance::Scope submit("effects.particle_multi_submit");
    static const auto indices = [] {
        std::array<WORD, MaxVertices / 4 * 6> output{};
        const unsigned corners[] = {0, 1, 2, 2, 1, 3};
        for (unsigned q = 0; q < MaxVertices / 4; ++q)
            for (unsigned i = 0; i < 6; ++i) output[q * 6 + i] = static_cast<WORD>(q * 4 + corners[i]);
        return output;
    }();
    const D3DRENDERSTATETYPE renderTypes[] = {D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_LIGHTING,
        D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_ZWRITEENABLE, D3DRS_FOGENABLE};
    const DWORD desired[] = {state.source, state.destination, FALSE, TRUE, FALSE, D3DCULL_NONE, FALSE, FALSE};
    DWORD oldRender[8];
    for (unsigned i = 0; i < 8; ++i)
    {
        oldRender[i] = STATEMANAGER.GetRenderState(renderTypes[i]);
        STATEMANAGER.SetRenderState(renderTypes[i], desired[i]);
    }
    DWORD oldFVF = 0; STATEMANAGER.GetFVF(&oldFVF);
    IDirect3DVertexDeclaration9* oldDeclaration = nullptr;
    STATEMANAGER.GetVertexDeclaration(&oldDeclaration);
    IDirect3DVertexShader9* oldVS = nullptr; IDirect3DPixelShader9* oldPS = nullptr;
    STATEMANAGER.GetVertexShader(&oldVS); STATEMANAGER.GetPixelShader(&oldPS);
    float oldConstants[32]{};
    const bool restoreConstants = SUCCEEDED(state.device->GetVertexShaderConstantF(0, oldConstants, 8));
    D3DXMATRIX oldWorld, identity;
    STATEMANAGER.GetTransform(D3DTS_WORLD, &oldWorld);
    D3DXMatrixIdentity(&identity); STATEMANAGER.SetTransform(D3DTS_WORLD, &identity);
    IDirect3DBaseTexture9* oldTextures[4];
    DWORD oldSampler[3][11];
    for (unsigned slot = 0; slot < 4; ++slot)
    {
        STATEMANAGER.GetTexture(slot, &oldTextures[slot]);
        STATEMANAGER.SetTexture(slot, state.textures[slot < state.textureCount ? slot : 0]);
        if (slot)
            for (unsigned i = 0; i < 11; ++i)
            {
                const auto type = static_cast<D3DSAMPLERSTATETYPE>(i + 1);
                STATEMANAGER.GetSamplerState(slot, type, &oldSampler[slot - 1][i]);
                STATEMANAGER.SetSamplerState(slot, type, state.sampler[i]);
            }
    }
    D3DXMATRIX matrices[2];
    STATEMANAGER.GetTransform(D3DTS_VIEW, &matrices[0]);
    STATEMANAGER.GetTransform(D3DTS_PROJECTION, &matrices[1]);
    STATEMANAGER.SetVertexShaderConstant(0, matrices, 8);
    STATEMANAGER.SetVertexDeclaration(state.declaration);
    STATEMANAGER.SetVertexShader(state.vertexShader);
    STATEMANAGER.SetPixelShader(state.shader);
    const HRESULT result = STATEMANAGER.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, state.count,
        state.count / 2, indices.data(), D3DFMT_INDEX16, state.vertices, sizeof(Vertex));
    STATEMANAGER.SetPixelShader(nullptr);
    STATEMANAGER.SetVertexShader(nullptr);
    for (unsigned slot = 0; slot < 4; ++slot)
    {
        STATEMANAGER.SetTexture(slot, oldTextures[slot]);
        if (slot)
            for (unsigned i = 0; i < 11; ++i)
                STATEMANAGER.SetSamplerState(slot, static_cast<D3DSAMPLERSTATETYPE>(i + 1), oldSampler[slot - 1][i]);
    }
    if (SUCCEEDED(result))
    {
        MapPerformance::RecordParticleDrawResult(true, false);
        MapPerformance::Count(MapPerformance::Counter::ParticleBatches);
        MapPerformance::Count(MapPerformance::Counter::ParticleMultiDraws);
        MapPerformance::Count(MapPerformance::Counter::ParticleMultiSaved, state.commands - 1);
        if (!state.logged)
        {
            state.logged = true;
            TraceError("[PARTICLE_MULTI] ACTIVE textures=%u source_batches=%u order=preserved disable=M2_PARTICLE_MULTI_TEXTURE=0",
                state.textureCount, state.commands);
        }
    }
    else
    {
        // The failed merged draw emitted no primitives. Replay its original
        // texture runs in their original order through the existing renderer.
        state.failed = true;
        MapPerformance::Count(MapPerformance::Counter::ParticleMultiFallbacks);
        TraceError("[PARTICLE_MULTI] FALLBACK draw_hr=0x%08X", static_cast<unsigned>(result));
        for (unsigned i = 0; i < state.count; ++i)
        {
            const auto& v = state.vertices[i];
            state.fallback[i].position = v.position; state.fallback[i].diffuse = v.diffuse;
            state.fallback[i].texCoord = v.uv;
        }
        const D3DTEXTURESTAGESTATETYPE types[] = {D3DTSS_COLORARG1, D3DTSS_ALPHAARG1, D3DTSS_COLOROP};
        const DWORD values[] = {D3DTA_DIFFUSE, D3DTA_DIFFUSE, D3DTOP_MODULATE};
        DWORD old[3];
        for (unsigned i = 0; i < 3; ++i)
        {
            STATEMANAGER.GetTextureStageState(0, types[i], &old[i]);
            STATEMANAGER.SetTextureStageState(0, types[i], values[i]);
        }
        STATEMANAGER.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        for (unsigned i = 0; i < state.commands; ++i)
        {
            const auto& c = state.command[i];
            STATEMANAGER.SetTexture(0, state.textures[c.texture]);
            const HRESULT fallback = STATEMANAGER.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, c.count,
                c.count / 2, indices.data(), D3DFMT_INDEX16, state.fallback + c.first, sizeof(TPDTVertex));
            MapPerformance::RecordParticleDrawResult(SUCCEEDED(fallback), false);
            MapPerformance::Count(MapPerformance::Counter::ParticleBatches);
        }
        STATEMANAGER.SetTexture(0, oldTextures[0]);
        for (unsigned i = 0; i < 3; ++i) STATEMANAGER.SetTextureStageState(0, types[i], old[i]);
    }
    if (oldFVF) STATEMANAGER.SetFVF(oldFVF);
    else STATEMANAGER.SetVertexDeclaration(oldDeclaration);
    STATEMANAGER.SetVertexShader(oldVS); STATEMANAGER.SetPixelShader(oldPS);
    if (restoreConstants) STATEMANAGER.SetVertexShaderConstant(0, oldConstants, 8);
    STATEMANAGER.SetTransform(D3DTS_WORLD, &oldWorld);
    for (unsigned i = 0; i < 8; ++i) STATEMANAGER.SetRenderState(renderTypes[i], oldRender[i]);
    state.count = state.commands = state.textureCount = 0;
}

bool Append(const TPDTVertex* vertices, unsigned count)
{
    auto& state = Data();
    if (!state.depth || !Available()) { Flush(); return false; }
    IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
    STATEMANAGER.GetVertexShader(&vs); STATEMANAGER.GetPixelShader(&ps);
    DWORD operation, alpha, coordinates, flags, second, colorArg, alphaArg, colorArg2, alphaArg2;
    STATEMANAGER.GetTextureStageState(0, D3DTSS_COLOROP, &operation);
    STATEMANAGER.GetTextureStageState(0, D3DTSS_ALPHAOP, &alpha);
    STATEMANAGER.GetTextureStageState(0, D3DTSS_COLORARG1, &colorArg);
    STATEMANAGER.GetTextureStageState(0, D3DTSS_ALPHAARG1, &alphaArg);
    STATEMANAGER.GetTextureStageState(0, D3DTSS_COLORARG2, &colorArg2);
    STATEMANAGER.GetTextureStageState(0, D3DTSS_ALPHAARG2, &alphaArg2);
    STATEMANAGER.GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &coordinates);
    STATEMANAGER.GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &flags);
    STATEMANAGER.GetTextureStageState(1, D3DTSS_COLOROP, &second);
    IDirect3DBaseTexture9* texture = nullptr; STATEMANAGER.GetTexture(0, &texture);
    if (vs || ps || !texture || STATEMANAGER.GetRenderState(D3DRS_FOGENABLE) ||
        operation != D3DTOP_MODULATE || alpha != D3DTOP_MODULATE || second != D3DTOP_DISABLE ||
        colorArg != D3DTA_DIFFUSE || alphaArg != D3DTA_DIFFUSE ||
        colorArg2 != D3DTA_TEXTURE || alphaArg2 != D3DTA_TEXTURE || coordinates || flags != D3DTTFF_DISABLE ||
        STATEMANAGER.GetRenderState(D3DRS_VERTEXBLEND) != D3DVBF_DISABLE ||
        STATEMANAGER.GetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE)) { Flush(); return false; }
    if (!state.samplerValid)
    {
        // Read effective sampler state. The legacy effect path requests NONE
        // for min/mag filters, which some drivers reject without applying it.
        for (unsigned i = 0; i < 11; ++i)
            if (FAILED(state.device->GetSamplerState(0, static_cast<D3DSAMPLERSTATETYPE>(i + 1), &state.sampler[i])))
                { Flush(); return false; }
        state.samplerValid = true;
    }
    const DWORD source = STATEMANAGER.GetRenderState(D3DRS_SRCBLEND);
    const DWORD destination = STATEMANAGER.GetRenderState(D3DRS_DESTBLEND);
    if (state.count && (state.source != source || state.destination != destination || state.count + count > MaxVertices)) Flush();
    unsigned slot = 0;
    while (slot < state.textureCount && state.textures[slot] != texture) ++slot;
    if (slot == 4) { Flush(); slot = 0; }
    if (slot == state.textureCount) state.textures[state.textureCount++] = texture;
    state.source = source; state.destination = destination;
    state.command[state.commands++] = {state.count, count, slot};
    for (unsigned i = 0; i < count; ++i)
    {
        auto& v = state.vertices[state.count++];
        v.position = vertices[i].position; v.diffuse = vertices[i].diffuse;
        v.uv = vertices[i].texCoord; v.texture = static_cast<float>(slot);
    }
    return true;
}
}

void CParticleSystemInstance::BeginRenderBatch(IDirect3DDevice9* device)
{
    auto& state = NParticleMultiTexture::Data();
    if (state.device != device)
    {
        NParticleMultiTexture::Destroy();
        state.device = device;
    }
    if (!state.depth) state.samplerValid = false;
    ++state.depth;
}
void CParticleSystemInstance::EndRenderBatch()
{
    auto& state = NParticleMultiTexture::Data();
    if (state.depth && !--state.depth) NParticleMultiTexture::Flush();
}
void CParticleSystemInstance::FlushRenderBatch()
{
    NParticleMultiTexture::Flush();
    NParticleMultiTexture::Data().samplerValid = false;
}

namespace NParticleRenderer
{
bool PreparationEnabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("M2_PARTICLE_PREP");
        return value && strcmp(value, "1") == 0;
    }();
    return enabled;
}
struct Batch
{
    using Vertex = TPDTVertex;
    static constexpr unsigned QuadCapacity = 128;
    static constexpr unsigned Capacity = QuadCapacity * 4;
    Vertex vertices[Capacity];
    unsigned count = 0;
    DWORD lastColor = 0;
    bool hasColor = false;
    bool enabled = true;
    void Flush()
    {
        if (!count) return;
        MapPerformance::Scope submit("effects.particle_submit");
        if (NParticleMultiTexture::Append(vertices, count)) { count = 0; return; }
        static const auto indices = [] {
            std::array<WORD, QuadCapacity * 6> result{};
            static constexpr unsigned corners[6] = {0, 1, 2, 2, 1, 3};
            for (unsigned quad = 0; quad < QuadCapacity; ++quad)
                for (unsigned corner = 0; corner < 6; ++corner)
                    result[quad * 6 + corner] = static_cast<WORD>(quad * 4 + corners[corner]);
            return result;
        }();
        bool streamed = false;
        const HRESULT result = CGraphicBase::DrawParticleQuads(vertices, count, indices.data(), &streamed);
        MapPerformance::Count(streamed ? MapPerformance::Counter::ParticleStreamDraws :
            MapPerformance::Counter::ParticleStreamFallbacks);
        MapPerformance::RecordParticleDrawResult(SUCCEEDED(result), false);
        MapPerformance::Count(MapPerformance::Counter::ParticleBatches);
        count = 0;
    }
    void Append(CParticleInstance* particle)
    {
        if (count + 4 > Capacity) Flush();
        const TPTVertex* quad = particle->GetParticleMeshPointer();
        const DWORD color = particle->GetColor();
        if (!enabled)
        {
            const HRESULT result = STATEMANAGER.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TPTVertex));
            MapPerformance::RecordParticleDrawResult(SUCCEEDED(result), true);
            MapPerformance::Count(MapPerformance::Counter::ParticleQuads);
            MapPerformance::Count(MapPerformance::Counter::ParticleBatches);
            return;
        }
        for (unsigned corner = 0; corner < 4; ++corner)
        {
            auto& out = vertices[count++];
            out.position = quad[corner].position;
            out.diffuse = color;
            out.texCoord = quad[corner].texCoord;
        }
        lastColor = color; hasColor = true;
        MapPerformance::Count(MapPerformance::Counter::ParticleQuads);
    }
    void AppendPrepared(CParticleInstance* particle, const TParticleRenderGeometry& geometry,
        bool rotate = false, float cosine = 1.0f, float sine = 0.0f)
    {
        if (count + 4 > Capacity) Flush();
        particle->WriteRenderQuad(geometry, vertices + count, rotate, cosine, sine);
        count += 4;
        lastColor = particle->GetColor(); hasColor = true;
        MapPerformance::Count(MapPerformance::Counter::ParticleQuads);
    }
};
struct Renderer
{
    Batch& batch;
    const D3DXMATRIX* matrix;
    unsigned faces;
    TParticleRenderContext context;
    float faceCosine, faceSine;
    Renderer(Batch& batch, const D3DXMATRIX* matrix, unsigned faces)
        : batch(batch), matrix(matrix), faces(faces),
          context(CCameraManager::Instance().GetCurrentCamera())
    {
        const float angle = D3DXToRadian(faces == 2 ? 30.0f : 60.0f);
        faceCosine = cosf(angle); faceSine = sinf(angle);
    }
    void Flush() const { batch.Flush(); }
    void operator()(CParticleInstance* particle) const
    {
        if (batch.enabled && PreparationEnabled())
        {
            TParticleRenderGeometry geometry;
            particle->PrepareRender(matrix, context, geometry);
            if (faces == 1) batch.AppendPrepared(particle, geometry);
            else
            {
                if (faces == 3) batch.AppendPrepared(particle, geometry);
                batch.AppendPrepared(particle, geometry, true, faceCosine, -faceSine);
                batch.AppendPrepared(particle, geometry, true, faceCosine, faceSine);
                MapPerformance::Count(MapPerformance::Counter::ParticleBasisReuse, faces - 1);
            }
            return;
        }
        if (faces == 2)
        {
            particle->Transform(matrix, D3DXToRadian(-30.0f), !batch.enabled); batch.Append(particle);
            particle->Transform(matrix, D3DXToRadian(+30.0f), !batch.enabled); batch.Append(particle);
        }
        else if (faces == 3)
        {
            particle->Transform(matrix, !batch.enabled); batch.Append(particle);
            particle->Transform(matrix, D3DXToRadian(-60.0f), !batch.enabled); batch.Append(particle);
            particle->Transform(matrix, D3DXToRadian(+60.0f), !batch.enabled); batch.Append(particle);
        }
        else
        {
            particle->Transform(matrix, !batch.enabled); batch.Append(particle);
        }
    }
};
}

void CParticleSystemInstance::OnRender()
{
    MapPerformance::Scope profile("effects.particles_render");
    MapPerformance::RecordParticleSystem(m_dwCurrentEmissionCount, true, m_pParticleProperty->m_bAttachFlag != 0);
    CScreen::Identity();
    STATEMANAGER.SetRenderState(D3DRS_SRCBLEND, m_pParticleProperty->m_bySrcBlendType);
    STATEMANAGER.SetRenderState(D3DRS_DESTBLEND, m_pParticleProperty->m_byDestBlendType);
    STATEMANAGER.SetTextureStageState(0, D3DTSS_COLOROP, m_pParticleProperty->m_byColorOperationType);
    // Each quad's old texture-factor color is constant across its vertices.
    // Diffuse color preserves it while allowing differently colored quads in
    // one ordered draw. Restore the state expected by following effect meshes.
    DWORD oldColorArg = 0, oldAlphaArg = 0, secondColorOp = 0;
    STATEMANAGER.GetTextureStageState(0, D3DTSS_COLORARG1, &oldColorArg);
    STATEMANAGER.GetTextureStageState(0, D3DTSS_ALPHAARG1, &oldAlphaArg);
    STATEMANAGER.GetTextureStageState(1, D3DTSS_COLOROP, &secondColorOp);
    const DWORD oldLighting = STATEMANAGER.GetRenderState(D3DRS_LIGHTING);
    DWORD oldFVF = 0; STATEMANAGER.GetFVF(&oldFVF);
    NParticleRenderer::Batch batch;
    batch.enabled = oldColorArg == D3DTA_TFACTOR && oldAlphaArg == D3DTA_TFACTOR && secondColorOp == D3DTOP_DISABLE;
    if (!batch.enabled) FlushRenderBatch();
    if (batch.enabled)
    {
        STATEMANAGER.SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        STATEMANAGER.SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        STATEMANAGER.SetRenderState(D3DRS_LIGHTING, FALSE);
        STATEMANAGER.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    }
    const unsigned type = m_pParticleProperty->m_byBillboardType;
    const unsigned faces = type == BILLBOARD_TYPE_2FACE ? 2u : type == BILLBOARD_TYPE_3FACE ? 3u : 1u;
    if (type <= BILLBOARD_TYPE_3FACE)
        ForEachParticleRendering(NParticleRenderer::Renderer(batch, m_pParticleProperty->m_bAttachFlag ? mc_pmatLocal : nullptr, faces));
    batch.Flush();
    if (batch.hasColor) STATEMANAGER.SetRenderState(D3DRS_TEXTUREFACTOR, batch.lastColor);
    STATEMANAGER.SetTextureStageState(0, D3DTSS_COLORARG1, oldColorArg);
    STATEMANAGER.SetTextureStageState(0, D3DTSS_ALPHAARG1, oldAlphaArg);
    STATEMANAGER.SetRenderState(D3DRS_LIGHTING, oldLighting);
    STATEMANAGER.SetFVF(oldFVF);
}

void CParticleSystemInstance::OnSetDataPointer(CEffectElementBase * pElement)
{
	m_pData = (CParticleSystemData *)pElement;

	m_dwCurrentEmissionCount = 0;
	m_pParticleProperty = m_pData->GetParticlePropertyPointer();
	m_pEmitterProperty = m_pData->GetEmitterPropertyPointer();
	m_iLoopCount = m_pEmitterProperty->GetLoopCount();
	m_ParticleInstanceListVector.resize(m_pParticleProperty->GetTextureAnimationFrameCount()*2+2);

	/////

	assert(m_kVct_pkImgInst.empty());
	m_kVct_pkImgInst.reserve(m_pParticleProperty->m_ImageVector.size());
	for (DWORD i = 0; i < m_pParticleProperty->m_ImageVector.size(); ++i)
	{
		CGraphicImage * pImage = m_pParticleProperty->m_ImageVector[i];

		CGraphicImageInstance* pkImgInstNew = CGraphicImageInstance::New();
		pkImgInstNew->SetImagePointer(pImage);
		m_kVct_pkImgInst.push_back(pkImgInstNew);
	}
}

void CParticleSystemInstance::OnInitialize()
{
	m_dwCurrentEmissionCount = 0;
	m_iLoopCount = 0;
	m_fEmissionResidue = 0.0f;
}

void CParticleSystemInstance::OnDestroy()
{
	TParticleInstanceListVector::iterator i;
	for(i = m_ParticleInstanceListVector.begin(); i!=m_ParticleInstanceListVector.end(); ++i)
	{
		TParticleInstanceList& rkLst_kParticleInst=*i;

		TParticleInstanceList::iterator j;
		for(j = rkLst_kParticleInst.begin(); j!=rkLst_kParticleInst.end(); ++j)
		{
			CParticleInstance* pkParticleInst=*j;
			pkParticleInst->DeleteThis();
		}

		rkLst_kParticleInst.clear();
	}
	m_ParticleInstanceListVector.clear();

	std::for_each(m_kVct_pkImgInst.begin(), m_kVct_pkImgInst.end(), CGraphicImageInstance::Delete);
	m_kVct_pkImgInst.clear();
}

CParticleSystemInstance::CParticleSystemInstance()
{
	Initialize();
}

CParticleSystemInstance::~CParticleSystemInstance()
{
	assert(m_ParticleInstanceListVector.empty());
	assert(m_kVct_pkImgInst.empty());
}

