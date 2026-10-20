#include "StdAfx.h"
#include "DX11GrannyProbe.h"
#include "DX11Device.h"
#include "DX11Resources.h"
#include "DX11ShaderLibrary.h"

#include "../eterBase/MappedFile.h"
#include "../EterPack/EterPackManager.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <utility>

#pragma comment(lib, "d3dcompiler.lib")

namespace graphics
{
namespace
{
constexpr UINT kPreviewSize = 256;
constexpr UINT kMaxVertices = 30000;
constexpr UINT kMaxIndices = 90000;
constexpr UINT kMaxMaterialGroups = 64;
constexpr UINT kMaxTextureSize = 4096;
constexpr size_t kTextureBudgetBytes = 64u * 1024u * 1024u;

DX11GrannySnapshot& PendingSnapshot()
{
	static DX11GrannySnapshot snapshot;
	return snapshot;
}

bool& SnapshotTaken()
{
	static bool taken = false;
	return taken;
}

constexpr char kModelPreviewShader[] = R"(
cbuffer ObjectTransform : register(b0)
{
    row_major float4x4 viewProjection;
};
cbuffer ModelParamsPS : register(b1)
{
    // X: diffuse is bound, Y: opacity mask is bound.
    float4 materialFlags;
};
Texture2D Diffuse : register(t0);
Texture2D Opacity : register(t1);
SamplerState DiffSamp : register(s0);
struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};
struct VSOutput
{
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};
VSOutput VSMain(VSInput input)
{
    VSOutput result;
    result.position = mul(float4(input.position, 1.0), viewProjection);
    result.normal = input.normal;
    result.uv = input.uv;
    return result;
}
float4 PSMain(VSOutput input) : SV_Target
{
    float light = 0.32 + 0.68 * saturate(dot(normalize(input.normal),
                                                   normalize(float3(0.4, 0.3, 0.85))));
    // Diffuse is an actual CGrannyMaterial texture, decoded from the pack.
    // Missing textures retain the earlier cyan diagnostic material.
    float3 albedo = float3(0.25, 0.74, 0.94);
    if (materialFlags.x > 0.5)
    {
        float4 texel = Diffuse.Sample(DiffSamp, input.uv);
        clip(texel.a - 0.05);
        albedo = texel.rgb;
    }
    else
        albedo *= 0.88 + 0.12 * sin(input.uv.x * 30.0) * sin(input.uv.y * 30.0);
    if (materialFlags.y > 0.5)
        clip(Opacity.Sample(DiffSamp, input.uv).r - 0.05);
    return float4(albedo * light, 1.0);
}
)";

struct ModelPipeline
{
	DX11ShaderProgram vs;
	DX11ShaderProgram ps;
	DX11Buffer vb;
	DX11Buffer ib;
	DX11Buffer cb;
	DX11Buffer materialFlags;
	DX11InputLayout layout;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer;
	Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
};

struct ModelMaterial
{
	const DX11Texture* diffuse = nullptr;
	const DX11Texture* opacity = nullptr;
};

// Temporary pack-image decode bridge, independent of the D3D9 GPU texture.
// The material image path comes from the current Granny instance, including
// armour/material overrides applied by the game. All work is one-shot.
HRESULT LoadPackedMaterial(IDirect3DDevice9* decodeDevice, ID3D11Device* device,
	const std::string& path, size_t* budget, DX11Texture* output)
{
	if (!decodeDevice || !device || !budget || !output || path.empty())
		return E_INVALIDARG;
	CMappedFile mapped;
	LPCVOID packedBytes = nullptr;
	if (!CEterPackManager::Instance().Get(mapped, path.c_str(), &packedBytes) ||
		!packedBytes || !mapped.Size())
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	D3DXIMAGE_INFO info = {};
	HRESULT hr = D3DXGetImageInfoFromFileInMemory(packedBytes, mapped.Size(), &info);
	if (FAILED(hr)) return hr;
	if (!info.Width || !info.Height || info.Width > kMaxTextureSize ||
		info.Height > kMaxTextureSize)
		return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
	const size_t imageBytes = static_cast<size_t>(info.Width) * info.Height * 4u;
	if (imageBytes > *budget)
		return E_OUTOFMEMORY;
	Microsoft::WRL::ComPtr<IDirect3DTexture9> decoded;
	hr = D3DXCreateTextureFromFileInMemoryEx(decodeDevice,
		packedBytes, mapped.Size(), info.Width, info.Height, 1,
		0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
		D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0,
		nullptr, nullptr, decoded.GetAddressOf());
	if (FAILED(hr)) return hr;
	D3DLOCKED_RECT locked = {};
	hr = decoded->LockRect(0, &locked, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr)) return hr;
	const UINT rowBytes = info.Width * 4u;
	std::vector<unsigned char> rgba(imageBytes);
	for (UINT y = 0; y < info.Height; ++y)
	{
		const unsigned char* bgra = static_cast<const unsigned char*>(locked.pBits) +
			static_cast<size_t>(locked.Pitch) * y;
		unsigned char* row = rgba.data() + static_cast<size_t>(rowBytes) * y;
		for (UINT x = 0; x < info.Width; ++x)
		{
			row[x * 4 + 0] = bgra[x * 4 + 2];
			row[x * 4 + 1] = bgra[x * 4 + 1];
			row[x * 4 + 2] = bgra[x * 4 + 0];
			row[x * 4 + 3] = bgra[x * 4 + 3];
		}
	}
	decoded->UnlockRect(0);
	hr = output->CreateRGBA8(device, info.Width, info.Height, rgba.data(), rowBytes);
	if (SUCCEEDED(hr)) *budget -= imageBytes;
	return hr;
}

HRESULT CreateModelPipeline(ID3D11Device* device,
	const DX11GrannySnapshot& snapshot, ModelPipeline* pipeline)
{
	Microsoft::WRL::ComPtr<ID3DBlob> vertexCode;
	Microsoft::WRL::ComPtr<ID3DBlob> pixelCode;
	HRESULT hr = D3DCompile(kModelPreviewShader, sizeof(kModelPreviewShader) - 1,
		nullptr, nullptr, nullptr, "VSMain", "vs_5_0",
		D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, vertexCode.GetAddressOf(), nullptr);
	if (FAILED(hr)) return hr;
	hr = D3DCompile(kModelPreviewShader, sizeof(kModelPreviewShader) - 1,
		nullptr, nullptr, nullptr, "PSMain", "ps_5_0",
		D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, pixelCode.GetAddressOf(), nullptr);
	if (FAILED(hr)) return hr;
	hr = DX11ShaderLibrary::CreateFromMemory(device, DX11ShaderStage::Vertex,
		vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), &pipeline->vs);
	if (FAILED(hr)) return hr;
	hr = DX11ShaderLibrary::CreateFromMemory(device, DX11ShaderStage::Pixel,
		pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), &pipeline->ps);
	if (FAILED(hr)) return hr;

	const D3D11_INPUT_ELEMENT_DESC attributes[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	hr = pipeline->layout.Create(device, pipeline->vs, attributes, ARRAYSIZE(attributes));
	if (FAILED(hr)) return hr;

	// Center the *actual mesh* in an inspection camera for a reproducible
	// preview independent of the current in-game world position or distance.
	float low[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
	float high[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	for (const auto& vertex : snapshot.vertices)
		for (UINT k = 0; k < 3; ++k)
		{
			low[k] = (std::min)(low[k], vertex.position[k]);
			high[k] = (std::max)(high[k], vertex.position[k]);
		}
	float span = 0.0f;
	float center[3];
	for (UINT k = 0; k < 3; ++k)
	{
		center[k] = 0.5f * (low[k] + high[k]);
		span = (std::max)(span, high[k] - low[k]);
	}
	if (!std::isfinite(span) || span <= 1e-5f) return E_INVALIDARG;
	const float scale = 1.7f / span;
	std::vector<DX11GrannyVertex> normalized = snapshot.vertices;
	for (auto& vertex : normalized)
		for (UINT k = 0; k < 3; ++k)
			vertex.position[k] = (vertex.position[k] - center[k]) * scale;

	hr = pipeline->vb.CreateVertex(device, normalized.data(),
		sizeof(DX11GrannyVertex), static_cast<UINT>(normalized.size()));
	if (FAILED(hr)) return hr;
	hr = pipeline->ib.CreateIndex(device, snapshot.indices.data(),
		DXGI_FORMAT_R16_UINT, static_cast<UINT>(snapshot.indices.size()));
	if (FAILED(hr)) return hr;

	D3DXMATRIX view, projection, viewProjection;
	const D3DXVECTOR3 eye(2.8f, -3.0f, 2.0f);
	const D3DXVECTOR3 target(0, 0, 0);
	const D3DXVECTOR3 up(0, 0, 1);
	D3DXMatrixLookAtLH(&view, &eye, &target, &up);
	D3DXMatrixPerspectiveFovLH(&projection, D3DX_PI / 3.0f, 1.0f, 0.1f, 100.0f);
	D3DXMatrixMultiply(&viewProjection, &view, &projection);
	hr = pipeline->cb.CreateConstant(device, &viewProjection, sizeof(viewProjection));
	if (FAILED(hr)) return hr;
	const float flags[4] = {};
	hr = pipeline->materialFlags.CreateConstant(device, flags, sizeof(flags), true);
	if (FAILED(hr)) return hr;
	D3D11_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	hr = device->CreateSamplerState(&sampler, pipeline->sampler.GetAddressOf());
	if (FAILED(hr)) return hr;

	D3D11_DEPTH_STENCIL_DESC depth = {};
	depth.DepthEnable = TRUE;
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	hr = device->CreateDepthStencilState(&depth, pipeline->depthState.GetAddressOf());
	if (FAILED(hr)) return hr;
	D3D11_RASTERIZER_DESC raster = {};
	raster.FillMode = D3D11_FILL_SOLID;
	raster.CullMode = D3D11_CULL_NONE;
	raster.DepthClipEnable = TRUE;
	return device->CreateRasterizerState(&raster, pipeline->rasterizer.GetAddressOf());
}

HRESULT DrawModel(ID3D11DeviceContext* context, const ModelPipeline& pipeline,
	const DX11GrannySnapshot& snapshot, const std::vector<ModelMaterial>& materials)
{
	context->OMSetDepthStencilState(pipeline.depthState.Get(), 0);
	context->RSSetState(pipeline.rasterizer.Get());
	pipeline.layout.Bind(context);
	pipeline.vb.BindVertex(context);
	pipeline.ib.BindIndex(context);
	pipeline.cb.BindVSConstant(context);
	pipeline.materialFlags.BindPSConstant(context, 1);
	ID3D11SamplerState* sampler = pipeline.sampler.Get();
	context->PSSetSamplers(0, 1, &sampler);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pipeline.vs.Bind(context);
	pipeline.ps.Bind(context);
	for (size_t groupIndex = 0; groupIndex < snapshot.groups.size(); ++groupIndex)
	{
		const DX11GrannyMaterialGroup& group = snapshot.groups[groupIndex];
		const ModelMaterial& material = materials[groupIndex];
		const float flags[4] = { material.diffuse ? 1.0f : 0.0f,
			material.opacity ? 1.0f : 0.0f, 0.0f, 0.0f };
		const HRESULT hr = pipeline.materialFlags.Update(context, flags, sizeof(flags));
		if (FAILED(hr)) return hr;
		ID3D11ShaderResourceView* empty = nullptr;
		context->PSSetShaderResources(0, 1, &empty);
		context->PSSetShaderResources(1, 1, &empty);
		if (material.diffuse) material.diffuse->BindPS(context, 0);
		if (material.opacity) material.opacity->BindPS(context, 1);
		context->DrawIndexed(group.indexCount, group.firstIndex, 0);
	}
	return S_OK;
}
}

bool WantsDX11GrannyCapture(bool skinned)
{
	static const bool enabled = []() {
		char flag[8] = {};
		return GetEnvironmentVariableA("M2_DX11_MODEL_PREVIEW", flag,
			ARRAYSIZE(flag)) == 1 && flag[0] == '1';
	}();
	static const bool skinnedOnly = []() {
		char target[16] = {};
		return GetEnvironmentVariableA("M2_DX11_MODEL_TARGET", target,
			ARRAYSIZE(target)) == 7 && _stricmp(target, "skinned") == 0;
	}();
	return enabled && (!skinnedOnly || skinned) &&
		!SnapshotTaken() && PendingSnapshot().vertices.empty();
}

bool CaptureDX11GrannyMesh(const DX11GrannyVertex* vertices, UINT vertexCount,
	const WORD* indices, UINT indexCount, bool skinned,
	const DX11GrannyMaterialGroup* groups, UINT groupCount, const char* meshName)
{
	if (!WantsDX11GrannyCapture(skinned) || !vertices || !indices ||
		vertexCount < 12 || vertexCount > kMaxVertices ||
		indexCount < 36 || indexCount > kMaxIndices || indexCount % 3 != 0 ||
		groupCount > kMaxMaterialGroups || (groupCount && !groups))
		return false;
	for (UINT i = 0; i < vertexCount; ++i)
		for (UINT j = 0; j < 3; ++j)
			if (!std::isfinite(vertices[i].position[j]) ||
				!std::isfinite(vertices[i].normal[j]))
				return false;
	for (UINT i = 0; i < indexCount; ++i)
		if (indices[i] >= vertexCount)
			return false;
	for (UINT i = 0; i < groupCount; ++i)
		if (groups[i].firstIndex > indexCount ||
			groups[i].indexCount > indexCount - groups[i].firstIndex ||
			groups[i].indexCount == 0 || groups[i].indexCount % 3)
			return false;
	DX11GrannySnapshot& capture = PendingSnapshot();
	capture.vertices.assign(vertices, vertices + vertexCount);
	capture.indices.assign(indices, indices + indexCount);
	if (groupCount)
		capture.groups.assign(groups, groups + groupCount);
	else
	{
		DX11GrannyMaterialGroup untextured;
		untextured.indexCount = indexCount;
		capture.groups.push_back(untextured);
	}
	capture.meshName = meshName ? meshName : "";
	capture.skinned = skinned;
	return true;
}

bool TakeDX11GrannySnapshot(DX11GrannySnapshot* out)
{
	if (!out || PendingSnapshot().vertices.empty() || SnapshotTaken())
		return false;
	*out = std::move(PendingSnapshot());
	SnapshotTaken() = true;
	return true;
}

HRESULT RunDX11GrannyPreview(HWND gameWindow, IDirect3DDevice9Ex* gameDevice,
	const DX11GrannySnapshot& snapshot, IDirect3DTexture9** texture,
	unsigned char rgba[4], UINT* coveredPixels,
	UINT* texturesLoaded, UINT* texturesMissing,
	const char** stage, const char** format)
{
	if (!texture || !rgba || !coveredPixels || !texturesLoaded ||
		!texturesMissing || !stage || !format) return E_POINTER;
	*texture = nullptr;
	*coveredPixels = 0;
	*texturesLoaded = 0;
	*texturesMissing = 0;
	*stage = "ValidateInput";
	*format = "none";
	std::memset(rgba, 0, 4);
	if (!gameDevice || !IsWindow(gameWindow) || snapshot.vertices.empty() ||
		snapshot.indices.empty() || snapshot.groups.empty()) return E_INVALIDARG;

	*stage = "CreateDX11ModelDevice";
	DX11Device renderer;
	if (!renderer.CreateOffscreen(gameWindow, kPreviewSize, kPreviewSize))
		return renderer.GetLastResult();
	*stage = "CreateDX11ModelPipeline";
	ModelPipeline pipeline;
	HRESULT hr = CreateModelPipeline(renderer.GetDevice(), snapshot, &pipeline);
	if (FAILED(hr)) return hr;

	*stage = "LoadDX11ModelMaterials";
	std::vector<ModelMaterial> materials(snapshot.groups.size());
	size_t textureBudget = kTextureBudgetBytes;
	// One Granny mesh often has several groups using the same texture. Cache
	// each pack image only once and enforce a total decoded-memory budget.
	std::map<std::string, DX11Texture> cachedTextures;
	std::set<std::string> failedTextures;
	const auto acquireTexture = [&](const std::string& path) -> const DX11Texture* {
		if (path.empty() || failedTextures.find(path) != failedTextures.end())
			return nullptr;
		auto found = cachedTextures.find(path);
		if (found != cachedTextures.end())
			return &found->second;
		DX11Texture& output = cachedTextures[path];
		if (FAILED(LoadPackedMaterial(gameDevice, renderer.GetDevice(),
			path, &textureBudget, &output)))
		{
			cachedTextures.erase(path);
			failedTextures.insert(path);
			++*texturesMissing;
			return nullptr;
		}
		++*texturesLoaded;
		return &output;
	};
	for (size_t i = 0; i < snapshot.groups.size(); ++i)
	{
		const DX11GrannyMaterialGroup& group = snapshot.groups[i];
		ModelMaterial& material = materials[i];
		material.diffuse = acquireTexture(group.diffusePath);
		material.opacity = acquireTexture(group.opacityPath);
	}

	Microsoft::WRL::ComPtr<IDirect3DTexture9> shared9;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> shared11;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> sharedRTV;
	D3DFORMAT selected = D3DFMT_UNKNOWN;
	const D3DFORMAT choices[] = { D3DFMT_A8B8G8R8, D3DFMT_A8R8G8B8 };
	for (UINT i = 0; i < ARRAYSIZE(choices); ++i)
	{
		shared9.Reset();
		shared11.Reset();
		sharedRTV.Reset();
		HANDLE handle = nullptr;
		*stage = "CreateSharedModelSurface";
		hr = gameDevice->CreateTexture(kPreviewSize, kPreviewSize, 1,
			D3DUSAGE_RENDERTARGET, choices[i], D3DPOOL_DEFAULT,
			shared9.GetAddressOf(), &handle);
		if (FAILED(hr)) continue;
		if (!handle) { hr = E_FAIL; continue; }
		*stage = "OpenSharedModelSurface";
		hr = renderer.GetDevice()->OpenSharedResource(handle,
			IID_PPV_ARGS(shared11.GetAddressOf()));
		if (FAILED(hr)) continue;
		*stage = "CreateSharedModelRTV";
		hr = renderer.GetDevice()->CreateRenderTargetView(
			shared11.Get(), nullptr, sharedRTV.GetAddressOf());
		if (FAILED(hr)) continue;
		selected = choices[i];
		*format = i == 0 ? "A8B8G8R8" : "A8R8G8B8";
		break;
	}
	if (selected == D3DFMT_UNKNOWN) return hr;

	*stage = "DrawDX11GrannyModel";
	const float clear[4] = { 0.07f, 0.09f, 0.12f, 1.0f };
	if (!renderer.BeginFrame(clear)) return E_FAIL;
	ID3D11DeviceContext* context = renderer.GetContext();
	ID3D11RenderTargetView* renderTarget = sharedRTV.Get();
	context->OMSetRenderTargets(1, &renderTarget, renderer.GetDepthTarget());
	context->ClearRenderTargetView(renderTarget, clear);
	hr = DrawModel(context, pipeline, snapshot, materials);
	if (FAILED(hr)) return hr;
	context->OMSetRenderTargets(0, nullptr, nullptr);
	context->Flush();

	*stage = "ReadDX11ModelViaD3D9";
	Microsoft::WRL::ComPtr<IDirect3DSurface9> source;
	hr = shared9->GetSurfaceLevel(0, source.GetAddressOf());
	if (FAILED(hr)) return hr;
	Microsoft::WRL::ComPtr<IDirect3DSurface9> readback;
	hr = gameDevice->CreateOffscreenPlainSurface(kPreviewSize, kPreviewSize,
		selected, D3DPOOL_SYSTEMMEM, readback.GetAddressOf(), nullptr);
	if (FAILED(hr)) return hr;
	hr = gameDevice->GetRenderTargetData(source.Get(), readback.Get());
	if (FAILED(hr)) return hr;
	D3DLOCKED_RECT locked = {};
	hr = readback->LockRect(&locked, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr)) return hr;
	const unsigned char* middle = static_cast<const unsigned char*>(locked.pBits) +
		static_cast<size_t>(locked.Pitch) * (kPreviewSize / 2) + kPreviewSize * 2;
	rgba[0] = middle[selected == D3DFMT_A8R8G8B8 ? 2 : 0];
	rgba[1] = middle[1];
	rgba[2] = middle[selected == D3DFMT_A8R8G8B8 ? 0 : 2];
	rgba[3] = middle[3];
	// Do not report a successful mesh draw if we only cleared the target.
	// Compare the RGB bytes with a background corner, independently of
	// the two possible D3D9 shared texture channel layouts.
	const unsigned char* clearPixel = static_cast<const unsigned char*>(locked.pBits);
	for (UINT y = 0; y < kPreviewSize; ++y)
	{
		const unsigned char* row = static_cast<const unsigned char*>(locked.pBits) +
			static_cast<size_t>(locked.Pitch) * y;
		for (UINT x = 0; x < kPreviewSize; ++x)
		{
			const unsigned char* pixel = row + 4 * x;
			if (std::abs(static_cast<int>(pixel[0]) - clearPixel[0]) > 8 ||
				std::abs(static_cast<int>(pixel[1]) - clearPixel[1]) > 8 ||
				std::abs(static_cast<int>(pixel[2]) - clearPixel[2]) > 8)
				++*coveredPixels;
		}
	}
	readback->UnlockRect();
	if (*coveredPixels < 16)
	{
		*stage = "VerifyDX11ModelPixels";
		return E_FAIL;
	}
	*texture = shared9.Detach();
	*stage = "Complete";
	return S_OK;
}
}
