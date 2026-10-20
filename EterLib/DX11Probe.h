#pragma once

#include "DX11Device.h"

namespace graphics
{
// An opt-in real GPU draw/readback inside the running game process.
// It never presents over the still-D3D9-powered game window.
struct DX11ProbeResult
{
	DX11AdapterInfo adapter;
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	unsigned char centerPixelRGBA[4] = {};
	bool bitmapSaved = false;
	const char* stage = "CreateDevice";
};

HRESULT RunDX11Probe(HWND gameWindow, const wchar_t* bitmapPath, DX11ProbeResult* result);
}
