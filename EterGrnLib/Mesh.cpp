#include "StdAfx.h"
#include "Mesh.h"
#include "Model.h"
#include "Material.h"
#include "../eterLib/DX11GrannyProbe.h"
#include "OrderedTriangleBatch.h"
#include <cmath>

namespace
{
// Only enabled for an explicitly requested, temporary DX11 preview.
// This cap is shared by loaded Granny meshes to avoid doubling large models.
constexpr size_t kDX11PreviewIndexBudget = 32u * 1024u * 1024u;
size_t s_dx11PreviewIndexBytes = 0;
constexpr size_t kDX11PreviewRigidVertexBudget = 64u * 1024u * 1024u;
size_t s_dx11PreviewRigidVertexBytes = 0;
}

granny_data_type_definition GrannyPNT3322VertexType[5] =
{
	{GrannyReal32Member, GrannyVertexPositionName, nullptr, 3},
	{GrannyReal32Member, GrannyVertexNormalName, nullptr, 3},
	{GrannyReal32Member, GrannyVertexTextureCoordinatesName"0", nullptr, 2},
	{GrannyReal32Member, GrannyVertexTextureCoordinatesName"1", nullptr, 2},
	{GrannyEndMember}
};

void CGrannyMesh::LoadIndices(void * dstBaseIndices) const
{
	const granny_mesh * pgrnMesh = GetGrannyMeshPointer();

	TIndex * dstIndices = ((TIndex *)dstBaseIndices) + m_idxBasePos;
	// Preserve the same indices used for the working DX9Ex model while they
	// are available at asset-load time. Calling GrannyCopyMeshIndices again
	// during character animation can yield invalid debug-heap values such as
	// 0xCDCD / 0xDDDD after the source topology's lifetime has ended.
	const char* mode = graphics::GetDX11GrannyCaptureMode();
	const bool characterMode = strcmp(mode, "character") == 0;
	const int vertexCount = GrannyGetMeshVertexCount(pgrnMesh);
	const int indexCount = GrannyGetMeshIndexCount(pgrnMesh);
	// Full-character capture must retain small costume detail meshes too.
	// Keep the original larger minimum for the single-mesh diagnostic.
	const int minVertices = characterMode ? 3 : 12;
	const int minIndices = characterMode ? 3 : 36;
	const int maxVertices = characterMode ? 60000 : 30000;
	const int maxIndices = characterMode ? 180000 : 90000;
	if (indexCount > 0 && m_dx11PreviewIndices.size() == static_cast<size_t>(indexCount))
	{
		memcpy(dstIndices, m_dx11PreviewIndices.data(),
			static_cast<size_t>(indexCount) * sizeof(TIndex));
		return;
	}
	if (strcmp(mode, "off") != 0 &&
		(strcmp(mode, "skinned") != 0 || !GrannyMeshIsRigid(pgrnMesh)) &&
		vertexCount >= minVertices && vertexCount <= maxVertices &&
		indexCount >= minIndices && indexCount <= maxIndices && indexCount % 3 == 0)
	{
		const size_t bytes = static_cast<size_t>(indexCount) * sizeof(TIndex);
		if (bytes <= kDX11PreviewIndexBudget - s_dx11PreviewIndexBytes)
		{
			m_dx11PreviewIndices.resize(indexCount);
			GrannyCopyMeshIndices(pgrnMesh, sizeof(TIndex), m_dx11PreviewIndices.data());
			// Populate the D3D9 write-only buffer from the very same CPU array;
			// no attempt to read back a D3DUSAGE_WRITEONLY resource is needed.
			memcpy(dstIndices, m_dx11PreviewIndices.data(), bytes);
			s_dx11PreviewIndexBytes += bytes;
			return;
		}
	}
	GrannyCopyMeshIndices(pgrnMesh, sizeof(TIndex), dstIndices);
}

const std::vector<WORD>& CGrannyMesh::GetDX11PreviewIndices() const
{
	return m_dx11PreviewIndices;
}

const CGrannyMesh::TTriGroupNode* CGrannyMesh::GetDrawTriGroupNodeList(
    CGrannyMaterial::EType type, bool batched) const
{
    if (batched && !m_drawGroups[type].empty()) return m_drawGroups[type].data();
    return GetTriGroupNodeList(type);
}

void CGrannyMesh::BuildDrawBatches(const WORD* modelIndices, unsigned maxTriangles,
    std::vector<WORD>& batchedIndices)
{
    for (int type = 0; type < CGrannyMaterial::TYPE_MAX_NUM; ++type)
    {
        auto& destination = m_drawGroups[type];
        destination.clear();
        std::vector<OrderedTriangleBatch::Group> source;
        for (const TTriGroupNode* group = m_triGroupNodeLists[type]; group;
            group = group->pNextTriGroupNode)
            source.push_back({group->idxPos - m_idxBasePos, group->triCount, group->mtrlIndex});

        OrderedTriangleBatch::Result batch;
        if (!OrderedTriangleBatch::Build(modelIndices + m_idxBasePos,
            GrannyGetMeshIndexCount(m_pgrnMesh), source, maxTriangles, batch))
            continue;

        // Bound the extra per-model cache. Exceeding the budget changes only
        // batching eligibility, never the model's geometry or appearance.
        constexpr size_t maxIndices = 16u * 1024u * 1024u; // 32 MiB
        if (batch.indices.size() > maxIndices - batchedIndices.size()) continue;
        const int base = static_cast<int>(batchedIndices.size());
        batchedIndices.insert(batchedIndices.end(), batch.indices.begin(), batch.indices.end());
        destination.resize(batch.groups.size());
        for (size_t i = 0; i < batch.groups.size(); ++i)
        {
            TTriGroupNode& node = destination[i];
            node.idxPos = base + batch.groups[i].firstIndex;
            node.triCount = batch.groups[i].triangleCount;
            node.mtrlIndex = batch.groups[i].material;
            node.sourceDraws = batch.groups[i].sourceDraws;
            node.pNextTriGroupNode = i + 1 < destination.size() ? &destination[i + 1] : nullptr;
        }
    }
}

const std::vector<TPNTVertex>& CGrannyMesh::GetDX11PreviewRigidVertices() const
{
	return m_dx11PreviewRigidVertices;
}

void CGrannyMesh::LoadPNTVertices(void * dstBaseVertices) const
{
	const granny_mesh * pgrnMesh = GetGrannyMeshPointer();

	if (!GrannyMeshIsRigid(pgrnMesh))
		return;

	TPNTVertex * dstVertices = ((TPNTVertex *)dstBaseVertices) + m_vtxBasePos;
	GrannyCopyMeshVertices(pgrnMesh, m_pgrnMeshType, dstVertices);
	// Mirror the vertices while the model is being loaded and Granny's source
	// is known to be valid. This is opt-in and globally bounded; it never
	// reads back the write-only DX9Ex GPU vertex buffer.
	if (strcmp(graphics::GetDX11GrannyCaptureMode(), "character") == 0)
	{
		const int count = GrannyGetMeshVertexCount(pgrnMesh);
		if (count >= 3 && count <= 60000 && m_dx11PreviewRigidVertices.empty())
		{
			const size_t bytes = static_cast<size_t>(count) * sizeof(TPNTVertex);
			if (bytes <= kDX11PreviewRigidVertexBudget - s_dx11PreviewRigidVertexBytes)
			{
				m_dx11PreviewRigidVertices.assign(dstVertices, dstVertices + count);
				s_dx11PreviewRigidVertexBytes += bytes;
			}
		}
	}
}

void CGrannyMesh::NEW_LoadVertices(void * dstBaseVertices) const
{
	const granny_mesh * pgrnMesh = GetGrannyMeshPointer();

	if (!GrannyMeshIsRigid(pgrnMesh))
		return;

	TPNTVertex * dstVertices = ((TPNTVertex *)dstBaseVertices) + m_vtxBasePos;
	GrannyCopyMeshVertices(pgrnMesh, m_pgrnMeshType, dstVertices);
	// Modern model-loading paths use NEW_LoadVertices rather than
	// LoadPNTVertices. Cache the exact DX9Ex upload bytes here as well,
	// otherwise later GrannyCopyMeshVertices calls can return corrupt data.
	if (strcmp(graphics::GetDX11GrannyCaptureMode(), "character") == 0)
	{
		const int count = GrannyGetMeshVertexCount(pgrnMesh);
		if (count >= 3 && count <= 60000 && m_dx11PreviewRigidVertices.empty())
		{
			const size_t bytes = static_cast<size_t>(count) * sizeof(TPNTVertex);
			if (bytes <= kDX11PreviewRigidVertexBudget - s_dx11PreviewRigidVertexBytes)
			{
				m_dx11PreviewRigidVertices.assign(dstVertices, dstVertices + count);
				s_dx11PreviewRigidVertexBytes += bytes;
			}
		}
	}
}

void CGrannyMesh::DeformPNTVertices(void * dstBaseVertices, D3DXMATRIX * boneMatrices, granny_mesh_binding* pgrnMeshBinding) const
{
	assert(dstBaseVertices != nullptr);
	assert(boneMatrices != nullptr);
	assert(m_pgrnMeshDeformer != nullptr);

	const granny_mesh * pgrnMesh = GetGrannyMeshPointer();

	const auto srcVertices = (TPNTVertex *) GrannyGetMeshVertices(pgrnMesh);
	TPNTVertex * dstVertices = ((TPNTVertex *) dstBaseVertices) + m_vtxBasePos;

	const int vtxCount = GrannyGetMeshVertexCount(pgrnMesh);

	// WORK
#if GrannyProductMinorVersion==4
	int * boneIndices = GrannyGetMeshBindingToBoneIndices(pgrnMeshBinding);
#elif GrannyProductMinorVersion==11 || GrannyProductMinorVersion==9 || GrannyProductMinorVersion==8 || GrannyProductMinorVersion==7
	const granny_int32x * boneIndices = GrannyGetMeshBindingToBoneIndices(pgrnMeshBinding);
#else
#error "unknown granny version"
#endif
	// END_OF_WORK

	GrannyDeformVertices(
		m_pgrnMeshDeformer,
		boneIndices,
		(float *)boneMatrices,
		vtxCount,
		srcVertices,
		dstVertices);
}

bool CGrannyMesh::CanDeformPNTVertices() const
{
	return m_canDeformPNTVertex;
}

void CGrannyMesh::LoadGPUSkinVertices()
{
    if (!GPUSkinning::Enabled() || !m_canDeformPNTVertex) return;
    if (m_pgrnMesh->BoneBindingCount <= 0 || m_pgrnMesh->BoneBindingCount > GPUSkinning::MaxBones)
    {
        GPUSkinning::ReportFallback("mesh_palette_exceeds_72_bones");
        return;
    }
    // Refuse truncation: meshes with more than four influences keep Granny's
    // CPU deformer. Convert formats only while their original data is alive.
    int weightCount = 0, indexCount = 0;
    for (auto* member = GrannyGetMeshVertexType(m_pgrnMesh); member->Type != GrannyEndMember; ++member)
    {
        if (!member->Name) continue;
        if (strcmp(member->Name, GrannyVertexBoneWeightsName) == 0) weightCount = member->ArrayWidth;
        if (strcmp(member->Name, GrannyVertexBoneIndicesName) == 0) indexCount = member->ArrayWidth;
    }
    if (weightCount <= 0 || weightCount > 4 || indexCount != weightCount)
    {
        GPUSkinning::ReportFallback("unsupported_vertex_influences");
        return;
    }
    granny_data_type_definition type[] = {
        {GrannyReal32Member, GrannyVertexPositionName, nullptr, 3},
        {GrannyNormalUInt8Member, GrannyVertexBoneWeightsName, nullptr, 4},
        {GrannyUInt8Member, GrannyVertexBoneIndicesName, nullptr, 4},
        {GrannyReal32Member, GrannyVertexNormalName, nullptr, 3},
        {GrannyReal32Member, GrannyVertexTextureCoordinatesName "0", nullptr, 2},
        {GrannyReal32Member, GrannyVertexTextureCoordinatesName "1", nullptr, 2},
        {GrannyEndMember}
    };
    const int count = GrannyGetMeshVertexCount(m_pgrnMesh);
    if (count <= 0) return;
    std::vector<TGPUSkinVertex> vertices(count);
    GrannyCopyMeshVertices(m_pgrnMesh, type, vertices.data());
    for (auto& vertex : vertices)
    {
        unsigned weightSum = 0;
        for (unsigned influence = 0; influence < 4; ++influence)
        {
            weightSum += vertex.weights[influence];
            // A zero-weight slot is still fetched by the unrolled VS.
            if (!vertex.weights[influence]) vertex.indices[influence] = 0;
            if (vertex.indices[influence] >= m_pgrnMesh->BoneBindingCount)
            {
                GPUSkinning::ReportFallback("invalid_vertex_bone_index");
                return;
            }
        }
        if (weightSum != 255)
        {
            GPUSkinning::ReportFallback("non_normalized_vertex_weights");
            return;
        }
        for (float value : vertex.position) if (!std::isfinite(value)) return;
        for (float value : vertex.normal) if (!std::isfinite(value)) return;
        for (float value : vertex.uv) if (!std::isfinite(value)) return;
        for (float value : vertex.uv1) if (!std::isfinite(value)) return;
    }
    m_gpuSkinVertices.swap(vertices);
    m_gpuSkinBoneCount = m_pgrnMesh->BoneBindingCount;
}

bool CGrannyMesh::CreateGPUSkinDeviceObjects(IDirect3DDevice9* device)
{
    if (!device || m_gpuSkinVertices.empty()) return false;
    if (m_gpuSkinDevice != device) { DestroyGPUSkinDeviceObjects(); m_gpuSkinDevice = device; }
    if (m_gpuSkinBuffer) return true;
    if (m_gpuSkinUploadFailed) return false;
    const UINT bytes = static_cast<UINT>(m_gpuSkinVertices.size() * sizeof(TGPUSkinVertex));
    HRESULT result = device->CreateVertexBuffer(bytes, D3DUSAGE_WRITEONLY, 0,
        D3DPOOL_DEFAULT, &m_gpuSkinBuffer, nullptr);
    void* destination = nullptr;
    if (SUCCEEDED(result)) result = m_gpuSkinBuffer->Lock(0, bytes, &destination, 0);
    if (SUCCEEDED(result))
    {
        memcpy(destination, m_gpuSkinVertices.data(), bytes);
        result = m_gpuSkinBuffer->Unlock();
    }
    if (FAILED(result))
    {
        DestroyGPUSkinDeviceObjects();
        m_gpuSkinDevice = device;
        m_gpuSkinUploadFailed = true;
        GPUSkinning::ReportFallback("static_vertex_buffer_upload");
        return false;
    }
    return true;
}

void CGrannyMesh::DestroyGPUSkinDeviceObjects()
{
    if (m_gpuSkinBuffer) { m_gpuSkinBuffer->Release(); m_gpuSkinBuffer = nullptr; }
    m_gpuSkinDevice = nullptr;
    m_gpuSkinUploadFailed = false;
}

const granny_mesh * CGrannyMesh::GetGrannyMeshPointer() const
{
	return m_pgrnMesh;
}

const CGrannyMesh::TTriGroupNode * CGrannyMesh::GetTriGroupNodeList(CGrannyMaterial::EType eMtrlType) const
{
	return m_triGroupNodeLists[eMtrlType];
}

int CGrannyMesh::GetVertexCount() const
{
	assert(m_pgrnMesh!=nullptr);
	return GrannyGetMeshVertexCount(m_pgrnMesh);
}

int CGrannyMesh::GetVertexBasePosition() const
{
	return m_vtxBasePos;
}

int CGrannyMesh::GetIndexBasePosition() const
{
	return m_idxBasePos;
}

// WORK
#if GrannyProductMinorVersion==4
int * CGrannyMesh::GetDefaultBoneIndices() const
#elif GrannyProductMinorVersion==11 || GrannyProductMinorVersion==9 || GrannyProductMinorVersion==8 || GrannyProductMinorVersion==7
const granny_int32x * CGrannyMesh::GetDefaultBoneIndices() const
#else
#error "unknown granny version"
#endif
{
	return GrannyGetMeshBindingToBoneIndices(m_pgrnMeshBindingTemp);
}
// END_OF_WORK

bool CGrannyMesh::IsEmpty() const
{
	if (m_pgrnMesh)
		return false;

	return true;
}

bool CGrannyMesh::CreateFromGrannyMeshPointer(granny_skeleton * pgrnSkeleton, granny_mesh * pgrnMesh, int vtxBasePos, int idxBasePos, CGrannyMaterialPalette& rkMtrlPal)
{
	assert(IsEmpty());

	m_pgrnMesh = pgrnMesh;
	m_vtxBasePos = vtxBasePos;
	m_idxBasePos = idxBasePos;

	if (m_pgrnMesh->BoneBindingCount < 0)
		return true;

	// WORK
	m_pgrnMeshBindingTemp = GrannyNewMeshBinding(m_pgrnMesh, pgrnSkeleton, pgrnSkeleton);
	// END_OF_WORK

	if (!GrannyMeshIsRigid(m_pgrnMesh))
	{
		m_canDeformPNTVertex = true;

		granny_data_type_definition * pgrnInputType = GrannyGetMeshVertexType(m_pgrnMesh);
		granny_data_type_definition * pgrnOutputType = m_pgrnMeshType;

#if GrannyProductMinorVersion==4
		m_pgrnMeshDeformer = GrannyNewMeshDeformer(pgrnInputType, pgrnOutputType, GrannyDeformPositionNormal);
#elif GrannyProductMinorVersion==11 || GrannyProductMinorVersion==9 || GrannyProductMinorVersion==8 || GrannyProductMinorVersion==7
		m_pgrnMeshDeformer = GrannyNewMeshDeformer(pgrnInputType, pgrnOutputType, GrannyDeformPositionNormal, GrannyDontAllowUncopiedTail);
#else
#error "unknown granny version"
#endif
		assert(m_pgrnMeshDeformer != nullptr && "Cannot create mesh deformer");
	}

	// Two Side Mesh
	if (!strncmp(m_pgrnMesh->Name, "2x", 2))
		m_isTwoSide = true;

	if (!LoadMaterials(rkMtrlPal))
		return false;

	if (!LoadTriGroupNodeList(rkMtrlPal))
		return false;

    LoadGPUSkinVertices();

	return true;
}

bool CGrannyMesh::LoadTriGroupNodeList(CGrannyMaterialPalette& rkMtrlPal)
{
	assert(m_pgrnMesh != nullptr);
	assert(m_triGroupNodes == nullptr);

	const int mtrlCount		= m_pgrnMesh->MaterialBindingCount;
	if (mtrlCount <= 0)
		return true;

	const int GroupNodeCount	= GrannyGetMeshTriangleGroupCount(m_pgrnMesh);
	if (GroupNodeCount <= 0)
		return true;

	m_triGroupNodes		= new TTriGroupNode[GroupNodeCount];

	const granny_tri_material_group * c_pgrnTriGroups = GrannyGetMeshTriangleGroups(m_pgrnMesh);

	for (int g = 0; g < GroupNodeCount; ++g)
	{
		const granny_tri_material_group & c_rgrnTriGroup = c_pgrnTriGroups[g];
		TTriGroupNode * pTriGroupNode = m_triGroupNodes + g;

		pTriGroupNode->idxPos = m_idxBasePos + c_rgrnTriGroup.TriFirst * 3;
		pTriGroupNode->triCount = c_rgrnTriGroup.TriCount;

		const int iMtrl = c_rgrnTriGroup.MaterialIndex;
		if (iMtrl < 0 || iMtrl >= mtrlCount)
		{
			pTriGroupNode->mtrlIndex=0;//m_mtrlIndexVector[iMtrl];
		}
		else
		{
			pTriGroupNode->mtrlIndex=m_mtrlIndexVector[iMtrl];
		}

		const CGrannyMaterial& rkMtrl=rkMtrlPal.GetMaterialRef(pTriGroupNode->mtrlIndex);
		pTriGroupNode->pNextTriGroupNode		= m_triGroupNodeLists[rkMtrl.GetType()];
		m_triGroupNodeLists[rkMtrl.GetType()]	= pTriGroupNode;

	}

	return true;
}

void CGrannyMesh::RebuildTriGroupNodeList() const
{
	assert(!"CGrannyMesh::RebuildTriGroupNodeList() - Why should you rebuild it -?");
}

bool CGrannyMesh::LoadMaterials(CGrannyMaterialPalette& rkMtrlPal)
{
	assert(m_pgrnMesh != nullptr);

	if (m_pgrnMesh->MaterialBindingCount <= 0)
		return true;

	const int mtrlCount = m_pgrnMesh->MaterialBindingCount;
	bool bHaveBlendThing = false;

	for (int m = 0; m < mtrlCount; ++m)
	{
		granny_material* pgrnMaterial = m_pgrnMesh->MaterialBindings[m].Material;
		DWORD mtrlIndex=rkMtrlPal.RegisterMaterial(pgrnMaterial);
		m_mtrlIndexVector.push_back(mtrlIndex);
		bHaveBlendThing |= rkMtrlPal.GetMaterialRef(mtrlIndex).GetType() == CGrannyMaterial::TYPE_BLEND_PNT;
	}
	m_bHaveBlendThing = bHaveBlendThing;

	return true;
}

bool CGrannyMesh::IsTwoSide() const
{
	return m_isTwoSide;
}

void CGrannyMesh::SetPNT2Mesh()
{
	m_pgrnMeshType = GrannyPNT3322VertexType;
}

void CGrannyMesh::Destroy()
{
    for (auto& groups : m_drawGroups) std::vector<TTriGroupNode>().swap(groups);
    DestroyGPUSkinDeviceObjects();
    std::vector<TGPUSkinVertex>().swap(m_gpuSkinVertices);
    m_gpuSkinBoneCount = 0;
	const size_t rigidBytes = m_dx11PreviewRigidVertices.size() * sizeof(TPNTVertex);
	if (rigidBytes)
	{
		s_dx11PreviewRigidVertexBytes -= rigidBytes;
		std::vector<TPNTVertex>().swap(m_dx11PreviewRigidVertices);
	}
	const size_t cachedBytes = m_dx11PreviewIndices.size() * sizeof(WORD);
	if (cachedBytes)
	{
		s_dx11PreviewIndexBytes -= cachedBytes;
		std::vector<WORD>().swap(m_dx11PreviewIndices);
	}
	if (m_triGroupNodes)
		delete [] m_triGroupNodes;

	m_mtrlIndexVector.clear();

	// WORK
	if (m_pgrnMeshBindingTemp)
		GrannyFreeMeshBinding(m_pgrnMeshBindingTemp);
	// END_OF_WORK

    if (m_pgrnMeshDeformer)
		GrannyFreeMeshDeformer(m_pgrnMeshDeformer);

	Initialize();
}

void CGrannyMesh::Initialize()
{
	for (int r = 0; r < CGrannyMaterial::TYPE_MAX_NUM; ++r)
		m_triGroupNodeLists[r] = nullptr;

	m_pgrnMeshType = GrannyPNT332VertexType;
	m_pgrnMesh = nullptr;
	// WORK
	m_pgrnMeshBindingTemp = nullptr;
	// END_OF_WORK
	m_pgrnMeshDeformer = nullptr;

	m_triGroupNodes = nullptr;

	m_vtxBasePos = 0;
	m_idxBasePos = 0;

	m_canDeformPNTVertex = false;
	m_isTwoSide = false;
	m_bHaveBlendThing = false;
}

CGrannyMesh::CGrannyMesh()
{
	Initialize();
}

CGrannyMesh::~CGrannyMesh()
{
	Destroy();
}

