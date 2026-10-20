#pragma once

#include <Windows.h>
#include <string>
#include <vector>

struct IDirect3DDevice9Ex;
struct IDirect3DTexture9;

namespace graphics
{
// Same memory layout as the engine's TPNTVertex: position, normal, UV0.
// Copies are made while Granny's CPU vertices are still valid; no D3D9
// GPU buffer is locked/read back by this diagnostic path.
struct DX11GrannyVertex
{
	float position[3];
	float normal[3];
	float uv[2];
};

struct DX11GrannyMaterialGroup
{
	UINT firstIndex = 0;
	UINT indexCount = 0;
	std::string diffusePath;
	std::string opacityPath;
	bool alphaBlend = false;
	bool twoSided = false;
	bool specularEnabled = false;
	float specularPower = 0.0f;
};

struct DX11GrannySnapshot
{
	std::vector<DX11GrannyVertex> vertices;
	std::vector<WORD> indices;
	std::vector<DX11GrannyMaterialGroup> groups;
	std::string meshName;
	bool skinned = false;
};

// One-shot, opt-in snapshot from actual CGrannyModelInstance data.
// M2_DX11_MODEL_TARGET=skinned restricts capture to CPU-deformed meshes.
// "off", "any", or "skinned"; useful to distinguish a disabled probe
// from a capture that could not find a suitable mesh.
const char* GetDX11GrannyCaptureMode();
// Reserves a throttled capture attempt. Call once per model instance before
// doing Granny index/vertex copies; CaptureDX11GrannyMesh does not re-reserve.
bool WantsDX11GrannyCapture(bool skinned);
bool CaptureDX11GrannyMesh(const DX11GrannyVertex* vertices, UINT vertexCount,
	const WORD* indices, UINT indexCount, bool skinned,
	const DX11GrannyMaterialGroup* groups, UINT groupCount, const char* meshName);
bool TakeDX11GrannySnapshot(DX11GrannySnapshot* out);

// Draw the *captured game mesh* using D3D11 VS/PS, indexed geometry, depth
// testing and a simple preview material. The D3D9Ex shared texture is only
// used to show that frame within the existing game UI.
HRESULT RunDX11GrannyPreview(HWND gameWindow, IDirect3DDevice9Ex* gameDevice,
	const DX11GrannySnapshot& snapshot, IDirect3DTexture9** texture,
	unsigned char rgba[4], UINT* coveredPixels,
	UINT* texturesLoaded, UINT* texturesMissing,
	const char** stage, const char** format);
}
