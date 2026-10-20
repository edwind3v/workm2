#pragma once

#include "DX11Device.h"

struct IDirect3DDevice9;
struct IDirect3DDevice9Ex;
struct IDirect3DTexture9;

namespace graphics
{
// An opt-in real GPU draw/readback inside the running game process.
// It never presents over the still-D3D9-powered game window.
struct DX11ProbeResult
{
	DX11AdapterInfo adapter;
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	unsigned char centerPixelRGBA[4] = {};
	UINT sourceWidth = 0;
	UINT sourceHeight = 0;
	bool bitmapSaved = false;
	const char* stage = "CreateDevice";
};

// Optional D3D11 -> D3D9Ex interoperability checkpoint. The caller owns the
// returned texture reference and must release it before a D3D9 device reset.
struct DX11SharedProbeResult
{
	const char* stage = "NotAttempted";
	const char* format = "none";
	HRESULT status = S_FALSE;
	unsigned char centerPixelRGBA[4] = {};
	IDirect3DTexture9* texture = nullptr;
};

HRESULT RunDX11Probe(HWND gameWindow, const wchar_t* bitmapPath, DX11ProbeResult* result);

// Exercises reusable vertex/index/constant buffers, input layouts, textures
// and explicit D3D11 pipeline states with an indexed textured quad.
HRESULT RunDX11MeshProbe(HWND gameWindow, const wchar_t* bitmapPath, DX11ProbeResult* result);

// Loads a genuine texture from the running client's pack manager, decodes it
// through the existing D3DX9 image loader into CPU-accessible memory, then
// uploads that data to a separate D3D11 device. Diagnostic only.
HRESULT RunDX11AssetProbe(HWND gameWindow, IDirect3DDevice9* imageDecodeDevice,
	const char* packPath, const wchar_t* bitmapPath, DX11ProbeResult* result,
	IDirect3DDevice9Ex* sharedDevice = nullptr,
	DX11SharedProbeResult* sharedResult = nullptr);

// Display a texture that DX11 rendered, through the existing D3D9Ex UI pass.
// This is a temporary visual bridge, not a DX11 game backbuffer/present path.
HRESULT DrawDX11SharedPreview(IDirect3DDevice9Ex* gameDevice,
	IDirect3DTexture9* sharedTexture);
}
