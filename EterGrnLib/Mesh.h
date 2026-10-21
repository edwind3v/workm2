#pragma once

#include "Material.h"
#include "../eterLib/GrpBase.h"
#include "GPUSkinning.h"

extern granny_data_type_definition GrannyPNT3322VertexType[5];

struct granny_pnt3322_vertex
{
    granny_real32 Position[3];
    granny_real32 Normal[3];
    granny_real32 UV0[2];
    granny_real32 UV1[2];
};

class CGrannyMesh
{
	public:
		enum EType
		{
			TYPE_RIGID,
			TYPE_DEFORM,
			TYPE_MAX_NUM
		};

		typedef struct STriGroupNode
		{
			STriGroupNode *		pNextTriGroupNode;
			int					idxPos;
			int					triCount;
			DWORD				mtrlIndex;
			unsigned sourceDraws = 1;
		} TTriGroupNode;

	public:
		CGrannyMesh();
		virtual ~CGrannyMesh();

		bool					IsEmpty() const;
		bool					CreateFromGrannyMeshPointer(granny_skeleton* pgrnSkeleton, granny_mesh* pgrnMesh, int vtxBasePos, int idxBasePos, CGrannyMaterialPalette& rkMtrlPal);
		void					LoadIndices(void* dstBaseIndices) const;
		// Opt-in CPU copy retained when the original index buffer is created.
		// The later Granny mesh pointer is not guaranteed to retain live topology.
		const std::vector<WORD>& GetDX11PreviewIndices() const;
		// Same source vertices uploaded to the DX9Ex rigid vertex buffer at
		// model load. Avoid copying possibly stale Granny vertices during play.
		const std::vector<TPNTVertex>& GetDX11PreviewRigidVertices() const;
 		void					LoadPNTVertices(void* dstBaseVertices) const;
		void					NEW_LoadVertices(void* dstBaseVertices) const;
		void					Destroy();

		void					SetPNT2Mesh();

		void					DeformPNTVertices(void* dstBaseVertices, D3DXMATRIX* boneMatrices, granny_mesh_binding* pgrnMeshBinding) const;
		bool					CanDeformPNTVertices() const;
        bool CreateGPUSkinDeviceObjects(IDirect3DDevice9* device);
        void DestroyGPUSkinDeviceObjects();
        IDirect3DVertexBuffer9* GetGPUSkinBuffer() const { return m_gpuSkinBuffer; }
        unsigned GetGPUSkinBoneCount() const { return m_gpuSkinBoneCount; }
		granny_mesh_binding* GetDefaultMeshBinding() const { return m_pgrnMeshBindingTemp; }
		bool					IsTwoSide() const;

		int						GetVertexCount() const;

		// WORK
#if GrannyProductMinorVersion==4
		int *					GetDefaultBoneIndices() const;
#elif GrannyProductMinorVersion==11 || GrannyProductMinorVersion==9 || GrannyProductMinorVersion==8 || GrannyProductMinorVersion==7
		const granny_int32x *					GetDefaultBoneIndices() const;
#else
#error "unknown granny version"
#endif
		// END_OF_WORK

		int						GetVertexBasePosition() const;
		int						GetIndexBasePosition() const;

		const granny_mesh *					GetGrannyMeshPointer() const;
		const CGrannyMesh::TTriGroupNode *	GetTriGroupNodeList(CGrannyMaterial::EType eMtrlType) const;
		const TTriGroupNode* GetDrawTriGroupNodeList(CGrannyMaterial::EType type, bool batched) const;
		void BuildDrawBatches(const WORD* modelIndices, unsigned maxTriangles,
			std::vector<WORD>& batchedIndices);

		void					RebuildTriGroupNodeList() const;
		void					ReloadMaterials();

	protected:
		void					Initialize();
        void LoadGPUSkinVertices();

		bool					LoadMaterials(CGrannyMaterialPalette& rkMtrlPal);
		bool					LoadTriGroupNodeList(CGrannyMaterialPalette& rkMtrlPal);

	protected:
		// Granny Mesh Data
		granny_data_type_definition *	m_pgrnMeshType;
		granny_mesh *			m_pgrnMesh;

		// WORK
		granny_mesh_binding *	m_pgrnMeshBindingTemp;
		// END_OF_WORK

		granny_mesh_deformer *	m_pgrnMeshDeformer;

		// Granny Material Data
		std::vector<DWORD>		m_mtrlIndexVector;

		// TriGroups Data
		TTriGroupNode *			m_triGroupNodes;
		TTriGroupNode *			m_triGroupNodeLists[CGrannyMaterial::TYPE_MAX_NUM];
		std::vector<TTriGroupNode> m_drawGroups[CGrannyMaterial::TYPE_MAX_NUM];

		int						m_vtxBasePos;
		int						m_idxBasePos;
		mutable std::vector<WORD>	m_dx11PreviewIndices;
		mutable std::vector<TPNTVertex> m_dx11PreviewRigidVertices;
        std::vector<TGPUSkinVertex> m_gpuSkinVertices;
        IDirect3DVertexBuffer9* m_gpuSkinBuffer = nullptr;
        IDirect3DDevice9* m_gpuSkinDevice = nullptr;
        unsigned m_gpuSkinBoneCount = 0;
        bool m_gpuSkinUploadFailed = false;

		bool					m_canDeformPNTVertex;
		bool					m_isTwoSide;
	private:
		bool						m_bHaveBlendThing;
	public:
		bool						HaveBlendThing() const { return m_bHaveBlendThing; }
};

