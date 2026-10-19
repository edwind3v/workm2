#include "StdAfx.h"
#include "DX11Device.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace graphics
{
DX11Device::~DX11Device()
{
	Shutdown();
}

bool DX11Device::Create(HWND window, UINT width, UINT height, bool enableDebugLayer)
{
	Shutdown();
	m_lastResult = E_INVALIDARG;
	if (!IsWindow(window) || width == 0 || height == 0)
		return false;

	m_window = window;
	const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT |
		(enableDebugLayer ? D3D11_CREATE_DEVICE_DEBUG : 0);
	const D3D_FEATURE_LEVEL requested[] = {
		D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0
	};

	m_lastResult = D3D11CreateDevice(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
		requested, ARRAYSIZE(requested), D3D11_SDK_VERSION,
		m_device.GetAddressOf(), &m_featureLevel, m_context.GetAddressOf());

	// Older D3D11 runtimes reject 11_1 in the feature-level list.
	if (m_lastResult == E_INVALIDARG)
	{
		m_lastResult = D3D11CreateDevice(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
			requested + 1, 1, D3D11_SDK_VERSION,
			m_device.GetAddressOf(), &m_featureLevel, m_context.GetAddressOf());
	}
	if (FAILED(m_lastResult))
	{
		Shutdown();
		return false;
	}

	Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
	Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
	Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
	m_lastResult = m_device.As(&dxgiDevice);
	if (SUCCEEDED(m_lastResult))
		m_lastResult = dxgiDevice->GetAdapter(adapter.GetAddressOf());
	if (SUCCEEDED(m_lastResult))
		m_lastResult = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
	if (FAILED(m_lastResult))
	{
		Shutdown();
		return false;
	}

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 2;
	desc.Scaling = DXGI_SCALING_STRETCH;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

	m_lastResult = factory->CreateSwapChainForHwnd(
		m_device.Get(), window, &desc, nullptr, nullptr,
		m_swapChain.GetAddressOf());
	if (FAILED(m_lastResult))
	{
		Shutdown();
		return false;
	}
	factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

	if (!CreateTargets(width, height))
	{
		Shutdown();
		return false;
	}
	return true;
}

bool DX11Device::CreateTargets(UINT width, UINT height)
{
	Microsoft::WRL::ComPtr<ID3D11Texture2D> color;
	m_lastResult = m_swapChain->GetBuffer(0, IID_PPV_ARGS(color.GetAddressOf()));
	if (FAILED(m_lastResult))
		return false;

	m_lastResult = m_device->CreateRenderTargetView(
		color.Get(), nullptr, m_colorTarget.GetAddressOf());
	if (FAILED(m_lastResult))
		return false;

	D3D11_TEXTURE2D_DESC depth = {};
	depth.Width = width;
	depth.Height = height;
	depth.MipLevels = 1;
	depth.ArraySize = 1;
	depth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth.SampleDesc.Count = 1;
	depth.Usage = D3D11_USAGE_DEFAULT;
	depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	m_lastResult = m_device->CreateTexture2D(
		&depth, nullptr, m_depthTexture.GetAddressOf());
	if (FAILED(m_lastResult))
		return false;

	m_lastResult = m_device->CreateDepthStencilView(
		m_depthTexture.Get(), nullptr, m_depthTarget.GetAddressOf());
	if (FAILED(m_lastResult))
		return false;

	m_width = width;
	m_height = height;
	return true;
}

void DX11Device::ReleaseTargets()
{
	m_depthTarget.Reset();
	m_depthTexture.Reset();
	m_colorTarget.Reset();
}

bool DX11Device::Resize(UINT width, UINT height)
{
	if (!m_swapChain || !m_context || width == 0 || height == 0)
	{
		m_lastResult = E_INVALIDARG;
		return false;
	}
	if (width == m_width && height == m_height)
		return true;

	m_context->OMSetRenderTargets(0, nullptr, nullptr);
	ReleaseTargets();
	m_context->Flush();
	m_lastResult = m_swapChain->ResizeBuffers(
		0, width, height, DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(m_lastResult))
		return false;
	return CreateTargets(width, height);
}

bool DX11Device::BeginFrame(const float clearColor[4])
{
	if (!m_context || !m_colorTarget || !m_depthTarget || !clearColor)
		return false;

	ID3D11RenderTargetView* target = m_colorTarget.Get();
	m_context->OMSetRenderTargets(1, &target, m_depthTarget.Get());
	D3D11_VIEWPORT viewport = {};
	viewport.Width = static_cast<float>(m_width);
	viewport.Height = static_cast<float>(m_height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_context->RSSetViewports(1, &viewport);
	m_context->ClearRenderTargetView(target, clearColor);
	m_context->ClearDepthStencilView(
		m_depthTarget.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	return true;
}

HRESULT DX11Device::Present(UINT syncInterval)
{
	if (!m_swapChain || syncInterval > 4)
		return m_lastResult = E_INVALIDARG;
	return m_lastResult = m_swapChain->Present(syncInterval, 0);
}

void DX11Device::Shutdown()
{
	if (m_context)
	{
		m_context->OMSetRenderTargets(0, nullptr, nullptr);
		m_context->ClearState();
		m_context->Flush();
	}
	ReleaseTargets();
	m_swapChain.Reset();
	m_context.Reset();
	m_device.Reset();
	m_window = nullptr;
	m_width = 0;
	m_height = 0;
}
}
