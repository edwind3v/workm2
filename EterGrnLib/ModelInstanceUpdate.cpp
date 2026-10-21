#include "StdAfx.h"
#include "../eterbase/Debug.h"
#include "ModelInstance.h"
#include "Model.h"
#include "../EterBase/MapPerformance.h"
#include <cstring>
#include "../eterLib/DX11GrannyProbe.h"

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

void CGrannyModelInstance::UpdateTransform(D3DXMATRIX * pMatrix, float fSecondsElapsed) const
{
	if (!m_pgrnModelInstance)
	{
		TraceError("CGrannyModelIstance::UpdateTransform - m_pgrnModelInstance = Null");
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

void CGrannyModelInstance::Deform(const D3DXMATRIX* world)
{
    if (IsEmpty()) return;
    MapPerformance::Scope profile("actor.deform");
    UpdateWorldPose();
    MapPerformance::Measure("actor.mesh_matrices", nullptr, [&] { UpdateWorldMatrices(world); });
    if (m_pModel->CanDeformPNTVertices())
    {
        CGraphicVertexBuffer& buffer = __GetDeformableVertexBufferRef();
        const auto poseRevision = GetPoseRevision();
        // LOD variants share a buffer: a pose cache hit alone is insufficient.
        // Any other writer or device recreation invalidates the uploaded data.
        if (m_skinnedPoseRevision == poseRevision && m_skinnedBuffer == &buffer &&
            m_skinnedBufferVersion == buffer.GetContentVersion() && poseRevision != 0)
        {
            MapPerformance::Count(MapPerformance::Counter::SkinReuse);
        }
        else
        {
            TPNTVertex* vertices = nullptr;
            const bool locked = MapPerformance::Measure("actor.vertex_lock", nullptr, [&] {
                return buffer.LockDiscardRange(m_pModel->GetDeformVertexCount(), reinterpret_cast<void**>(&vertices));
            });
            if (locked)
            {
                MapPerformance::Measure("actor.vertex_skin", nullptr, [&] { DeformPNTVertices(vertices); });
                TryCaptureDX11ModelPreview(vertices);
                const bool uploaded = MapPerformance::Measure("actor.vertex_unlock", nullptr, [&] { return buffer.Unlock(); });
                if (uploaded)
                {
                    m_skinnedPoseRevision = poseRevision;
                    m_skinnedBuffer = &buffer;
                    m_skinnedBufferVersion = buffer.GetContentVersion();
                    MapPerformance::Count(MapPerformance::Counter::SkinUploads);
                    MapPerformance::Count(MapPerformance::Counter::SkinnedVertices, m_pModel->GetDeformVertexCount());
                }
            }
            else TraceError("GRANNY DEFORM DYNAMIC BUFFER LOCK ERROR");
        }
    }
    if (graphics::IsDX11PrimaryCharacterCaptureActive()) TryCaptureDX11ModelPreview(nullptr);
}

//////////////////////////////////////////////////////
class CGrannyLocalPose
{
	public:
		CGrannyLocalPose()
		{
			m_pgrnLocalPose = nullptr;
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

void CGrannyModelInstance::UpdateSkeleton(const D3DXMATRIX * c_pWorldMatrix, float /*fLocalTime*/) const
{
	// DELETED
	//m_pgrnWorldPose = m_pgrnWorldPoseReal;
	///////////////////////////////////////////
	UpdateWorldPose();
	MapPerformance::Measure("actor.mesh_matrices", nullptr, [&] { UpdateWorldMatrices(c_pWorldMatrix); });
}

std::uint64_t CGrannyModelInstance::GetPoseRevision() const
{
    if (m_ppkSkeletonInst && *m_ppkSkeletonInst && *m_ppkSkeletonInst != this)
        return (*m_ppkSkeletonInst)->GetPoseRevision();
    return m_poseRevision;
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

void CGrannyModelInstance::UpdateWorldMatrices(const D3DXMATRIX* c_pWorldMatrix) const
{
	// NO_MESH_BUG_FIX
	if (!m_meshMatrices)
		return;
	// END_OF_NO_MESH_BUG_FIX

	assert(m_pModel != nullptr);
	assert(ms_lpd3dMatStack != nullptr);

	const int meshCount = m_pModel->GetMeshCount();

	granny_matrix_4x4 * pgrnMatCompositeBuffer = GrannyGetWorldPoseComposite4x4Array(__GetWorldPosePtr());
	const auto boneMatrices = (D3DXMATRIX *) pgrnMatCompositeBuffer;

	for (int i = 0; i < meshCount; ++i)
	{
		D3DXMATRIX & rWorldMatrix = m_meshMatrices[i];

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

		if (pMesh->CanDeformPNTVertices())
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
			D3DXMatrixMultiply(&rWorldMatrix, &boneMatrices[iBone], c_pWorldMatrix);
		}
	}

#ifdef _TEST
	TEST_matWorld = *c_pWorldMatrix;
#endif
}

void CGrannyModelInstance::DeformPNTVertices(void * pvDest) const
{
	assert(m_pModel != nullptr);
	assert(m_pModel->CanDeformPNTVertices());

	// WORK
	m_pModel->DeformPNTVertices(pvDest, (D3DXMATRIX *) GrannyGetWorldPoseComposite4x4Array(__GetWorldPosePtr()), m_vct_pgrnMeshBinding);
	// END_OF_WORK
}

