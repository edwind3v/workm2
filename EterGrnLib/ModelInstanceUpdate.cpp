#include "StdAfx.h"
#include "../eterbase/Debug.h"
#include "ModelInstance.h"
#include "Model.h"
#include "../EterBase/MapPerformance.h"

void CGrannyModelInstance::Update(DWORD dwAniFPS)
{
	if (!dwAniFPS)
		return;

	const DWORD c_dwCurUpdateFrame = (DWORD) (GetLocalTime() * static_cast<float>(ANIFPS_MAX));
	const DWORD ANIFPS_STEP = ANIFPS_MAX/dwAniFPS;
	if (!ANIFPS_STEP || c_dwCurUpdateFrame>ANIFPS_STEP && c_dwCurUpdateFrame/ANIFPS_STEP==m_dwOldUpdateFrame/ANIFPS_STEP)
		return;

	m_dwOldUpdateFrame=c_dwCurUpdateFrame;

	//DWORD t1=timeGetTime();
	GrannyFreeCompletedModelControls(m_pgrnModelInstance); //@fixme029
	GrannySetModelClock(m_pgrnModelInstance, GetLocalTime());
	InvalidatePose();
	//DWORD t2=timeGetTime();

#ifdef __PERFORMANCE_CHECKER__
	{
		static FILE* fp=fopen("perf_grn_setmodelclock.txt", "w");

		if (t2-t1>3)
		{
			fprintf(fp, "%f:%x:- GrannySetModelClock(time=%f) = %dms\n", timeGetTime()/1000.0f, this, GetLocalTime(), t2-t1);
			fflush(fp);
		}
	}
#endif
}

void CGrannyModelInstance::UpdateLocalTime(float fElapsedTime)
{
	m_fSecondsElapsed = fElapsedTime;
	m_fLocalTime += fElapsedTime;
}

void CGrannyModelInstance::UpdateTransform(Matrix * pMatrix, float fSecondsElapsed)
{
	if (!m_pgrnModelInstance)
	{
		TraceError("CGrannyModelIstance::UpdateTransform - m_pgrnModelInstance = NULL");
		return;
	}
#if GrannyProductMinorVersion==4
	GrannyUpdateModelMatrix(m_pgrnModelInstance, fSecondsElapsed, (const float *) pMatrix, (float *) pMatrix);
#elif GrannyProductMinorVersion==11 || GrannyProductMinorVersion==9 || GrannyProductMinorVersion==8 || GrannyProductMinorVersion==7
	GrannyUpdateModelMatrix(m_pgrnModelInstance, fSecondsElapsed, (const float *) pMatrix, (float *) pMatrix, false);
#else
#error "unknown granny version"
#endif
	//Tracef("%f %f %f",pMatrix->_41,pMatrix->_42,pMatrix->_43);
}

void CGrannyModelInstance::Deform(const Matrix * c_pWorldMatrix)
{
	if (IsEmpty())
		return;

	UpdateWorldPose();
	UpdateWorldMatrices(c_pWorldMatrix);

}

//////////////////////////////////////////////////////
class CGrannyLocalPose
{
	public:
		CGrannyLocalPose()
		{
			m_pgrnLocalPose = NULL;
			m_boneCount = 0;
		}

		virtual ~CGrannyLocalPose()
		{
			if (m_pgrnLocalPose)
				GrannyFreeLocalPose(m_pgrnLocalPose);
		}

		granny_local_pose * Get(int boneCount)
		{
			if (m_pgrnLocalPose)
			{
				if (m_boneCount >= boneCount)
					return m_pgrnLocalPose;

				GrannyFreeLocalPose(m_pgrnLocalPose);
			}

			m_boneCount = boneCount;
			m_pgrnLocalPose = GrannyNewLocalPose(m_boneCount);
			return m_pgrnLocalPose;
		}

	private:
		granny_local_pose *	m_pgrnLocalPose;
		int					m_boneCount;
};
//////////////////////////////////////////////////////

void CGrannyModelInstance::UpdateSkeleton(const Matrix * c_pWorldMatrix, float /*fLocalTime*/)
{
	// DELETED
	//m_pgrnWorldPose = m_pgrnWorldPoseReal;
	///////////////////////////////////////////
	UpdateWorldPose();
	UpdateWorldMatrices(c_pWorldMatrix);
}

void CGrannyModelInstance::UpdateWorldPose() const
{
    if (m_ppkSkeletonInst && *m_ppkSkeletonInst != this) return;
    MapPerformance::Scope profile("actor.pose_evaluate");
    const float* parentMatrix = mc_pParentInstance ? mc_pParentInstance->GetBoneMatrixPointer(m_iParentBoneIndex) : nullptr;
    const bool sameParent = m_sampledParent == mc_pParentInstance &&
        m_hasParentMatrix == (parentMatrix != nullptr) &&
        (!parentMatrix || memcmp(&m_sampledParentMatrix, parentMatrix, sizeof(m_sampledParentMatrix)) == 0);
    if (m_poseRevision && m_sampledInputRevision == m_poseInputRevision && sameParent)
    {
        MapPerformance::Count(MapPerformance::Counter::PoseReuse);
        return;
    }
    static CGrannyLocalPose localPose;
    const granny_skeleton* skeleton = GrannyGetSourceSkeleton(m_pgrnModelInstance);
    granny_local_pose* local = localPose.Get(skeleton->BoneCount);
    GrannySampleModelAnimationsAccelerated(m_pgrnModelInstance, skeleton->BoneCount, parentMatrix, local, __GetWorldPosePtr());
    GrannyFreeCompletedModelControls(m_pgrnModelInstance);
    m_sampledInputRevision = m_poseInputRevision;
    m_sampledParent = mc_pParentInstance;
    m_hasParentMatrix = parentMatrix != nullptr;
    if (parentMatrix) memcpy(&m_sampledParentMatrix, parentMatrix, sizeof(m_sampledParentMatrix));
    // Monotonic across pooled instances, including skeleton/LOD replacement.
    static std::uint64_t nextRevision = 0;
    m_poseRevision = ++nextRevision;
    MapPerformance::Count(MapPerformance::Counter::PoseSamples);
}

void CGrannyModelInstance::UpdateWorldMatrices(const Matrix* c_pWorldMatrix)
{
	// NO_MESH_BUG_FIX
	if (!m_meshMatrices)
		return;
	// END_OF_NO_MESH_BUG_FIX

	assert(m_pModel != NULL);

	int meshCount = m_pModel->GetMeshCount();

	granny_matrix_4x4 * pgrnMatCompositeBuffer = GrannyGetWorldPoseComposite4x4Array(__GetWorldPosePtr());
	Matrix * boneMatrices = (Matrix *) pgrnMatCompositeBuffer;

	for (int i = 0; i < meshCount; ++i)
	{
		Matrix & rWorldMatrix = m_meshMatrices[i];

		const CGrannyMesh * pMesh = m_pModel->GetMeshPointer(i);

		// WORK

#if GrannyProductMinorVersion==4
		int * boneIndices = __GetMeshBoneIndices(i);
#elif GrannyProductMinorVersion==11 || GrannyProductMinorVersion==9 || GrannyProductMinorVersion==8 || GrannyProductMinorVersion==7
		const granny_int32x * boneIndices = __GetMeshBoneIndices(i);
#else
#error "unknown granny version"
#endif
		// END_OF_WORK

		if (!GrannyMeshIsRigid(pMesh->GetGrannyMeshPointer()))
		{
			rWorldMatrix = *c_pWorldMatrix;
		}
		else
		{
#if GrannyProductMinorVersion==4
			int iBone = *boneIndices;
#elif GrannyProductMinorVersion==11 || GrannyProductMinorVersion==11 || GrannyProductMinorVersion==9 || GrannyProductMinorVersion==8 || GrannyProductMinorVersion==7
			const granny_int32x iBone = *boneIndices;
#else
#error "unknown granny version"
#endif
			MatrixMultiply(&rWorldMatrix, &boneMatrices[iBone], c_pWorldMatrix);
		}
	}

#ifdef _TEST
	TEST_matWorld = *c_pWorldMatrix;
#endif
}
//martysama0134's dcf42890919f0da1c0e6dbb7f15bc7ec

std::uint64_t CGrannyModelInstance::GetPoseRevision() const
{
    if (m_ppkSkeletonInst && *m_ppkSkeletonInst && *m_ppkSkeletonInst != this)
        return (*m_ppkSkeletonInst)->GetPoseRevision();
    return m_poseRevision;
}
