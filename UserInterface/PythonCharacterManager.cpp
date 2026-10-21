#include "stdafx.h"
#include "pythoncharactermanager.h"
#include "PythonBackground.h"
#include "PythonNonPlayer.h"
#include "AbstractPlayer.h"
#include "packet.h"

#include "../eterLib/Camera.h"
#include "../EterLib/ShaderManager.h"
#include "../EterLib/VTFInstanceManager.h"
#include "../EffectLib/EffectManager.h"
#ifdef ENABLE_FRUSTUM_CULLING
int CPythonCharacterManager::ms_iCulledCount = 0;
int CPythonCharacterManager::ms_iRenderedCount = 0;
int CPythonCharacterManager::ms_iTotalCount = 0;
#endif

#ifdef ENABLE_CHAR_RENDER_LIMIT
#ifndef CHAR_RENDER_LIMIT_DEFAULT
#define CHAR_RENDER_LIMIT_DEFAULT 200
#endif
int CPythonCharacterManager::ms_iRenderLimit = CHAR_RENDER_LIMIT_DEFAULT;
int CPythonCharacterManager::ms_iSkippedByLimit = 0;
#endif

#ifdef ENABLE_RENDER_MODE_GROUPING
int CPythonCharacterManager::ms_iRenderModeNormal = 0;
int CPythonCharacterManager::ms_iRenderModeBlend = 0;
int CPythonCharacterManager::ms_iRenderModeAdd = 0;
int CPythonCharacterManager::ms_iRenderModeModulate = 0;
#endif

#ifdef ENABLE_UPDATE_CULLING
int CPythonCharacterManager::ms_iUpdateSkipped = 0;
int CPythonCharacterManager::ms_iUpdateFull = 0;
int CPythonCharacterManager::ms_iUpdateReduced = 0;
#endif

#if defined(ENABLE_UPDATE_CULLING) || defined(ENABLE_ANIMATION_LOD) || defined(ENABLE_DEFORM_CULLING)
DWORD CPythonCharacterManager::ms_dwFrameCounter = 0;
#endif

#ifdef ENABLE_DEFORM_CULLING
int CPythonCharacterManager::ms_iDeformSkipped = 0;
int CPythonCharacterManager::ms_iDeformRendered = 0;
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
// Frame Process





///////////////////////////////////////////////////////////////////////////////////////////////////
// Frame Process

int CHAR_STAGE_VIEW_BOUND = 200*100;

struct FCharacterManagerCharacterInstanceUpdate
{
	inline void operator () (const std::pair<DWORD,CInstanceBase *>& cr_Pair) const
	{
		cr_Pair.second->Update();
	}
};

void CPythonCharacterManager::AdjustCollisionWithOtherObjects(CActorInstance* pInst )
{
	if( !pInst->IsPC() )
		return;

	CPythonCharacterManager& rkChrMgr=CPythonCharacterManager::Instance();
	for(CPythonCharacterManager::CharacterIterator i = rkChrMgr.CharacterInstanceBegin(); i!=rkChrMgr.CharacterInstanceEnd();++i)
	{
		CInstanceBase*  pkInstEach=*i;
		CActorInstance* rkActorEach=pkInstEach->GetGraphicThingInstancePtr();

		if (rkActorEach==pInst)
			continue;

		if( rkActorEach->IsPC() || rkActorEach->IsNPC() || rkActorEach->IsEnemy() )
			continue;

		if(pInst->TestPhysicsBlendingCollision(*rkActorEach) )
		{
			TPixelPosition curPos;
			pInst->GetPixelPosition(&curPos);
			pInst->SetBlendingPosition(curPos);
			//Tracef("!!!!!! Collision Adjusted\n");
			break;
		}
	}
}

void CPythonCharacterManager::EnableSortRendering(bool isEnable) const
{
}

void CPythonCharacterManager::InsertPVPKey(DWORD dwVIDSrc, DWORD dwVIDDst)
{
	CInstanceBase::InsertPVPKey(dwVIDSrc, dwVIDDst);

	CInstanceBase* pkInstSrc=GetInstancePtr(dwVIDSrc);
	if (pkInstSrc)
		pkInstSrc->RefreshTextTail();

	CInstanceBase* pkInstDst=GetInstancePtr(dwVIDDst);
	if (pkInstDst)
		pkInstDst->RefreshTextTail();
}

void CPythonCharacterManager::RemovePVPKey(DWORD dwVIDSrc, DWORD dwVIDDst)
{
	CInstanceBase::RemovePVPKey(dwVIDSrc, dwVIDDst);

	CInstanceBase* pkInstSrc=GetInstancePtr(dwVIDSrc);
	if (pkInstSrc)
		pkInstSrc->RefreshTextTail();

	CInstanceBase* pkInstDst=GetInstancePtr(dwVIDDst);
	if (pkInstDst)
		pkInstDst->RefreshTextTail();
}

void CPythonCharacterManager::ChangeGVG(DWORD dwSrcGuildID, DWORD dwDstGuildID)
{
	TCharacterInstanceMap::iterator itor;
	for (itor = m_kAliveInstMap.begin(); itor != m_kAliveInstMap.end(); itor++)
	{
		CInstanceBase * pInstance = itor->second;

		const DWORD dwInstanceGuildID = pInstance->GetGuildID();
		if (dwSrcGuildID == dwInstanceGuildID || dwDstGuildID == dwInstanceGuildID)
		{
			pInstance->RefreshTextTail();
		}
	}
}

#ifdef ENABLE_AREA_OPTIMIZATION
void CPythonCharacterManager::RefreshAllCharacterHeights()
{
	const float fNudgeEpsilon = 0.01f;
	CPythonBackground& rkBg = CPythonBackground::Instance();
	for (auto itor = m_kAliveInstMap.begin(); itor != m_kAliveInstMap.end(); ++itor)
	{
		CInstanceBase* pInstance = itor->second;
		TPixelPosition kPPos;
		pInstance->NEW_GetPixelPosition(&kPPos);
		kPPos.z = rkBg.GetHeight(kPPos.x, kPPos.y);
		pInstance->NEW_SetPixelPosition(kPPos);
		kPPos.x += fNudgeEpsilon;
		pInstance->NEW_SetPixelPosition(kPPos);
		kPPos.x -= fNudgeEpsilon;
		pInstance->NEW_SetPixelPosition(kPPos);
	}
}
#endif

void CPythonCharacterManager::ClearMainInstance()
{
	m_pkInstMain= nullptr;
}

bool CPythonCharacterManager::SetMainInstance(DWORD dwVID)
{
	m_pkInstMain=GetInstancePtr(dwVID);

	if (!m_pkInstMain)
		return false;

	return true;
}

CInstanceBase* CPythonCharacterManager::GetMainInstancePtr() const
{
	return m_pkInstMain;
}

void CPythonCharacterManager::GetInfo(std::string* pstInfo) const
{
	pstInfo->append("Actor: ");

	CInstanceBase::GetInfo(pstInfo);

	char szInfo[256];
	sprintf(szInfo, "Container - Live %d, Dead %d", m_kAliveInstMap.size(), m_kDeadInstList.size());
	pstInfo->append(szInfo);
}

bool CPythonCharacterManager::IsCacheMode() const
{
	static bool s_isOldCacheMode=false;

	bool isCacheMode=s_isOldCacheMode;
	if (s_isOldCacheMode)
	{
		if (m_kAliveInstMap.size()<30)
			isCacheMode=false;
	}
	else
	{
		if (m_kAliveInstMap.size()>40)
			isCacheMode=true;
	}
	s_isOldCacheMode=isCacheMode;

	return isCacheMode;
}

void CPythonCharacterManager::Update()
{
#ifdef __PERFORMANCE_CHECKER__
	DWORD t1=timeGetTime();
#endif
	CInstanceBase::ResetPerformanceCounter();
#if defined(ENABLE_UPDATE_CULLING) || defined(ENABLE_ANIMATION_LOD) || defined(ENABLE_DEFORM_CULLING)
    ++ms_dwFrameCounter;
#endif
#ifdef ENABLE_UPDATE_CULLING
    ms_iUpdateSkipped = ms_iUpdateFull = ms_iUpdateReduced = 0;
#endif

	CInstanceBase* pkInstMain=GetMainInstancePtr();
#ifdef __PERFORMANCE_CHECKER__
	DWORD t2=timeGetTime();
	DWORD dwDeadInstCount=0;
	DWORD dwForceVisibleInstCount=0;
#endif

	auto i=m_kAliveInstMap.begin();
	while (m_kAliveInstMap.end()!=i)
	{
		const auto c=i++;

		CInstanceBase* pkInstEach=c->second;
		bool update = true;
#ifdef ENABLE_UPDATE_CULLING
        if (pkInstMain && pkInstEach != pkInstMain && !pkInstEach->IsPC())
        {
            const float distance = pkInstEach->NEW_GetDistanceFromDestInstance(*pkInstMain);
            const DWORD cadence = distance > UPDATE_CULLING_DISTANCE_FAR ? UPDATE_CULLING_FRAME_SKIP_FAR :
                distance > UPDATE_CULLING_DISTANCE_MID ? UPDATE_CULLING_FRAME_SKIP_MID : 1;
            update = (ms_dwFrameCounter % cadence) == (pkInstEach->GetVirtualID() % cadence);
        }
        if (update) ++ms_iUpdateFull; else ++ms_iUpdateSkipped;
#endif
        if (update) pkInstEach->Update();

		if (pkInstMain)
		{
#ifdef __PERFORMANCE_CHECKER__
			if (pkInstEach->IsForceVisible())
			{
				dwForceVisibleInstCount++;
				continue;
			}
#endif

			const int nDistance = int(pkInstEach->NEW_GetDistanceFromDestInstance(*pkInstMain));
			if (nDistance > CHAR_STAGE_VIEW_BOUND + 10)
			{
				__DeleteBlendOutInstance(pkInstEach);
				m_kAliveInstMap.erase(c);
#ifdef __PERFORMANCE_CHECKER__
				dwDeadInstCount++;
#endif
			}
		}
	}
#ifdef __PERFORMANCE_CHECKER__
	DWORD t3=timeGetTime();
#endif
	UpdateTransform();
#ifdef __PERFORMANCE_CHECKER__
	DWORD t4=timeGetTime();
#endif

	UpdateDeleting();
#ifdef __PERFORMANCE_CHECKER__
	DWORD t5=timeGetTime();
#endif

	__NEW_Pick();
#ifdef __PERFORMANCE_CHECKER__
	DWORD t6=timeGetTime();
#endif

#ifdef __PERFORMANCE_CHECKER__
	{
		static FILE* fp=fopen("perf_chrmgr_update.txt", "w");

		if (t6-t1>1)
		{
			fprintf(fp, "CU.Total %d (Time %d, Alive %d, Dead %d)\n",
				t6-t1, ELTimer_GetMSec(),
				m_kAliveInstMap.size(),
				m_kDeadInstList.size());
			fprintf(fp, "CU.Counter %d\n", t2-t1);
			fprintf(fp, "CU.ForEach %d\n", t3-t2);
			fprintf(fp, "CU.Trans %d\n", t4-t3);
			fprintf(fp, "CU.Del %d\n", t5-t4);
			fprintf(fp, "CU.Pick %d\n", t6-t5);
			fprintf(fp, "CU.AI %d\n", m_kAliveInstMap.size());
			fprintf(fp, "CU.DI %d\n", dwDeadInstCount);
			fprintf(fp, "CU.FVI %d\n", dwForceVisibleInstCount);
			fprintf(fp, "-------------------------------- \n");
			fflush(fp);
		}
	}
#endif
}

void CPythonCharacterManager::ShowPointEffect(DWORD ePoint, DWORD dwVID)
{
	CInstanceBase * pkInstSel = (dwVID == 0xffffffff) ? GetMainInstancePtr() : GetInstancePtr(dwVID);

	if (!pkInstSel)
		return;

	switch (ePoint)
	{
		case POINT_LEVEL:
			pkInstSel->LevelUp();
			break;
		case POINT_LEVEL_STEP:
			pkInstSel->SkillUp();
			break;
	}
}

bool CPythonCharacterManager::RegisterPointEffect(DWORD ePoint, const char* c_szFileName)
{
	if (ePoint>=POINT_MAX_NUM)
		return false;

	CEffectManager& rkEftMgr=CEffectManager::Instance();
	rkEftMgr.RegisterEffect2(c_szFileName, &m_adwPointEffect[ePoint]);

	return true;
}

void CPythonCharacterManager::UpdateTransform()
{
#ifdef __PERFORMANCE_CHECKER__
	DWORD t1=timeGetTime();
	DWORD t2=timeGetTime();
#endif

	const CInstanceBase * pMainInstance = GetMainInstancePtr();
	if (pMainInstance)
	{
		CPythonBackground& rkBG=CPythonBackground::Instance();
		for (auto i = m_kAliveInstMap.begin(); i != m_kAliveInstMap.end(); ++i)
		{
			CInstanceBase * pSrcInstance = i->second;

			pSrcInstance->CheckAdvancing();

			if (pSrcInstance->IsPushing())
				rkBG.CheckAdvancing(pSrcInstance);
		}
#ifdef __PERFORMANCE_CHECKER__
		t2=timeGetTime();
#endif

#ifdef __MOVIE_MODE__
		if (!m_pkInstMain->IsMovieMode())
		{
			rkBG.CheckAdvancing(m_pkInstMain);
		}
#else
		rkBG.CheckAdvancing(m_pkInstMain);
#endif
	}

#ifdef __PERFORMANCE_CHECKER__
	DWORD t3=timeGetTime();
#endif

	{
		for (auto itor = m_kAliveInstMap.begin(); itor != m_kAliveInstMap.end(); ++itor)
		{
			CInstanceBase * pInstance = itor->second;
			pInstance->Transform();
		}
	}

#ifdef __PERFORMANCE_CHECKER__
	DWORD t4=timeGetTime();
#endif

#ifdef __PERFORMANCE_CHECKER__
	{
		static FILE* fp=fopen("perf_chrmgr_updatetransform.txt", "w");

		if (t4-t1>5)
		{
			fprintf(fp, "CUT.Total %d (Time %f, Alive %d, Dead %d)\n",
				t4-t1, ELTimer_GetMSec()/1000.0f,
				m_kAliveInstMap.size(),
				m_kDeadInstList.size());
			fprintf(fp, "CUT.ChkAdvInst %d\n", t2-t1);
			fprintf(fp, "CUT.ChkAdvBG %d\n", t3-t2);
			fprintf(fp, "CUT.Trans %d\n", t4-t3);

			fprintf(fp, "-------------------------------- \n");
			fflush(fp);
		}

		fflush(fp);
	}
#endif
}
void CPythonCharacterManager::UpdateDeleting()
{
	auto itor = m_kDeadInstList.begin();
	for (; itor != m_kDeadInstList.end();)
	{
		CInstanceBase * pInstance = *itor;

		if (pInstance->UpdateDeleting())
		{
			++itor;
		}
		else
		{
			CInstanceBase::Delete(pInstance);
			itor = m_kDeadInstList.erase(itor);
		}
	}
}

struct FCharacterManagerCharacterInstanceDeform
{
	inline void operator () (const std::pair<DWORD,CInstanceBase *>& cr_Pair) const
	{
		cr_Pair.second->Deform();
		//pInstance->Update();
	}
};
struct FCharacterManagerCharacterInstanceListDeform
{
	inline void operator () (CInstanceBase * pInstance) const
	{
		pInstance->Deform();
	}
};

void CPythonCharacterManager::Deform()
{
#if defined(ENABLE_DEFORM_CULLING) || defined(ENABLE_ANIMATION_LOD)
#ifdef ENABLE_DEFORM_CULLING
    ms_iDeformSkipped = ms_iDeformRendered = 0;
#endif
    CInstanceBase* main = GetMainInstancePtr();
    for (const auto& entry : m_kAliveInstMap)
    {
        CInstanceBase* instance = entry.second;
        bool deform = true;
        if (main && main != instance && !instance->IsPC())
        {
            const float distance = instance->NEW_GetDistanceFromDestInstance(*main);
#ifdef ENABLE_DEFORM_CULLING
            if (distance > UPDATE_CULLING_DISTANCE_FAR) deform = false;
#endif
#ifdef ENABLE_ANIMATION_LOD
            const DWORD cadence = distance > ANIMATION_LOD_DISTANCE_MID ? ANIMATION_LOD_SKIP_FRAMES_FAR :
                distance > ANIMATION_LOD_DISTANCE_HIGH ? ANIMATION_LOD_SKIP_FRAMES_MID : 1;
            deform = deform && (ms_dwFrameCounter + entry.first) % cadence == 0;
#endif
        }
        if (deform) instance->Deform();
#ifdef ENABLE_DEFORM_CULLING
        if (deform) ++ms_iDeformRendered; else ++ms_iDeformSkipped;
#endif
    }
#else
    std::for_each(m_kAliveInstMap.begin(), m_kAliveInstMap.end(), FCharacterManagerCharacterInstanceDeform());
#endif
    std::for_each(m_kDeadInstList.begin(), m_kDeadInstList.end(), FCharacterManagerCharacterInstanceListDeform());
}

bool CPythonCharacterManager::OLD_GetPickedInstanceVID(DWORD* pdwPickedActorID) const
{
	if (!m_pkInstPick)
		return false;

	*pdwPickedActorID=m_pkInstPick->GetVirtualID();
	return true;
}

CInstanceBase * CPythonCharacterManager::OLD_GetPickedInstancePtr() const
{
	return m_pkInstPick;
}

Vector2 & CPythonCharacterManager::OLD_GetPickedInstPosReference()
{
	return m_v2PickedInstProjPos;
}

bool CPythonCharacterManager::IsRegisteredVID(DWORD dwVID)
{
	if (!m_kAliveInstMap.contains(dwVID))
		return false;

	return true;
}

bool CPythonCharacterManager::IsAliveVID(DWORD dwVID)
{
	return m_kAliveInstMap.contains(dwVID);
}

bool CPythonCharacterManager::IsDeadVID(DWORD dwVID)
{
	for (auto f=m_kDeadInstList.begin(); f!=m_kDeadInstList.end(); ++f)
	{
		if ((*f)->GetVirtualID()==dwVID)
			return true;
	}

	return false;
}

struct LessCharacterInstancePtrRenderOrder
{
	bool operator() (CInstanceBase* pkLeft, CInstanceBase* pkRight) const
	{
		return pkLeft->LessRenderOrder(pkRight);
	}
};

struct FCharacterManagerCharacterInstanceRender
{
	inline void operator () (const std::pair<DWORD,CInstanceBase *>& cr_Pair) const
	{
		cr_Pair.second->Render();
		cr_Pair.second->RenderTrace();
	}
};
struct FCharacterInstanceRender
{
	inline void operator () (CInstanceBase * pInstance) const
	{
		pInstance->Render();
	}
};
struct FCharacterInstanceRenderTrace
{
	inline void operator () (CInstanceBase * pInstance) const
	{
		pInstance->RenderTrace();
	}
};

void CPythonCharacterManager::__RenderSortedAliveActorList()
{
#if defined(ENABLE_FRUSTUM_CULLING) || defined(ENABLE_CHAR_RENDER_LIMIT) || defined(ENABLE_RENDER_MODE_GROUPING)
	ResetCullingStats();
#endif

	CInstanceBase* pkInstMain = GetMainInstancePtr();
	if (pkInstMain)
	{
		Vector3 v3PlayerPos = pkInstMain->GetGraphicThingInstanceRef().GetPosition();
#ifdef ENABLE_EFFECT_LIMIT
		CEffectManager::SetMainPlayerPosition(v3PlayerPos);
#endif


	}

	static std::vector<CInstanceBase*> s_kVct_pkInstAliveSort;
	s_kVct_pkInstAliveSort.clear();

	TCharacterInstanceMap& rkMap_pkInstAlive = m_kAliveInstMap;
	s_kVct_pkInstAliveSort.reserve(rkMap_pkInstAlive.size());

	for (auto& [key, inst] : rkMap_pkInstAlive)
		s_kVct_pkInstAliveSort.push_back(inst);

#ifdef ENABLE_CHAR_RENDER_LIMIT
	if (ms_iRenderLimit > 0)
	{
		CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();

		if (pCamera)
		{
			Vector3 v3CameraPos = pCamera->GetEye();

			std::sort(s_kVct_pkInstAliveSort.begin(), s_kVct_pkInstAliveSort.end(),
				[&v3CameraPos](CInstanceBase* a, CInstanceBase* b)
				{
					const Vector3& v3PosA = a->GetGraphicThingInstanceRef().GetPosition();
					const Vector3& v3PosB = b->GetGraphicThingInstanceRef().GetPosition();

					float fDistA =
						(v3PosA.x - v3CameraPos.x) * (v3PosA.x - v3CameraPos.x) +
						(v3PosA.y - v3CameraPos.y) * (v3PosA.y - v3CameraPos.y) +
						(v3PosA.z - v3CameraPos.z) * (v3PosA.z - v3CameraPos.z);

					float fDistB =
						(v3PosB.x - v3CameraPos.x) * (v3PosB.x - v3CameraPos.x) +
						(v3PosB.y - v3CameraPos.y) * (v3PosB.y - v3CameraPos.y) +
						(v3PosB.z - v3CameraPos.z) * (v3PosB.z - v3CameraPos.z);

					return fDistA < fDistB;
				});
		}
		else
		{
			std::sort(s_kVct_pkInstAliveSort.begin(), s_kVct_pkInstAliveSort.end(), LessCharacterInstancePtrRenderOrder());
		}
	}
	else
#endif
	{
		std::sort(s_kVct_pkInstAliveSort.begin(), s_kVct_pkInstAliveSort.end(), LessCharacterInstancePtrRenderOrder());
	}

#ifdef ENABLE_RENDER_MODE_GROUPING
	static std::vector<CInstanceBase*> s_kVct_pkInstNormal;
	static std::vector<CInstanceBase*> s_kVct_pkInstBlend;
	static std::vector<CInstanceBase*> s_kVct_pkInstAdd;
	static std::vector<CInstanceBase*> s_kVct_pkInstModulate;

	s_kVct_pkInstNormal.clear();
	s_kVct_pkInstBlend.clear();
	s_kVct_pkInstAdd.clear();
	s_kVct_pkInstModulate.clear();
#else
	static std::vector<CInstanceBase*> s_kVct_pkInstVTFDeferred;
	s_kVct_pkInstVTFDeferred.clear();
#endif

#ifdef ENABLE_CHAR_RENDER_LIMIT
	int iRenderCount = 0;
	int iLimit = ms_iRenderLimit;
#endif

	if (VTFMANAGER.IsInitialized())
		VTFMANAGER.ClearDeferredRigidBatches();

	for (CInstanceBase* pInstance : s_kVct_pkInstAliveSort)
	{
#ifdef ENABLE_CHAR_RENDER_LIMIT
		bool bIsImportant = pInstance == pkInstMain || pInstance->IsPC();

		if (!bIsImportant && iLimit > 0 && iRenderCount >= iLimit)
		{
			++ms_iSkippedByLimit;
			continue;
		}
#endif

#ifdef ENABLE_FRUSTUM_CULLING
		if (pkInstMain)
		{
			++ms_iTotalCount;

			Vector3 v3Center;
			float fRadius = 0.0f;
			pInstance->GetGraphicThingInstanceRef().GetBoundingSphere(v3Center, fRadius);

			if (CScreen::GetFrustum().ViewVolumeTest(Vector3d(v3Center.x, v3Center.y, v3Center.z), fRadius) == VS_OUTSIDE)
			{
				++ms_iCulledCount;
				continue;
			}

			++ms_iRenderedCount;
		}
#endif

#ifdef ENABLE_CHAR_RENDER_LIMIT
		++iRenderCount;
#endif

#ifdef ENABLE_RENDER_MODE_GROUPING
		switch (pInstance->GetGraphicThingInstanceRef().GetRenderMode())
		{
		case CActorInstance::RENDER_MODE_BLEND:
			s_kVct_pkInstBlend.push_back(pInstance);
			break;

		case CActorInstance::RENDER_MODE_ADD:
			s_kVct_pkInstAdd.push_back(pInstance);
			break;

		case CActorInstance::RENDER_MODE_MODULATE:
			s_kVct_pkInstModulate.push_back(pInstance);
			break;

		default:
			s_kVct_pkInstNormal.push_back(pInstance);
			break;
		}
#else
		if (VTFMANAGER.IsInitialized())
		{
			s_kVct_pkInstVTFDeferred.push_back(pInstance);
			pInstance->RenderWithRigidDefer();
		}
		else
		{
			pInstance->Render();
		}

		pInstance->RenderTrace();
#endif
	}

#ifndef ENABLE_RENDER_MODE_GROUPING
	SHADERMANAGER.PushState();
	SHADERMANAGER.SetPipelineState(PSTATE_CULLMODE, CULL_NONE);

	if (VTFMANAGER.IsInitialized() && VTFMANAGER.HasDeferredRigidBatches())
		VTFMANAGER.FlushDeferredRigidBatches();

	for (CInstanceBase* pInstance : s_kVct_pkInstVTFDeferred)
		pInstance->RenderBlendPassDeferred();

	SHADERMANAGER.PopState();
#endif

#ifdef ENABLE_RENDER_MODE_GROUPING
	if (VTFMANAGER.IsInitialized())
	{
		VTFMANAGER.ClearDeferredRigidBatches();

		for (CInstanceBase* pInstance : s_kVct_pkInstNormal)
		{
			pInstance->RenderWithRigidDefer();
			pInstance->RenderTrace();
		}

		SHADERMANAGER.PushState();
		SHADERMANAGER.SetPipelineState(PSTATE_CULLMODE, CULL_NONE);

		VTFMANAGER.FlushDeferredRigidBatches();

		for (CInstanceBase* pInstance : s_kVct_pkInstNormal)
			pInstance->RenderBlendPassDeferred();

		SHADERMANAGER.PopState();
	}
	else
	{
		for (CInstanceBase* pInstance : s_kVct_pkInstNormal)
		{
			pInstance->Render();
			pInstance->RenderTrace();
		}
	}

	if (VTFMANAGER.IsInitialized())
	{
		auto renderModeVTF = [](std::vector<CInstanceBase*>& vec)
			{
				if (vec.empty())
					return;

				VTFMANAGER.ClearDeferredRigidBatches();

				for (CInstanceBase* pInstance : vec)
				{
					pInstance->RenderWithRigidDefer();
					pInstance->RenderTrace();
				}

				SHADERMANAGER.PushState();
				SHADERMANAGER.SetPipelineState(PSTATE_CULLMODE, CULL_NONE);

				VTFMANAGER.FlushDeferredRigidBatches();

				for (CInstanceBase* pInstance : vec)
					pInstance->RenderBlendPassDeferred();

				SHADERMANAGER.PopState();
			};

		renderModeVTF(s_kVct_pkInstBlend);
		renderModeVTF(s_kVct_pkInstAdd);
		renderModeVTF(s_kVct_pkInstModulate);
	}
	else
	{
		for (CInstanceBase* pInstance : s_kVct_pkInstBlend)
		{
			pInstance->Render();
			pInstance->RenderTrace();
		}

		for (CInstanceBase* pInstance : s_kVct_pkInstAdd)
		{
			pInstance->Render();
			pInstance->RenderTrace();
		}

		for (CInstanceBase* pInstance : s_kVct_pkInstModulate)
		{
			pInstance->Render();
			pInstance->RenderTrace();
		}
	}

	ms_iRenderModeNormal = (int)s_kVct_pkInstNormal.size();
	ms_iRenderModeBlend = (int)s_kVct_pkInstBlend.size();
	ms_iRenderModeAdd = (int)s_kVct_pkInstAdd.size();
	ms_iRenderModeModulate = (int)s_kVct_pkInstModulate.size();
#endif
}

void CPythonCharacterManager::__RenderSortedDeadActorList()
{
	static std::vector<CInstanceBase*> s_kVct_pkInstDeadSort;
	s_kVct_pkInstDeadSort.clear();

	TCharacterInstanceList& rkLst_pkInstDead=m_kDeadInstList;
	TCharacterInstanceList::iterator i;
	for (i=rkLst_pkInstDead.begin(); i!=rkLst_pkInstDead.end(); ++i)
		s_kVct_pkInstDeadSort.push_back(*i);

	std::sort(s_kVct_pkInstDeadSort.begin(), s_kVct_pkInstDeadSort.end(), LessCharacterInstancePtrRenderOrder());
	std::for_each(s_kVct_pkInstDeadSort.begin(), s_kVct_pkInstDeadSort.end(), FCharacterInstanceRender());
}

void CPythonCharacterManager::Render()
{
	// Terrain/tree helpers may have changed native bindings outside this pass.
	SHADERMANAGER.InvalidateDeviceStateCache();
	SHADERMANAGER.SetViewProjection(&CGraphicBase::GetViewMatrix(), &CGraphicBase::GetProjectionMatrix());
	const UINT drawStart = SHADERMANAGER.GetGlobalDrawCount();
	CGrannyMaterial::ResetRenderStateCache();
	SHADERMANAGER.SetLightingEnabled(true);
	SHADERMANAGER.SetDiffuseColor(1.0f, 1.0f, 1.0f, 1.0f);
	SHADERMANAGER.SetDefaultTexture(0);
	SHADERMANAGER.SetShaderResource(1, nullptr);
	SHADERMANAGER.SetTwoTextureBlend(false);
	__RenderSortedAliveActorList();
	__RenderSortedDeadActorList();
	if (CInstanceBase* picked = OLD_GetPickedInstancePtr())
	{
		const Vector3& pos = picked->GetGraphicThingInstanceRef().GetPosition();
		CPythonGraphic::Instance().ProjectPosition(pos.x, pos.y, pos.z, &m_v2PickedInstProjPos.x, &m_v2PickedInstProjPos.y);
	}
    static DWORD lastReport = 0;
    const DWORD now = GetTickCount();
    if (!m_kAliveInstMap.empty() && now - lastReport >= 5000)
    {
        lastReport = now;
        auto* main = GetMainInstancePtr();
        Vector3 center(0, 0, 0); float radius = 0.0f;
        if (main) main->GetGraphicThingInstanceRef().GetBoundingSphere(center, radius);
        TraceError("DX11_ACTORS alive=%u dead=%u draws=%u main_shown=%d center=(%.1f,%.1f,%.1f) radius=%.1f",
            unsigned(m_kAliveInstMap.size()), unsigned(m_kDeadInstList.size()),
            SHADERMANAGER.GetGlobalDrawCount() - drawStart,
            main ? int(main->GetGraphicThingInstanceRef().isShow()) : -1, center.x, center.y, center.z, radius);
    }
}

void CPythonCharacterManager::RenderShadowMainInstance() const
{
	CInstanceBase* pkInstMain=GetMainInstancePtr();
	if (pkInstMain)
		pkInstMain->RenderToShadowMap();
}

struct FCharacterManagerCharacterInstanceRenderToShadowMap
{
	inline void operator () (const std::pair<DWORD,CInstanceBase *>& cr_Pair) const
	{
		cr_Pair.second->RenderToShadowMap();
	}
};

void CPythonCharacterManager::RenderShadowAllInstances()
{
	CGrannyModelInstance::ms_bShadowSkipRigid = VTFMANAGER.IsInitialized();

	for (auto* pkInst : m_kVct_pkShadowCasters)
		pkInst->RenderToShadowMap();

	CGrannyModelInstance::ms_bShadowSkipRigid = false;

	VTFMANAGER.FlushDeferredShadowBatches();
}

struct FCharacterManagerCharacterInstanceRenderCollision
{
	inline void operator () (const std::pair<DWORD,CInstanceBase *>& cr_Pair) const
	{
		cr_Pair.second->RenderCollision();
	}
};

void CPythonCharacterManager::RenderCollision()
{
 	std::for_each(m_kAliveInstMap.begin(), m_kAliveInstMap.end(), FCharacterManagerCharacterInstanceRenderCollision());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Managing Process

CInstanceBase * CPythonCharacterManager::CreateInstance(const CInstanceBase::SCreateData& c_rkCreateData)
{
	CInstanceBase * pCharacterInstance = RegisterInstance(c_rkCreateData.m_dwVID);
	if (!pCharacterInstance)
	{
		TraceError("CPythonCharacterManager::CreateInstance: VID[%d] - ALREADY EXIST\n", c_rkCreateData.m_dwVID); // @fixme010
		return nullptr;
	}

	if (!pCharacterInstance->Create(c_rkCreateData))
	{
		TraceError("CPythonCharacterManager::CreateInstance VID[%d] Race[%d]", c_rkCreateData.m_dwVID, c_rkCreateData.m_dwRace);
		DeleteInstance(c_rkCreateData.m_dwVID);
		return nullptr;
	}

	if (c_rkCreateData.m_isMain)
		SelectInstance(c_rkCreateData.m_dwVID);

	return (pCharacterInstance);
}

CInstanceBase * CPythonCharacterManager::RegisterInstance(DWORD VirtualID)
{
	const auto itor = m_kAliveInstMap.find(VirtualID);

	if (m_kAliveInstMap.end() != itor)
	{
		return nullptr;
	}

	CInstanceBase * pCharacterInstance = CInstanceBase::New();
	m_kAliveInstMap.insert(TCharacterInstanceMap::value_type(VirtualID, pCharacterInstance));

	return (pCharacterInstance);
}

void CPythonCharacterManager::DeleteInstance(DWORD dwDelVID)
{
	const auto itor = m_kAliveInstMap.find(dwDelVID);

	if (m_kAliveInstMap.end() == itor)
	{
		Tracef("DeleteCharacterInstance: no vid by %d\n", dwDelVID);
		return;
	}

	CInstanceBase * pkInstDel = itor->second;

	if (pkInstDel == m_pkInstBind)
		m_pkInstBind = nullptr;

	if (pkInstDel == m_pkInstMain)
		m_pkInstMain = nullptr;

	if (pkInstDel == m_pkInstPick)
		m_pkInstPick = nullptr;

	CInstanceBase::Delete(pkInstDel);

	m_kAliveInstMap.erase(itor);
}

void CPythonCharacterManager::__DeleteBlendOutInstance(CInstanceBase* pkInstDel)
{
	pkInstDel->DeleteBlendOut();
	m_kDeadInstList.push_back(pkInstDel);

	IAbstractPlayer& rkPlayer=IAbstractPlayer::GetSingleton();
	rkPlayer.NotifyCharacterDead(pkInstDel->GetVirtualID());
}

void CPythonCharacterManager::DeleteInstanceByFade(DWORD dwVID)
{
	const auto f = m_kAliveInstMap.find(dwVID);
	if (m_kAliveInstMap.end() == f)
	{
		return;
	}
	__DeleteBlendOutInstance(f->second);
	m_kAliveInstMap.erase(f);
}

void CPythonCharacterManager::SelectInstance(DWORD VirtualID)
{
	const auto itor = m_kAliveInstMap.find(VirtualID);

	if (m_kAliveInstMap.end() == itor)
	{
		Tracef("SelectCharacterInstance: no vid by %d\n", VirtualID);
		return;
	}

	m_pkInstBind = itor->second;
}

CInstanceBase * CPythonCharacterManager::GetInstancePtr(DWORD VirtualID)
{
	const auto itor = m_kAliveInstMap.find(VirtualID);

	if (m_kAliveInstMap.end() == itor)
		return nullptr;

	return itor->second;
}

CInstanceBase * CPythonCharacterManager::GetInstancePtrByName(const char *name)
{
	TCharacterInstanceMap::iterator itor;

	for (itor = m_kAliveInstMap.begin(); itor != m_kAliveInstMap.end(); itor++)
	{
		CInstanceBase * pInstance = itor->second;

		if (!strcmp(pInstance->GetNameString(), name))
			return pInstance;
	}

	return nullptr;
}

CInstanceBase * CPythonCharacterManager::GetSelectedInstancePtr() const
{
	return m_pkInstBind;
}

CInstanceBase* CPythonCharacterManager::FindClickableInstancePtr() const
{
	return nullptr;
}

void CPythonCharacterManager::__UpdateSortPickedActorList()
{
	__UpdatePickedActorList();
	__SortPickedActorList();
}

void CPythonCharacterManager::__UpdatePickedActorList()
{
	m_kVct_pkInstPicked.clear();

	TCharacterInstanceMap::iterator i;
	for (i=m_kAliveInstMap.begin(); i!=m_kAliveInstMap.end(); ++i)
	{
		CInstanceBase* pkInstEach=i->second;
		if (pkInstEach->CanPickInstance())
		{
			if (pkInstEach->IsDead())
			{
				if (pkInstEach->IntersectBoundingBox())
					m_kVct_pkInstPicked.push_back(pkInstEach);
			}
			else
			{
				if (pkInstEach->IntersectDefendingSphere())
					m_kVct_pkInstPicked.push_back(pkInstEach);
			}
		}
	}
}

struct CInstanceBase_SLessCameraDistance
{
	TPixelPosition m_kPPosEye;

	bool operator() (CInstanceBase* pkInstLeft, CInstanceBase* pkInstRight) const
	{
		const int nLeftDeadPoint=pkInstLeft->IsDead();
		const int nRightDeadPoint=pkInstRight->IsDead();

		if (nLeftDeadPoint<nRightDeadPoint)
			return true;

		if (pkInstLeft->CalculateDistanceSq3d(m_kPPosEye)<pkInstRight->CalculateDistanceSq3d(m_kPPosEye))
			return true;

		return false;
	}
};

void CPythonCharacterManager::__SortPickedActorList()
{
	const CCamera * pCamera = CCameraManager::Instance().GetCurrentCamera();
	const Vector3& c_rv3EyePos=pCamera->GetEye();

	CInstanceBase_SLessCameraDistance kLess;
	kLess.m_kPPosEye=TPixelPosition(+c_rv3EyePos.x, -c_rv3EyePos.y, +c_rv3EyePos.z);

	std::sort(m_kVct_pkInstPicked.begin(), m_kVct_pkInstPicked.end(), kLess);
}

void CPythonCharacterManager::__NEW_Pick()
{
	__UpdateSortPickedActorList();

	CInstanceBase* pkInstMain=GetMainInstancePtr();

#ifdef __MOVIE_MODE
	if (pkInstMain)
		if (pkInstMain->IsMovieMode())
		{
			if (m_pkInstPick)
				m_pkInstPick->OnUnselected();
			return;
		}
#endif

	{
		std::vector<CInstanceBase*>::iterator f;
		for (f=m_kVct_pkInstPicked.begin(); f!=m_kVct_pkInstPicked.end(); ++f)
		{
			CInstanceBase* pkInstEach=*f;
			if (pkInstEach!=pkInstMain && pkInstEach->IntersectBoundingBox())
			{
				if (m_pkInstPick)
					if (m_pkInstPick!=pkInstEach)
						m_pkInstPick->OnUnselected();

				if (pkInstEach->CanPickInstance())
				{
					m_pkInstPick = pkInstEach;
					m_pkInstPick->OnSelected();
					return;
				}
			}
		}
	}

	{
		std::vector<CInstanceBase*>::iterator f;
		for (f=m_kVct_pkInstPicked.begin(); f!=m_kVct_pkInstPicked.end(); ++f)
		{
			CInstanceBase* pkInstEach=*f;
			if (pkInstEach!=pkInstMain)
			{
				if (m_pkInstPick)
					if (m_pkInstPick!=pkInstEach)
						m_pkInstPick->OnUnselected();

				if (pkInstEach->CanPickInstance())
				{
					m_pkInstPick = pkInstEach;
					m_pkInstPick->OnSelected();
					return;
				}
			}
		}
	}

	if (pkInstMain)
	if (pkInstMain->CanPickInstance())
	if (m_kVct_pkInstPicked.end() != std::find(m_kVct_pkInstPicked.begin(), m_kVct_pkInstPicked.end(), pkInstMain))
	{
		if (m_pkInstPick)
			if (m_pkInstPick!=pkInstMain)
				m_pkInstPick->OnUnselected();

		m_pkInstPick = pkInstMain;
		m_pkInstPick->OnSelected();
		return;
	}

	if (m_pkInstPick)
	{
		m_pkInstPick->OnUnselected();
		m_pkInstPick= nullptr;
	}
}

void CPythonCharacterManager::__OLD_Pick()
{
	for (auto itor = m_kAliveInstMap.begin(); itor != m_kAliveInstMap.end(); ++itor)
	{
		CInstanceBase * pkInstEach = itor->second;

		if (pkInstEach == m_pkInstMain)
			continue;

		if (pkInstEach->IntersectDefendingSphere())
		{
			if (m_pkInstPick)
				if (m_pkInstPick!=pkInstEach)
					m_pkInstPick->OnUnselected();

			m_pkInstPick = pkInstEach;
			m_pkInstPick->OnSelected();

			return;
		}
	}

	if (m_pkInstPick)
	{
		m_pkInstPick->OnUnselected();
		m_pkInstPick= nullptr;
	}
}

int CPythonCharacterManager::PickAll()
{
	for (auto itor = m_kAliveInstMap.begin(); itor != m_kAliveInstMap.end(); ++itor)
	{
		CInstanceBase * pInstance = itor->second;

		if (pInstance->IntersectDefendingSphere())
			return pInstance->GetVirtualID();
	}

	return -1;
}

CInstanceBase * CPythonCharacterManager::GetCloseInstance(CInstanceBase * pInstance)
{
	float fMinDistance = 10000.0f;
	CInstanceBase * pCloseInstance = nullptr;

	auto itor = m_kAliveInstMap.begin();
	for (; itor != m_kAliveInstMap.end(); ++itor)
	{
		CInstanceBase * pTargetInstance = itor->second;

		if (pTargetInstance == pInstance)
			continue;

		const DWORD dwVirtualNumber = pTargetInstance->GetVirtualNumber();
		if (CPythonNonPlayer::ON_CLICK_EVENT_BATTLE != CPythonNonPlayer::Instance().GetEventType(dwVirtualNumber))
			continue;

		const float fDistance = pInstance->GetDistance(pTargetInstance);
		if (fDistance < fMinDistance)
		{
			fMinDistance = fDistance;
			pCloseInstance = pTargetInstance;
		}
	}

	return pCloseInstance;
}

void CPythonCharacterManager::RefreshAllPCTextTail()
{
	CPythonCharacterManager::CharacterIterator itor = CharacterInstanceBegin();
	const CPythonCharacterManager::CharacterIterator itorEnd = CharacterInstanceEnd();
	for (; itor != itorEnd; ++itor)
	{
		CInstanceBase * pInstance = *itor;
		if (!pInstance->IsPC())
			continue;

		pInstance->RefreshTextTail();
	}
}

void CPythonCharacterManager::RefreshAllGuildMark()
{
	CPythonCharacterManager::CharacterIterator itor = CharacterInstanceBegin();
	const CPythonCharacterManager::CharacterIterator itorEnd = CharacterInstanceEnd();
	for (; itor != itorEnd; ++itor)
	{
		CInstanceBase * pInstance = *itor;
		if (!pInstance->IsPC())
			continue;

		pInstance->ChangeGuild(pInstance->GetGuildID());
		pInstance->RefreshTextTail();
	}
}

void CPythonCharacterManager::DeleteAllInstances()
{
	DestroyAliveInstanceMap();
	DestroyDeadInstanceList();
}

void CPythonCharacterManager::DestroyAliveInstanceMap()
{
	for (auto i = m_kAliveInstMap.begin(); i != m_kAliveInstMap.end(); ++i)
		CInstanceBase::Delete(i->second);

	m_kAliveInstMap.clear();
}

void CPythonCharacterManager::DestroyDeadInstanceList()
{
	std::for_each(m_kDeadInstList.begin(), m_kDeadInstList.end(), CInstanceBase::Delete);
	m_kDeadInstList.clear();
}

void CPythonCharacterManager::Destroy()
{
	DeleteAllInstances();

	CInstanceBase::DestroySystem();

	__Initialize();
}

void CPythonCharacterManager::__Initialize()
{
	memset(m_adwPointEffect, 0, sizeof(m_adwPointEffect));
	m_pkInstMain = nullptr;
	m_pkInstBind = nullptr;
	m_pkInstPick = nullptr;
	m_v2PickedInstProjPos = Vector2(0.0f, 0.0f);
}

CPythonCharacterManager::CPythonCharacterManager()
{
	__Initialize();
}

CPythonCharacterManager::~CPythonCharacterManager()
{
	Destroy();
}


namespace {
	void __DeferShadowRigidParts(CInstanceBase* pkInst)
	{
		if (!pkInst) return;
		CActorInstance& rkActor = pkInst->GetGraphicThingInstanceRef();
		for (DWORD idx = 0; idx < rkActor.GetLODControllerCount(); ++idx)
		{
			CGrannyLODController* pLOD = rkActor.GetLODControllerPointer(idx);
			if (!pLOD || !pLOD->isModelInstance()) continue;
			CGrannyModelInstance* pModelInst = pLOD->GetCurrentModelInstance();
			if (!pModelInst || pModelInst->IsEmpty()) continue;
			if (pModelInst->HasRigidMeshes())
			{
				CGrannyModel* pModel = pModelInst->GetModel();
				if (pModel)
					VTFMANAGER.DeferRigidModelInstanceForShadow(pModel, pModelInst);
			}
		}
	}
}

void CPythonCharacterManager::ResetCullingStats()
{
#ifdef ENABLE_FRUSTUM_CULLING
	ms_iCulledCount = 0;
	ms_iRenderedCount = 0;
	ms_iTotalCount = 0;
#endif
#ifdef ENABLE_CHAR_RENDER_LIMIT
	ms_iSkippedByLimit = 0;
#endif
#ifdef ENABLE_RENDER_MODE_GROUPING
	ms_iRenderModeNormal = 0;
	ms_iRenderModeBlend = 0;
	ms_iRenderModeAdd = 0;
	ms_iRenderModeModulate = 0;
#endif
}

void CPythonCharacterManager::EndShadowCastersForFrame()
{
	m_kVct_pkShadowCasters.clear();
	VTFMANAGER.ClearDeferredShadowBatches();
}

void CPythonCharacterManager::CollectShadowCastersForFrame()
{
	m_kVct_pkShadowCasters.clear();
	VTFMANAGER.ClearDeferredShadowBatches();
	for (const auto& entry : m_kAliveInstMap)
	{
#ifdef ENABLE_SHADOW_RENDER_LIMIT
		if (SHADOW_RENDER_LIMIT_DEFAULT > 0 && m_kVct_pkShadowCasters.size() >= SHADOW_RENDER_LIMIT_DEFAULT) break;
#endif
		m_kVct_pkShadowCasters.push_back(entry.second);
		if (VTFMANAGER.IsInitialized()) __DeferShadowRigidParts(entry.second);
	}
}
