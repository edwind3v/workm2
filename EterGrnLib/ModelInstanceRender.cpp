#include "StdAfx.h"
#include "../eterlib/StateManager.h"
#include "ModelInstance.h"
#include "Model.h"
#include "../EterBase/MapPerformance.h"
#include "../eterLib/DX11GrannyProbe.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#ifdef _TEST

#include "../eterlib/GrpScreen.h"

void Granny_RenderBoxBones(const granny_skeleton* pkGrnSkeleton, const granny_world_pose* pkGrnWorldPose, const D3DXMATRIX& matBase)
{
	D3DXMATRIX matWorld;
	CScreen screen;
	for (int iBone = 0; iBone != pkGrnSkeleton->BoneCount; ++iBone)
	{
		const granny_bone& rkGrnBone = pkGrnSkeleton->Bones[iBone];
		const D3DXMATRIX* c_matBone=(const D3DXMATRIX*)GrannyGetWorldPose4x4(pkGrnWorldPose, iBone);

		D3DXMatrixMultiply(&matWorld, c_matBone, &matBase);

		STATEMANAGER.SetTransform(D3DTS_WORLD, &matWorld);
		screen.RenderBox3d(-5.0f, -5.0f, -5.0f, 5.0f, 5.0f, 5.0f);
	}
}

#endif

void CGrannyModelInstance::DeformNoSkin(const D3DXMATRIX * c_pWorldMatrix)
{
	if (IsEmpty())
		return;

	// DELETED
	//m_pgrnWorldPose = m_pgrnWorldPoseReal;
	///////////////////////////////

	UpdateWorldPose();
	UpdateWorldMatrices(c_pWorldMatrix);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
//// Render
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
void CGrannyModelInstance::TryCaptureDX11ModelPreview(const TPNTVertex* deformedVertices) const
{
	const bool chooseDeformed = deformedVertices != nullptr;
	if (!m_pModel || !graphics::WantsDX11GrannyCapture(chooseDeformed))
		return;
	static_assert(sizeof(TPNTVertex) == sizeof(graphics::DX11GrannyVertex),
		"Granny TPNTVertex must be POSITION/NORMAL/TEXCOORD0 float3/float3/float2");
	const bool compositeMode = graphics::IsDX11PrimaryCharacterCaptureActive();
	const int minVertices = compositeMode ? 3 : 12;
	const int minIndices = compositeMode ? 3 : 36;
	const int maxVertices = compositeMode ? 60000 : 30000;
	const int maxIndices = compositeMode ? 180000 : 90000;
	static unsigned reportedSkips = 0;
	static unsigned reportedScans = 0;
	unsigned skippedSize = 0;
	unsigned skippedUncached = 0;
	unsigned matchingMeshes = 0;
	unsigned eligibleMeshes = 0;
	unsigned cachedMeshes = 0;
	unsigned submittedMeshes = 0;
	bool capturedAny = false;
	for (int meshIndex = 0; meshIndex < m_pModel->GetMeshCount(); ++meshIndex)
	{
		const CGrannyMesh* mesh = m_pModel->GetMeshPointer(meshIndex);
		if (!mesh)
			continue;
		const granny_mesh* grannyMesh = mesh->GetGrannyMeshPointer();
		if (!grannyMesh || GrannyMeshIsRigid(grannyMesh) == chooseDeformed)
			continue;
		++matchingMeshes;
		const int vertexCount = GrannyGetMeshVertexCount(grannyMesh);
		const int indexCount = GrannyGetMeshIndexCount(grannyMesh);
		if (vertexCount < minVertices || vertexCount > maxVertices ||
			indexCount < minIndices || indexCount > maxIndices || indexCount % 3)
		{
			++skippedSize;
			if (compositeMode && reportedSkips++ < 24)
				TraceError("DX11_CHARACTER_SKIP name=\"%s\" reason=geometry_limits vertices=%d indices=%d",
					grannyMesh->Name ? grannyMesh->Name : "", vertexCount, indexCount);
			continue;
		}
		++eligibleMeshes;

		// Use the CPU index copy made when the model's working DX9Ex index
		// buffer was created. Granny's original topology can be unavailable
		// by the time the live actor is deformed/rendered.
		const std::vector<WORD>& meshIndices = mesh->GetDX11PreviewIndices();
		if (meshIndices.size() != static_cast<size_t>(indexCount))
		{
			++skippedUncached;
			if (compositeMode && reportedSkips++ < 24)
				TraceError("DX11_CHARACTER_SKIP name=\"%s\" reason=uncached_indices vertices=%d indices=%d cached=%u",
					grannyMesh->Name ? grannyMesh->Name : "", vertexCount, indexCount,
					static_cast<UINT>(meshIndices.size()));
			continue;
		}
		++cachedMeshes;
		std::vector<TPNTVertex> rigidVertices;
		const TPNTVertex* source = nullptr;
		bool usedRigidCache = false;
		if (chooseDeformed)
			source = deformedVertices + mesh->GetVertexBasePosition();
		else
		{
			const std::vector<TPNTVertex>& cached = mesh->GetDX11PreviewRigidVertices();
			if (cached.size() == static_cast<size_t>(vertexCount))
			{
				source = cached.data();
				usedRigidCache = true;
			}
			else
			{
				rigidVertices.resize(vertexCount);
				GrannyCopyMeshVertices(grannyMesh, GrannyPNT332VertexType,
					rigidVertices.data());
				source = rigidVertices.data();
			}
		}
		std::vector<graphics::DX11GrannyVertex> previewVertices(vertexCount);
		// The DX9Ex renderer applies m_meshMatrices[meshIndex] separately to
		// *every* skinned and rigid mesh at draw time. A merged DX11 vertex
		// buffer has no per-mesh world transforms, so bake the same matrices
		// into the composite's positions and normals before merging. This is
		// particularly important for equipment attached to animated bones.
		const bool composite = graphics::IsDX11PrimaryCharacterCaptureActive();
		if (composite && !m_meshMatrices)
			continue;
		const D3DXMATRIX* meshWorld = composite ? &m_meshMatrices[meshIndex] : nullptr;
		unsigned repairedNormals = 0;
		unsigned repairedUVs = 0;
		for (int vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
		{
			const TPNTVertex& input = source[vertexIndex];
			graphics::DX11GrannyVertex& output = previewVertices[vertexIndex];
			D3DXVECTOR3 position(input.position.x, input.position.y, input.position.z);
			D3DXVECTOR3 normal(input.normal.x, input.normal.y, input.normal.z);
			// Some Granny rigid meshes contain undefined normal components.
			// Rejecting an entire costume piece for one such normal leaves holes
			// in the preview; use a neutral fallback normal instead.
			if (!std::isfinite(normal.x) || !std::isfinite(normal.y) ||
				!std::isfinite(normal.z))
			{
				normal = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
				++repairedNormals;
			}
			if (meshWorld)
			{
				D3DXVECTOR3 transformed;
				D3DXVec3TransformCoord(&transformed, &position, meshWorld);
				position = transformed;
				D3DXVec3TransformNormal(&transformed, &normal, meshWorld);
				normal = transformed;
			}
			const float normalLengthSq = D3DXVec3LengthSq(&normal);
			if (!std::isfinite(normalLengthSq) || normalLengthSq < 1e-10f)
			{
				normal = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
				++repairedNormals;
			}
			else
				D3DXVec3Normalize(&normal, &normal);
			output.position[0] = position.x;
			output.position[1] = position.y;
			output.position[2] = position.z;
			output.normal[0] = normal.x;
			output.normal[1] = normal.y;
			output.normal[2] = normal.z;
			output.uv[0] = std::isfinite(input.texCoord.x) ? input.texCoord.x : 0.0f;
			output.uv[1] = std::isfinite(input.texCoord.y) ? input.texCoord.y : 0.0f;
			if (!std::isfinite(input.texCoord.x) || !std::isfinite(input.texCoord.y))
				++repairedUVs;
		}
		// A late Granny copy with most normals corrupt is not a safe
		// attachment snapshot. Skipping it protects the whole preview until
		// the load-time vertex cache can supply real data for that asset.
		if (composite && !chooseDeformed && repairedNormals > static_cast<unsigned>(vertexCount / 4))
		{
			TraceError("DX11_CHARACTER_MESH_REJECT name=\"%s\" reason=corrupt_rigid_normals source=%s repaired=%u vertices=%d",
				grannyMesh->Name ? grannyMesh->Name : "",
				usedRigidCache ? "load_cache" : "granny_copy", repairedNormals, vertexCount);
			continue;
		}
		// Retain the *instance's* material overrides and the engine's Granny
		// triangle-group ranges. The index buffer below is mesh-local, whereas
		// TTriGroupNode::idxPos is relative to the complete model index buffer.
		std::vector<graphics::DX11GrannyMaterialGroup> materials;
		const int meshIndexBase = mesh->GetIndexBasePosition();
		for (int materialType = 0; materialType < CGrannyMaterial::TYPE_MAX_NUM; ++materialType)
		{
			const CGrannyMesh::TTriGroupNode* group = mesh->GetTriGroupNodeList(
				static_cast<CGrannyMaterial::EType>(materialType));
			for (; group; group = group->pNextTriGroupNode)
			{
				const int offset = group->idxPos - meshIndexBase;
				if (group->triCount <= 0 || group->triCount > indexCount / 3)
					continue;
				const int count = group->triCount * 3;
				if (offset < 0 || offset > indexCount || count > indexCount - offset ||
					group->mtrlIndex >= m_kMtrlPal.GetMaterialCount())
					continue;
				graphics::DX11GrannyMaterialGroup material;
				material.firstIndex = static_cast<UINT>(offset);
				material.indexCount = static_cast<UINT>(count);
				const CGrannyMaterial& original = m_kMtrlPal.GetMaterialRef(group->mtrlIndex);
				const CGraphicImage* diffuse = original.GetImagePointer(0);
				const CGraphicImage* opacity = original.GetImagePointer(1);
				if (diffuse) material.diffusePath = diffuse->GetFileName();
				if (opacity) material.opacityPath = opacity->GetFileName();
				material.alphaBlend = original.GetType() == CGrannyMaterial::TYPE_BLEND_PNT;
				material.twoSided = original.IsTwoSided();
				material.specularEnabled = original.__IsSpecularEnabled();
				material.specularPower = original.__GetSpecularPower();
				materials.push_back(std::move(material));
			}
		}
		std::sort(materials.begin(), materials.end(),
			[](const graphics::DX11GrannyMaterialGroup& a,
				const graphics::DX11GrannyMaterialGroup& b) {
				return a.firstIndex < b.firstIndex;
			});
		// If a GR2 has polygons outside its material groups, the preview
		// cannot shade those triangles. Record coverage for diagnosis without
		// changing the material ranges used by the working DX9Ex path.
		unsigned groupedIndices = 0;
		for (const auto& group : materials)
			groupedIndices += group.indexCount;
		++submittedMeshes;
		if (graphics::CaptureDX11GrannyMesh(
			previewVertices.data(),
			static_cast<UINT>(vertexCount), meshIndices.data(),
			static_cast<UINT>(indexCount), chooseDeformed,
			materials.data(), static_cast<UINT>(materials.size()), grannyMesh->Name))
		{
			capturedAny = true;
			if (composite)
				TraceError("DX11_CHARACTER_MESH name=\"%s\" type=%s source=%s vertices=%d indices=%d groups=%u grouped_indices=%u repaired_normals=%u repaired_uvs=%u space=world",
					grannyMesh->Name ? grannyMesh->Name : "", chooseDeformed ? "skinned" : "rigid",
					chooseDeformed ? "deformed" : usedRigidCache ? "load_cache" : "granny_copy",
					vertexCount, indexCount, static_cast<UINT>(materials.size()),
					groupedIndices, repairedNormals, repairedUVs);
			// A single-mesh probe stops here. The explicit main-character
			// scope collects every eligible mesh across active costume parts.
			if (!graphics::IsDX11PrimaryCharacterCaptureActive())
				return;
		}
	}
	// Report once per model instance in the main actor's one-shot capture.
	// This distinguishes a missing GR2 mesh from a texture/material problem.
	if (compositeMode && matchingMeshes && reportedScans++ < 24)
		TraceError("DX11_CHARACTER_SCAN mode=%s matching=%u eligible=%u cached=%u submitted=%u skipped_limits=%u skipped_uncached=%u",
			chooseDeformed ? "skinned" : "rigid", matchingMeshes, eligibleMeshes,
			cachedMeshes, submittedMeshes, skippedSize, skippedUncached);
	if (capturedAny)
		return;
	if (graphics::IsDX11PrimaryCharacterCaptureActive() && matchingMeshes == 0)
		return;
	// Log only a few bounded diagnostics. The snapshot remains opt-in and
	// must never perform potentially expensive mesh copies every frame.
	static unsigned misses = 0;
	++misses;
	if (misses == 1 || misses == 8 || misses == 16 || misses == 32)
		TraceError("DX11_MODEL_CAPTURE_MISS mode=%s attempt=%u matching=%u eligible=%u cached=%u submitted=%u",
			chooseDeformed ? "skinned" : "rigid", misses,
			matchingMeshes, eligibleMeshes, cachedMeshes, submittedMeshes);
}

// With One Texture
void CGrannyModelInstance::RenderWithOneTexture()
{
	if (IsEmpty())
		return;
	TryCaptureDX11ModelPreview(nullptr);

#ifdef _TEST
	Granny_RenderBoxBones(GrannyGetSourceSkeleton(m_pgrnModelInstance), m_pgrnWorldPose, TEST_matWorld);
	if (GetAsyncKeyState('P'))
		Tracef("render %x", m_pgrnModelInstance);
	return;
#endif

	STATEMANAGER.SetFVF(ms_pntVS);

	// WORK
	const LPDIRECT3DVERTEXBUFFER9 lpd3dDeformPNTVtxBuf = __GetDeformableD3DVertexBufferPtr();
	// END_OF_WORK

	const LPDIRECT3DVERTEXBUFFER9 lpd3dRigidPNTVtxBuf = m_pModel->GetPNTD3DVertexBuffer();

	if (lpd3dDeformPNTVtxBuf)
	{
		STATEMANAGER.SetStreamSource(0, lpd3dDeformPNTVtxBuf, sizeof(TPNTVertex));
		RenderMeshNodeListWithOneTexture(CGrannyMesh::TYPE_DEFORM, CGrannyMaterial::TYPE_DIFFUSE_PNT);
	}
	if (lpd3dRigidPNTVtxBuf)
	{
		STATEMANAGER.SetStreamSource(0, lpd3dRigidPNTVtxBuf, sizeof(TPNTVertex));
		RenderMeshNodeListWithOneTexture(CGrannyMesh::TYPE_RIGID, CGrannyMaterial::TYPE_DIFFUSE_PNT);
	}
}

void CGrannyModelInstance::BlendRenderWithOneTexture()
{
	if (IsEmpty())
		return;

	// WORK
	const LPDIRECT3DVERTEXBUFFER9 lpd3dDeformPNTVtxBuf = __GetDeformableD3DVertexBufferPtr();
	// END_OF_WORK
	const LPDIRECT3DVERTEXBUFFER9 lpd3dRigidPNTVtxBuf = m_pModel->GetPNTD3DVertexBuffer();

 	STATEMANAGER.SetFVF(ms_pntVS);

	if (lpd3dDeformPNTVtxBuf)
	{
		STATEMANAGER.SetStreamSource(0, lpd3dDeformPNTVtxBuf, sizeof(TPNTVertex));
		RenderMeshNodeListWithOneTexture(CGrannyMesh::TYPE_DEFORM, CGrannyMaterial::TYPE_BLEND_PNT);
	}

	if (lpd3dRigidPNTVtxBuf)
	{
		STATEMANAGER.SetStreamSource(0, lpd3dRigidPNTVtxBuf, sizeof(TPNTVertex));
		RenderMeshNodeListWithOneTexture(CGrannyMesh::TYPE_RIGID, CGrannyMaterial::TYPE_BLEND_PNT);
	}
}

// With Two Texture
void CGrannyModelInstance::RenderWithTwoTexture()
{
	if (IsEmpty())
		return;
	TryCaptureDX11ModelPreview(nullptr);

	STATEMANAGER.SetFVF(ms_pntVS);

	// WORK
	const LPDIRECT3DVERTEXBUFFER9 lpd3dDeformPNTVtxBuf = __GetDeformableD3DVertexBufferPtr();
	// END_OF_WORK
	const LPDIRECT3DVERTEXBUFFER9 lpd3dRigidPNTVtxBuf = m_pModel->GetPNTD3DVertexBuffer();

	if (lpd3dDeformPNTVtxBuf)
	{
		STATEMANAGER.SetStreamSource(0, lpd3dDeformPNTVtxBuf, sizeof(TPNTVertex));
		RenderMeshNodeListWithTwoTexture(CGrannyMesh::TYPE_DEFORM, CGrannyMaterial::TYPE_DIFFUSE_PNT);
	}
	if (lpd3dRigidPNTVtxBuf)
	{
		STATEMANAGER.SetStreamSource(0, lpd3dRigidPNTVtxBuf, sizeof(TPNTVertex));
		RenderMeshNodeListWithTwoTexture(CGrannyMesh::TYPE_RIGID, CGrannyMaterial::TYPE_DIFFUSE_PNT);
	}
}

void CGrannyModelInstance::BlendRenderWithTwoTexture()
{
	if (IsEmpty())
		return;

	// WORK
	const LPDIRECT3DVERTEXBUFFER9 lpd3dDeformPNTVtxBuf = __GetDeformableD3DVertexBufferPtr();
	// END_OF_WORK
	const LPDIRECT3DVERTEXBUFFER9 lpd3dRigidPNTVtxBuf = m_pModel->GetPNTD3DVertexBuffer();

 	STATEMANAGER.SetFVF(ms_pntVS);

	if (lpd3dDeformPNTVtxBuf)
	{
		STATEMANAGER.SetStreamSource(0, lpd3dDeformPNTVtxBuf, sizeof(TPNTVertex));
		RenderMeshNodeListWithTwoTexture(CGrannyMesh::TYPE_DEFORM, CGrannyMaterial::TYPE_BLEND_PNT);
	}

	if (lpd3dRigidPNTVtxBuf)
	{
		STATEMANAGER.SetStreamSource(0, lpd3dRigidPNTVtxBuf, sizeof(TPNTVertex));
		RenderMeshNodeListWithTwoTexture(CGrannyMesh::TYPE_RIGID, CGrannyMaterial::TYPE_BLEND_PNT);
	}
}

void CGrannyModelInstance::RenderWithoutTexture()
{
	if (IsEmpty())
		return;

	STATEMANAGER.SetFVF(ms_pntVS);
	STATEMANAGER.SetTexture(0, nullptr);
	STATEMANAGER.SetTexture(1, nullptr);

	// WORK
	const LPDIRECT3DVERTEXBUFFER9 lpd3dDeformPNTVtxBuf = __GetDeformableD3DVertexBufferPtr();
	// END_OF_WORK
	const LPDIRECT3DVERTEXBUFFER9 lpd3dRigidPNTVtxBuf = m_pModel->GetPNTD3DVertexBuffer();

	if (lpd3dDeformPNTVtxBuf)
	{
		STATEMANAGER.SetStreamSource(0, lpd3dDeformPNTVtxBuf, sizeof(TPNTVertex));
		RenderMeshNodeListWithoutTexture(CGrannyMesh::TYPE_DEFORM, CGrannyMaterial::TYPE_DIFFUSE_PNT);
		RenderMeshNodeListWithoutTexture(CGrannyMesh::TYPE_DEFORM, CGrannyMaterial::TYPE_BLEND_PNT);
	}

	if (lpd3dRigidPNTVtxBuf)
	{
		STATEMANAGER.SetStreamSource(0, lpd3dRigidPNTVtxBuf, sizeof(TPNTVertex));
		RenderMeshNodeListWithoutTexture(CGrannyMesh::TYPE_RIGID, CGrannyMaterial::TYPE_DIFFUSE_PNT);
		RenderMeshNodeListWithoutTexture(CGrannyMesh::TYPE_RIGID, CGrannyMaterial::TYPE_BLEND_PNT);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
//// Render Mesh List
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// With One Texture
void CGrannyModelInstance::RenderMeshNodeListWithOneTexture(CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType) const
{
	MapPerformance::Scope drawProfile("actor.draw_submit", m_pModel->GetGrannyModelPointer()->Name);
	assert(m_pModel != nullptr);

	const LPDIRECT3DINDEXBUFFER9 lpd3dIdxBuf = m_pModel->GetD3DIndexBuffer();
	assert(lpd3dIdxBuf != nullptr);

	const CGrannyModel::TMeshNode * pMeshNode = m_pModel->GetMeshNodeList(eMeshType, eMtrlType);

	CGrannyMaterial* activeMaterial = nullptr;
	while (pMeshNode)
	{
		const CGrannyMesh * pMesh = pMeshNode->pMesh;
		const int vtxMeshBasePos = pMesh->GetVertexBasePosition();

		STATEMANAGER.SetIndices(lpd3dIdxBuf, vtxMeshBasePos);
		STATEMANAGER.SetTransform(D3DTS_WORLD, &m_meshMatrices[pMeshNode->iMesh]);

		/////
		const CGrannyMesh::TTriGroupNode* pTriGroupNode = pMesh->GetTriGroupNodeList(eMtrlType);
		const int vtxCount = pMesh->GetVertexCount();
		while (pTriGroupNode)
		{
			ms_faceCount += pTriGroupNode->triCount;

			CGrannyMaterial& rkMtrl=m_kMtrlPal.GetMaterialRef(pTriGroupNode->mtrlIndex);

            // Keep identical adjacent materials active; restore at each change
            // and on exit. Primitive and transparency ordering is unchanged.
            if (activeMaterial != &rkMtrl)
            {
                if (activeMaterial) activeMaterial->RestoreRenderState();
                if (!m_material_data.pImage)
                    rkMtrl.SetSpecularInfo(m_material_data.isSpecularEnable, m_material_data.fSpecularPower, m_material_data.bSphereMapIndex);
                rkMtrl.ApplyRenderState();
                activeMaterial = &rkMtrl;
                MapPerformance::Count(MapPerformance::Counter::MaterialBinds);
            }
			MapPerformance::Count(MapPerformance::Counter::ActorDrawCalls);
			STATEMANAGER.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, vtxCount, pTriGroupNode->idxPos, pTriGroupNode->triCount, vtxMeshBasePos);

			pTriGroupNode = pTriGroupNode->pNextTriGroupNode;
		}
		/////

		pMeshNode = pMeshNode->pNextMeshNode;
	}
	if (activeMaterial) activeMaterial->RestoreRenderState();
}

// With Two Texture
void CGrannyModelInstance::RenderMeshNodeListWithTwoTexture(CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType) const
{
	MapPerformance::Scope drawProfile("actor.draw_submit", m_pModel->GetGrannyModelPointer()->Name);
	assert(m_pModel != nullptr);

	const LPDIRECT3DINDEXBUFFER9 lpd3dIdxBuf = m_pModel->GetD3DIndexBuffer();
	assert(lpd3dIdxBuf != nullptr);

	const CGrannyModel::TMeshNode * pMeshNode = m_pModel->GetMeshNodeList(eMeshType, eMtrlType);

	const CGrannyMaterial* activeMaterial = nullptr;
	while (pMeshNode)
	{
		const CGrannyMesh * pMesh = pMeshNode->pMesh;
		const int vtxMeshBasePos = pMesh->GetVertexBasePosition();

		STATEMANAGER.SetIndices(lpd3dIdxBuf, vtxMeshBasePos);
		STATEMANAGER.SetTransform(D3DTS_WORLD, &m_meshMatrices[pMeshNode->iMesh]);

		/////
		const CGrannyMesh::TTriGroupNode* pTriGroupNode = pMesh->GetTriGroupNodeList(eMtrlType);
		const int vtxCount = pMesh->GetVertexCount();
		while (pTriGroupNode)
		{
			ms_faceCount += pTriGroupNode->triCount;

			const CGrannyMaterial& rkMtrl=m_kMtrlPal.GetMaterialRef(pTriGroupNode->mtrlIndex);
            if (activeMaterial != &rkMtrl)
            {
                STATEMANAGER.SetTexture(0, rkMtrl.GetD3DTexture(0));
                STATEMANAGER.SetTexture(1, rkMtrl.GetD3DTexture(1));
                activeMaterial = &rkMtrl;
                MapPerformance::Count(MapPerformance::Counter::MaterialBinds);
            }
			MapPerformance::Count(MapPerformance::Counter::ActorDrawCalls);
			STATEMANAGER.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, vtxCount, pTriGroupNode->idxPos, pTriGroupNode->triCount, vtxMeshBasePos);
			pTriGroupNode = pTriGroupNode->pNextTriGroupNode;
		}
		/////

		pMeshNode = pMeshNode->pNextMeshNode;
	}
}

// Without Texture
void CGrannyModelInstance::RenderMeshNodeListWithoutTexture(CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType) const
{
	MapPerformance::Scope drawProfile("actor.draw_submit", m_pModel->GetGrannyModelPointer()->Name);
	assert(m_pModel != nullptr);

	const LPDIRECT3DINDEXBUFFER9 lpd3dIdxBuf = m_pModel->GetD3DIndexBuffer();
	assert(lpd3dIdxBuf != nullptr);

	const CGrannyModel::TMeshNode * pMeshNode = m_pModel->GetMeshNodeList(eMeshType, eMtrlType);

	while (pMeshNode)
	{
		const CGrannyMesh * pMesh = pMeshNode->pMesh;
		const int vtxMeshBasePos = pMesh->GetVertexBasePosition();

		STATEMANAGER.SetIndices(lpd3dIdxBuf, vtxMeshBasePos);
		STATEMANAGER.SetTransform(D3DTS_WORLD, &m_meshMatrices[pMeshNode->iMesh]);

		/////
		const CGrannyMesh::TTriGroupNode* pTriGroupNode = pMesh->GetTriGroupNodeList(eMtrlType);
		const int vtxCount = pMesh->GetVertexCount();

		while (pTriGroupNode)
		{
			ms_faceCount += pTriGroupNode->triCount;
			MapPerformance::Count(MapPerformance::Counter::ActorDrawCalls);
			STATEMANAGER.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, vtxCount, pTriGroupNode->idxPos, pTriGroupNode->triCount, vtxMeshBasePos);
			pTriGroupNode = pTriGroupNode->pNextTriGroupNode;
		}
		/////

		pMeshNode = pMeshNode->pNextMeshNode;
	}
}

