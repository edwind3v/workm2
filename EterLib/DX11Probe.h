#pragma once

#include "DX11Device.h"

struct IDirect3DDevice9;

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

HRESULT RunDX11Probe(HWND gameWindow, const wchar_t* bitmapPath, DX11ProbeResult* result);

// Exercises reusable vertex/index/constant buffers, input layouts, textures
// and explicit D3D11 pipeline states with an indexed textured quad.
HRESULT RunDX11MeshProbe(HWND gameWindow, const wchar_t* bitmapPath, DX11ProbeResult* result);

// Loads a genuine texture from the running client's pack manager, decodes it
// through the existing D3DX9 image loader into CPU-accessible memory, then
// uploads that data to a separate D3D11 device. Diagnostic only.
HRESULT RunDX11AssetProbe(HWND gameWindow, IDirect3DDevice9* imageDecodeDevice,
	const char* packPath, const wchar_t* bitmapPath, DX11ProbeResult* result);
}
