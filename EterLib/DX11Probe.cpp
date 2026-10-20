#include "StdAfx.h"
#include "DX11Probe.h"
#include "DX11ShaderLibrary.h"

#include <d3dcompiler.h>
#include <cstring>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace graphics
{
namespace
{
constexpr UINT kProbeSize = 256;

// Independent, non-private Shader Model 5 program. The vertex ID drives a
// full-screen triangle; the pixel shader samples a D3D11 texture/SRV.
constexpr char kProbeHLSL[] = R"(
Texture2D spriteTexture : register(t0);
SamplerState spriteSampler : register(s0);
struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VSOutput VSMain(uint vertexID : SV_VertexID)
{
    float2 xy[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    VSOutput result;
    result.position = float4(xy[vertexID], 0, 1);
    result.uv = xy[vertexID] * 0.5 + 0.5;
    return result;
}
float4 PSMain(VSOutput input) : SV_Target
{
    return spriteTexture.Sample(spriteSampler, input.uv);
}
)";

HRESULT WriteBitmap(const wchar_t* path, const unsigned char* rgba,
	UINT rowPitch, UINT width, UINT height)
{
	if (!path || !*path)
		return E_INVALIDARG;

	const DWORD rowSize = width * 4;
	std::vector<unsigned char> bgra(static_cast<size_t>(rowSize) * height);
	for (UINT y = 0; y < height; ++y)
	{
		const unsigned char* src = rgba + static_cast<size_t>(rowPitch) * y;
		unsigned char* dst = bgra.data() + static_cast<size_t>(rowSize) * y;
		for (UINT x = 0; x < width; ++x)
		{
			dst[x * 4 + 0] = src[x * 4 + 2];
			dst[x * 4 + 1] = src[x * 4 + 1];
			dst[x * 4 + 2] = src[x * 4 + 0];
			dst[x * 4 + 3] = 255;
		}
	}

	BITMAPFILEHEADER fileHeader = {};
	BITMAPINFOHEADER imageHeader = {};
	imageHeader.biSize = sizeof(imageHeader);
	imageHeader.biWidth = static_cast<LONG>(width);
	imageHeader.biHeight = -static_cast<LONG>(height); // top-down, as read from D3D11
	imageHeader.biPlanes = 1;
	imageHeader.biBitCount = 32;
	imageHeader.biCompression = BI_RGB;
	imageHeader.biSizeImage = static_cast<DWORD>(bgra.size());
	fileHeader.bfType = 0x4d42;
	fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(imageHeader);
	fileHeader.bfSize = fileHeader.bfOffBits + imageHeader.biSizeImage;

	HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return HRESULT_FROM_WIN32(GetLastError());
	DWORD written = 0;
	const bool success =
		WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr) && written == sizeof(fileHeader) &&
		WriteFile(file, &imageHeader, sizeof(imageHeader), &written, nullptr) && written == sizeof(imageHeader) &&
		WriteFile(file, bgra.data(), static_cast<DWORD>(bgra.size()), &written, nullptr) && written == bgra.size();
	const DWORD error = success ? ERROR_SUCCESS : GetLastError();
	CloseHandle(file);
	return success ? S_OK : HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : error);
}
}

HRESULT RunDX11Probe(HWND gameWindow, const wchar_t* bitmapPath, DX11ProbeResult* result)
{
	if (!result)
		return E_POINTER;
	*result = DX11ProbeResult();

	DX11Device renderer;
	if (!renderer.CreateOffscreen(gameWindow, kProbeSize, kProbeSize))
		return renderer.GetLastResult();
	result->adapter = renderer.GetAdapterInfo();
	result->featureLevel = renderer.GetFeatureLevel();
	ID3D11Device* device = renderer.GetDevice();
	ID3D11DeviceContext* context = renderer.GetContext();

	result->stage = "CompileVS";
	Microsoft::WRL::ComPtr<ID3DBlob> vertexCode;
	HRESULT hr = D3DCompile(kProbeHLSL, sizeof(kProbeHLSL) - 1, nullptr,
		nullptr, nullptr, "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3,
		0, vertexCode.GetAddressOf(), nullptr);
	if (FAILED(hr))
		return hr;

	result->stage = "CompilePS";
	Microsoft::WRL::ComPtr<ID3DBlob> pixelCode;
	hr = D3DCompile(kProbeHLSL, sizeof(kProbeHLSL) - 1, nullptr,
		nullptr, nullptr, "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3,
		0, pixelCode.GetAddressOf(), nullptr);
	if (FAILED(hr))
		return hr;

	result->stage = "CreateShaders";
	DX11ShaderProgram vertexShader;
	DX11ShaderProgram pixelShader;
	hr = DX11ShaderLibrary::CreateFromMemory(device, DX11ShaderStage::Vertex,
		vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), &vertexShader);
	if (FAILED(hr))
		return hr;
	hr = DX11ShaderLibrary::CreateFromMemory(device, DX11ShaderStage::Pixel,
		pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), &pixelShader);
	if (FAILED(hr))
		return hr;

	result->stage = "CreateTexture";
	std::vector<unsigned char> pixels(static_cast<size_t>(kProbeSize) * kProbeSize * 4);
	for (UINT y = 0; y < kProbeSize; ++y)
	{
		for (UINT x = 0; x < kProbeSize; ++x)
		{
			unsigned char* pixel = pixels.data() + (static_cast<size_t>(y) * kProbeSize + x) * 4;
			pixel[0] = static_cast<unsigned char>(x);
			pixel[1] = static_cast<unsigned char>(y);
			pixel[2] = 64;
			pixel[3] = 255;
		}
	}
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width = kProbeSize;
	textureDesc.Height = kProbeSize;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA initialData = {};
	initialData.pSysMem = pixels.data();
	initialData.SysMemPitch = kProbeSize * 4;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> spriteTexture;
	hr = device->CreateTexture2D(&textureDesc, &initialData, spriteTexture.GetAddressOf());
	if (FAILED(hr))
		return hr;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> spriteView;
	hr = device->CreateShaderResourceView(spriteTexture.Get(), nullptr, spriteView.GetAddressOf());
	if (FAILED(hr))
		return hr;
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	Microsoft::WRL::ComPtr<ID3D11SamplerState> spriteSampler;
	hr = device->CreateSamplerState(&samplerDesc, spriteSampler.GetAddressOf());
	if (FAILED(hr))
		return hr;

	result->stage = "Draw";
	const float background[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	if (!renderer.BeginFrame(background))
		return E_FAIL;
	D3D11_RASTERIZER_DESC rasterDesc = {};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.DepthClipEnable = TRUE;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer;
	hr = device->CreateRasterizerState(&rasterDesc, rasterizer.GetAddressOf());
	if (FAILED(hr))
		return hr;
	context->RSSetState(rasterizer.Get());
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	vertexShader.Bind(context);
	pixelShader.Bind(context);
	ID3D11ShaderResourceView* srv = spriteView.Get();
	ID3D11SamplerState* sampler = spriteSampler.Get();
	context->PSSetShaderResources(0, 1, &srv);
	context->PSSetSamplers(0, 1, &sampler);
	context->Draw(3, 0);

	result->stage = "Readback";
	D3D11_TEXTURE2D_DESC stagingDesc = {};
	renderer.GetColorTexture()->GetDesc(&stagingDesc);
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
	hr = device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf());
	if (FAILED(hr))
		return hr;
	context->OMSetRenderTargets(0, nullptr, nullptr);
	context->CopyResource(staging.Get(), renderer.GetColorTexture());
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(hr))
		return hr;
	const unsigned char* rgba = static_cast<const unsigned char*>(mapped.pData);
	const unsigned char* center = rgba + static_cast<size_t>(mapped.RowPitch) * (kProbeSize / 2) + (kProbeSize / 2) * 4;
	std::memcpy(result->centerPixelRGBA, center, 4);
	if (bitmapPath && *bitmapPath)
		result->bitmapSaved = SUCCEEDED(WriteBitmap(bitmapPath, rgba,
			mapped.RowPitch, kProbeSize, kProbeSize));
	context->Unmap(staging.Get(), 0);

	result->stage = "ValidatePixel";
	if (result->centerPixelRGBA[0] < 110 || result->centerPixelRGBA[0] > 145 ||
		result->centerPixelRGBA[1] < 110 || result->centerPixelRGBA[1] > 145 ||
		result->centerPixelRGBA[2] < 55 || result->centerPixelRGBA[2] > 75 ||
		result->centerPixelRGBA[3] != 255)
		return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
	result->stage = "Complete";
	return S_OK;
}
}
