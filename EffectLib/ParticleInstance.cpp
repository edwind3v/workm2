#include "StdAfx.h"
#include "ParticleInstance.h"
#include "ParticleProperty.h"

#include "../eterBase/Random.h"
#include "../eterLib/Camera.h"
#include "../eterLib/StateManager.h"
#include "../EterBase/MapPerformance.h"
#include <cstdlib>

CDynamicPool<CParticleInstance> CParticleInstance::ms_kPool;

using namespace NEffectUpdateDecorator;

void CParticleInstance::DestroySystem()
{
	ms_kPool.Destroy();
}

CParticleInstance* CParticleInstance::New()
{
	return ms_kPool.Alloc();
}

void CParticleInstance::DeleteThis()
{
	Destroy();

	ms_kPool.Free(this);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//CDynamicPool<CRayParticleInstance> CRayParticleInstance::ms_kPool;

/*void CRayParticleInstance::DestroySystem()
{
	ms_kPool.Destroy();
}

CRayParticleInstance* CRayParticleInstance::New()
{
	return ms_kPool.Alloc();
}

void CRayParticleInstance::DeleteThis()
{
#ifdef RAY_TO_AFTERIMAGE
	m_PositionList.clear();
#else
	m_bStart = false;
#endif
	ms_kPool.Free(this);
}
*/
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float CParticleInstance::GetRadiusApproximation() const
{
	return m_v2HalfSize.y*m_v2Scale.y + m_v2HalfSize.x*m_v2Scale.x;
}
/*
D3DXVECTOR3 CBaseParticleInstance::GetCenterApproximation()
{
	return D3DXVECTOR3(
		m_v3Position.x + mc_pmatLocal._41,
		m_v3Position.y + mc_pmatLocal._42,
		m_v3Position.z + mc_pmatLocal._43
		);
}*/

namespace
{
constexpr unsigned CurveFinished = ~0u;

// Match CTimeEventDecorator's arithmetic, boundary rules and retirement.
// In particular, packed colors must retain the two separate rounded products.
template<class T>
void UpdateCurve(const std::vector<CTimeEvent<T>>& curve, unsigned& cursor,
    float time, T& value)
{
    if (cursor == CurveFinished) return;
    if (curve.empty()) { cursor = CurveFinished; return; }
    if (curve[cursor].m_fTime > time) { value = curve[cursor].m_Value; return; }
    while (cursor + 1 < curve.size() && curve[cursor + 1].m_fTime <= time) ++cursor;
    const auto& current = curve[cursor];
    if (cursor + 1 == curve.size())
    {
        value = current.m_Value;
        cursor = CurveFinished;
        return;
    }
    const auto& next = curve[cursor + 1];
    const float length = next.m_fTime - current.m_fTime;
    value = current.m_Value * (1 - (time - current.m_fTime) / length);
    value += next.m_Value * ((time - current.m_fTime) / length);
}
}

bool CParticleInstance::CompactUpdateEnabled()
{
#ifdef WORLD_EDITOR
    return false;
#else
    static const bool enabled = [] {
        const char* value = std::getenv("M2_PARTICLE_COMPACT_UPDATE");
        return !value || strcmp(value, "0") != 0;
    }();
    return enabled;
#endif
}

void CParticleInstance::InitializeCompactUpdate(const CParticleInstance* first)
{
#ifndef WORLD_EDITOR
    m_compactUpdate = true;
    for (auto& cursor : m_curveCursor) cursor = 0;
    const auto& property = *m_pParticleProperty;
    m_fLastFrameTime = property.GetTextureAnimationFrameDelay();
    if (first)
    {
        // The legacy burst clones its first decorator chain, including its
        // resolved random rotation/texture directions. Keep that behavior.
        m_updateRotationType = first->m_updateRotationType;
        m_updateTextureType = first->m_updateTextureType;
        m_fRotationSpeed = first->m_fRotationSpeed;
        m_fGravity = first->m_fGravity;
        m_fAirResistance = first->m_fAirResistance;
    }
    else
    {
        m_fGravity = property.m_TimeEventGravity.size() == 1 ? property.m_TimeEventGravity[0].m_Value : 0.0f;
        m_fAirResistance = property.m_TimeEventAirResistance.size() == 1 ? property.m_TimeEventAirResistance[0].m_Value : 0.0f;
        m_updateTextureType = m_byTextureAnimationType;
        if (property.GetTextureAnimationFrameCount() <= 1 || property.GetTextureAnimationFrameDelay() <= 1e-6f)
            m_updateTextureType = CParticleProperty::TEXTURE_ANIMATION_TYPE_NONE;
        m_updateRotationType = property.m_byRotationType;
        if (property.m_fRotationSpeed == 0.0f && m_updateRotationType != CParticleProperty::ROTATION_TYPE_TIME_EVENT)
            m_updateRotationType = CParticleProperty::ROTATION_TYPE_NONE;
        else if (m_updateRotationType == CParticleProperty::ROTATION_TYPE_RANDOM_DIRECTION)
            m_updateRotationType = (random() & 1) ? CParticleProperty::ROTATION_TYPE_CW : CParticleProperty::ROTATION_TYPE_CCW;
        m_fRotationSpeed = m_updateRotationType == CParticleProperty::ROTATION_TYPE_CCW ?
            -property.m_fRotationSpeed : property.m_fRotationSpeed;
        if (m_updateRotationType == CParticleProperty::ROTATION_TYPE_TIME_EVENT)
            m_fRotationSpeed = 0.0f;
    }
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        TraceError("[PARTICLE_CPU] ACTIVE storage=stable_vectors update=compact curves=original particle_count=unchanged disable=M2_PARTICLE_COMPACT_UPDATE=0");
    }
#endif
}

void CParticleInstance::UpdateCompact(float time, float elapsed)
{
#ifndef WORLD_EDITOR
    const auto& property = *m_pParticleProperty;
    // Decorators were pushed to the front: preserve this exact execution order.
    if (m_updateRotationType == CParticleProperty::ROTATION_TYPE_TIME_EVENT)
    {
        UpdateCurve(property.m_TimeEventRotation, m_curveCursor[0], time, m_fRotationSpeed);
        m_fRotation += m_fRotationSpeed * elapsed;
    }
    else if (m_updateRotationType == CParticleProperty::ROTATION_TYPE_CW ||
             m_updateRotationType == CParticleProperty::ROTATION_TYPE_CCW)
        m_fRotation += m_fRotationSpeed * elapsed;

    const DWORD frameCount = property.GetTextureAnimationFrameCount();
    if (m_updateTextureType == CParticleProperty::TEXTURE_ANIMATION_TYPE_CW ||
        m_updateTextureType == CParticleProperty::TEXTURE_ANIMATION_TYPE_CCW ||
        m_updateTextureType == CParticleProperty::TEXTURE_ANIMATION_TYPE_RANDOM_FRAME)
    {
        m_fLastFrameTime -= elapsed;
        // The legacy random/CCW decorators store the count in a BYTE.
        const BYTE byteCount = static_cast<BYTE>(frameCount);
        if (m_updateTextureType == CParticleProperty::TEXTURE_ANIMATION_TYPE_RANDOM_FRAME &&
            m_fLastFrameTime < 0.0f && byteCount != 0)
            m_byFrameIndex = static_cast<BYTE>(random_range(0, byteCount - 1));
        while (m_fLastFrameTime < 0.0f)
        {
            m_fLastFrameTime += property.GetTextureAnimationFrameDelay();
            if (m_updateTextureType == CParticleProperty::TEXTURE_ANIMATION_TYPE_CW)
            {
                if (++m_byFrameIndex >= frameCount) m_byFrameIndex = 0;
            }
            else if (m_updateTextureType == CParticleProperty::TEXTURE_ANIMATION_TYPE_CCW)
            {
                if (--m_byFrameIndex >= byteCount && byteCount != 0)
                    m_byFrameIndex = static_cast<BYTE>(byteCount - 1);
            }
        }
    }
    UpdateCurve(property.m_TimeEventScaleY, m_curveCursor[1], time, m_v2Scale.y);
    UpdateCurve(property.m_TimeEventScaleX, m_curveCursor[2], time, m_v2Scale.x);
    UpdateCurve(property.m_TimeEventColor, m_curveCursor[3], time, m_dcColor);
    if (!property.m_TimeEventGravity.empty())
    {
        if (property.m_TimeEventGravity.size() > 1)
            UpdateCurve(property.m_TimeEventGravity, m_curveCursor[4], time, m_fGravity);
        m_v3Velocity.z -= m_fGravity * elapsed;
    }
    if (!property.m_TimeEventAirResistance.empty())
    {
        if (property.m_TimeEventAirResistance.size() > 1)
            UpdateCurve(property.m_TimeEventAirResistance, m_curveCursor[5], time, m_fAirResistance);
        m_v3Velocity *= 1.0f - m_fAirResistance;
    }
#endif
}

BOOL CParticleInstance::Update(float fElapsedTime, float fAngle)
{
	m_fLastLifeTime -= fElapsedTime;
	if (m_fLastLifeTime < 0.0f)
		return FALSE;

	const float fLifePercentage = (m_fLifeTime - m_fLastLifeTime) / m_fLifeTime;

    if (m_compactUpdate) UpdateCompact(fLifePercentage, fElapsedTime);
    else m_pDecorator->Excute(CDecoratorData(fLifePercentage,fElapsedTime,this));

	m_v3LastPosition = m_v3Position;
	m_v3Position += m_v3Velocity * fElapsedTime;

	if (fAngle)
	{
		if (m_pParticleProperty->m_bAttachFlag)
		{
			float fCos, fSin;
			fAngle = D3DXToRadian(fAngle);
			fCos = cos(fAngle);
			fSin = sin(fAngle);

			const float rx = m_v3Position.x - m_v3StartPosition.x;
			const float ry = m_v3Position.y - m_v3StartPosition.y;

			m_v3Position.x =   fCos * rx + fSin * ry + m_v3StartPosition.x;
			m_v3Position.y = - fSin * rx + fCos * ry + m_v3StartPosition.y;
		}
		else
		{
			D3DXQUATERNION q,qc;
			D3DXQuaternionRotationAxis(&q,&m_pParticleProperty->m_v3ZAxis,D3DXToRadian(fAngle));
			D3DXQuaternionConjugate(&qc,&q);

			D3DXQUATERNION qr(
				m_v3Position.x-m_v3StartPosition.x,
				m_v3Position.y-m_v3StartPosition.y,
				m_v3Position.z-m_v3StartPosition.z,
				0.0f);
			D3DXQuaternionMultiply(&qr,&q,&qr);
			D3DXQuaternionMultiply(&qr,&qr,&qc);

			m_v3Position.x = qr.x;
			m_v3Position.y = qr.y;
			m_v3Position.z = qr.z;

			m_v3Position += m_v3StartPosition;
		}
	}

	return TRUE;
}

DWORD CParticleInstance::GetColor() const
{
#ifdef WORLD_EDITOR
    return static_cast<DWORD>(m_Color);
#else
    return static_cast<DWORD>(m_dcColor);
#endif
}

TParticleRenderContext::TParticleRenderContext(const CCamera* camera)
    : up(camera->GetUp()), cross(camera->GetCross()), view(camera->GetView())
{
    const D3DXVECTOR3 axis(0.0f, 0.0f, 1.0f);
    const D3DXVECTOR3 horizontal(view.x, view.y, 0.0f);
    D3DXVec3Cross(&yCross, &axis, &horizontal);
    D3DXVec3Normalize(&yCross, &yCross);
}

void CParticleInstance::PrepareRender(const D3DXMATRIX* c_matLocal,
    const TParticleRenderContext& context, TParticleRenderGeometry& geometry) const
{
	D3DXVECTOR3 v3Up;
	D3DXVECTOR3 v3Cross;

	if (!m_pParticleProperty->m_bStretchFlag)
	{

		const D3DXVECTOR3 & c_rv3Up = context.up;
		const D3DXVECTOR3 & c_rv3Cross = context.cross;

		switch(m_pParticleProperty->m_byBillboardType) {
		case BILLBOARD_TYPE_LIE:
			{
				const float fCos = cosf(D3DXToRadian(m_fRotation)), fSin = sinf(D3DXToRadian(m_fRotation));
				v3Up.x = fCos;
				v3Up.y = -fSin;
				v3Up.z = 0;
				v3Cross.x = fSin;
				v3Cross.y = fCos;
				v3Cross.z = 0;
			}
			break;
		case BILLBOARD_TYPE_2FACE:
		case BILLBOARD_TYPE_3FACE:
		case BILLBOARD_TYPE_Y:
			{
				v3Up = D3DXVECTOR3(0.0f,0.0f,1.0f);
				const D3DXVECTOR3 & c_rv3View = context.view;
				if (v3Up.x * c_rv3View.y - v3Up.y * c_rv3View.x<0)
					v3Up*=-1;
				v3Cross = context.yCross;

				if (m_fRotation)
				{
					const float fCos = -sinf(D3DXToRadian(m_fRotation)); // + 90
					const float fSin = cosf(D3DXToRadian(m_fRotation));

					const D3DXVECTOR3 v3Temp = v3Up * fCos - v3Cross * fSin;
					v3Cross = v3Cross * fCos + v3Up * fSin;
					v3Up = v3Temp;
				}

			}
			break;
		case BILLBOARD_TYPE_ALL:
		default:
			{
				if (m_fRotation==0.0f)
				{
					v3Up = -c_rv3Cross;
					v3Cross = c_rv3Up;
				}
				else
				{
					const D3DXVECTOR3 & c_rv3View = context.view;
					D3DXQUATERNION q,qc;
					D3DXQuaternionRotationAxis(&q, &c_rv3View, D3DXToRadian(m_fRotation));
					D3DXQuaternionConjugate(&qc, &q);

					{
						D3DXQUATERNION qr(-c_rv3Cross.x, -c_rv3Cross.y, -c_rv3Cross.z, 0);
						D3DXQuaternionMultiply(&qr,&qc,&qr);
						D3DXQuaternionMultiply(&qr,&qr,&q);
						v3Up.x = qr.x;
						v3Up.y = qr.y;
						v3Up.z = qr.z;
					}
					{
						D3DXQUATERNION qr(c_rv3Up.x, c_rv3Up.y, c_rv3Up.z, 0);
						D3DXQuaternionMultiply(&qr,&qc,&qr);
						D3DXQuaternionMultiply(&qr,&qr,&q);
						v3Cross.x = qr.x;
						v3Cross.y = qr.y;
						v3Cross.z = qr.z;
					}

				}

			}
			break;
		}

	}
	else
	{
		v3Up = m_v3Position - m_v3LastPosition;

		if (c_matLocal)
		{
				D3DXVec3TransformNormal(&v3Up, &v3Up, c_matLocal);
		}

		const float length = D3DXVec3Length(&v3Up);
		if (length == 0.0f)
		{
			v3Up = D3DXVECTOR3(0.0f,0.0f,1.0f);
		}
		else
			v3Up *=(1+log(1+length))/length;

		const D3DXVECTOR3 & c_rv3View = context.view;
		D3DXVec3Cross(&v3Cross, &v3Up, &c_rv3View);
		D3DXVec3Normalize(&v3Cross, &v3Cross);

	}

    geometry.up = v3Up;
    geometry.cross = v3Cross;
    if (c_matLocal && m_pParticleProperty->m_bAttachFlag)
        D3DXVec3TransformCoord(&geometry.position, &m_v3Position, c_matLocal);
    else
        geometry.position = m_v3Position;
}

void CParticleInstance::WriteRenderQuad(const TParticleRenderGeometry& geometry,
    TPDTVertex* vertices, bool rotate, float cosine, float sine) const
{
    D3DXVECTOR3 up = geometry.up, cross = geometry.cross;
    if (rotate)
    {
        const float ux = up.x, uy = up.y, cx = cross.x, cy = cross.y;
        up.x = ux * cosine - uy * sine;
        up.y = uy * cosine + ux * sine;
        cross.x = cx * cosine - cy * sine;
        cross.y = cy * cosine + cx * sine;
    }
    cross = -(m_v2HalfSize.x * m_v2Scale.x) * cross;
    up = (m_v2HalfSize.y * m_v2Scale.y) * up;
    vertices[0].position = geometry.position - up + cross;
    vertices[1].position = geometry.position - up - cross;
    vertices[2].position = geometry.position + up + cross;
    vertices[3].position = geometry.position + up - cross;
    const DWORD color = GetColor();
    for (unsigned i = 0; i < 4; ++i)
    {
        vertices[i].diffuse = color;
        vertices[i].texCoord = m_ParticleMesh[i].texCoord;
    }
}

void CParticleInstance::Transform(const D3DXMATRIX * c_matLocal, bool setColorState)
{
#ifdef WORLD_EDITOR
	if (setColorState) STATEMANAGER.SetRenderState(D3DRS_TEXTUREFACTOR, m_Color);
#else
	if (setColorState) STATEMANAGER.SetRenderState(D3DRS_TEXTUREFACTOR, m_dcColor);
#endif

	/////

	D3DXVECTOR3 v3Up;
	D3DXVECTOR3 v3Cross;

	if (!m_pParticleProperty->m_bStretchFlag)
	{
		const CCamera * pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
		const D3DXVECTOR3 & c_rv3Up = pCurrentCamera->GetUp();
		const D3DXVECTOR3 & c_rv3Cross = pCurrentCamera->GetCross();

		D3DXVECTOR3 v3Rotation;

		switch(m_pParticleProperty->m_byBillboardType) {
		case BILLBOARD_TYPE_LIE:
			{
				const float fCos = cosf(D3DXToRadian(m_fRotation)), fSin = sinf(D3DXToRadian(m_fRotation));
				v3Up.x = fCos;
				v3Up.y = -fSin;
				v3Up.z = 0;
				v3Cross.x = fSin;
				v3Cross.y = fCos;
				v3Cross.z = 0;
			}
			break;
		case BILLBOARD_TYPE_2FACE:
		case BILLBOARD_TYPE_3FACE:
			// using setting with y, and local rotation at render
		case BILLBOARD_TYPE_Y:
			{
				v3Up = D3DXVECTOR3(0.0f,0.0f,1.0f);
				//v3Up = D3DXVECTOR3(cosf(D3DXToRadian(m_fRotation)),0.0f,-sinf(D3DXToRadian(m_fRotation)));
				const D3DXVECTOR3 & c_rv3View = pCurrentCamera->GetView();
				if (v3Up.x * c_rv3View.y - v3Up.y * c_rv3View.x<0)
					v3Up*=-1;
				const auto val = D3DXVECTOR3(c_rv3View.x,c_rv3View.y,0);
				D3DXVec3Cross(&v3Cross, &v3Up, &val);
				D3DXVec3Normalize(&v3Cross, &v3Cross);

				if (m_fRotation)
				{
					const float fCos = -sinf(D3DXToRadian(m_fRotation)); // + 90
					const float fSin = cosf(D3DXToRadian(m_fRotation));

					const D3DXVECTOR3 v3Temp = v3Up * fCos - v3Cross * fSin;
					v3Cross = v3Cross * fCos + v3Up * fSin;
					v3Up = v3Temp;
				}

				//D3DXVECTOR3 v3Rotation;
				//D3DXVec3Cross(&v3Rotation, &v3Up, &v3Cross);

			}
			break;
		case BILLBOARD_TYPE_ALL:
		default:
			{
				if (m_fRotation==0.0f)
				{
					v3Up = -c_rv3Cross;
					v3Cross = c_rv3Up;
				}
				else
				{
					const D3DXVECTOR3 & c_rv3View = pCurrentCamera->GetView();
					D3DXQUATERNION q,qc;
					D3DXQuaternionRotationAxis(&q, &c_rv3View, D3DXToRadian(m_fRotation));
					D3DXQuaternionConjugate(&qc, &q);

					{
						D3DXQUATERNION qr(-c_rv3Cross.x, -c_rv3Cross.y, -c_rv3Cross.z, 0);
						D3DXQuaternionMultiply(&qr,&qc,&qr);
						D3DXQuaternionMultiply(&qr,&qr,&q);
						v3Up.x = qr.x;
						v3Up.y = qr.y;
						v3Up.z = qr.z;
					}
					{
						D3DXQUATERNION qr(c_rv3Up.x, c_rv3Up.y, c_rv3Up.z, 0);
						D3DXQuaternionMultiply(&qr,&qc,&qr);
						D3DXQuaternionMultiply(&qr,&qr,&q);
						v3Cross.x = qr.x;
						v3Cross.y = qr.y;
						v3Cross.z = qr.z;
					}

				}
				//D3DXMATRIX matRotation;

				//D3DXMatrixRotationAxis(&matRotation, &c_rv3View, D3DXToRadian(m_fRotation));

				//D3DXVec3TransformCoord(&v3Up, &(-c_rv3Cross), &matRotation);
				//D3DXVec3TransformCoord(&v3Cross, &c_rv3Up, &matRotation);
			}
			break;
		}

	}
	else
	{
		v3Up = m_v3Position - m_v3LastPosition;

		if (c_matLocal)
		{
			//if (!m_pParticleProperty->m_bAttachFlag)
				D3DXVec3TransformNormal(&v3Up, &v3Up, c_matLocal);
		}

		const float length = D3DXVec3Length(&v3Up);
		if (length == 0.0f)
		{
			v3Up = D3DXVECTOR3(0.0f,0.0f,1.0f);
		}
		else
			v3Up *=(1+log(1+length))/length;

		const CCamera * pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
		const D3DXVECTOR3 & c_rv3View = pCurrentCamera->GetView();
		D3DXVec3Cross(&v3Cross, &v3Up, &c_rv3View);
		D3DXVec3Normalize(&v3Cross, &v3Cross);

	}

	v3Cross = -(m_v2HalfSize.x*m_v2Scale.x) * v3Cross;
	v3Up = (m_v2HalfSize.y*m_v2Scale.y) * v3Up;

	if (c_matLocal && m_pParticleProperty->m_bAttachFlag)
	{
		D3DXVECTOR3 v3Position;
		D3DXVec3TransformCoord(&v3Position, &m_v3Position, c_matLocal);
		m_ParticleMesh[0].position = v3Position - v3Up + v3Cross;
		m_ParticleMesh[1].position = v3Position - v3Up - v3Cross;
		m_ParticleMesh[2].position = v3Position + v3Up + v3Cross;
		m_ParticleMesh[3].position = v3Position + v3Up - v3Cross;
	}
	else
	{
		m_ParticleMesh[0].position = m_v3Position - v3Up + v3Cross;
		m_ParticleMesh[1].position = m_v3Position - v3Up - v3Cross;
		m_ParticleMesh[2].position = m_v3Position + v3Up + v3Cross;
		m_ParticleMesh[3].position = m_v3Position + v3Up - v3Cross;
	}
}

void CParticleInstance::Transform(const D3DXMATRIX * c_matLocal, const float c_fZRotation, bool setColorState)
{
#ifdef WORLD_EDITOR
	if (setColorState) STATEMANAGER.SetRenderState(D3DRS_TEXTUREFACTOR, m_Color);
#else
	if (setColorState) STATEMANAGER.SetRenderState(D3DRS_TEXTUREFACTOR, (DWORD)m_dcColor);
#endif

	/////

	D3DXVECTOR3 v3Up;
	D3DXVECTOR3 v3Cross;

	if (!m_pParticleProperty->m_bStretchFlag)
	{
		CCamera * pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
		const D3DXVECTOR3 & c_rv3Up = pCurrentCamera->GetUp();
		const D3DXVECTOR3 & c_rv3Cross = pCurrentCamera->GetCross();

		D3DXVECTOR3 v3Rotation;

		switch(m_pParticleProperty->m_byBillboardType) {
		case BILLBOARD_TYPE_LIE:
			{
				float fCos = cosf(D3DXToRadian(m_fRotation)), fSin = sinf(D3DXToRadian(m_fRotation));
				v3Up.x = fCos;
				v3Up.y = -fSin;
				v3Up.z = 0;

				v3Cross.x = fSin;
				v3Cross.y = fCos;
				v3Cross.z = 0;
			}
			break;
		case BILLBOARD_TYPE_2FACE:
		case BILLBOARD_TYPE_3FACE:
			// using setting with y, and local rotation at render
		case BILLBOARD_TYPE_Y:
			{
				v3Up = D3DXVECTOR3(0.0f,0.0f,1.0f);
				//v3Up = D3DXVECTOR3(cosf(D3DXToRadian(m_fRotation)),0.0f,-sinf(D3DXToRadian(m_fRotation)));
				const D3DXVECTOR3 & c_rv3View = pCurrentCamera->GetView();
				if (v3Up.x * c_rv3View.y - v3Up.y * c_rv3View.x<0)
					v3Up*=-1;
				auto val = D3DXVECTOR3(c_rv3View.x,c_rv3View.y,0);
				D3DXVec3Cross(&v3Cross, &v3Up, &val);
				D3DXVec3Normalize(&v3Cross, &v3Cross);

				if (m_fRotation)
				{
					float fCos = -sinf(D3DXToRadian(m_fRotation)); // + 90
					float fSin = cosf(D3DXToRadian(m_fRotation));

					D3DXVECTOR3 v3Temp = v3Up * fCos - v3Cross * fSin;
					v3Cross = v3Cross * fCos + v3Up * fSin;
					v3Up = v3Temp;
				}

				//D3DXVECTOR3 v3Rotation;
				//D3DXVec3Cross(&v3Rotation, &v3Up, &v3Cross);

			}
			break;
		case BILLBOARD_TYPE_ALL:
		default:
			{
				if (m_fRotation==0.0f)
				{
					v3Up = -c_rv3Cross;
					v3Cross = c_rv3Up;
				}
				else
				{
					const D3DXVECTOR3 & c_rv3View = pCurrentCamera->GetView();
					D3DXMATRIX matRotation;

					D3DXMatrixRotationAxis(&matRotation, &c_rv3View, D3DXToRadian(m_fRotation));
					auto val = (-c_rv3Cross);
					D3DXVec3TransformCoord(&v3Up, &val, &matRotation);
					D3DXVec3TransformCoord(&v3Cross, &c_rv3Up, &matRotation);
				}
			}
			break;
		}
	}
	else
	{
		v3Up = m_v3Position - m_v3LastPosition;

		if (c_matLocal)
		{
			//if (!m_pParticleProperty->m_bAttachFlag)
				D3DXVec3TransformNormal(&v3Up, &v3Up, c_matLocal);
		}

		float length = D3DXVec3Length(&v3Up);
		if (length == 0.0f)
		{
			v3Up = D3DXVECTOR3(0.0f,0.0f,1.0f);
		}
		else
			v3Up *=(1+log(1+length))/length;
		//D3DXVec3Normalize(&v3Up,&v3Up);
		//v3Up *= 1+log(1+length);

		CCamera * pCurrentCamera = CCameraManager::Instance().GetCurrentCamera();
		const D3DXVECTOR3 & c_rv3View = pCurrentCamera->GetView();
		D3DXVec3Cross(&v3Cross, &v3Up, &c_rv3View);
		D3DXVec3Normalize(&v3Cross, &v3Cross);

	}

	if (c_fZRotation)
	{
		float x, y;
		float fCos = cosf(c_fZRotation);
		float fSin = sinf(c_fZRotation);

		x = v3Up.x;
		y = v3Up.y;
		v3Up.x = x * fCos - y * fSin;
		v3Up.y = y * fCos + x * fSin;

		x = v3Cross.x;
		y = v3Cross.y;
		v3Cross.x = x * fCos - y * fSin;
		v3Cross.y = y * fCos + x * fSin;
	}

	v3Cross = -(m_v2HalfSize.x*m_v2Scale.x) * v3Cross;
	v3Up = (m_v2HalfSize.y*m_v2Scale.y) * v3Up;

	if (c_matLocal && m_pParticleProperty->m_bAttachFlag)
	{
		D3DXVECTOR3 v3Position;
		D3DXVec3TransformCoord(&v3Position, &m_v3Position, c_matLocal);
		m_ParticleMesh[0].position = v3Position - v3Up + v3Cross;
		m_ParticleMesh[1].position = v3Position - v3Up - v3Cross;
		m_ParticleMesh[2].position = v3Position + v3Up + v3Cross;
		m_ParticleMesh[3].position = v3Position + v3Up - v3Cross;
	}
	else
	{
		m_ParticleMesh[0].position = m_v3Position - v3Up + v3Cross;
		m_ParticleMesh[1].position = m_v3Position - v3Up - v3Cross;
		m_ParticleMesh[2].position = m_v3Position + v3Up + v3Cross;
		m_ParticleMesh[3].position = m_v3Position + v3Up - v3Cross;
	}
}

void CParticleInstance::Destroy()
{
	if (m_pDecorator)
		m_pDecorator->DeleteThis();

	__Initialize();
}

void CParticleInstance::__Initialize()
{
	//*
	m_pDecorator= nullptr;
    m_compactUpdate = false;

	m_v3Position = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	m_v3LastPosition = m_v3Position;
	m_v3Velocity = D3DXVECTOR3(0.0f, 0.0f, 0.0f);

	m_v2Scale = D3DXVECTOR2(1.0f, 1.0f);
#ifdef WORLD_EDITOR
	m_Color = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
#else
	m_dcColor.m_dwColor = 0xffffffff;
#endif

	m_byFrameIndex = 0;
	m_ParticleMesh[0].texCoord = D3DXVECTOR2(0.0f, 1.0f);
	m_ParticleMesh[1].texCoord = D3DXVECTOR2(0.0f, 0.0f);
	m_ParticleMesh[2].texCoord = D3DXVECTOR2(1.0f, 1.0f);
	m_ParticleMesh[3].texCoord = D3DXVECTOR2(1.0f, 0.0f);
}

CParticleInstance::CParticleInstance()
{
	__Initialize();
}

CParticleInstance::~CParticleInstance()
{
	Destroy();
}

/*CRayParticleInstance::CRayParticleInstance()
{
#ifdef RAY_TO_AFTERIMAGE
	int i;
	for(i=0;i<RAY_VERTEX_COUNT*2;i+=2)
	{
		m_ParticleMesh[i].texCoord = D3DXVECTOR2(i+0.0f, 1.0f);
		m_ParticleMesh[i+1].texCoord = D3DXVECTOR2(i+1.0f, 0.0f);
	}
#else
	m_ParticleMesh[0].texCoord = D3DXVECTOR2(0.0f, 1.0f);
	m_ParticleMesh[1].texCoord = D3DXVECTOR2(0.0f, 0.0f);
	m_ParticleMesh[2].texCoord = D3DXVECTOR2(0.5f, 1.0f);
	m_ParticleMesh[3].texCoord = D3DXVECTOR2(0.5f, 0.0f);
	m_ParticleMesh[4].texCoord = D3DXVECTOR2(1.0f, 1.0f);
	m_ParticleMesh[5].texCoord = D3DXVECTOR2(1.0f, 0.0f);

	m_bStart = false;
#endif
}

CRayParticleInstance::~CRayParticleInstance()
{
}*/

TPTVertex * CParticleInstance::GetParticleMeshPointer()
{
	return m_ParticleMesh;
}

/*TPTVertex * CRayParticleInstance::GetParticleMeshPointer()
{
	return m_ParticleMesh;
}

void CRayParticleInstance::Transform(const D3DXMATRIX * c_matLocal, const float c_fZRotation)
{
	assert(false && "NOT_REACHED");
}*/

