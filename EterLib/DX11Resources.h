#pragma once

#include "DX11ShaderLibrary.h"

#include <d3d11.h>
#include <wrl/client.h>

namespace graphics
{
// Reusable D3D11 resources, independent of the legacy D3D9 device and its
// resource lifetime. Every object is owned by a single D3D11 device.
class DX11Buffer final
{
public:
	HRESULT CreateVertex(ID3D11Device* device, const void* vertices,
		UINT stride, UINT count, bool dynamic = false);
	HRESULT CreateIndex(ID3D11Device* device, const void* indices,
		DXGI_FORMAT format, UINT count, bool dynamic = false);
	HRESULT CreateConstant(ID3D11Device* device, const void* data,
		UINT byteCount, bool dynamic = true);
	HRESULT Update(ID3D11DeviceContext* context, const void* data, UINT byteCount);

	void BindVertex(ID3D11DeviceContext* context, UINT slot = 0) const;
	void BindIndex(ID3D11DeviceContext* context) const;
	void BindVSConstant(ID3D11DeviceContext* context, UINT slot = 0) const;
	void BindPSConstant(ID3D11DeviceContext* context, UINT slot = 0) const;

	ID3D11Buffer* Get() const { return m_buffer.Get(); }
	UINT GetCount() const { return m_count; }
	void Reset();

private:
	HRESULT Create(ID3D11Device* device, D3D11_BIND_FLAG bind,
		const void* data, UINT stride, UINT count, DXGI_FORMAT indexFormat,
		bool dynamic);

	Microsoft::WRL::ComPtr<ID3D11Buffer> m_buffer;
	D3D11_BIND_FLAG m_bind = D3D11_BIND_VERTEX_BUFFER;
	DXGI_FORMAT m_indexFormat = DXGI_FORMAT_UNKNOWN;
	UINT m_stride = 0;
	UINT m_count = 0;
	UINT m_byteCount = 0;
	bool m_dynamic = false;
};

class DX11Texture final
{
public:
	// Uploads tightly packed RGBA8 rows (or rows with the supplied larger pitch).
	HRESULT CreateRGBA8(ID3D11Device* device, UINT width, UINT height,
		const void* pixels, UINT sourceRowPitch, bool dynamic = false);
	HRESULT UpdateRGBA8(ID3D11DeviceContext* context,
		const void* pixels, UINT sourceRowPitch);
	void BindPS(ID3D11DeviceContext* context, UINT slot = 0) const;
	void Reset();

private:
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_view;
	UINT m_width = 0;
	UINT m_height = 0;
	bool m_dynamic = false;
};

class DX11InputLayout final
{
public:
	HRESULT Create(ID3D11Device* device, const DX11ShaderProgram& vertexShader,
		const D3D11_INPUT_ELEMENT_DESC* elements, UINT count);
	void Bind(ID3D11DeviceContext* context) const;

private:
	Microsoft::WRL::ComPtr<ID3D11InputLayout> m_layout;
};

// A small, explicit baseline for textured opaque geometry. Later Metin2
// material/render-state translations can add variants without hardcoding
// states inside every draw call.
class DX11RenderStates final
{
public:
	HRESULT Create(ID3D11Device* device);
	void BindOpaqueTwoSided(ID3D11DeviceContext* context) const;

private:
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizer;
	Microsoft::WRL::ComPtr<ID3D11BlendState> m_opaqueBlend;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthDisabled;
	Microsoft::WRL::ComPtr<ID3D11SamplerState> m_linearClamp;
};
}
