#include "StdAfx.h"
#include "../eterBase/Stl.h"
#include "GrpVertexBuffer.h"
#include "StateManager.h"

int	CGraphicVertexBuffer::GetVertexStride() const
{
	const int retSize = m_vertexStride;
	return retSize;
}

DWORD CGraphicVertexBuffer::GetFlexibleVertexFormat() const
{
	return m_dwFVF;
}

int CGraphicVertexBuffer::GetVertexCount() const
{
	return m_vtxCount;
}

void CGraphicVertexBuffer::SetStream(int stride, int layer) const
{
	assert(ms_lpd3dDevice != nullptr);
	STATEMANAGER.SetStreamSource(layer, m_lpd3dVB, stride);
}

bool CGraphicVertexBuffer::LockRange(unsigned count, void** pretVertices) const
{
	if (!m_lpd3dVB)
		return false;

	const DWORD dwLockSize=GetVertexStride() * count;
	if (FAILED(m_lpd3dVB->Lock(0, dwLockSize, pretVertices, m_dwLockFlag)))
		return false;

	++m_contentVersion;
	return true;
}

bool CGraphicVertexBuffer::LockDiscardRange(unsigned count, void** vertices) const
{
    if (!m_lpd3dVB || !vertices || !count || count > static_cast<unsigned>(m_vtxCount)) return false;
    // This path replaces every vertex subsequently drawn; partial/general locks
    // retain their original behavior. DISCARD applies to the entire allocation.
    const DWORD flags = (m_dwUsage & D3DUSAGE_DYNAMIC) ? D3DLOCK_DISCARD : 0;
    if (FAILED(m_lpd3dVB->Lock(0, m_vertexStride * count, vertices, flags))) return false;
    ++m_contentVersion;
    return true;
}

bool CGraphicVertexBuffer::Lock(void ** pretVertices) const
{
	if (!m_lpd3dVB)
		return false;

	const DWORD dwLockSize=GetVertexStride()*GetVertexCount();
	if (FAILED(m_lpd3dVB->Lock(0, dwLockSize, pretVertices, m_dwLockFlag)))
		return false;

	++m_contentVersion;
	return true;
}

bool CGraphicVertexBuffer::Unlock() const
{
	if (!m_lpd3dVB)
		return false;

	if ( FAILED(m_lpd3dVB->Unlock()) )
		return false;
	return true;
}

bool CGraphicVertexBuffer::IsEmpty() const
{
	if (m_lpd3dVB)
		return true;
	else
		return false;
}

bool CGraphicVertexBuffer::LockDynamic(void** pretVertices) const
{
	if (!m_lpd3dVB)
		return false;

	if (FAILED(m_lpd3dVB->Lock(0, 0, pretVertices, 0)))
		return false;

	++m_contentVersion;
	return true;
}

bool CGraphicVertexBuffer::Lock(void ** pretVertices)
{
	if (!m_lpd3dVB)
		return false;

	if (FAILED(m_lpd3dVB->Lock(0, 0, pretVertices, m_dwLockFlag)))
		return false;

	++m_contentVersion;
	return true;
}

bool CGraphicVertexBuffer::Unlock()
{
	if (!m_lpd3dVB)
		return false;

	if ( FAILED(m_lpd3dVB->Unlock()) )
		return false;
	return true;
}

bool CGraphicVertexBuffer::Copy(int bufSize, const void* srcVertices)
{
	void * dstVertices;

	if (!Lock(&dstVertices))
		return false;

	memcpy(dstVertices, srcVertices, bufSize);

	Unlock();
	return true;
}

bool CGraphicVertexBuffer::CreateDeviceObjects()
{
	assert(ms_lpd3dDevice != nullptr);
	assert(m_lpd3dVB == nullptr);

	if (FAILED(
		ms_lpd3dDevice->CreateVertexBuffer(
		m_dwBufferSize,
		m_dwUsage,
		m_dwFVF,
		m_d3dPool,
		&m_lpd3dVB, nullptr)
		))
		return false;

	return true;
}

void CGraphicVertexBuffer::DestroyDeviceObjects()
{
	++m_contentVersion;
	safe_release(m_lpd3dVB);
}

bool CGraphicVertexBuffer::Create(int vtxCount, DWORD fvf, DWORD usage, D3DPOOL d3dPool)
{
	assert(ms_lpd3dDevice != nullptr);
	assert(vtxCount > 0);

	Destroy();

	m_vtxCount = vtxCount;
	m_vertexStride = D3DXGetFVFVertexSize(fvf);
	m_dwBufferSize = m_vertexStride * m_vtxCount;
	m_d3dPool = d3dPool;
	m_dwUsage = usage;
	m_dwFVF = fvf;

	if (usage & (D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC))
		m_dwLockFlag = 0;
	else
		m_dwLockFlag = D3DLOCK_READONLY;

	return CreateDeviceObjects();
}

void CGraphicVertexBuffer::Destroy()
{
	DestroyDeviceObjects();
}

void CGraphicVertexBuffer::Initialize()
{
	m_lpd3dVB = nullptr;
	m_vtxCount = 0;
	m_dwBufferSize = 0;
}

CGraphicVertexBuffer::CGraphicVertexBuffer()
{
	Initialize();
}

CGraphicVertexBuffer::~CGraphicVertexBuffer()
{
	Destroy();
}

