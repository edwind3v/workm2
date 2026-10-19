#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace graphics
{
enum class DX11ShaderStage
{
	Vertex,
	Pixel,
	Geometry,
	Hull,
	Domain,
	Compute
};

// Keeps the original bytecode because D3D11 input layouts require the vertex
// shader signature. Each program owns one shader stage.
struct DX11ShaderProgram
{
	DX11ShaderStage stage = DX11ShaderStage::Vertex;
	std::vector<std::uint8_t> bytecode;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
	Microsoft::WRL::ComPtr<ID3D11GeometryShader> geometry;
	Microsoft::WRL::ComPtr<ID3D11HullShader> hull;
	Microsoft::WRL::ComPtr<ID3D11DomainShader> domain;
	Microsoft::WRL::ComPtr<ID3D11ComputeShader> compute;

	bool IsValid() const;
	bool Bind(ID3D11DeviceContext* context) const;
};

class DX11ShaderLibrary final
{
public:
	// Accepts standard D3D11 DXBC bytecode, with no dependency on filenames or
	// any particular game's shader set.
	static HRESULT CreateFromMemory(ID3D11Device* device,
		DX11ShaderStage stage, const void* bytes, std::size_t size,
		DX11ShaderProgram* output);

	static HRESULT LoadFromFile(ID3D11Device* device,
		DX11ShaderStage stage, const wchar_t* filename,
		DX11ShaderProgram* output);
};
}
