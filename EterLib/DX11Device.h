#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <climits>

// Independent D3D11 device lifecycle. The existing D3D9 client continues to
// run unchanged until its rendering paths have been ported explicitly.
namespace graphics
{
// The DXGI adapter actually backing the device, not an inferred GPU name.
struct DX11AdapterInfo
{
	wchar_t description[128] = {};
	LUID luid = {};
	UINT64 dedicatedVideoMemoryBytes = 0;
	UINT adapterIndex = UINT_MAX;
	UINT outputIndex = UINT_MAX;
	UINT outputCount = 0;
};

class DX11Device final
{
public:
	DX11Device() = default;
	~DX11Device();

	DX11Device(const DX11Device&) = delete;
	DX11Device& operator=(const DX11Device&) = delete;

	bool Create(HWND window, UINT width, UINT height, bool enableDebugLayer = false);
	// Render into a D3D11 texture without taking over the D3D9 game's HWND.
	// Useful while migrating individual render passes between APIs.
	bool CreateOffscreen(HWND window, UINT width, UINT height, bool enableDebugLayer = false);
	bool Resize(UINT width, UINT height);
	bool BeginFrame(const float clearColor[4]);
	HRESULT Present(UINT syncInterval = 0);
	void Shutdown();

	ID3D11Device* GetDevice() const { return m_device.Get(); }
	ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
	IDXGISwapChain1* GetSwapChain() const { return m_swapChain.Get(); }
	ID3D11RenderTargetView* GetRenderTarget() const { return m_colorTarget.Get(); }
	ID3D11Texture2D* GetColorTexture() const { return m_colorTexture.Get(); }
	ID3D11DepthStencilView* GetDepthTarget() const { return m_depthTarget.Get(); }
	const DX11AdapterInfo& GetAdapterInfo() const { return m_adapterInfo; }
	D3D_FEATURE_LEVEL GetFeatureLevel() const { return m_featureLevel; }
	HRESULT GetLastResult() const { return m_lastResult; }
	UINT GetWidth() const { return m_width; }
	UINT GetHeight() const { return m_height; }

private:
	bool CreateHardwareDevice(HWND window, bool enableDebugLayer);
	bool CreateTargets(UINT width, UINT height);
	void ReleaseTargets();

	HWND m_window = nullptr;
	UINT m_width = 0;
	UINT m_height = 0;
	D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;
	HRESULT m_lastResult = S_OK;
	DX11AdapterInfo m_adapterInfo;

	Microsoft::WRL::ComPtr<ID3D11Device> m_device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_colorTexture;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_colorTarget;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthTexture;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthTarget;
};
}
