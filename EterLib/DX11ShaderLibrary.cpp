#include "StdAfx.h"
#include "DX11ShaderLibrary.h"

#include <cstring>
#include <utility>

namespace graphics
{
bool DX11ShaderProgram::IsValid() const
{
	switch (stage)
	{
	case DX11ShaderStage::Vertex:   return vertex.Get() != nullptr;
	case DX11ShaderStage::Pixel:    return pixel.Get() != nullptr;
	case DX11ShaderStage::Geometry: return geometry.Get() != nullptr;
	case DX11ShaderStage::Hull:     return hull.Get() != nullptr;
	case DX11ShaderStage::Domain:   return domain.Get() != nullptr;
	case DX11ShaderStage::Compute:  return compute.Get() != nullptr;
	}
	return false;
}

bool DX11ShaderProgram::Bind(ID3D11DeviceContext* context) const
{
	if (!context || !IsValid())
		return false;

	switch (stage)
	{
	case DX11ShaderStage::Vertex:
		context->VSSetShader(vertex.Get(), nullptr, 0);
		break;
	case DX11ShaderStage::Pixel:
		context->PSSetShader(pixel.Get(), nullptr, 0);
		break;
	case DX11ShaderStage::Geometry:
		context->GSSetShader(geometry.Get(), nullptr, 0);
		break;
	case DX11ShaderStage::Hull:
		context->HSSetShader(hull.Get(), nullptr, 0);
		break;
	case DX11ShaderStage::Domain:
		context->DSSetShader(domain.Get(), nullptr, 0);
		break;
	case DX11ShaderStage::Compute:
		context->CSSetShader(compute.Get(), nullptr, 0);
		break;
	default:
		return false;
	}
	return true;
}

HRESULT DX11ShaderLibrary::CreateFromMemory(ID3D11Device* device,
	DX11ShaderStage stage, const void* bytes, std::size_t size,
	DX11ShaderProgram* output)
{
	if (!device || !bytes || size < 4 || size > 32 * 1024 * 1024 || !output ||
		std::memcmp(bytes, "DXBC", 4) != 0)
		return E_INVALIDARG;

	DX11ShaderProgram program;
	program.stage = stage;
	HRESULT result = E_INVALIDARG;
	switch (stage)
	{
	case DX11ShaderStage::Vertex:
		result = device->CreateVertexShader(bytes, size, nullptr,
			program.vertex.GetAddressOf());
		break;
	case DX11ShaderStage::Pixel:
		result = device->CreatePixelShader(bytes, size, nullptr,
			program.pixel.GetAddressOf());
		break;
	case DX11ShaderStage::Geometry:
		result = device->CreateGeometryShader(bytes, size, nullptr,
			program.geometry.GetAddressOf());
		break;
	case DX11ShaderStage::Hull:
		result = device->CreateHullShader(bytes, size, nullptr,
			program.hull.GetAddressOf());
		break;
	case DX11ShaderStage::Domain:
		result = device->CreateDomainShader(bytes, size, nullptr,
			program.domain.GetAddressOf());
		break;
	case DX11ShaderStage::Compute:
		result = device->CreateComputeShader(bytes, size, nullptr,
			program.compute.GetAddressOf());
		break;
	}
	if (FAILED(result))
		return result;

	const auto* begin = static_cast<const std::uint8_t*>(bytes);
	program.bytecode.assign(begin, begin + size);
	*output = std::move(program);
	return S_OK;
}

HRESULT DX11ShaderLibrary::LoadFromFile(ID3D11Device* device,
	DX11ShaderStage stage, const wchar_t* filename,
	DX11ShaderProgram* output)
{
	if (!device || !filename || !output)
		return E_INVALIDARG;

	HANDLE file = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return HRESULT_FROM_WIN32(GetLastError());

	LARGE_INTEGER length = {};
	if (!GetFileSizeEx(file, &length))
	{
		const HRESULT error = HRESULT_FROM_WIN32(GetLastError());
		CloseHandle(file);
		return error;
	}

	// Compiled shader blobs are small; refuse unexpectedly large input files.
	if (length.QuadPart < 4 || length.QuadPart > 32 * 1024 * 1024)
	{
		CloseHandle(file);
		return HRESULT_FROM_WIN32(ERROR_BAD_LENGTH);
	}

	std::vector<std::uint8_t> bytes(static_cast<size_t>(length.QuadPart));
	DWORD read = 0;
	const BOOL succeeded = ReadFile(file, bytes.data(),
		static_cast<DWORD>(bytes.size()), &read, nullptr);
	const DWORD readError = succeeded ? ERROR_SUCCESS : GetLastError();
	CloseHandle(file);
	if (!succeeded)
		return HRESULT_FROM_WIN32(readError);
	if (static_cast<std::size_t>(read) != bytes.size())
		return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);

	return CreateFromMemory(device, stage, bytes.data(), bytes.size(), output);
}
}
