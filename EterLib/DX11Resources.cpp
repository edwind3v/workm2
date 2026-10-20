#include "StdAfx.h"
#include "DX11Resources.h"

#include <cstring>
#include <climits>

namespace graphics
{
HRESULT DX11Buffer::Create(ID3D11Device* device, D3D11_BIND_FLAG bind,
	const void* data, UINT stride, UINT count, DXGI_FORMAT indexFormat,
	bool dynamic)
{
	Reset();
	if (!device || stride == 0 || count == 0 ||
		count > UINT_MAX / stride || (!data && !dynamic))
		return E_INVALIDARG;

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = stride * count;
	desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = bind;
	desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
	D3D11_SUBRESOURCE_DATA initial = {};
	initial.pSysMem = data;
	const HRESULT hr = device->CreateBuffer(&desc, data ? &initial : nullptr,
		m_buffer.GetAddressOf());
	if (SUCCEEDED(hr))
	{
		m_bind = bind;
		m_stride = stride;
		m_count = count;
		m_byteCount = desc.ByteWidth;
		m_indexFormat = indexFormat;
		m_dynamic = dynamic;
	}
	return hr;
}

HRESULT DX11Buffer::CreateVertex(ID3D11Device* device, const void* vertices,
	UINT stride, UINT count, bool dynamic)
{
	return Create(device, D3D11_BIND_VERTEX_BUFFER, vertices, stride, count,
		DXGI_FORMAT_UNKNOWN, dynamic);
}

HRESULT DX11Buffer::CreateIndex(ID3D11Device* device, const void* indices,
	DXGI_FORMAT format, UINT count, bool dynamic)
{
	if (format != DXGI_FORMAT_R16_UINT && format != DXGI_FORMAT_R32_UINT)
		return E_INVALIDARG;
	return Create(device, D3D11_BIND_INDEX_BUFFER, indices,
		format == DXGI_FORMAT_R16_UINT ? 2u : 4u, count, format, dynamic);
}

HRESULT DX11Buffer::CreateConstant(ID3D11Device* device, const void* data,
	UINT byteCount, bool dynamic)
{
	// A D3D11 constant buffer must contain whole float4 registers.
	if (byteCount == 0 || byteCount > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16u ||
		(byteCount & 15u) != 0)
		return E_INVALIDARG;
	return Create(device, D3D11_BIND_CONSTANT_BUFFER, data, byteCount, 1,
		DXGI_FORMAT_UNKNOWN, dynamic);
}

HRESULT DX11Buffer::Update(ID3D11DeviceContext* context, const void* data,
	UINT byteCount)
{
	if (!context || !data || !m_buffer || !m_dynamic || byteCount != m_byteCount)
		return E_INVALIDARG;
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	const HRESULT hr = context->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD,
		0, &mapped);
	if (FAILED(hr))
		return hr;
	std::memcpy(mapped.pData, data, byteCount);
	context->Unmap(m_buffer.Get(), 0);
	return S_OK;
}

void DX11Buffer::BindVertex(ID3D11DeviceContext* context, UINT slot) const
{
	if (context && m_buffer && m_bind == D3D11_BIND_VERTEX_BUFFER)
	{
		ID3D11Buffer* buffer = m_buffer.Get();
		const UINT offset = 0;
		context->IASetVertexBuffers(slot, 1, &buffer, &m_stride, &offset);
	}
}

void DX11Buffer::BindIndex(ID3D11DeviceContext* context) const
{
	if (context && m_buffer && m_bind == D3D11_BIND_INDEX_BUFFER)
		context->IASetIndexBuffer(m_buffer.Get(), m_indexFormat, 0);
}

void DX11Buffer::BindVSConstant(ID3D11DeviceContext* context, UINT slot) const
{
	if (context && m_buffer && m_bind == D3D11_BIND_CONSTANT_BUFFER)
	{
		ID3D11Buffer* buffer = m_buffer.Get();
		context->VSSetConstantBuffers(slot, 1, &buffer);
	}
}

void DX11Buffer::BindPSConstant(ID3D11DeviceContext* context, UINT slot) const
{
	if (context && m_buffer && m_bind == D3D11_BIND_CONSTANT_BUFFER)
	{
		ID3D11Buffer* buffer = m_buffer.Get();
		context->PSSetConstantBuffers(slot, 1, &buffer);
	}
}

void DX11Buffer::Reset()
{
	m_buffer.Reset();
	m_bind = D3D11_BIND_VERTEX_BUFFER;
	m_indexFormat = DXGI_FORMAT_UNKNOWN;
	m_stride = 0;
	m_count = 0;
	m_byteCount = 0;
	m_dynamic = false;
}

HRESULT DX11Texture::CreateRGBA8(ID3D11Device* device, UINT width, UINT height,
	const void* pixels, UINT sourceRowPitch, bool dynamic)
{
	Reset();
	if (!device || width == 0 || height == 0 ||
		width > UINT_MAX / 4u ||
		sourceRowPitch < width * 4u || (!pixels && !dynamic))
		return E_INVALIDARG;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
	D3D11_SUBRESOURCE_DATA initial = {};
	initial.pSysMem = pixels;
	initial.SysMemPitch = sourceRowPitch;
	HRESULT hr = device->CreateTexture2D(&desc, pixels ? &initial : nullptr,
		m_texture.GetAddressOf());
	if (FAILED(hr))
		return hr;
	hr = device->CreateShaderResourceView(m_texture.Get(), nullptr,
		m_view.GetAddressOf());
	if (FAILED(hr))
	{
		Reset();
		return hr;
	}
	m_width = width;
	m_height = height;
	m_dynamic = dynamic;
	return S_OK;
}

HRESULT DX11Texture::UpdateRGBA8(ID3D11DeviceContext* context,
	const void* pixels, UINT sourceRowPitch)
{
	if (!context || !pixels || !m_dynamic || !m_texture ||
		sourceRowPitch < m_width * 4u)
		return E_INVALIDARG;
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	const HRESULT hr = context->Map(m_texture.Get(), 0, D3D11_MAP_WRITE_DISCARD,
		0, &mapped);
	if (FAILED(hr))
		return hr;
	for (UINT y = 0; y < m_height; ++y)
	{
		std::memcpy(static_cast<unsigned char*>(mapped.pData) +
			static_cast<size_t>(mapped.RowPitch) * y,
			static_cast<const unsigned char*>(pixels) +
			static_cast<size_t>(sourceRowPitch) * y, m_width * 4u);
	}
	context->Unmap(m_texture.Get(), 0);
	return S_OK;
}

void DX11Texture::BindPS(ID3D11DeviceContext* context, UINT slot) const
{
	if (context && m_view)
	{
		ID3D11ShaderResourceView* view = m_view.Get();
		context->PSSetShaderResources(slot, 1, &view);
	}
}

void DX11Texture::Reset()
{
	m_view.Reset();
	m_texture.Reset();
	m_width = 0;
	m_height = 0;
	m_dynamic = false;
}

HRESULT DX11InputLayout::Create(ID3D11Device* device,
	const DX11ShaderProgram& vertexShader,
	const D3D11_INPUT_ELEMENT_DESC* elements, UINT count)
{
	m_layout.Reset();
	if (!device || !elements || count == 0 ||
		vertexShader.stage != DX11ShaderStage::Vertex || !vertexShader.IsValid() ||
		vertexShader.bytecode.empty())
		return E_INVALIDARG;
	return device->CreateInputLayout(elements, count, vertexShader.bytecode.data(),
		vertexShader.bytecode.size(), m_layout.GetAddressOf());
}

void DX11InputLayout::Bind(ID3D11DeviceContext* context) const
{
	if (context && m_layout)
		context->IASetInputLayout(m_layout.Get());
}

HRESULT DX11RenderStates::Create(ID3D11Device* device)
{
	m_rasterizer.Reset();
	m_opaqueBlend.Reset();
	m_depthDisabled.Reset();
	m_linearClamp.Reset();
	if (!device)
		return E_INVALIDARG;

	D3D11_RASTERIZER_DESC rasterizer = {};
	rasterizer.FillMode = D3D11_FILL_SOLID;
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;
	HRESULT hr = device->CreateRasterizerState(&rasterizer,
		m_rasterizer.GetAddressOf());
	if (FAILED(hr)) return hr;

	D3D11_BLEND_DESC blend = {};
	blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blend.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
	blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	hr = device->CreateBlendState(&blend, m_opaqueBlend.GetAddressOf());
	if (FAILED(hr)) return hr;

	D3D11_DEPTH_STENCIL_DESC depth = {};
	depth.DepthEnable = FALSE;
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
	depth.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depth.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depth.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	depth.BackFace = depth.FrontFace;
	hr = device->CreateDepthStencilState(&depth, m_depthDisabled.GetAddressOf());
	if (FAILED(hr)) return hr;

	D3D11_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	return device->CreateSamplerState(&sampler, m_linearClamp.GetAddressOf());
}

void DX11RenderStates::BindOpaqueTwoSided(ID3D11DeviceContext* context) const
{
	if (!context || !m_rasterizer || !m_opaqueBlend ||
		!m_depthDisabled || !m_linearClamp)
		return;
	context->RSSetState(m_rasterizer.Get());
	const float blendFactor[4] = {};
	context->OMSetBlendState(m_opaqueBlend.Get(), blendFactor, 0xffffffff);
	context->OMSetDepthStencilState(m_depthDisabled.Get(), 0);
	ID3D11SamplerState* sampler = m_linearClamp.Get();
	context->PSSetSamplers(0, 1, &sampler);
}
}
