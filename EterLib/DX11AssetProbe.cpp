#include "StdAfx.h"
#include "DX11Probe.h"
#include "StateManager.h"
#include "DX11Resources.h"
#include "DX11ShaderLibrary.h"

#include "../eterBase/MappedFile.h"
#include "../EterPack/EterPackManager.h"

#include <d3dcompiler.h>
#include <cstring>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace graphics
{
namespace
{
constexpr UINT kPreviewSize = 256;
constexpr UINT kMaxSourceDimension = 4096;

constexpr char kAssetShader[] = R"(
cbuffer ImageSize : register(b0) { float4 screenScale; };
Texture2D asset : register(t0);
SamplerState linearClamp : register(s0);
struct VSInput { float2 xy : POSITION; float2 uv : TEXCOORD0; };
struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VSOutput VSMain(VSInput input)
{
    VSOutput result;
    result.position = float4(input.xy * screenScale.xy, 0.5, 1);
    result.uv = input.uv;
    return result;
}
float4 PSMain(VSOutput input) : SV_Target
{
    return asset.Sample(linearClamp, input.uv);
}
)";

struct AssetVertex
{
	float x, y, u, v;
};

// Output DX11's RGBA8 render texture as a simple top-down BMP preview.
HRESULT SavePreview(const wchar_t* path, const unsigned char* rgba,
	UINT rowPitch, UINT width, UINT height)
{
	if (!path || !*path || !rgba)
		return E_INVALIDARG;
	const UINT rowBytes = width * 4u;
	std::vector<unsigned char> bgra(static_cast<size_t>(rowBytes) * height);
	for (UINT y = 0; y < height; ++y)
	{
		const unsigned char* src = rgba + static_cast<size_t>(y) * rowPitch;
		unsigned char* dst = bgra.data() + static_cast<size_t>(y) * rowBytes;
		for (UINT x = 0; x < width; ++x)
		{
			dst[x * 4 + 0] = src[x * 4 + 2];
			dst[x * 4 + 1] = src[x * 4 + 1];
			dst[x * 4 + 2] = src[x * 4 + 0];
			dst[x * 4 + 3] = 255;
		}
	}
	BITMAPINFOHEADER info = {};
	info.biSize = sizeof(info);
	info.biWidth = static_cast<LONG>(width);
	info.biHeight = -static_cast<LONG>(height);
	info.biPlanes = 1;
	info.biBitCount = 32;
	info.biCompression = BI_RGB;
	info.biSizeImage = static_cast<DWORD>(bgra.size());
	BITMAPFILEHEADER fileHeader = {};
	fileHeader.bfType = 0x4d42;
	fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(info);
	fileHeader.bfSize = fileHeader.bfOffBits + info.biSizeImage;

	HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return HRESULT_FROM_WIN32(GetLastError());
	DWORD written = 0;
	const bool okay =
		WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr) && written == sizeof(fileHeader) &&
		WriteFile(file, &info, sizeof(info), &written, nullptr) && written == sizeof(info) &&
		WriteFile(file, bgra.data(), static_cast<DWORD>(bgra.size()), &written, nullptr) && written == bgra.size();
	const DWORD error = okay ? ERROR_SUCCESS : GetLastError();
	CloseHandle(file);
	return okay ? S_OK : HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : error);
}

// Cross-API sharing is deliberately performed only once. The D3D9Ex texture
// owns the shared allocation; the D3D11 device can then be destroyed without
// affecting subsequent D3D9 UI draws. The APIs must use the same GPU adapter.
HRESULT RenderSharedPreview(IDirect3DDevice9Ex* gameDevice, DX11Device& renderer,
	UINT indexCount, DX11SharedProbeResult* result)
{
	result->stage = "CreateD3D9SharedTexture";
	Microsoft::WRL::ComPtr<IDirect3DTexture9> shared9;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> shared11;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> sharedRTV;
	D3DFORMAT chosenFormat = D3DFMT_UNKNOWN;
	HRESULT hr = E_FAIL;
	// Prefer RGBA (the specifically documented D3D9Ex/D3D11 shared
	// format). Some drivers offer only the equivalent BGRA render target.
	const D3DFORMAT candidates[] = { D3DFMT_A8B8G8R8, D3DFMT_A8R8G8B8 };
	for (UINT i = 0; i < ARRAYSIZE(candidates); ++i)
	{
		shared9.Reset();
		shared11.Reset();
		sharedRTV.Reset();
		HANDLE sharedHandle = nullptr;
		result->stage = "CreateD3D9SharedTexture";
		hr = gameDevice->CreateTexture(kPreviewSize, kPreviewSize, 1,
			D3DUSAGE_RENDERTARGET, candidates[i], D3DPOOL_DEFAULT,
			shared9.GetAddressOf(), &sharedHandle);
		if (FAILED(hr)) continue;
		if (!sharedHandle) { hr = E_FAIL; continue; }

		result->stage = "OpenD3D11SharedTexture";
		hr = renderer.GetDevice()->OpenSharedResource(sharedHandle,
			IID_PPV_ARGS(shared11.GetAddressOf()));
		if (FAILED(hr)) continue;

		result->stage = "CreateSharedRenderTarget";
		hr = renderer.GetDevice()->CreateRenderTargetView(
			shared11.Get(), nullptr, sharedRTV.GetAddressOf());
		if (FAILED(hr)) continue;
		chosenFormat = candidates[i];
		result->format = i == 0 ? "A8B8G8R8" : "A8R8G8B8";
		break;
	}
	if (chosenFormat == D3DFMT_UNKNOWN) return hr;

	result->stage = "DrawSharedTexture";
	ID3D11DeviceContext* context = renderer.GetContext();
	ID3D11RenderTargetView* target = sharedRTV.Get();
	context->OMSetRenderTargets(1, &target, nullptr);
	const float transparentBlack[4] = { 0, 0, 0, 0 };
	context->ClearRenderTargetView(target, transparentBlack);
	context->DrawIndexed(indexCount, 0, 0);
	context->OMSetRenderTargets(0, nullptr, nullptr);
	// Required by D3D9Ex / D3D11 shared surface interoperability.
	context->Flush();

	// A D3D9-side readback proves the *other* API can consume DX11's work.
	// GetRenderTargetData also waits for the GPU copy before LockRect returns.
	result->stage = "ReadSharedPixelsWithD3D9";
	Microsoft::WRL::ComPtr<IDirect3DSurface9> sharedSurface;
	hr = shared9->GetSurfaceLevel(0, sharedSurface.GetAddressOf());
	if (FAILED(hr)) return hr;
	Microsoft::WRL::ComPtr<IDirect3DSurface9> readback;
	hr = gameDevice->CreateOffscreenPlainSurface(kPreviewSize, kPreviewSize,
		chosenFormat, D3DPOOL_SYSTEMMEM, readback.GetAddressOf(), nullptr);
	if (FAILED(hr)) return hr;
	hr = gameDevice->GetRenderTargetData(sharedSurface.Get(), readback.Get());
	if (FAILED(hr)) return hr;
	D3DLOCKED_RECT locked = {};
	hr = readback->LockRect(&locked, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr)) return hr;
	const unsigned char* center = static_cast<const unsigned char*>(locked.pBits) +
		static_cast<size_t>(locked.Pitch) * (kPreviewSize / 2) + (kPreviewSize / 2) * 4;
	result->centerPixelRGBA[0] = center[chosenFormat == D3DFMT_A8R8G8B8 ? 2 : 0];
	result->centerPixelRGBA[1] = center[1];
	result->centerPixelRGBA[2] = center[chosenFormat == D3DFMT_A8R8G8B8 ? 0 : 2];
	result->centerPixelRGBA[3] = center[3];
	readback->UnlockRect();
	result->texture = shared9.Detach();
	result->stage = "Complete";
	return S_OK;
}
}

HRESULT RunDX11AssetProbe(HWND gameWindow, IDirect3DDevice9* imageDecodeDevice,
	const char* packPath, const wchar_t* bitmapPath, DX11ProbeResult* result,
	IDirect3DDevice9Ex* sharedDevice, DX11SharedProbeResult* sharedResult)
{
	if (!result)
		return E_POINTER;
	*result = DX11ProbeResult();
	if (sharedResult)
		*sharedResult = DX11SharedProbeResult();
	if (!imageDecodeDevice || !packPath || !*packPath || !IsWindow(gameWindow))
		return E_INVALIDARG;

	result->stage = "LoadPackAsset";
	CMappedFile mappedFile;
	LPCVOID encodedBytes = nullptr;
	if (!CEterPackManager::Instance().Get(mappedFile, packPath, &encodedBytes) ||
		!encodedBytes || mappedFile.Size() == 0)
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

	result->stage = "InspectAssetImage";
	D3DXIMAGE_INFO imageInfo = {};
	const UINT encodedSize = mappedFile.Size();
	HRESULT hr = D3DXGetImageInfoFromFileInMemory(encodedBytes, encodedSize, &imageInfo);
	if (FAILED(hr))
		return hr;
	if (!imageInfo.Width || !imageInfo.Height ||
		imageInfo.Width > kMaxSourceDimension ||
		imageInfo.Height > kMaxSourceDimension)
		return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
	result->sourceWidth = imageInfo.Width;
	result->sourceHeight = imageInfo.Height;

	// This is an interim image-format bridge, not the final DX11 asset loader.
	// It decodes a packed TGA/DDS/etc. into SYSTEMMEM A8R8G8B8 without reading
	// from a D3D9 GPU texture or modifying any existing game resource.
	result->stage = "DecodeAsset";
	Microsoft::WRL::ComPtr<IDirect3DTexture9> decoded;
	hr = D3DXCreateTextureFromFileInMemoryEx(imageDecodeDevice,
		encodedBytes, encodedSize, imageInfo.Width, imageInfo.Height, 1,
		0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
		D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0,
		nullptr, nullptr, decoded.GetAddressOf());
	if (FAILED(hr))
		return hr;
	result->stage = "ReadDecodedPixels";
	D3DLOCKED_RECT locked = {};
	hr = decoded->LockRect(0, &locked, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr))
		return hr;
	const UINT width = imageInfo.Width;
	const UINT height = imageInfo.Height;
	const UINT rowBytes = width * 4u;
	std::vector<unsigned char> rgba(static_cast<size_t>(rowBytes) * height);
	for (UINT y = 0; y < height; ++y)
	{
		const unsigned char* bgra = static_cast<const unsigned char*>(locked.pBits) +
			static_cast<size_t>(locked.Pitch) * y;
		unsigned char* dst = rgba.data() + static_cast<size_t>(rowBytes) * y;
		for (UINT x = 0; x < width; ++x)
		{
			dst[x * 4 + 0] = bgra[x * 4 + 2];
			dst[x * 4 + 1] = bgra[x * 4 + 1];
			dst[x * 4 + 2] = bgra[x * 4 + 0];
			dst[x * 4 + 3] = bgra[x * 4 + 3];
		}
	}
	decoded->UnlockRect(0);
	decoded.Reset();

	result->stage = "CreateAssetDevice";
	DX11Device renderer;
	if (!renderer.CreateOffscreen(gameWindow, kPreviewSize, kPreviewSize))
		return renderer.GetLastResult();
	result->adapter = renderer.GetAdapterInfo();
	result->featureLevel = renderer.GetFeatureLevel();
	ID3D11Device* device = renderer.GetDevice();
	ID3D11DeviceContext* context = renderer.GetContext();

	result->stage = "CompileAssetShaders";
	Microsoft::WRL::ComPtr<ID3DBlob> vsBytecode;
	Microsoft::WRL::ComPtr<ID3DBlob> psBytecode;
	hr = D3DCompile(kAssetShader, sizeof(kAssetShader) - 1, nullptr,
		nullptr, nullptr, "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3,
		0, vsBytecode.GetAddressOf(), nullptr);
	if (FAILED(hr)) return hr;
	hr = D3DCompile(kAssetShader, sizeof(kAssetShader) - 1, nullptr,
		nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3,
		0, psBytecode.GetAddressOf(), nullptr);
	if (FAILED(hr)) return hr;

	DX11ShaderProgram vs;
	DX11ShaderProgram ps;
	hr = DX11ShaderLibrary::CreateFromMemory(device, DX11ShaderStage::Vertex,
		vsBytecode->GetBufferPointer(), vsBytecode->GetBufferSize(), &vs);
	if (FAILED(hr)) return hr;
	hr = DX11ShaderLibrary::CreateFromMemory(device, DX11ShaderStage::Pixel,
		psBytecode->GetBufferPointer(), psBytecode->GetBufferSize(), &ps);
	if (FAILED(hr)) return hr;

	result->stage = "CreateAssetGeometry";
	const D3D11_INPUT_ELEMENT_DESC elements[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	DX11InputLayout layout;
	hr = layout.Create(device, vs, elements, ARRAYSIZE(elements));
	if (FAILED(hr)) return hr;
	const AssetVertex vertices[] = {
		{ -1, -1, 0, 1 }, { 1, -1, 1, 1 },
		{  1,  1, 1, 0 }, { -1, 1, 0, 0 }
	};
	const unsigned short indices[] = { 0, 1, 2, 0, 2, 3 };
	DX11Buffer vertexBuffer;
	DX11Buffer indexBuffer;
	DX11Buffer scaleConstants;
	hr = vertexBuffer.CreateVertex(device, vertices, sizeof(AssetVertex),
		ARRAYSIZE(vertices));
	if (FAILED(hr)) return hr;
	hr = indexBuffer.CreateIndex(device, indices, DXGI_FORMAT_R16_UINT,
		ARRAYSIZE(indices));
	if (FAILED(hr)) return hr;
	float scale[4] = { 0.85f, 0.85f, 0, 0 };
	if (width > height)
		scale[1] *= static_cast<float>(height) / width;
	else
		scale[0] *= static_cast<float>(width) / height;
	hr = scaleConstants.CreateConstant(device, scale, sizeof(scale));
	if (FAILED(hr)) return hr;
	DX11Texture assetTexture;
	hr = assetTexture.CreateRGBA8(device, width, height, rgba.data(), rowBytes);
	if (FAILED(hr)) return hr;
	DX11RenderStates states;
	hr = states.Create(device);
	if (FAILED(hr)) return hr;

	result->stage = "DrawPackedAsset";
	const float background[4] = { 0, 0, 0, 1 };
	if (!renderer.BeginFrame(background)) return E_FAIL;
	states.BindOpaqueTwoSided(context);
	layout.Bind(context);
	vertexBuffer.BindVertex(context);
	indexBuffer.BindIndex(context);
	scaleConstants.BindVSConstant(context);
	assetTexture.BindPS(context);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	vs.Bind(context);
	ps.Bind(context);
	context->DrawIndexed(indexBuffer.GetCount(), 0, 0);
	if (sharedResult)
	{
		if (sharedDevice)
		{
			sharedResult->status = RenderSharedPreview(sharedDevice, renderer,
				indexBuffer.GetCount(), sharedResult);
			// Keep the regular BMP/readback checkpoint independent of any
			// cross-API support failure.
			ID3D11RenderTargetView* originalTarget = renderer.GetRenderTarget();
			context->OMSetRenderTargets(1, &originalTarget, renderer.GetDepthTarget());
		}
	}

	result->stage = "ReadbackPackedAsset";
	D3D11_TEXTURE2D_DESC stagingDesc = {};
	renderer.GetColorTexture()->GetDesc(&stagingDesc);
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
	hr = device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf());
	if (FAILED(hr)) return hr;
	context->OMSetRenderTargets(0, nullptr, nullptr);
	context->CopyResource(staging.Get(), renderer.GetColorTexture());
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(hr)) return hr;
	const unsigned char* preview = static_cast<const unsigned char*>(mapped.pData);
	const unsigned char* center = preview +
		static_cast<size_t>(mapped.RowPitch) * (kPreviewSize / 2) + (kPreviewSize / 2) * 4;
	std::memcpy(result->centerPixelRGBA, center, 4);
	const bool borderIsClear = preview[0] == 0 && preview[1] == 0 &&
		preview[2] == 0 && preview[3] == 255;
	if (bitmapPath && *bitmapPath)
		result->bitmapSaved = SUCCEEDED(SavePreview(bitmapPath, preview,
			mapped.RowPitch, kPreviewSize, kPreviewSize));
	context->Unmap(staging.Get(), 0);
	result->stage = "ValidatePreview";
	if (!borderIsClear)
		return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
	result->stage = "Complete";
	return S_OK;
}

HRESULT DrawDX11SharedPreview(IDirect3DDevice9Ex* gameDevice,
	IDirect3DTexture9* sharedTexture, int top, int size)
{
	if (!gameDevice || !sharedTexture || top < 0 || size <= 0)
		return E_INVALIDARG;
	D3DVIEWPORT9 viewport = {};
	HRESULT hr = gameDevice->GetViewport(&viewport);
	if (FAILED(hr)) return hr;
	if (viewport.Width < static_cast<DWORD>(size + 32) ||
		viewport.Height < static_cast<DWORD>(top + size + 16))
		return S_FALSE;

	Microsoft::WRL::ComPtr<IDirect3DStateBlock9> saved;
	hr = gameDevice->CreateStateBlock(D3DSBT_ALL, saved.GetAddressOf());
	if (FAILED(hr)) return hr;
	hr = saved->Capture();
	if (FAILED(hr)) return hr;

	struct ScreenVertex { float x, y, z, rhw; DWORD color; float u, v; };
	const float left = static_cast<float>(viewport.X + viewport.Width - size - 16) - 0.5f;
	const float upper = static_cast<float>(viewport.Y + top) - 0.5f;
	const ScreenVertex vertices[4] = {
		{ left,        upper,        0, 1, 0xffffffff, 0, 0 },
		{ left + size, upper,        0, 1, 0xffffffff, 1, 0 },
		{ left,        upper + size, 0, 1, 0xffffffff, 0, 1 },
		{ left + size, upper + size, 0, 1, 0xffffffff, 1, 1 }
	};
	gameDevice->SetVertexShader(nullptr);
	gameDevice->SetPixelShader(nullptr);
	gameDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	gameDevice->SetTexture(0, sharedTexture);
	gameDevice->SetTexture(1, nullptr);
	gameDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
	gameDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	gameDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	gameDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
	gameDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	gameDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	gameDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	gameDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	gameDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	gameDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
	gameDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	gameDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	gameDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	gameDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	gameDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	gameDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	gameDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	gameDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	gameDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	gameDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	gameDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	hr = gameDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2,
		vertices, sizeof(ScreenVertex));
	const HRESULT restore = saved->Apply();
    // A failed restore leaves native shader constants unknown to our cache.
    if (FAILED(restore)) STATEMANAGER.InvalidateShaderConstants();
	return FAILED(hr) ? hr : restore;
}
}
