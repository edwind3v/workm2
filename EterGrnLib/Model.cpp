#include "StdAfx.h"
#include "../EterBase/MapPerformance.h"
#include "Model.h"
#include "Mesh.h"
#include "OrderedTriangleBatch.h"
#include <cmath>

const CGrannyMaterialPalette& CGrannyModel::GetMaterialPalette() const
{
	return m_kMtrlPal;
}

const CGrannyModel::TMeshNode* CGrannyModel::GetMeshNodeList(CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType) const
{
	return m_meshNodeLists[eMeshType][eMtrlType];
}

CGrannyMesh * CGrannyModel::GetMeshPointer(int iMesh)
{
	assert(CheckMeshIndex(iMesh));
	assert(m_meshs != nullptr);

	return m_meshs + iMesh;
}

const CGrannyMesh* CGrannyModel::GetMeshPointer(int iMesh) const
{
	assert(CheckMeshIndex(iMesh));
	assert(m_meshs != nullptr);

	return m_meshs + iMesh;
}

bool CGrannyModel::CanDeformPNTVertices() const
{
	return m_canDeformPNVertices;
}

void CGrannyModel::DeformPNTVertices(void * dstBaseVertices, D3DXMATRIX * boneMatrices, const std::vector<granny_mesh_binding*>& c_rvct_pgrnMeshBinding) const
{
	const int meshCount = GetMeshCount();

	for (int iMesh = 0; iMesh < meshCount; ++iMesh)
	{
		assert(iMesh < c_rvct_pgrnMeshBinding.size());

		CGrannyMesh & rMesh = m_meshs[iMesh];
		if (rMesh.CanDeformPNTVertices())
			rMesh.DeformPNTVertices(dstBaseVertices, boneMatrices, c_rvct_pgrnMeshBinding[iMesh]);
	}
}

int CGrannyModel::GetRigidVertexCount() const
{
	return m_rigidVtxCount;
}

int CGrannyModel::GetDeformVertexCount() const
{
	return m_deformVtxCount;
}

int CGrannyModel::GetVertexCount() const
{
	return m_vtxCount;
}

int CGrannyModel::GetMeshCount() const
{
	return m_pgrnModel ? m_pgrnModel->MeshBindingCount : 0;
}

granny_model* CGrannyModel::GetGrannyModelPointer() const
{
	return m_pgrnModel;
}

LPDIRECT3DINDEXBUFFER9 CGrannyModel::GetD3DIndexBuffer() const
{
	return m_idxBuf.GetD3DIndexBuffer();
}

LPDIRECT3DINDEXBUFFER9 CGrannyModel::GetBatchedD3DIndexBuffer() const
{
    return m_batchedIdxBuf.GetD3DIndexBuffer();
}

void CGrannyModel::UploadBatchedIndices()
{
    m_batchedIdxBuf.Destroy();
    if (m_batchedIndices.empty()) return;
    if (!m_batchedIdxBuf.Create(static_cast<int>(m_batchedIndices.size()), D3DFMT_INDEX16) ||
        !m_batchedIdxBuf.Copy(static_cast<int>(m_batchedIndices.size() * sizeof(WORD)), m_batchedIndices.data()))
    {
        m_batchedIdxBuf.Destroy();
        TraceError("MODEL_BATCH_FALLBACK reason=index_buffer_upload");
    }
}

LPDIRECT3DVERTEXBUFFER9 CGrannyModel::GetPNTD3DVertexBuffer() const
{
	return m_pntVtxBuf.GetD3DVertexBuffer();
}

bool CGrannyModel::LockVertices(void** indicies, void** vertices) const
{
	if (!m_idxBuf.Lock(indicies))
		return false;

	if (!m_pntVtxBuf.Lock(vertices))
	{
		m_idxBuf.Unlock();
		return false;
	}

	return true;
}

void CGrannyModel::UnlockVertices() const
{
	m_idxBuf.Unlock();
	m_pntVtxBuf.Unlock();
}

bool CGrannyModel::LoadPNTVertices()
{
	if (m_rigidVtxCount <= 0)
		return true;

	assert(m_meshs != nullptr);

	if (!m_pntVtxBuf.Create(m_rigidVtxCount, m_dwFvF, D3DUSAGE_WRITEONLY, D3DPOOL_DEFAULT))
		return false;

	void* vertices;
	if (!m_pntVtxBuf.Lock(&vertices))
		return false;

	for (int m = 0; m < m_pgrnModel->MeshBindingCount; ++m)
	{
		CGrannyMesh& rMesh = m_meshs[m];
		rMesh.LoadPNTVertices(vertices);
	}

	m_pntVtxBuf.Unlock();
	return true;
}

bool CGrannyModel::LoadIndices()
{
	//assert(m_idxCount > 0);
	if (m_idxCount <= 0)
		return true;

	if (!m_idxBuf.Create(m_idxCount, D3DFMT_INDEX16))
		return false;

	// Retain the original index layout for collision/debug readers. The optional
	// second buffer follows the exact existing draw order, including alpha.
	std::vector<WORD> source(m_idxCount);
	m_batchedIndices.clear();
	char batchingMode[8]{};
	const bool batchingEnabled = !(GetEnvironmentVariableA("M2_MODEL_BATCHING", batchingMode,
		sizeof(batchingMode)) == 1 && batchingMode[0] == '0');
	D3DCAPS9 caps{};
	IDirect3DDevice9* device = nullptr;
	bool canBatch = false;
	if (batchingEnabled && SUCCEEDED(m_idxBuf.GetD3DIndexBuffer()->GetDevice(&device)))
	{
		canBatch = SUCCEEDED(device->GetDeviceCaps(&caps));
		device->Release();
	}
	for (int m = 0; m < m_pgrnModel->MeshBindingCount; ++m)
	{
		CGrannyMesh& mesh = m_meshs[m];
		mesh.LoadIndices(source.data());
		if (canBatch) mesh.BuildDrawBatches(source.data(), caps.MaxPrimitiveCount, m_batchedIndices);
	}
    if (canBatch) BuildRigidBatches(source, caps);
	void * indices;

	if (!m_idxBuf.Lock((void**)&indices))
		return false;

	memcpy(indices, source.data(), source.size() * sizeof(WORD));
	m_idxBuf.Unlock();
	UploadBatchedIndices();
	return true;
}

void CGrannyModel::BuildRigidBatches(const std::vector<WORD>& source, const D3DCAPS9& caps)
{
    char mode[8]{};
    if (GetEnvironmentVariableA("M2_RIGID_BATCHING", mode, sizeof(mode)) == 1 && mode[0] == '0') return;
    if (m_dwFvF != (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1) ||
        m_rigidVtxCount <= 0 || m_rigidVtxCount > 65536 ||
        static_cast<unsigned>(m_rigidVtxCount - 1) > caps.MaxVertexIndex) return;
    // Bound any matrix-comparison tolerance by actual vertex magnitudes.
    // Read source vertices only here, before Granny's source sections are freed.
    m_rigidPositionBound = m_rigidNormalBound = 1;
    for (int i = 0; i < GetMeshCount(); ++i)
    {
        const CGrannyMesh& mesh = m_meshs[i];
        if (!GrannyMeshIsRigid(mesh.GetGrannyMeshPointer())) continue;
        std::vector<TPNTVertex> vertices(mesh.GetVertexCount());
        GrannyCopyMeshVertices(mesh.GetGrannyMeshPointer(), GrannyPNT332VertexType, vertices.data());
        for (const auto& vertex : vertices)
        {
            const double position = std::abs(static_cast<double>(vertex.position.x)) +
                std::abs(static_cast<double>(vertex.position.y)) + std::abs(static_cast<double>(vertex.position.z));
            const double normal = std::abs(static_cast<double>(vertex.normal.x)) +
                std::abs(static_cast<double>(vertex.normal.y)) + std::abs(static_cast<double>(vertex.normal.z));
            if (!std::isfinite(position) || !std::isfinite(normal)) return;
            m_rigidPositionBound = (std::max)(m_rigidPositionBound, position);
            m_rigidNormalBound = (std::max)(m_rigidNormalBound, normal);
        }
    }
    for (int type = 0; type < CGrannyMaterial::TYPE_MAX_NUM; ++type)
    {
        const auto materialType = static_cast<CGrannyMaterial::EType>(type);
        const TMeshNode* list = GetMeshNodeList(CGrannyMesh::TYPE_RIGID, materialType);
        if (!list || !list->pNextMeshNode) continue;
        std::vector<WORD> orderedIndices;
        std::vector<OrderedTriangleBatch::Group> groups;
        std::vector<TRigidSource> originals;
        bool valid = true;
        // Follow the actual mesh AND group linked lists, never sort by material.
        for (const TMeshNode* node = list; node && valid; node = node->pNextMeshNode)
        {
            const CGrannyMesh& mesh = *node->pMesh;
            for (const auto* group = mesh.GetTriGroupNodeList(materialType); group; group = group->pNextTriGroupNode)
            {
                if (group->idxPos < 0 || group->idxPos > static_cast<int>(source.size()) ||
                    group->triCount <= 0 || group->triCount > (static_cast<int>(source.size()) - group->idxPos) / 3 ||
                    orderedIndices.size() + static_cast<size_t>(group->triCount) * 3 > source.size())
                { valid = false; break; }
                groups.push_back({static_cast<int>(orderedIndices.size()), group->triCount, group->mtrlIndex});
                originals.push_back({node->iMesh, *group});
                for (int i = 0; i < group->triCount * 3; ++i)
                {
                    const unsigned local = source[group->idxPos + i];
                    const unsigned rebased = local + mesh.GetVertexBasePosition();
                    if (local >= static_cast<unsigned>(mesh.GetVertexCount()) || rebased >= static_cast<unsigned>(m_rigidVtxCount))
                    { valid = false; break; }
                    orderedIndices.push_back(static_cast<WORD>(rebased));
                }
                if (!valid) break;
            }
        }
        OrderedTriangleBatch::Result batch;
        if (!valid || !OrderedTriangleBatch::Build(orderedIndices.data(), static_cast<int>(orderedIndices.size()),
            groups, caps.MaxPrimitiveCount, batch)) continue;
        constexpr size_t maxIndices = 16u * 1024u * 1024u;
        if (batch.indices.size() > maxIndices - m_batchedIndices.size()) continue;
        const int base = static_cast<int>(m_batchedIndices.size());
        auto& draws = m_rigidBatches[type];
        draws.clear();
        unsigned first = 0;
        for (const auto& group : batch.groups)
        {
            TRigidBatch draw{};
            draw.group.idxPos = base + group.firstIndex;
            draw.group.triCount = group.triangleCount;
            draw.group.mtrlIndex = group.material;
            draw.group.sourceDraws = group.sourceDraws;
            draw.firstSource = first;
            draw.sourceCount = group.sourceDraws;
            first += group.sourceDraws;
            draws.push_back(draw);
        }
        m_rigidSources[type].swap(originals);
        m_batchedIndices.insert(m_batchedIndices.end(), batch.indices.begin(), batch.indices.end());
        TraceError("RIGID_BATCH_READY asset=\"%s\" material_type=%d sources=%u draws=%u vertices=%d max_position_error=0.0001 max_normal_error=0.000001",
            GetDiagnosticAsset(), type, first, static_cast<unsigned>(draws.size()), m_rigidVtxCount);
    }
}

bool CGrannyModel::LoadMeshs()
{
	assert(m_meshs == nullptr);
	assert(m_pgrnModel != nullptr);

	if (m_pgrnModel->MeshBindingCount <= 0)
		return true;

	granny_skeleton * pgrnSkeleton = m_pgrnModel->Skeleton;

	int vtxRigidPos = 0;
	int vtxDeformPos = 0;
	int vtxPos = 0;
	int idxPos = 0;

	int diffusePNTMeshNodeCount = 0;
	int blendPNTMeshNodeCount = 0;
	const int blendPNT2MeshNodeCount = 0;

	const int meshCount = GetMeshCount();
	m_meshs = new CGrannyMesh[meshCount];

	m_dwFvF = 0;

	for (int m = 0; m < meshCount; ++m)
	{
		CGrannyMesh& rMesh = m_meshs[m];
		granny_mesh* pgrnMesh = m_pgrnModel->MeshBindings[m].Mesh;

		if (GrannyMeshIsRigid(pgrnMesh))
		{
			if (!rMesh.CreateFromGrannyMeshPointer(pgrnSkeleton, pgrnMesh, vtxRigidPos, idxPos, m_kMtrlPal))
				return false;

			vtxRigidPos += GrannyGetMeshVertexCount(pgrnMesh);
		}
		else
		{
			if (!rMesh.CreateFromGrannyMeshPointer(pgrnSkeleton, pgrnMesh, vtxDeformPos, idxPos, m_kMtrlPal))
				return false;

			vtxDeformPos += GrannyGetMeshVertexCount(pgrnMesh);
			m_canDeformPNVertices |= rMesh.CanDeformPNTVertices();
		}
		m_bHaveBlendThing |= rMesh.HaveBlendThing();

		const granny_int32x grni32xTypeCount = GrannyGetTotalTypeSize(pgrnMesh->PrimaryVertexData->VertexType) / 32;
		int i = 0;
		while (i < grni32xTypeCount)
		{
			if (nullptr == pgrnMesh->PrimaryVertexData->VertexType[i].Name || 0 == strlen(pgrnMesh->PrimaryVertexData->VertexType[i].Name))
			{
				++i;
				continue;
			}
			if ( 0 == strcmp(pgrnMesh->PrimaryVertexData->VertexType[i].Name, GrannyVertexPositionName) )
				m_dwFvF |= D3DFVF_XYZ;
			else if ( 0 == strcmp(pgrnMesh->PrimaryVertexData->VertexType[i].Name, GrannyVertexNormalName) )
				m_dwFvF |= D3DFVF_NORMAL;
			else if ( 0 == strcmp(pgrnMesh->PrimaryVertexData->VertexType[i].Name, GrannyVertexTextureCoordinatesName"0") )
				m_dwFvF |= D3DFVF_TEX1;
			else if ( 0 == strcmp(pgrnMesh->PrimaryVertexData->VertexType[i].Name, GrannyVertexTextureCoordinatesName"1") )
				m_dwFvF |= D3DFVF_TEX2;
			++i;
		}

		vtxPos += GrannyGetMeshVertexCount(pgrnMesh);
		idxPos += GrannyGetMeshIndexCount(pgrnMesh);

		if (rMesh.GetTriGroupNodeList(CGrannyMaterial::TYPE_DIFFUSE_PNT))
			++diffusePNTMeshNodeCount;

		if (rMesh.GetTriGroupNodeList(CGrannyMaterial::TYPE_BLEND_PNT))
			++blendPNTMeshNodeCount;
	}

	m_meshNodeCapacity = diffusePNTMeshNodeCount + blendPNTMeshNodeCount + blendPNT2MeshNodeCount;
	m_meshNodes = new TMeshNode[m_meshNodeCapacity];

	for (int n = 0; n < meshCount; ++n)
	{
		CGrannyMesh& rMesh = m_meshs[n];
		const granny_mesh* pgrnMesh = m_pgrnModel->MeshBindings[n].Mesh;

		const CGrannyMesh::EType eMeshType = GrannyMeshIsRigid(pgrnMesh) ? CGrannyMesh::TYPE_RIGID : CGrannyMesh::TYPE_DEFORM;

		if (rMesh.GetTriGroupNodeList(CGrannyMaterial::TYPE_DIFFUSE_PNT))
			AppendMeshNode(eMeshType, CGrannyMaterial::TYPE_DIFFUSE_PNT, n);

		if (rMesh.GetTriGroupNodeList(CGrannyMaterial::TYPE_BLEND_PNT))
			AppendMeshNode(eMeshType, CGrannyMaterial::TYPE_BLEND_PNT, n);
	}

	// For Dungeon Block
	if ((D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1|D3DFVF_TEX2) == m_dwFvF)
	{
		for (int n = 0; n < meshCount; ++n)
		{
			CGrannyMesh& rMesh = m_meshs[n];
			rMesh.SetPNT2Mesh();
		}
	}

	m_rigidVtxCount = vtxRigidPos;
	m_deformVtxCount = vtxDeformPos;

	m_vtxCount = vtxPos;
	m_idxCount = idxPos;
	return true;
}

BOOL CGrannyModel::CheckMeshIndex(int iIndex) const
{
	if (iIndex < 0)
		return FALSE;
	if (iIndex >= GetMeshCount())
		return FALSE;

	return TRUE;
}

void CGrannyModel::AppendMeshNode(CGrannyMesh::EType eMeshType, CGrannyMaterial::EType eMtrlType, int iMesh)
{
	assert(m_meshNodeSize < m_meshNodeCapacity);

	TMeshNode& rMeshNode = m_meshNodes[m_meshNodeSize++];

	rMeshNode.iMesh = iMesh;
	rMeshNode.pMesh = m_meshs + iMesh;
	rMeshNode.pNextMeshNode = m_meshNodeLists[eMeshType][eMtrlType];
	m_meshNodeLists[eMeshType][eMtrlType] = &rMeshNode;
}

bool CGrannyModel::CreateFromGrannyModelPointer(granny_model* pgrnModel)
{
	assert(IsEmpty());

	m_pgrnModel = pgrnModel;

	if (!LoadMeshs())
		return false;

	if (!__LoadVertices())
		return false;

	if (!LoadIndices())
		return false;

	AddReference();

	return true;
}

int CGrannyModel::GetIdxCount() const
{
	return m_idxCount;
}

bool CGrannyModel::CreateDeviceObjects()
{
	MapPerformance::Scope profile("graphics.model_upload");
	if (m_rigidVtxCount > 0)
		if (!m_pntVtxBuf.CreateDeviceObjects())
			return false;

	if (m_idxCount > 0)
		if (!m_idxBuf.CreateDeviceObjects())
			return false;
	UploadBatchedIndices();

	const int meshCount = GetMeshCount();

	for (int i = 0; i < meshCount; ++i)
	{
		CGrannyMesh& rMesh = m_meshs[i];
		rMesh.RebuildTriGroupNodeList();
	}

	return true;
}

void CGrannyModel::DestroyDeviceObjects()
{
	m_batchedIdxBuf.Destroy();
    for (int mesh = 0; mesh < GetMeshCount(); ++mesh)
        m_meshs[mesh].DestroyGPUSkinDeviceObjects();
	m_pntVtxBuf.DestroyDeviceObjects();
	m_idxBuf.DestroyDeviceObjects();
}

bool CGrannyModel::IsEmpty() const
{
	if (m_pgrnModel)
		return false;

	return true;
}

void CGrannyModel::Destroy()
{
	m_kMtrlPal.Clear();

	if (m_meshNodes)
		delete [] m_meshNodes;

	if (m_meshs)
		delete [] m_meshs;

	m_pntVtxBuf.Destroy();
	m_idxBuf.Destroy();
	m_batchedIdxBuf.Destroy();
	std::vector<WORD>().swap(m_batchedIndices);
    for (auto& batches : m_rigidBatches) std::vector<TRigidBatch>().swap(batches);
    for (auto& sources : m_rigidSources) std::vector<TRigidSource>().swap(sources);

	Initialize();
}

bool CGrannyModel::__LoadVertices()
{
	if (m_rigidVtxCount <= 0)
		return true;

	assert(m_meshs != nullptr);

	if (!m_pntVtxBuf.Create(m_rigidVtxCount, m_dwFvF, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, D3DPOOL_DEFAULT))
		return false;

	void* vertices;
	if (!m_pntVtxBuf.Lock(&vertices))
		return false;

	for (int m = 0; m < m_pgrnModel->MeshBindingCount; ++m)
	{
		CGrannyMesh& rMesh = m_meshs[m];
		rMesh.NEW_LoadVertices(vertices);
	}

	m_pntVtxBuf.Unlock();
	return true;
}

void CGrannyModel::Initialize()
{
	m_diagnosticAsset.clear();
	memset(m_meshNodeLists, 0, sizeof(m_meshNodeLists));

	m_pgrnModel = nullptr;
	m_meshs = nullptr;
	m_meshNodes = nullptr;

	m_meshNodeSize = 0;
	m_meshNodeCapacity = 0;

	m_rigidVtxCount = 0;
	m_deformVtxCount = 0;
	m_vtxCount = 0;
	m_idxCount = 0;

	m_canDeformPNVertices = false;

	m_dwFvF = 0;
	m_bHaveBlendThing = false;
}

CGrannyModel::CGrannyModel()
{
	Initialize();
}

CGrannyModel::~CGrannyModel()
{
	Destroy();
}

