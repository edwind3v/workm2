#pragma once
#include <map>
#include "SpeedTreeForest.h"
#include "SpeedTreeMaterial.h"

class CSpeedTreeDirectX : public CSpeedTreeForest, public CGraphicBase
{
public:
	CSpeedTreeDirectX();
	~CSpeedTreeDirectX();

	void                        UploadWindMatrix(unsigned int uiLocation, const float* pMatrix) const;
	bool						SetRenderingDevice();
	void                        Render(unsigned long ulRenderBitVector = Forest_RenderAll);
	void						UpdateCompundMatrix(const Vector3& c_rEyeVec, const Matrix& c_rmatView, const Matrix& c_rmatProj);

private:
	bool                        InitVertexShaders();

private:
	LPDIRECT3DVERTEXSHADER9     m_dwBranchVertexShader;
	LPDIRECT3DVERTEXSHADER9     m_dwLeafVertexShader;
};

