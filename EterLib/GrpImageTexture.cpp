#include "StdAfx.h"
#include "../EterBase/MapPerformance.h"
#include <climits>
#include "../eterBase/MappedFile.h"
#include "../eterPack/EterPackManager.h"
#include "../eterImageLib/TGAImage.h"
#include "GrpImageTexture.h"
#include "JpegFile.h"
#include <vector>
#include <wincodec.h>
#include <stb_image.h>

#pragma comment(lib, "windowscodecs.lib")

// Helper to check if file extension matches




// Helper function to get bytes per pixel for a format
static UINT GetBytesPerPixel(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R32G32B32A32_FLOAT:	return 16;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:	return 8;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:		return 4;
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
	case DXGI_FORMAT_R8G8_UNORM:			return 2;
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_A8_UNORM:				return 1;
	default:								return 4;
	}
}

bool CGraphicImageTexture::Lock(int* pRetPitch, void** ppRetPixels, int level)
{
	if (!m_pTexture || !ms_pContext)
		return false;

	D3D11_TEXTURE2D_DESC texDesc;
	m_pTexture->GetDesc(&texDesc);
	if (texDesc.Format == DXGI_FORMAT_BC1_UNORM ||
		texDesc.Format == DXGI_FORMAT_BC2_UNORM ||
		texDesc.Format == DXGI_FORMAT_BC3_UNORM)
		return false;

	// Create a staging texture if not already created
	if (!m_pStagingTexture)
	{
		D3D11_TEXTURE2D_DESC desc;
		m_pTexture->GetDesc(&desc);

		D3D11_TEXTURE2D_DESC stagingDesc = desc;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		stagingDesc.MiscFlags = 0;

		if (FAILED(ms_pDevice->CreateTexture2D(&stagingDesc, nullptr, &m_pStagingTexture)))
			return false;

		// Copy current texture to staging
		ms_pContext->CopyResource(m_pStagingTexture, m_pTexture);
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(ms_pContext->Map(m_pStagingTexture, level, D3D11_MAP_READ_WRITE, 0, &mapped)))
		return false;

	*pRetPitch = mapped.RowPitch;
	*ppRetPixels = mapped.pData;
	return true;
}

void CGraphicImageTexture::Unlock(int level)
{
	if (!m_pStagingTexture || !ms_pContext)
		return;

	ms_pContext->Unmap(m_pStagingTexture, level);

	// Copy staging back to GPU texture
	ms_pContext->CopyResource(m_pTexture, m_pStagingTexture);
}

void CGraphicImageTexture::Initialize()
{
	CGraphicTexture::Initialize();

	m_stFileName = "";
	m_dxgiFmt = DXGI_FORMAT_UNKNOWN;
	m_dwFilter = 0;
	m_pStagingTexture = nullptr;
}

void CGraphicImageTexture::Destroy()
{
	if (m_pStagingTexture)
	{
		m_pStagingTexture->Release();
		m_pStagingTexture = nullptr;
	}

	CGraphicTexture::Destroy();
	Initialize();
}

bool CGraphicImageTexture::CreateDeviceObjects()
{
	assert(ms_pDevice != NULL);
	assert(m_pTexture == NULL);

	if (m_stFileName.empty())
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = m_width;
		desc.Height = m_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = m_dxgiFmt;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		UINT pixelSize = GetBytesPerPixel(m_dxgiFmt);
		UINT rowPitch = m_width * pixelSize;
		UINT totalSize = rowPitch * m_height;
		std::vector<BYTE> initData(totalSize, 0);  // All zeros = transparent black

		D3D11_SUBRESOURCE_DATA subresourceData = {};
		subresourceData.pSysMem = initData.data();
		subresourceData.SysMemPitch = rowPitch;
		subresourceData.SysMemSlicePitch = 0;

		if (FAILED(ms_pDevice->CreateTexture2D(&desc, &subresourceData, &m_pTexture)))
			return false;

		if (!CreateShaderResourceView())
			return false;
	}
	else
	{
		CMappedFile	mappedFile;
		LPCVOID		c_pvMap;

		if (!CEterPackManager::Instance().Get(mappedFile, m_stFileName.c_str(), &c_pvMap))
			return false;

		if (!CreateFromMemoryFile(mappedFile.Size(), c_pvMap, m_dxgiFmt, m_stFileName.c_str(), m_dwFilter))
		{
			TraceError("CGraphicImageTexture::CreateDeviceObjects: CreateFromMemoryFile: texture not found(%s)", m_stFileName.c_str());
			return false;
		}
		return true;
	}

	m_bEmpty = false;
	return true;
}

bool CGraphicImageTexture::Create(UINT width, UINT height, DXGI_FORMAT dxgiFmt, DWORD dwFilter)
{
	assert(ms_pDevice != NULL);
	Destroy();

	m_width = width;
	m_height = height;
	m_dxgiFmt = dxgiFmt;
	m_dwFilter = dwFilter;

	return CreateDeviceObjects();
}

void CGraphicImageTexture::CreateFromTexturePointer(const CGraphicTexture* c_pSrcTexture)
{
	if (m_pTexture)
	{
		m_pTexture->Release();
		m_pTexture = nullptr;
	}
	if (m_pSRV)
	{
		m_pSRV->Release();
		m_pSRV = nullptr;
	}

	m_width = c_pSrcTexture->GetWidth();
	m_height = c_pSrcTexture->GetHeight();
	m_pTexture = c_pSrcTexture->GetTexture();
	m_pSRV = c_pSrcTexture->GetSRV();

	if (m_pTexture)
		m_pTexture->AddRef();
	if (m_pSRV)
		m_pSRV->AddRef();

	m_bEmpty = false;
}



bool CGraphicImageTexture::CreateDDSTexture(CDXTCImage& image, const BYTE*)
{
    MapPerformance::Scope profile("texture.dds_upload", m_stFileName.c_str());
    const bool compressed = image.m_CompFormat != PF_ARGB;
    const DXGI_FORMAT format = image.m_CompFormat == PF_DXT1 ? DXGI_FORMAT_BC1_UNORM :
        image.m_CompFormat == PF_DXT3 ? DXGI_FORMAT_BC2_UNORM :
        image.m_CompFormat == PF_DXT5 ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    const UINT bias = IsLowTextureMemory() && image.m_dwMipMapCount > 1 ? 1 : 0;
    const UINT levels = image.m_dwMipMapCount - bias;
    const UINT width = max(1U, UINT(image.m_nWidth) >> bias);
    const UINT height = max(1U, UINT(image.m_nHeight) >> bias);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = levels; desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> data(levels);
    std::vector<std::vector<DWORD>> converted(compressed ? 0 : levels);
    auto channel = [](DWORD value, DWORD mask, BYTE missing) -> BYTE {
        if (!mask) return missing;
        unsigned shift = 0; while (!(mask & 1)) { ++shift; mask >>= 1; }
        return BYTE((uint64_t((value >> shift) & mask) * 255) / mask);
    };
    for (UINT mip = 0; mip < levels; ++mip)
    {
        const UINT source = mip + bias, w = max(1U, width >> mip), h = max(1U, height >> mip);
        if (!image.m_pbCompBufferByLevels[source]) return false;
        data[mip].pSysMem = image.m_pbCompBufferByLevels[source];
        data[mip].SysMemPitch = image.m_compRowBytes[source];
        if (!compressed)
        {
            converted[mip].resize(size_t(w) * h);
            const UINT bytesPerPixel = image.m_xddPixelFormat.dwRGBBitCount / 8;
            for (UINT y = 0; y < h; ++y)
                for (UINT x = 0; x < w; ++x)
                {
                    DWORD value = 0;
                    memcpy(&value, image.m_pbCompBufferByLevels[source] + size_t(y) * image.m_compRowBytes[source] + x * bytesPerPixel, bytesPerPixel);
                    const auto& pf = image.m_xddPixelFormat;
                    BYTE r = channel(value, pf.dwRBitMask, 0), g = channel(value, pf.dwGBitMask, 0), b = channel(value, pf.dwBBitMask, 0);
                    BYTE a = channel(value, pf.dwRGBAlphaBitMask, 255);
                    if (r == 255 && g == 0 && b == 255) a = 0;
                    converted[mip][size_t(y) * w + x] = PackColor(a, r, g, b);
                }
            data[mip].pSysMem = converted[mip].data();
            data[mip].SysMemPitch = w * 4;
        }
    }
    const HRESULT hr = MapPerformance::Measure("texture.dds_transfer", m_stFileName.c_str(), [&] {
        return ms_pDevice->CreateTexture2D(&desc, data.data(), &m_pTexture);
    });
    if (FAILED(hr)) return false;
    if (!CreateShaderResourceView()) { m_pTexture->Release(); m_pTexture = nullptr; return false; }
    m_width = width; m_height = height; m_Format = format; m_bEmpty = false;
    return true;
}

bool CGraphicImageTexture::CreateFromMemoryFile(UINT size, const void* bytes, DXGI_FORMAT, const char* fileName, DWORD)
{
    MapPerformance::Scope profile("texture.load", fileName);
    if (!ms_pDevice || !ms_pContext || !bytes || !size || size > INT_MAX || m_pTexture) return false;
    if (size >= 4 && memcmp(bytes, "DDS ", 4) == 0)
    {
        CDXTCImage image;
        return image.LoadHeaderFromMemory(static_cast<const BYTE*>(bytes), int(size)) && CreateDDSTexture(image, static_cast<const BYTE*>(bytes));
    }
    int w = 0, h = 0, channels = 0;
    const auto* input = static_cast<const stbi_uc*>(bytes);
    if (!stbi_info_from_memory(input, int(size), &w, &h, &channels) ||
        w <= 0 || h <= 0 || w > 16384 || h > 16384 || uint64_t(w) * h * 4 > 256ULL * 1024 * 1024) return false;
    stbi_uc* pixels = MapPerformance::Measure("texture.image_decode", fileName, [&] {
        return stbi_load_from_memory(input, int(size), &w, &h, &channels, 4);
    });
    if (!pixels) return false;
    for (size_t i = 0, count = size_t(w) * h; i < count; ++i)
        if (pixels[i * 4] == 255 && pixels[i * 4 + 1] == 0 && pixels[i * 4 + 2] == 255) pixels[i * 4 + 3] = 0;
    UINT support = 0;
    const bool generateMips = SUCCEEDED(ms_pDevice->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM, &support)) &&
        (support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) && (w > 1 || h > 1);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w; desc.Height = h; desc.MipLevels = generateMips ? 0 : 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (generateMips) { desc.BindFlags |= D3D11_BIND_RENDER_TARGET; desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS; }
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = pixels; data.SysMemPitch = UINT(w) * 4;
    HRESULT hr = ms_pDevice->CreateTexture2D(&desc, generateMips ? nullptr : &data, &m_pTexture);
    if (SUCCEEDED(hr) && generateMips) ms_pContext->UpdateSubresource(m_pTexture, 0, nullptr, pixels, UINT(w) * 4, 0);
    stbi_image_free(pixels);
    if (FAILED(hr)) return false;
    if (!CreateShaderResourceView()) { m_pTexture->Release(); m_pTexture = nullptr; return false; }
    if (generateMips) ms_pContext->GenerateMips(m_pSRV);
    m_width = w; m_height = h; m_Format = DXGI_FORMAT_R8G8B8A8_UNORM; m_bEmpty = false;
    return true;
}

void CGraphicImageTexture::SetFileName(const char* c_szFileName)
{
	m_stFileName = c_szFileName;
}

bool CGraphicImageTexture::CreateFromDiskFile(const char* c_szFileName, DXGI_FORMAT dxgiFmt, DWORD dwFilter)
{
	Destroy();

	SetFileName(c_szFileName);

	m_dxgiFmt = dxgiFmt;
	m_dwFilter = dwFilter;
	return CreateDeviceObjects();
}

CGraphicImageTexture::CGraphicImageTexture()
{
	Initialize();
}

CGraphicImageTexture::~CGraphicImageTexture()
{
	Destroy();
}
