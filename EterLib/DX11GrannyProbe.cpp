#include "StdAfx.h"
#include "DX11GrannyProbe.h"
#include "DX11Device.h"
#include "DX11Resources.h"
#include "DX11ShaderLibrary.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

#pragma comment(lib, "d3dcompiler.lib")

namespace graphics
{
namespace
{
constexpr UINT kPreviewSize = 256;
constexpr UINT kMaxVertices = 30000;
constexpr UINT kMaxIndices = 90000;

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
    // A debug material: original material textures are not yet ported.
    float pattern = 0.88 + 0.12 * sin(input.uv.x * 30.0) * sin(input.uv.y * 30.0);
    return float4(float3(0.25, 0.74, 0.94) * light * pattern, 1.0);
}
)";

struct ModelPipeline
{
	DX11ShaderProgram vs;
	DX11ShaderProgram ps;
	DX11Buffer vb;
	DX11Buffer ib;
	DX11Buffer cb;
	DX11InputLayout layout;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer;
};

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

void DrawModel(ID3D11DeviceContext* context, const ModelPipeline& pipeline)
{
	context->OMSetDepthStencilState(pipeline.depthState.Get(), 0);
	context->RSSetState(pipeline.rasterizer.Get());
	pipeline.layout.Bind(context);
	pipeline.vb.BindVertex(context);
	pipeline.ib.BindIndex(context);
	pipeline.cb.BindVSConstant(context);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pipeline.vs.Bind(context);
	pipeline.ps.Bind(context);
	context->DrawIndexed(pipeline.ib.GetCount(), 0, 0);
}
}

bool WantsDX11GrannyCapture()
{
	static const bool enabled = []() {
		char flag[8] = {};
		return GetEnvironmentVariableA("M2_DX11_MODEL_PREVIEW", flag,
			ARRAYSIZE(flag)) == 1 && flag[0] == '1';
	}();
	return enabled && !SnapshotTaken() && PendingSnapshot().vertices.empty();
}

bool CaptureDX11GrannyMesh(const DX11GrannyVertex* vertices, UINT vertexCount,
	const WORD* indices, UINT indexCount, bool skinned)
{
	if (!WantsDX11GrannyCapture() || !vertices || !indices ||
		vertexCount < 12 || vertexCount > kMaxVertices ||
		indexCount < 36 || indexCount > kMaxIndices || indexCount % 3 != 0)
		return false;
	for (UINT i = 0; i < vertexCount; ++i)
		for (UINT j = 0; j < 3; ++j)
			if (!std::isfinite(vertices[i].position[j]) ||
				!std::isfinite(vertices[i].normal[j]))
				return false;
	for (UINT i = 0; i < indexCount; ++i)
		if (indices[i] >= vertexCount)
			return false;
	DX11GrannySnapshot& capture = PendingSnapshot();
	capture.vertices.assign(vertices, vertices + vertexCount);
	capture.indices.assign(indices, indices + indexCount);
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
	const char** stage, const char** format)
{
	if (!texture || !rgba || !coveredPixels || !stage || !format) return E_POINTER;
	*texture = nullptr;
	*coveredPixels = 0;
	*stage = "ValidateInput";
	*format = "none";
	std::memset(rgba, 0, 4);
	if (!gameDevice || !IsWindow(gameWindow) || snapshot.vertices.empty() ||
		snapshot.indices.empty()) return E_INVALIDARG;

	*stage = "CreateDX11ModelDevice";
	DX11Device renderer;
	if (!renderer.CreateOffscreen(gameWindow, kPreviewSize, kPreviewSize))
		return renderer.GetLastResult();
	*stage = "CreateDX11ModelPipeline";
	ModelPipeline pipeline;
	HRESULT hr = CreateModelPipeline(renderer.GetDevice(), snapshot, &pipeline);
	if (FAILED(hr)) return hr;

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
	DrawModel(context, pipeline);
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
