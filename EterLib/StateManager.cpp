#include "StdAfx.h"
#include "StateManager.h"

//#define StateManager_Assert(a) if (!(a)) puts("assert"#a)
#define StateManager_Assert(a) assert(a)

struct SLightData
{
	enum
	{
		LIGHT_NUM = 8,
	};
	D3DLIGHT9 m_akD3DLight[LIGHT_NUM];
    bool enabled[LIGHT_NUM]{};
} m_kLightData;

void CStateManager::SetLight(DWORD index, CONST D3DLIGHT9* pLight) const
{
	assert(index<SLightData::LIGHT_NUM);
	if (memcmp(&m_kLightData.m_akD3DLight[index], pLight, sizeof(*pLight)) != 0)
		++m_vertexProcessingRevision;
	m_kLightData.m_akD3DLight[index]=*pLight;

	m_lpD3DDev->SetLight(index, pLight);
}

void CStateManager::GetLight(DWORD index, D3DLIGHT9* pLight) const
{
	assert(index<8);
	*pLight=m_kLightData.m_akD3DLight[index];
}

void CStateManager::LightEnable(DWORD index, BOOL enabled)
{
    assert(index < SLightData::LIGHT_NUM);
    if (index >= SLightData::LIGHT_NUM) return;
    if (m_bForce || m_kLightData.enabled[index] != (enabled != FALSE))
        if (SUCCEEDED(m_lpD3DDev->LightEnable(index, enabled)))
        {
            m_kLightData.enabled[index] = enabled != FALSE;
            ++m_vertexProcessingRevision;
        }
}

bool CStateManager::GetLightEnable(DWORD index) const
{
    return index < SLightData::LIGHT_NUM && m_kLightData.enabled[index];
}

bool CStateManager::BeginScene()
{
	m_bScene=true;

	D3DXMATRIX m4Proj;
	D3DXMATRIX m4View;
	D3DXMATRIX m4World;
	GetTransform(D3DTS_WORLD, &m4World);
	GetTransform(D3DTS_PROJECTION, &m4Proj);
	GetTransform(D3DTS_VIEW, &m4View);
	SetTransform(D3DTS_WORLD, &m4World);
	SetTransform(D3DTS_PROJECTION, &m4Proj);
	SetTransform(D3DTS_VIEW, &m4View);

	if (FAILED(m_lpD3DDev->BeginScene()))
		return false;
	return true;
}

void CStateManager::EndScene()
{
	m_lpD3DDev->EndScene();
	m_bScene=false;
}

CStateManager::CStateManager(LPDIRECT3DDEVICE9 lpDevice) : m_lpD3DDev(nullptr)
{
	m_bScene = false;
	m_dwBestMinFilter = D3DTEXF_LINEAR;
	m_dwBestMagFilter = D3DTEXF_LINEAR;
	SetDevice(lpDevice);
}

CStateManager::~CStateManager()
{
	if (m_lpD3DDev)
	{
		m_lpD3DDev->Release();
		m_lpD3DDev = nullptr;
	}
}

void CStateManager::SetDevice(LPDIRECT3DDEVICE9 lpDevice)
{
	StateManager_Assert(lpDevice);
	lpDevice->AddRef();

	if (m_lpD3DDev)
	{
		m_lpD3DDev->Release();
		m_lpD3DDev = nullptr;
	}

	m_lpD3DDev = lpDevice;

	SetDefaultState();
}

void CStateManager::SetBestFiltering(DWORD dwStage)
{
	SetSamplerState(dwStage, D3DSAMP_MINFILTER, m_dwBestMinFilter);
	SetSamplerState(dwStage, D3DSAMP_MAGFILTER, m_dwBestMagFilter);
	SetSamplerState(dwStage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
}

void CStateManager::Restore()
{
    InvalidateShaderConstants();
	int i, j;

	m_bForce = true;

	for (i = 0; i < STATEMANAGER_MAX_RENDERSTATES; ++i)
		SetRenderState(D3DRENDERSTATETYPE(i), m_CurrentState.m_RenderStates[i]);

	for (i = 0; i < STATEMANAGER_MAX_STAGES; ++i)
		for (j = 0; j < STATEMANAGER_MAX_TEXTURESTATES; ++j)
			SetTextureStageState(i, D3DTEXTURESTAGESTATETYPE(j), m_CurrentState.m_TextureStates[i][j]);

	for (i = 0; i < STATEMANAGER_MAX_STAGES; ++i)
		for (j = 0; j < STATEMANAGER_MAX_SAMPLERSTATES; ++j)
			SetSamplerState(i, D3DSAMPLERSTATETYPE(j), m_CurrentState.m_SamplerStates[i][j]);

	for (i = 0; i < STATEMANAGER_MAX_STAGES; ++i)
		SetTexture(i, m_CurrentState.m_Textures[i]);

	m_bForce = false;
}

void CStateManager::InvalidateShaderConstants()
{
    ++m_vertexProcessingRevision;
    m_materialValid = false;
    memset(m_vertexConstantsValid, 0, sizeof(m_vertexConstantsValid));
    memset(m_pixelConstantsValid, 0, sizeof(m_pixelConstantsValid));
}

void CStateManager::SetDefaultState()
{
    m_materialValid = false;
    InvalidateShaderConstants();
	// @fixme039 BEGIN (moved from SetDevice)
	D3DCAPS9 d3dCaps;
	m_lpD3DDev->GetDeviceCaps(&d3dCaps);
    m_mipFilterCaps = d3dCaps.TextureFilterCaps;
    m_vertexConstantLimit = (std::min)(d3dCaps.MaxVertexShaderConst, STATEMANAGER_MAX_VCONSTANTS);

	if (d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFANISOTROPIC)
		m_dwBestMagFilter = D3DTEXF_ANISOTROPIC;
	else
		m_dwBestMagFilter = D3DTEXF_LINEAR;

	if (d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC)
		m_dwBestMinFilter = D3DTEXF_ANISOTROPIC;
	else
		m_dwBestMinFilter = D3DTEXF_LINEAR;

	DWORD dwMax = d3dCaps.MaxAnisotropy;
	dwMax = dwMax < 4 ? dwMax : 4;

	for (int i = 0; i < 8; ++i)
		m_lpD3DDev->SetSamplerState(i, D3DSAMP_MAXANISOTROPY, dwMax);
	// @fixme039 END

	m_CurrentState.ResetState();
	m_CopyState.ResetState();
	m_ChipState.ResetState();

	m_bScene = false;
	m_bForce = true;

	D3DXMATRIX Identity;
    for (DWORD light = 0; light < SLightData::LIGHT_NUM; ++light)
        LightEnable(light, FALSE);
	D3DXMatrixIdentity(&Identity);

	SetTransform(D3DTS_WORLD, &Identity);
	SetTransform(D3DTS_VIEW, &Identity);
	SetTransform(D3DTS_PROJECTION, &Identity);

	D3DMATERIAL9 DefaultMat;
	ZeroMemory(&DefaultMat, sizeof(D3DMATERIAL9));

	DefaultMat.Diffuse.r = 1.0f;
	DefaultMat.Diffuse.g = 1.0f;
	DefaultMat.Diffuse.b = 1.0f;
	DefaultMat.Diffuse.a = 1.0f;
	DefaultMat.Ambient.r = 1.0f;
	DefaultMat.Ambient.g = 1.0f;
	DefaultMat.Ambient.b = 1.0f;
	DefaultMat.Ambient.a = 1.0f;
	DefaultMat.Emissive.r = 0.0f;
	DefaultMat.Emissive.g = 0.0f;
	DefaultMat.Emissive.b = 0.0f;
	DefaultMat.Emissive.a = 0.0f;
	DefaultMat.Specular.r = 0.0f;
	DefaultMat.Specular.g = 0.0f;
	DefaultMat.Specular.b = 0.0f;
	DefaultMat.Specular.a = 0.0f;
	DefaultMat.Power = 0.0f;

	SetMaterial(&DefaultMat);

	SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
	SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
	SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
	SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);

	SetRenderState(D3DRS_LASTPIXEL, FALSE);
	SetRenderState(D3DRS_ALPHAREF, 1);
	SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
	SetRenderState(D3DRS_FOGSTART, 0);
	SetRenderState(D3DRS_FOGEND, 0);
	SetRenderState(D3DRS_FOGDENSITY, 0);
	SetRenderState(D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
	SetRenderState(D3DRS_AMBIENT, 0x00000000);
	SetRenderState(D3DRS_LOCALVIEWER, FALSE);
	SetRenderState(D3DRS_NORMALIZENORMALS, FALSE);
	SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
	SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
	m_lpD3DDev->SetSoftwareVertexProcessing(false);
	SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	SetRenderState(D3DRS_MULTISAMPLEMASK, 0xFFFFFFFF);
	SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
	SetRenderState(D3DRS_COLORWRITEENABLE, 0xFFFFFFFF);
	SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
	SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	SetRenderState(D3DRS_FOGENABLE, FALSE);
	SetRenderState(D3DRS_FOGCOLOR, 0xFF000000);
	SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
	SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
	SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
	SetRenderState(D3DRS_ZENABLE, TRUE);
	SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	SetRenderState(D3DRS_DITHERENABLE, TRUE);
	SetRenderState(D3DRS_STENCILENABLE, FALSE);
	SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	SetRenderState(D3DRS_CLIPPING, TRUE);
	SetRenderState(D3DRS_LIGHTING, FALSE);
	SetRenderState(D3DRS_SPECULARENABLE, FALSE);
	SetRenderState(D3DRS_COLORVERTEX, FALSE);
	SetRenderState(D3DRS_WRAP0, 0);
	SetRenderState(D3DRS_WRAP1, 0);
	SetRenderState(D3DRS_WRAP2, 0);
	SetRenderState(D3DRS_WRAP3, 0);
	SetRenderState(D3DRS_WRAP4, 0);
	SetRenderState(D3DRS_WRAP5, 0);
	SetRenderState(D3DRS_WRAP6, 0);
	SetRenderState(D3DRS_WRAP7, 0);

	SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
	SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
	SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);

	SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(2, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(2, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(3, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(3, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(3, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(3, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(4, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(4, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(4, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(4, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(4, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(4, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(5, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(5, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(5, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(5, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(5, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(5, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(6, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(6, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(6, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(6, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(6, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(6, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(7, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(7, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(7, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(7, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(7, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(7, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
	SetTextureStageState(2, D3DTSS_TEXCOORDINDEX, 2);
	SetTextureStageState(3, D3DTSS_TEXCOORDINDEX, 3);
	SetTextureStageState(4, D3DTSS_TEXCOORDINDEX, 4);
	SetTextureStageState(5, D3DTSS_TEXCOORDINDEX, 5);
	SetTextureStageState(6, D3DTSS_TEXCOORDINDEX, 6);
	SetTextureStageState(7, D3DTSS_TEXCOORDINDEX, 7);

	for (DWORD i = 0; i < 8; ++i)
	{
		SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
		SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		SetTextureStageState(i, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
		SetTexture(i, nullptr);
	}

	SetPixelShader(0);
	SetFVF(D3DFVF_XYZ);

	D3DXVECTOR4 av4Null[STATEMANAGER_MAX_VCONSTANTS]{};
	SetVertexShaderConstant(0, av4Null, m_vertexConstantLimit);
	SetPixelShaderConstant(0, av4Null, STATEMANAGER_MAX_PCONSTANTS);

	m_bForce = false;

#ifdef _DEBUG
	int i, j;
	for (i = 0; i < STATEMANAGER_MAX_RENDERSTATES; i++)
		m_bRenderStateSavingFlag[i] = FALSE;

	for (j = 0; j < STATEMANAGER_MAX_TRANSFORMSTATES; j++)
		m_bTransformSavingFlag[j] = FALSE;

	for (j = 0; j < STATEMANAGER_MAX_STAGES; ++j)
		for (i = 0; i < STATEMANAGER_MAX_TEXTURESTATES; ++i)
			m_bTextureStageStateSavingFlag[j][i] = FALSE;
	for (j = 0; j < STATEMANAGER_MAX_STAGES; ++j)
		for (i = 0; i < STATEMANAGER_MAX_SAMPLERSTATES; ++i)
			m_bSamplerStateSavingFlag[j][i] = FALSE;
#endif _DEBUG
}

// Material
void CStateManager::SaveMaterial()
{
	m_CopyState.m_D3DMaterial = m_CurrentState.m_D3DMaterial;
}

void CStateManager::SaveMaterial(const D3DMATERIAL9 * pMaterial)
{
	// Check that we have set this up before, if not, the default is this.
	m_CopyState.m_D3DMaterial = m_CurrentState.m_D3DMaterial;
	SetMaterial(pMaterial);
}

void CStateManager::RestoreMaterial()
{
	SetMaterial(&m_CopyState.m_D3DMaterial);
}

void CStateManager::SetMaterial(const D3DMATERIAL9 * pMaterial)
{
	if (m_materialValid && !m_bForce && memcmp(&m_CurrentState.m_D3DMaterial, pMaterial, sizeof(*pMaterial)) == 0)
		return;
	++m_vertexProcessingRevision;
	const HRESULT result = m_lpD3DDev->SetMaterial(pMaterial);
	m_CurrentState.m_D3DMaterial = *pMaterial;
	m_materialValid = SUCCEEDED(result);
}

void CStateManager::GetMaterial(D3DMATERIAL9 * pMaterial) const
{
	// Set the renderstate and remember it.
	*pMaterial = m_CurrentState.m_D3DMaterial;
}

// Renderstates
DWORD CStateManager::GetRenderState(D3DRENDERSTATETYPE Type) const
{
	return m_CurrentState.m_RenderStates[Type];
}

void CStateManager::SaveRenderState(D3DRENDERSTATETYPE Type, DWORD dwValue)
{
#ifdef _DEBUG
	if (m_bRenderStateSavingFlag[Type])
	{
		Tracef(" CStateManager::SaveRenderState - This render state is already saved [%d, %d]\n", Type, dwValue);
		StateManager_Assert(!" This render state is already saved!");
	}
	m_bRenderStateSavingFlag[Type] = TRUE;
#endif _DEBUG

	// Check that we have set this up before, if not, the default is this.
	m_CopyState.m_RenderStates[Type] = m_CurrentState.m_RenderStates[Type];
	SetRenderState(Type, dwValue);
}

void CStateManager::RestoreRenderState(D3DRENDERSTATETYPE Type)
{
#ifdef _DEBUG
	if (!m_bRenderStateSavingFlag[Type])
	{
		Tracef(" CStateManager::SaveRenderState - This render state was not saved [%d, %d]\n", Type);
		StateManager_Assert(!" This render state was not saved!");
	}
	m_bRenderStateSavingFlag[Type] = FALSE;
#endif _DEBUG

	SetRenderState(Type, m_CopyState.m_RenderStates[Type]);
}

void CStateManager::SetRenderState(D3DRENDERSTATETYPE Type, DWORD Value)
{
	if (m_CurrentState.m_RenderStates[Type] == Value)
		return;

    switch (Type)
    {
    case D3DRS_LIGHTING: case D3DRS_NORMALIZENORMALS: case D3DRS_RANGEFOGENABLE:
    case D3DRS_FOGSTART: case D3DRS_FOGEND: case D3DRS_FOGDENSITY:
    case D3DRS_FOGENABLE: case D3DRS_FOGVERTEXMODE: case D3DRS_SPECULARENABLE:
    case D3DRS_COLORVERTEX: case D3DRS_DIFFUSEMATERIALSOURCE: case D3DRS_AMBIENTMATERIALSOURCE:
    case D3DRS_EMISSIVEMATERIALSOURCE: case D3DRS_AMBIENT: case D3DRS_CLIPPLANEENABLE:
    case D3DRS_VERTEXBLEND: case D3DRS_INDEXEDVERTEXBLENDENABLE:
        ++m_vertexProcessingRevision;
        break;
    }

	m_lpD3DDev->SetRenderState(Type, Value);
	m_CurrentState.m_RenderStates[Type] = Value;
}

void CStateManager::GetRenderState(D3DRENDERSTATETYPE Type, DWORD * pdwValue) const
{
	*pdwValue = m_CurrentState.m_RenderStates[Type];
}

// Textures
void CStateManager::SaveTexture(DWORD dwStage, LPDIRECT3DBASETEXTURE9 pTexture)
{
	// Check that we have set this up before, if not, the default is this.
	m_CopyState.m_Textures[dwStage] = m_CurrentState.m_Textures[dwStage];
	SetTexture(dwStage, pTexture);
}

void CStateManager::RestoreTexture(DWORD dwStage)
{
	SetTexture(dwStage, m_CopyState.m_Textures[dwStage]);
}

void CStateManager::SetTexture(DWORD dwStage, LPDIRECT3DBASETEXTURE9 pTexture)
{
	if (pTexture == m_CurrentState.m_Textures[dwStage])
		return;

	m_lpD3DDev->SetTexture(dwStage, pTexture);
	m_CurrentState.m_Textures[dwStage] = pTexture;
}

void CStateManager::GetTexture(DWORD dwStage, LPDIRECT3DBASETEXTURE9 * ppTexture) const
{
	*ppTexture = m_CurrentState.m_Textures[dwStage];
}

// Texture stage states
void CStateManager::SaveTextureStageState(DWORD dwStage,D3DTEXTURESTAGESTATETYPE Type, DWORD dwValue)
{
	// Check that we have set this up before, if not, the default is this.
#ifdef _DEBUG
	if (m_bTextureStageStateSavingFlag[dwStage][Type])
	{
		Tracef(" CStateManager::SaveTextureStageState - This texture stage state is already saved [%d, %d]\n", dwStage, Type);
		StateManager_Assert(!" This texture stage state is already saved!");
	}
	m_bTextureStageStateSavingFlag[dwStage][Type] = TRUE;
#endif _DEBUG
	m_CopyState.m_TextureStates[dwStage][Type] = m_CurrentState.m_TextureStates[dwStage][Type];
	SetTextureStageState(dwStage, Type, dwValue);
}

void CStateManager::RestoreTextureStageState(DWORD dwStage, D3DTEXTURESTAGESTATETYPE Type)
{
#ifdef _DEBUG
	if (!m_bTextureStageStateSavingFlag[dwStage][Type])
	{
		Tracef(" CStateManager::RestoreTextureStageState - This texture stage state was not saved [%d, %d]\n", dwStage, Type);
		StateManager_Assert(!" This texture stage state was not saved!");
	}
	m_bTextureStageStateSavingFlag[dwStage][Type] = FALSE;
#endif _DEBUG
	SetTextureStageState(dwStage, Type, m_CopyState.m_TextureStates[dwStage][Type]);
}

void CStateManager::SetTextureStageState(DWORD dwStage, D3DTEXTURESTAGESTATETYPE Type, DWORD dwValue)
{
	if (m_CurrentState.m_TextureStates[dwStage][Type] == dwValue)
		return;
	// Transform flags are compared explicitly by the skinning cache: its own
	// VS bind/restore temporarily changes those flags on every draw.
	if ((dwStage < 2 && Type == D3DTSS_TEXCOORDINDEX) || (dwStage >= 2 && Type == D3DTSS_COLOROP))
		++m_vertexProcessingRevision;

	m_lpD3DDev->SetTextureStageState(dwStage, Type, dwValue);
	m_CurrentState.m_TextureStates[dwStage][Type] = dwValue;
}

void CStateManager::GetTextureStageState(DWORD dwStage, D3DTEXTURESTAGESTATETYPE Type, DWORD * pdwValue) const
{
	*pdwValue = m_CurrentState.m_TextureStates[dwStage][Type];
}

////Converting2DirectX9
void CStateManager::SaveSamplerState(DWORD dwStage, D3DSAMPLERSTATETYPE Type, DWORD dwValue)
{
	// Check that we have set this up before, if not, the default is this.
#ifdef _DEBUG
	if (m_bSamplerStateSavingFlag[dwStage][Type])
	{
	}
	m_bSamplerStateSavingFlag[dwStage][Type] = TRUE;
#endif _DEBUG
	m_CopyState.m_SamplerStates[dwStage][Type] = m_CurrentState.m_SamplerStates[dwStage][Type];
	SetSamplerState(dwStage, Type, dwValue);
}

void CStateManager::RestoreSamplerState(DWORD dwStage, D3DSAMPLERSTATETYPE Type)
{
#ifdef _DEBUG
	if (!m_bSamplerStateSavingFlag[dwStage][Type])
	{
	}
	m_bSamplerStateSavingFlag[dwStage][Type] = FALSE;
#endif _DEBUG
	SetSamplerState(dwStage, Type, m_CopyState.m_SamplerStates[dwStage][Type]);
}

void CStateManager::SetSamplerState(DWORD dwStage, D3DSAMPLERSTATETYPE Type, DWORD dwValue)
{
    if (Type == D3DSAMP_MIPFILTER)
    {
        if (dwValue == D3DTEXF_ANISOTROPIC) dwValue = D3DTEXF_LINEAR;
        const DWORD caps = m_mipFilterCaps;
        if (dwValue == D3DTEXF_LINEAR && !(caps & D3DPTFILTERCAPS_MIPFLINEAR)) dwValue = D3DTEXF_POINT;
        if (dwValue == D3DTEXF_POINT && !(caps & D3DPTFILTERCAPS_MIPFPOINT)) dwValue = D3DTEXF_NONE;
    }
	if (m_CurrentState.m_SamplerStates[dwStage][Type] == dwValue)
		return;

	m_lpD3DDev->SetSamplerState(dwStage, Type, dwValue);

	m_CurrentState.m_SamplerStates[dwStage][Type] = dwValue;
}

void CStateManager::GetSamplerState(DWORD dwStage, D3DSAMPLERSTATETYPE Type, DWORD* pdwValue)
{
	*pdwValue = m_CurrentState.m_SamplerStates[dwStage][Type];
}

// Vertex Shader
void CStateManager::SaveVertexShader(LPDIRECT3DVERTEXSHADER9 dwShader)
{
	m_CopyState.m_dwVertexShader = m_CurrentState.m_dwVertexShader;
	SetVertexShader(dwShader);
}

void CStateManager::RestoreVertexShader()
{
	SetVertexShader(m_CopyState.m_dwVertexShader);
}

void CStateManager::SetVertexShader(LPDIRECT3DVERTEXSHADER9 dwShader)
{
	if (m_CurrentState.m_dwVertexShader != dwShader)
	{
		m_CurrentState.m_dwVertexShader = dwShader;
		m_lpD3DDev->SetVertexShader(dwShader);
	}
}

void CStateManager::GetVertexShader(LPDIRECT3DVERTEXSHADER9 * pdwShader) const
{
	*pdwShader = m_CurrentState.m_dwVertexShader;
}

// Vertex Declaration
void CStateManager::SaveVertexDeclaration(LPDIRECT3DVERTEXDECLARATION9 dwShader)
{
	m_CopyState.m_dwVertexDeclaration = m_CurrentState.m_dwVertexDeclaration;
	SetVertexDeclaration(dwShader);
}

void CStateManager::RestoreVertexDeclaration()
{
	SetVertexDeclaration(m_CopyState.m_dwVertexDeclaration);
}

void CStateManager::SetVertexDeclaration(LPDIRECT3DVERTEXDECLARATION9 dwShader)
{
	if (m_CurrentState.m_dwVertexDeclaration == dwShader)
		return;

	m_lpD3DDev->SetVertexDeclaration(dwShader);
	m_CurrentState.m_dwVertexDeclaration = dwShader;
    // SetVertexDeclaration and SetFVF select the same device state. A later
    // SetFVF must rebind it even when that numeric FVF was used before a VS.
    m_CurrentState.m_dwFVF = 0;
}

void CStateManager::GetVertexDeclaration(LPDIRECT3DVERTEXDECLARATION9* pdwShader)
{
	*pdwShader = m_CurrentState.m_dwVertexDeclaration;
}

// Pixel Shader
void CStateManager::SavePixelShader(LPDIRECT3DPIXELSHADER9 dwShader)
{
	m_CopyState.m_dwPixelShader = m_CurrentState.m_dwPixelShader;
	SetPixelShader(dwShader);
}

void CStateManager::RestorePixelShader()
{
	SetPixelShader(m_CopyState.m_dwPixelShader);
}

void CStateManager::SetPixelShader(LPDIRECT3DPIXELSHADER9 dwShader)
{
	if (m_CurrentState.m_dwPixelShader != dwShader)
	{
		m_CurrentState.m_dwPixelShader = dwShader;
		m_lpD3DDev->SetPixelShader(dwShader);
	}
}

void CStateManager::GetPixelShader(LPDIRECT3DPIXELSHADER9 * pdwShader) const
{
	*pdwShader = m_CurrentState.m_dwPixelShader;
}

// FVF
void CStateManager::SaveFVF(DWORD dwFVF)
{
	m_CopyState.m_dwFVF = m_CurrentState.m_dwFVF;
	SetFVF(dwFVF);
}

void CStateManager::RestoreFVF()
{
	SetFVF(m_CopyState.m_dwFVF);
}

void CStateManager::SetFVF(DWORD dwFVF)
{
	if (m_CurrentState.m_dwFVF != dwFVF)
	{
		m_lpD3DDev->SetFVF(dwFVF);
		m_CurrentState.m_dwFVF = dwFVF;
        m_CurrentState.m_dwVertexDeclaration = nullptr;
	}
}

void CStateManager::GetFVF(DWORD* dwFVF)
{
	*dwFVF = m_CurrentState.m_dwFVF;
}

// *** These states are cached, but not protected from multiple sends of the same value.
// Transform
void CStateManager::SaveTransform(D3DTRANSFORMSTATETYPE Type, const D3DMATRIX* pMatrix)
{
#ifdef _DEBUG
	if (m_bTransformSavingFlag[Type])
	{
		Tracef(" CStateManager::SaveTransform - This transform is already saved [%d]\n", Type);
		StateManager_Assert(!" This trasform is already saved!");
	}
	m_bTransformSavingFlag[Type] = TRUE;
#endif _DEBUG

	m_CopyState.m_Matrices[Type] = m_CurrentState.m_Matrices[Type];
	SetTransform(Type, (D3DXMATRIX *)pMatrix);
}

void CStateManager::RestoreTransform(D3DTRANSFORMSTATETYPE Type)
{
#ifdef _DEBUG
	if (!m_bTransformSavingFlag[Type])
	{
		Tracef(" CStateManager::RestoreTransform - This transform was not saved [%d]\n", Type);
		StateManager_Assert(!" This render state was not saved!");
	}
	m_bTransformSavingFlag[Type] = FALSE;
#endif _DEBUG

	SetTransform(Type, &m_CopyState.m_Matrices[Type]);
}

// Don't cache-check the transform.  To much to do
void CStateManager::SetTransform(D3DTRANSFORMSTATETYPE Type, const D3DMATRIX* pMatrix)
{
    if ((Type == D3DTS_VIEW || Type == D3DTS_PROJECTION || Type == D3DTS_TEXTURE0 || Type == D3DTS_TEXTURE1) &&
        memcmp(&m_CurrentState.m_Matrices[Type], pMatrix, sizeof(*pMatrix)) != 0)
        ++m_vertexProcessingRevision;
	if (m_bScene)
	{
		m_lpD3DDev->SetTransform(Type, pMatrix);
	}
	else
	{
		assert(D3DTS_VIEW==Type || D3DTS_PROJECTION==Type || D3DTS_WORLD==Type);
	}

	m_CurrentState.m_Matrices[Type] = *pMatrix;
}

void CStateManager::GetTransform(D3DTRANSFORMSTATETYPE Type, D3DMATRIX * pMatrix) const
{
	*pMatrix = m_CurrentState.m_Matrices[Type];
}

// SetVertexShaderConstant
void CStateManager::SaveVertexShaderConstant(DWORD dwRegister,CONST void* pConstantData,DWORD dwConstantCount)
{
    if (!pConstantData || dwRegister > STATEMANAGER_MAX_VCONSTANTS || dwConstantCount > STATEMANAGER_MAX_VCONSTANTS - dwRegister) return;
	DWORD i;

	for (i = 0; i < dwConstantCount; i++)
	{
		StateManager_Assert((dwRegister + i) < STATEMANAGER_MAX_VCONSTANTS);
		m_CopyState.m_VertexShaderConstants[dwRegister + i] = m_CurrentState.m_VertexShaderConstants[dwRegister + i];
	}

	SetVertexShaderConstant(dwRegister, pConstantData, dwConstantCount);
}

void CStateManager::RestoreVertexShaderConstant(DWORD dwRegister, DWORD dwConstantCount)
{
	SetVertexShaderConstant(dwRegister, &m_CopyState.m_VertexShaderConstants[dwRegister], dwConstantCount);
}

void CStateManager::SetVertexShaderConstant(DWORD first, CONST void* data, DWORD count)
{
    if (!data || first > m_vertexConstantLimit || count > m_vertexConstantLimit - first) return;
    const size_t bytes = count * sizeof(D3DXVECTOR4);
    if (!m_bForce && !memchr(m_vertexConstantsValid + first, 0, count) &&
        memcmp(m_CurrentState.m_VertexShaderConstants + first, data, bytes) == 0) return;
    const HRESULT result = m_lpD3DDev->SetVertexShaderConstantF(first, static_cast<const float*>(data), count);
    memset(m_vertexConstantsValid + first, SUCCEEDED(result) ? 1 : 0, count);
    if (SUCCEEDED(result)) memmove(m_CurrentState.m_VertexShaderConstants + first, data, bytes);
}

// SetPixelShaderConstant
void CStateManager::SavePixelShaderConstant(DWORD dwRegister,CONST void* pConstantData,DWORD dwConstantCount)
{
    if (!pConstantData || dwRegister > STATEMANAGER_MAX_PCONSTANTS || dwConstantCount > STATEMANAGER_MAX_PCONSTANTS - dwRegister) return;
	DWORD i;

	for (i = 0; i < dwConstantCount; i++)
	{
		StateManager_Assert((dwRegister + i) < STATEMANAGER_MAX_PCONSTANTS);
		m_CopyState.m_PixelShaderConstants[dwRegister + i] = *(((D3DXVECTOR4*)pConstantData) + i);
	}

	SetPixelShaderConstant(dwRegister, pConstantData, dwConstantCount);
}

void CStateManager::RestorePixelShaderConstant(DWORD dwRegister, DWORD dwConstantCount)
{
	SetPixelShaderConstant(dwRegister, &m_CopyState.m_PixelShaderConstants[dwRegister], dwConstantCount);
}

void CStateManager::SetPixelShaderConstant(DWORD first, CONST void* data, DWORD count)
{
    if (!data || first > STATEMANAGER_MAX_PCONSTANTS || count > STATEMANAGER_MAX_PCONSTANTS - first) return;
    const auto* values = static_cast<const D3DXVECTOR4*>(data);
    bool changed = m_bForce;
    for (DWORD i = 0; i < count && !changed; ++i)
        changed = !m_pixelConstantsValid[first + i] ||
            memcmp(&m_CurrentState.m_PixelShaderConstants[first + i], values + i, sizeof(D3DXVECTOR4)) != 0;
    if (!changed) return;
    const HRESULT result = m_lpD3DDev->SetPixelShaderConstantF(first, static_cast<const float*>(data), count);
    for (DWORD i = 0; i < count; ++i)
    {
        m_pixelConstantsValid[first + i] = SUCCEEDED(result);
        if (SUCCEEDED(result)) m_CurrentState.m_PixelShaderConstants[first + i] = values[i];
    }
}

void CStateManager::SaveStreamSource(UINT StreamNumber, LPDIRECT3DVERTEXBUFFER9 pStreamData,UINT Stride)
{
	// Check that we have set this up before, if not, the default is this.
	m_CopyState.m_StreamData[StreamNumber] = m_CurrentState.m_StreamData[StreamNumber];
	SetStreamSource(StreamNumber, pStreamData, Stride);
}

void CStateManager::RestoreStreamSource(UINT StreamNumber)
{
	SetStreamSourceOffset(StreamNumber,
					m_CopyState.m_StreamData[StreamNumber].m_lpStreamData,
					m_CopyState.m_StreamData[StreamNumber].m_Stride,
                    m_CopyState.m_StreamData[StreamNumber].m_Offset);
}

void CStateManager::SetStreamSource(UINT StreamNumber, LPDIRECT3DVERTEXBUFFER9 pStreamData, UINT Stride)
{
    SetStreamSourceOffset(StreamNumber, pStreamData, Stride, 0);
}

HRESULT CStateManager::SetStreamSourceOffset(UINT StreamNumber, LPDIRECT3DVERTEXBUFFER9 pStreamData, UINT Stride, UINT Offset)
{
	const CStreamData kStreamData(pStreamData, Stride, Offset);
	if (m_CurrentState.m_StreamData[StreamNumber] == kStreamData)
		return S_OK;

    const HRESULT result = m_lpD3DDev->SetStreamSource(StreamNumber, pStreamData, Offset, Stride);
    if (SUCCEEDED(result))
	    m_CurrentState.m_StreamData[StreamNumber] = kStreamData;
    return result;
}

void CStateManager::SaveIndices(LPDIRECT3DINDEXBUFFER9 pIndexData, UINT BaseVertexIndex)
{
	m_CopyState.m_IndexData = m_CurrentState.m_IndexData;
	SetIndices(pIndexData, BaseVertexIndex);
}

void CStateManager::RestoreIndices()
{
	SetIndices(m_CopyState.m_IndexData.m_lpIndexData, m_CopyState.m_IndexData.m_BaseVertexIndex);
}

void CStateManager::SetIndices(LPDIRECT3DINDEXBUFFER9 pIndexData, UINT BaseVertexIndex)
{
	const CIndexData kIndexData(pIndexData, BaseVertexIndex);

	if (m_CurrentState.m_IndexData == kIndexData)
		return;

	m_lpD3DDev->SetIndices(pIndexData);
	m_CurrentState.m_IndexData = kIndexData;
}

HRESULT CStateManager::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) const
{
	return (m_lpD3DDev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount));
}

HRESULT CStateManager::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	m_CurrentState.m_StreamData[0] = nullptr;
	return (m_lpD3DDev->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride));
}

HRESULT CStateManager::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT minIndex, UINT NumVertices, UINT startIndex, UINT primCount, INT baseVertexIndex) const
{
	return (m_lpD3DDev->DrawIndexedPrimitive(PrimitiveType, baseVertexIndex, minIndex, NumVertices, startIndex, primCount));
}

HRESULT CStateManager::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertexIndices, UINT PrimitiveCount, CONST void * pIndexData, D3DFORMAT IndexDataFormat, CONST void * pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	m_CurrentState.m_IndexData = nullptr;
	m_CurrentState.m_StreamData[0] = nullptr;
	return (m_lpD3DDev->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertexIndices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride));
}

