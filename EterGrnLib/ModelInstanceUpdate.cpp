#include "StdAfx.h"
#include "../eterbase/Debug.h"
#include "ModelInstance.h"
#include "Model.h"
#include "../EterBase/MapPerformance.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include "../eterLib/DX11GrannyProbe.h"

namespace
{
bool CrowdReuseEnabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("M2_CROWD_REUSE");
        return value && strcmp(value, "1") == 0;
    }();
    return enabled;
}
bool ConstantMotionEnabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("M2_CONSTANT_MOTION");
        return !value || strcmp(value, "0") != 0;
    }();
    return enabled;
}
}

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
    // Clock changes do not change a mathematically constant transform curve.
    // Motion/parent/control mutations still call InvalidatePose in full.
	++m_poseInputRevision;
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
    m_gpuSkinning = CanUseGPUSkinning();
    if (m_pModel->CanDeformPNTVertices())
    {
        if (m_gpuSkinning)
            MapPerformance::Count(MapPerformance::Counter::GPUSkinVertices, m_pModel->GetDeformVertexCount());
        else
            EnsureCPUSkinnedVertices();
    }
    if (graphics::IsDX11PrimaryCharacterCaptureActive()) TryCaptureDX11ModelPreview(nullptr);
}

bool CGrannyModelInstance::CanUseGPUSkinning()
{
    // The diagnostic DX11 preview consumes CPU-deformed vertices; retain its
    // original source while explicitly enabled. Normal gameplay stays on DX9.
    if (!m_pModel->CanDeformPNTVertices() || !GPUSkinning::Available(ms_lpd3dDevice) ||
        strcmp(graphics::GetDX11GrannyCaptureMode(), "off") != 0) return false;
    const int poseBones = GrannyGetWorldPoseBoneCount(__GetWorldPosePtr());
    if (m_vct_pgrnMeshBinding.size() != static_cast<size_t>(m_pModel->GetMeshCount())) return false;
    for (int index = 0; index < m_pModel->GetMeshCount(); ++index)
    {
        auto* mesh = m_pModel->GetMeshPointer(index);
        if (!mesh->CanDeformPNTVertices()) continue;
        if (!mesh->CreateGPUSkinDeviceObjects(ms_lpd3dDevice) || !m_vct_pgrnMeshBinding[index]) return false;
        const auto* indices = __GetMeshBoneIndices(index);
        if (!indices || GrannyGetMeshBindingBoneCount(m_vct_pgrnMeshBinding[index]) != mesh->GetGPUSkinBoneCount())
        {
            GPUSkinning::ReportFallback("mesh_binding_palette_size");
            return false;
        }
        for (unsigned bone = 0; bone < mesh->GetGPUSkinBoneCount(); ++bone)
            if (indices[bone] < 0 || indices[bone] >= poseBones)
            {
                GPUSkinning::ReportFallback("invalid_skeleton_binding");
                return false;
            }
    }
    return true;
}

bool CGrannyModelInstance::EnsureCPUSkinnedVertices()
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
                return uploaded;
            }
            else
            {
                TraceError("GRANNY DEFORM DYNAMIC BUFFER LOCK ERROR");
                return false;
            }
        }
        return true;
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
    // A model with no bound controls always samples its rest pose. Check the
    // complete control list, including blends; m_pgrnCtrl alone is insufficient.
    const bool withoutControls = CrowdReuseEnabled() &&
        GrannyModelControlsBegin(m_pgrnModelInstance) == GrannyModelControlsEnd(m_pgrnModelInstance);
    bool constantControl = false;
    if (ConstantMotionEnabled() && m_constantMotion)
    {
        const auto begin = GrannyModelControlsBegin(m_pgrnModelInstance);
        const auto end = GrannyModelControlsEnd(m_pgrnModelInstance);
        if (begin != end)
        {
            // Repeated wait controls may overlap. They can share a constant
            // result only when they bind the very same clip, have no masks,
            // and cover the full pose weight. Different clips still sample.
            const auto* binding = GrannyGetAnimationBindingFromControlBinding(begin);
            bool compatible = binding != nullptr, currentActive = false;
            double totalWeight = 0;
            unsigned controls = 0;
            for (auto entry = begin; compatible && entry != end; entry = GrannyModelControlsNext(entry))
            {
                auto* control = GrannyGetControlFromBinding(entry);
                const bool active = GrannyControlIsActive(control) && !GrannyControlIsComplete(control);
                const float weight = active ? GrannyGetControlEffectiveWeight(control) : 0.0f;
                compatible = ++controls <= 16 && std::isfinite(weight) && weight >= 0 &&
                    GrannyGetControlWeight(control) == 1.0f &&
                    GrannyGetAnimationBindingFromControlBinding(entry) == binding &&
                    !GrannyGetControlTrackGroupModelMask(control, m_pgrnModelInstance) &&
                    !GrannyGetControlTrackGroupTrackMask(control, m_pgrnAni, 0);
                totalWeight += weight;
                if (control == m_pgrnCtrl)
                    currentActive = active;
            }
            constantControl = compatible && currentActive && totalWeight >= 1.0;
        }
    }
    if (m_poseRevision && sameParent && constantControl && m_sampledConstantControl &&
        m_poseStateRevision == m_sampledPoseStateRevision)
    {
        GrannyFreeCompletedModelControls(m_pgrnModelInstance);
        m_sampledInputRevision = m_poseInputRevision;
        MapPerformance::Count(MapPerformance::Counter::PoseReuse);
        MapPerformance::Count(MapPerformance::Counter::ConstantPoseReuse);
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            TraceError("[CONSTANT_MOTION] ACTIVE asset=\"%s\" clocks=advancing events=unchanged disable=M2_CONSTANT_MOTION=0",
                m_pModel->GetDiagnosticAsset());
        }
        return;
    }
    if (m_poseRevision && sameParent && (m_sampledInputRevision == m_poseInputRevision ||
        (CrowdReuseEnabled() && withoutControls && m_sampledWithoutControls)))
    {
        MapPerformance::Count(MapPerformance::Counter::PoseReuse);
        if (withoutControls && m_sampledWithoutControls)
            MapPerformance::Count(MapPerformance::Counter::RestPoseReuse);
        m_sampledInputRevision = m_poseInputRevision;
        return;
    }
    static CGrannyLocalPose localPose;
    const granny_skeleton* skeleton = GrannyGetSourceSkeleton(m_pgrnModelInstance);
    granny_local_pose* local = localPose.Get(skeleton->BoneCount);
    GrannySampleModelAnimationsAccelerated(m_pgrnModelInstance, skeleton->BoneCount, parentMatrix, local, __GetWorldPosePtr());
    GrannyFreeCompletedModelControls(m_pgrnModelInstance);
    // Record the list BEFORE sampling/freeing: a completed animation's final
    // sample must not be mistaken for the rest pose on the next frame.
    m_sampledWithoutControls = withoutControls;
    m_sampledConstantControl = constantControl;
    m_sampledPoseStateRevision = m_poseStateRevision;
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
	const bool checkRigid = m_pModel->HasRigidBatches();
    const auto poseRevision = GetPoseRevision();
    // A stable pose revision is sufficient proof. Unlike the version 8
    // snapshot cache, this neither scans nor copies all bone matrices.
    if (ConstantMotionEnabled() && m_revisionMatricesValid && poseRevision &&
        m_revisionMatricesPose == poseRevision &&
        memcmp(&m_revisionMatricesWorld, c_pWorldMatrix, sizeof(D3DXMATRIX)) == 0)
    {
        MapPerformance::Count(MapPerformance::Counter::RevisionMatricesReuse);
        return;
    }
    const int boneCount = CrowdReuseEnabled() && checkRigid ? GrannyGetWorldPoseBoneCount(__GetWorldPosePtr()) : 0;
    // Cache only rigid batch candidates. Animated models keep their original
    // path; exact comparisons also catch constant animation tracks without
    // guessing from animation names or suppressing any animation sampling.
    const bool cacheMatrices = CrowdReuseEnabled() && checkRigid && boneCount > 0;
    if (cacheMatrices && m_matrixSnapshotValid &&
        m_matrixPoseSnapshot.size() == static_cast<size_t>(boneCount) &&
        memcmp(&m_matrixWorldSnapshot, c_pWorldMatrix, sizeof(D3DXMATRIX)) == 0 &&
        ((poseRevision != 0 && m_matrixPoseRevision == poseRevision) ||
         memcmp(m_matrixPoseSnapshot.data(), boneMatrices, boneCount * sizeof(D3DXMATRIX)) == 0))
    {
        m_matrixPoseRevision = poseRevision;
        MapPerformance::Count(MapPerformance::Counter::MeshMatricesReuse);
        return;
    }
	int firstRigid = -1;
	bool uniformRigid = true;
    double inverseBound = 0;

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
			if (checkRigid)
			{
                if (firstRigid < 0)
                {
                    firstRigid = i;
                    D3DXMATRIX inverse;
                    if (D3DXMatrixInverse(&inverse, nullptr, &rWorldMatrix))
                        for (unsigned r = 0; r < 3; ++r)
                            for (unsigned c = 0; c < 3; ++c)
                                inverseBound = (std::max)(inverseBound, std::abs(static_cast<double>(inverse(r, c))));
                }
                else if (uniformRigid && memcmp(&m_meshMatrices[firstRigid], &rWorldMatrix, sizeof(rWorldMatrix)) != 0)
                {
                    const auto& reference = m_meshMatrices[firstRigid];
                    double linear = 0, translation = 0;
                    for (unsigned r = 0; r < 4; ++r)
                        for (unsigned c = 0; c < 4; ++c)
                        {
                            const double delta = std::abs(static_cast<double>(reference(r, c)) - rWorldMatrix(r, c));
                            if (!std::isfinite(delta) || (c == 3 && delta != 0)) uniformRigid = false;
                            else if (r == 3) translation = (std::max)(translation, delta);
                            else linear = (std::max)(linear, delta);
                        }
                    // Accept only roundoff residuals, not independently animated
                    // parts. Bound position error using the asset's actual extent
                    // and inverse-transpose normal error via matrix perturbation.
                    if (linear > 1e-8 || !std::isfinite(inverseBound) || inverseBound == 0 ||
                        linear * m_pModel->GetRigidPositionBound() + translation > 1e-4 ||
                        3 * linear * inverseBound >= 0.001 ||
                        18 * linear * inverseBound * inverseBound * m_pModel->GetRigidNormalBound() > 1e-6)
                        uniformRigid = false;
                }
			}
		}
	}
	m_uniformRigidMatrices = firstRigid >= 0 && uniformRigid;
    if (ConstantMotionEnabled())
    {
        m_revisionMatricesWorld = *c_pWorldMatrix;
        m_revisionMatricesPose = poseRevision;
        m_revisionMatricesValid = true;
    }
    if (cacheMatrices)
    {
        m_matrixPoseSnapshot.assign(boneMatrices, boneMatrices + boneCount);
        m_matrixWorldSnapshot = *c_pWorldMatrix;
        m_matrixPoseRevision = poseRevision;
        m_matrixSnapshotValid = true;
        MapPerformance::Count(MapPerformance::Counter::MeshMatricesBuilds);
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

