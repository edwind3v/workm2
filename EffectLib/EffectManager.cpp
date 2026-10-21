#include "StdAfx.h"
#include "../EterBase/MapPerformance.h"
#include "../eterBase/Random.h"
#include "../eterlib/ShaderManager.h"
#include "EffectManager.h"
#include "GpuParticlePool.h"
#ifdef ENABLE_EFFECT_LIMIT
int CEffectManager::ms_iEffectLimit = EFFECT_LIMIT_DEFAULT;
Vector3 CEffectManager::ms_v3PlayerPosition(0, 0, 0);
#endif

void CEffectManager::GetInfo(std::string* pstInfo) const
{
	char szInfo[256];

	sprintf(szInfo, "Effect: Inst - ED %d, EI %d Pool - PSI %d, MI %d, LI %d, PI %d, EI %d, ED %d, PSD %d, EM %d, LD %d",
		m_kEftDataMap.size(),
		m_kEftInstMap.size(),
		CParticleSystemInstance::ms_kPool.GetCapacity(),
		CEffectMeshInstance::ms_kPool.GetCapacity(),
		CLightInstance::ms_kPool.GetCapacity(),
		CParticleInstance::ms_kPool.GetCapacity(),
		//CRayParticleInstance::ms_kPool.GetCapacity(),
		CEffectInstance::ms_kPool.GetCapacity(),
		CEffectData::ms_kPool.GetCapacity(),
		CParticleSystemData::ms_kPool.GetCapacity(),
		CEffectMeshScript::ms_kPool.GetCapacity(),
		CLightData::ms_kPool.GetCapacity()
	);
	pstInfo->append(szInfo);
}

void CEffectManager::UpdateSound()
{
	for (auto itor = m_kEftInstMap.begin(); itor != m_kEftInstMap.end(); ++itor)
	{
		CEffectInstance * pEffectInstance = itor->second;

		pEffectInstance->UpdateSound();
	}
}

bool CEffectManager::IsAliveEffect(DWORD dwInstanceIndex)
{
	const auto f = m_kEftInstMap.find(dwInstanceIndex);
	if (m_kEftInstMap.end()==f)
		return false;

	return f->second->isAlive() ? true : false;
}

void CEffectManager::Update()
{
	for (auto itor = m_kEftInstMap.begin(); itor != m_kEftInstMap.end();)
	{
		CEffectInstance * pEffectInstance = itor->second;

		pEffectInstance->Update(/*fElapsedTime*/);

		if (!pEffectInstance->isAlive())
		{
			InvalidateRenderOrder();
			itor = m_kEftInstMap.erase(itor);

			CEffectInstance::Delete(pEffectInstance);
		}
		else
		{
			++itor;
		}
	}
}

struct CEffectManager_LessEffectInstancePtrRenderOrder
{
	bool operator() (CEffectInstance* pkLeft, CEffectInstance* pkRight) const
	{
		return pkLeft->LessRenderOrder(pkRight);
	}
};

struct CEffectManager_FEffectInstanceRender
{
	inline void operator () (CEffectInstance * pkEftInst) const
	{
		pkEftInst->Render();
	}
};

void CEffectManager::Render()
{
	SHADERMANAGER.SetShaderResource(0, NULL);
	SHADERMANAGER.SetShaderResource(1, NULL);

	SHADERMANAGER.PushState();

	SHADERMANAGER.SetSamplerState(0, SAMPLER_MINFILTER, FILTER_ANISOTROPIC);
	SHADERMANAGER.SetSamplerState(0, SAMPLER_MAGFILTER, FILTER_ANISOTROPIC);
	SHADERMANAGER.SetPipelineState(PSTATE_BLENDENABLE, TRUE);
	SHADERMANAGER.SetPipelineState(PSTATE_SRCBLEND, BLEND_SRCALPHA);
	SHADERMANAGER.SetPipelineState(PSTATE_DESTBLEND, BLEND_INVSRCALPHA);
	SHADERMANAGER.SetAlphaTestEnabled(false);
	SHADERMANAGER.SetPipelineState(PSTATE_CULLMODE, CULL_NONE);
	SHADERMANAGER.SetPipelineState(PSTATE_DEPTHWRITEMASK, FALSE);
	SHADERMANAGER.SetInputLayout(INPUT_LAYOUT_PT);
	CEffectInstance::ms_bBatchRenderState = true;

	SHADERMANAGER.ResetParticleBatcher();
	SHADERMANAGER.ResetParticleBatchStats();
	SHADERMANAGER.SetParticleBatchingActive(true);



	unsigned rendered = 0;
    auto renderEffect = [&](CEffectInstance* effect) {
#ifdef ENABLE_EFFECT_LIMIT
        const Matrix& transform = effect->GetGlobalMatrix();
        const Vector3 delta = Vector3(transform._41, transform._42, transform._43) - ms_v3PlayerPosition;
        if (ms_iEffectLimit > 0 && rendered >= unsigned(ms_iEffectLimit) &&
            Vec3LengthSq(&delta) > EFFECT_LIMIT_DISTANCE_DEFAULT * EFFECT_LIMIT_DISTANCE_DEFAULT) return;
#endif
        effect->Render(); ++rendered;
    };
    if (m_isDisableSortRendering)
	{
		for (auto itor = m_kEftInstMap.begin(); itor != m_kEftInstMap.end();)
		{
			CEffectInstance * pEffectInstance = itor->second;
			renderEffect(pEffectInstance);
			++itor;
		}
	}
	else
	{
        if (m_renderOrderDirty)
        {
            MapPerformance::Scope sortProfile("effects.sort");
            m_renderOrder.clear();
            m_renderOrder.reserve(m_kEftInstMap.size());
            for (const auto& entry : m_kEftInstMap) m_renderOrder.push_back(entry.second);
            std::sort(m_renderOrder.begin(), m_renderOrder.end(), CEffectManager_LessEffectInstancePtrRenderOrder());
            m_renderOrderDirty = false;
            MapPerformance::Count(MapPerformance::Counter::EffectSorts);
        }
        for (CEffectInstance* effect : m_renderOrder) renderEffect(effect);
	}
	SHADERMANAGER.FlushParticleBatches();
	SHADERMANAGER.SetParticleBatchingActive(false);
	CEffectInstance::ms_bBatchRenderState = false;
	SHADERMANAGER.PopState();
}

BOOL CEffectManager::RegisterEffect(const char * c_szFileName,bool isExistDelete,bool isNeedCache)
{
	std::string strFileName;
	StringPath(c_szFileName, strFileName);
	DWORD dwCRC = GetCaseCRC32(strFileName.c_str(), strFileName.length());

	const auto itor = m_kEftDataMap.find(dwCRC);
	if (m_kEftDataMap.end() != itor)
	{
		if (isExistDelete)
		{
			CEffectData* pkEftData=itor->second;
			CEffectData::Delete(pkEftData);
			m_kEftDataMap.erase(itor);
		}
		else
		{
			//TraceError("CEffectManager::RegisterEffect - m_kEftDataMap.find [%s] Already Exist", c_szFileName);
			return TRUE;
		}
	}

	CEffectData * pkEftData = CEffectData::New();

	if (!pkEftData->LoadScript(c_szFileName))
	{
		TraceError("CEffectManager::RegisterEffect - LoadScript(%s) Error", c_szFileName);
		CEffectData::Delete(pkEftData);
		return FALSE;
	}

	m_kEftDataMap.emplace(dwCRC, pkEftData);

	if (isNeedCache)
	{
		if (!m_kEftCacheMap.contains(dwCRC))
		{
			CEffectInstance* pkNewEftInst=CEffectInstance::New();
			pkNewEftInst->SetEffectDataPointer(pkEftData);
			m_kEftCacheMap.emplace(dwCRC, pkNewEftInst);
		}
	}

	return TRUE;
}
BOOL CEffectManager::RegisterEffect2(const char * c_szFileName, DWORD* pdwRetCRC, bool isNeedCache)
{
	std::string strFileName;
	StringPath(c_szFileName, strFileName);
	const DWORD dwCRC = GetCaseCRC32(strFileName.c_str(), strFileName.length());
	*pdwRetCRC=dwCRC;

	return RegisterEffect(c_szFileName,false,isNeedCache);
}

int CEffectManager::CreateEffect(const char * c_szFileName, const Vector3 & c_rv3Position, const Vector3 & c_rv3Rotation)
{
	std::string strFileName;
	StringPath(c_szFileName, strFileName); //@fixme030
	const DWORD dwID = GetCaseCRC32(strFileName.c_str(), strFileName.size());
	return CreateEffect(dwID, c_rv3Position, c_rv3Rotation);
}

int CEffectManager::CreateEffect(DWORD dwID, const Vector3 & c_rv3Position, const Vector3 & c_rv3Rotation)
{
	const int iInstanceIndex = GetEmptyIndex();

	CreateEffectInstance(iInstanceIndex, dwID);
	SelectEffectInstance(iInstanceIndex);
	Matrix mat;
	MatrixRotationYawPitchRoll(&mat,ToRadian(c_rv3Rotation.x),ToRadian(c_rv3Rotation.y),ToRadian(c_rv3Rotation.z));
	mat._41 = c_rv3Position.x;
	mat._42 = c_rv3Position.y;
	mat._43 = c_rv3Position.z;
	SetEffectInstanceGlobalMatrix(mat);

	return iInstanceIndex;
}

void CEffectManager::CreateEffectInstance(DWORD dwInstanceIndex, DWORD dwID)
{
	if (!dwID)
		return;

	CEffectData * pEffect;
	if (!GetEffectData(dwID, &pEffect))
	{
		Tracef("CEffectManager::CreateEffectInstance - NO DATA :%d\n", dwID);
		return;
	}

	CEffectInstance * pEffectInstance = CEffectInstance::New();
	pEffectInstance->SetEffectDataPointer(pEffect);

	m_kEftInstMap.emplace(dwInstanceIndex, pEffectInstance);
	InvalidateRenderOrder();
}

bool CEffectManager::DestroyEffectInstance(DWORD dwInstanceIndex)
{
	const auto itor = m_kEftInstMap.find(dwInstanceIndex);

	if (itor == m_kEftInstMap.end())
		return false;

	CEffectInstance * pEffectInstance = itor->second;

	InvalidateRenderOrder();
	m_kEftInstMap.erase(itor);

	CEffectInstance::Delete(pEffectInstance);

	return true;
}

void CEffectManager::DeactiveEffectInstance(DWORD dwInstanceIndex)
{
	const auto itor = m_kEftInstMap.find(dwInstanceIndex);

	if (itor == m_kEftInstMap.end())
		return;

	CEffectInstance * pEffectInstance = itor->second;
	pEffectInstance->SetDeactive();
}

void CEffectManager::CreateUnsafeEffectInstance(DWORD dwEffectDataID, CEffectInstance ** ppEffectInstance)
{
	CEffectData * pEffect{nullptr}; //@fixme030
	if (!GetEffectData(dwEffectDataID, &pEffect))
	{
		Tracef("CEffectManager::CreateUnsafeEffectInstance - NO DATA :%d\n", dwEffectDataID);
		return;
	}

	CEffectInstance* pkEftInstNew=CEffectInstance::New();
	pkEftInstNew->SetEffectDataPointer(pEffect);

	*ppEffectInstance = pkEftInstNew;
}

bool CEffectManager::DestroyUnsafeEffectInstance(CEffectInstance * pEffectInstance) const
{
	if (!pEffectInstance)
		return false;

	CEffectInstance::Delete(pEffectInstance);

	return true;
}

BOOL CEffectManager::SelectEffectInstance(DWORD dwInstanceIndex)
{
	const auto itor = m_kEftInstMap.find(dwInstanceIndex);

	m_pSelectedEffectInstance = nullptr;

	if (m_kEftInstMap.end() == itor)
		return FALSE;

	m_pSelectedEffectInstance = itor->second;

	return TRUE;
}

void CEffectManager::SetEffectTextures(DWORD dwID,std::vector<string> textures)
{
	CEffectData * pEffectData;
	if (!GetEffectData(dwID, &pEffectData))
	{
		Tracef("CEffectManager::SetEffectTextures - NO DATA :%d\n", dwID);
		return;
	}

	for(DWORD i = 0; i < textures.size(); i++)
	{
		CParticleSystemData * pParticle = pEffectData->GetParticlePointer(i);
		pParticle->ChangeTexture(textures.at(i).c_str());
	}
}

void CEffectManager::SetEffectInstancePosition(const Vector3 & c_rv3Position) const
{
	if (!m_pSelectedEffectInstance)
	{
//		assert(!"Instance to use is not yet set!");
		return;
	}

	m_pSelectedEffectInstance->SetPosition(c_rv3Position);
}

void CEffectManager::SetEffectInstanceRotation(const Vector3 & c_rv3Rotation) const
{
	if (!m_pSelectedEffectInstance)
	{
//		assert(!"Instance to use is not yet set!");
		return;
	}

	m_pSelectedEffectInstance->SetRotation(c_rv3Rotation.x,c_rv3Rotation.y,c_rv3Rotation.z);
}

void CEffectManager::SetEffectInstanceGlobalMatrix(const Matrix & c_rmatGlobal) const
{
	if (!m_pSelectedEffectInstance)
		return;

	m_pSelectedEffectInstance->SetGlobalMatrix(c_rmatGlobal);
}

void CEffectManager::ShowEffect() const
{
	if (!m_pSelectedEffectInstance)
		return;

	m_pSelectedEffectInstance->Show();
}

void CEffectManager::HideEffect() const
{
	if (!m_pSelectedEffectInstance)
		return;

	m_pSelectedEffectInstance->Hide();
}

#ifdef __ENABLE_STEALTH_FIX__
void CEffectManager::ApplyAlwaysHidden() const
{
	if (!m_pSelectedEffectInstance)
		return;
	m_pSelectedEffectInstance->ApplyAlwaysHidden();
}

void CEffectManager::ReleaseAlwaysHidden() const
{
	if (!m_pSelectedEffectInstance)
		return;
	m_pSelectedEffectInstance->ReleaseAlwaysHidden();
}
#endif

bool CEffectManager::GetEffectData(DWORD dwID, CEffectData ** ppEffect)
{
	const auto itor = m_kEftDataMap.find(dwID);

	if (itor == m_kEftDataMap.end())
		return false;

	*ppEffect = itor->second;

	return true;
}

bool CEffectManager::GetEffectData(DWORD dwID, const CEffectData ** c_ppEffect)
{
	const auto itor = m_kEftDataMap.find(dwID);

	if (itor == m_kEftDataMap.end())
		return false;

	*c_ppEffect = itor->second;

	return true;
}

DWORD CEffectManager::GetRandomEffect()
{
	const int iIndex = random() % m_kEftDataMap.size();

	auto itor = m_kEftDataMap.begin();
	for (int i = 0; i < iIndex; ++i, ++itor);

	return itor->first;
}

int CEffectManager::GetEmptyIndex()
{
	static int iMaxIndex=1;

	if (iMaxIndex>2100000000)
		iMaxIndex = 1;

	int iNextIndex = iMaxIndex++;
	while(m_kEftInstMap.contains(iNextIndex))
		iNextIndex++;

	return iNextIndex;
}

void CEffectManager::DeleteAllInstances()
{
	__DestroyEffectInstanceMap();
}

void CEffectManager::__DestroyEffectInstanceMap()
{
	InvalidateRenderOrder();
	for (auto i = m_kEftInstMap.begin(); i != m_kEftInstMap.end(); ++i)
	{
		CEffectInstance * pkEftInst = i->second;
		CEffectInstance::Delete(pkEftInst);
	}

	m_kEftInstMap.clear();
	std::vector<CEffectInstance*>().swap(m_renderOrder);
}

void CEffectManager::__DestroyEffectCacheMap()
{
	for (auto i = m_kEftCacheMap.begin(); i != m_kEftCacheMap.end(); ++i)
	{
		CEffectInstance * pkEftInst = i->second;
		CEffectInstance::Delete(pkEftInst);
	}

	m_kEftCacheMap.clear();
}

void CEffectManager::__DestroyEffectDataMap()
{
	for (auto i = m_kEftDataMap.begin(); i != m_kEftDataMap.end(); ++i)
	{
		CEffectData * pData = i->second;
		CEffectData::Delete(pData);
	}

	m_kEftDataMap.clear();
}

void CEffectManager::Destroy()
{
	GPU_PARTICLE_POOL.Shutdown();
	__DestroyEffectInstanceMap();
	__DestroyEffectCacheMap();
	__DestroyEffectDataMap();

	__Initialize();
}

void CEffectManager::__Initialize()
{
	m_pSelectedEffectInstance = nullptr;
	m_isDisableSortRendering = false;
}

CEffectManager::CEffectManager()
{
	__Initialize();
}

CEffectManager::~CEffectManager()
{
	Destroy();
}

// just for map effect

