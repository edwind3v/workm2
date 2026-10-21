#include "StdAfx.h"
#include "ModelInstance.h"
#include "Model.h"
#include "../EterLib/ResourceManager.h"

CGrannyModel* CGrannyModelInstance::GetModel() const
{
	return m_pModel;
}

void CGrannyModelInstance::SetMaterialImagePointer(const char* c_szImageName, CGraphicImage* pImage)
{
	m_kMtrlPal.SetMaterialImagePointer(c_szImageName, pImage);
}

void CGrannyModelInstance::SetMaterialData(const char* c_szImageName, const SMaterialData& c_rkMaterialData)
{
	m_kMtrlPal.SetMaterialData(c_szImageName, c_rkMaterialData);
	m_material_data = c_rkMaterialData; // @fixme057
}

void CGrannyModelInstance::SetSpecularInfo(const char* c_szMtrlName, BOOL bEnable, float fPower) const
{
	m_kMtrlPal.SetSpecularInfo(c_szMtrlName, bEnable, fPower);
}

void CGrannyModelInstance::SetLocalTime(float fLocalTime)
{
	m_fLocalTime = fLocalTime;
	InvalidatePose();
}

int CGrannyModelInstance::ResetLocalTime()
{
	m_fLocalTime = 0.0f;
	InvalidatePose();
	return 0;
}

float CGrannyModelInstance::GetLocalTime() const
{
	return m_fLocalTime;
}

void CGrannyModelInstance::SetParentModelInstance(const CGrannyModelInstance* c_pParentModelInstance, const char * c_szBoneName)
{
	int iBoneIndex;
	if (!c_pParentModelInstance->GetBoneIndexByName(c_szBoneName, &iBoneIndex))
		return;

	SetParentModelInstance(c_pParentModelInstance, iBoneIndex);
}

void CGrannyModelInstance::SetParentModelInstance(const CGrannyModelInstance* c_pParentModelInstance, int iBone)
{
	mc_pParentInstance = c_pParentModelInstance;
	m_iParentBoneIndex = iBone;
	InvalidatePose();
}

bool CGrannyModelInstance::IsEmpty() const
{
	if (m_pModel)
	{
		// NO_MESH_BUG_FIX
		if (!m_meshMatrices)
			return true;
		// END_OF_NO_MESH_BUG_FIX
		return false;
	}

	return true;
}

bool CGrannyModelInstance::CreateDeviceObjects()
{
	__CreateDynamicVertexBuffer();

	return true;
}

void CGrannyModelInstance::DestroyDeviceObjects()
{
	__DestroyDynamicVertexBuffer();
}

void CGrannyModelInstance::__Initialize()
{
	if (m_pModel)
	{
		m_pModel->Release();
	}
	m_pModel = nullptr;
	mc_pParentInstance = nullptr;
	m_iParentBoneIndex = 0;

	m_pgrnModelInstance = nullptr;

	// WORK
	m_pgrnWorldPoseReal = nullptr;
	// END_OF_WORK

	// TEST
	m_ppkSkeletonInst = nullptr;
	// END_OF_TEST

	m_meshMatrices = nullptr;
	m_pgrnCtrl = nullptr;
	m_pgrnAni = nullptr;

	m_material_data = {}; // @fixme057
	m_dwOldUpdateFrame=0;
    m_poseInputRevision = 1; m_sampledInputRevision = m_poseRevision = 0;
    m_sampledParent = nullptr; m_hasParentMatrix = false;
    m_skinnedPoseRevision = m_skinnedBufferVersion = 0; m_skinnedBuffer = nullptr;
    m_ownsMeshBindings = true;
    m_gpuSkinning = false;
    m_uniformRigidMatrices = false;
    m_sampledWithoutControls = false;
    m_matrixPoseSnapshot.clear();
    m_matrixPoseRevision = 0;
    m_matrixSnapshotValid = false;
    m_gpuPaletteCaches.clear();
    m_constantMotion = m_sampledConstantControl = false;
    m_poseStateRevision = 1; m_sampledPoseStateRevision = 0;
    m_revisionMatricesValid = false; m_revisionMatricesPose = 0;
}

CGrannyModelInstance::CGrannyModelInstance()
{
	m_pModel = nullptr;
	__Initialize();
}

CGrannyModelInstance::~CGrannyModelInstance()
{
	Clear();
}

CDynamicPool<CGrannyModelInstance> CGrannyModelInstance::ms_kPool;

CGrannyModelInstance* CGrannyModelInstance::New()
{
	return ms_kPool.Alloc();
}

void CGrannyModelInstance::Delete(CGrannyModelInstance* pkInst)
{
	pkInst->Clear();
	ms_kPool.Free(pkInst);
}

void CGrannyModelInstance::DestroySystem()
{
	ms_kPool.Destroy();
	DestroyWorldPoseCache();
    GPUSkinning::Destroy();
}

