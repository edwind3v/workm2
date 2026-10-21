#pragma once

#include "../eterlib/GrpBase.h"
#include "../eterLib/Pool.h"
#include "EffectUpdateDecorator.h"
class CParticleProperty;
class CEmitterProperty;
class CCamera;

// Shared only for one particle-system render; camera movement never reuses a
// previous frame's basis. Scales, rotations and attachment positions stay local.
struct TParticleRenderContext
{
    D3DXVECTOR3 up, cross, view, yCross;
    explicit TParticleRenderContext(const CCamera* camera);
};

struct TParticleRenderGeometry
{
    D3DXVECTOR3 position, up, cross;
};

class CParticleInstance
{
	friend class CParticleSystemData;
	friend class CParticleSystemInstance;

	friend class NEffectUpdateDecorator::CBaseDecorator;
	friend class NEffectUpdateDecorator::CAirResistanceDecorator;
	friend class NEffectUpdateDecorator::CGravityDecorator;
	friend class NEffectUpdateDecorator::CRotationDecorator;

	public:
		CParticleInstance();
		~CParticleInstance();

		float GetRadiusApproximation() const;

		BOOL Update(float fElapsedTime, float fAngle);
        static bool CompactUpdateEnabled();
        void InitializeCompactUpdate(const CParticleInstance* first);

	protected:
		D3DXVECTOR3			m_v3StartPosition;

		D3DXVECTOR3			m_v3Position;
		D3DXVECTOR3			m_v3LastPosition;
		D3DXVECTOR3			m_v3Velocity;

		D3DXVECTOR2			m_v2HalfSize;
		D3DXVECTOR2			m_v2Scale;

		float				m_fRotation;
#ifdef WORLD_EDITOR
		D3DXCOLOR			m_Color;
#else
		DWORDCOLOR			m_dcColor;
#endif

		BYTE				m_byTextureAnimationType;
		float				m_fLastFrameTime;
		BYTE				m_byFrameIndex;

		float				m_fLifeTime;
		float				m_fLastLifeTime;

		CParticleProperty *	m_pParticleProperty;
		CEmitterProperty *	m_pEmitterProperty;

		float m_fAirResistance;
		float m_fRotationSpeed;
		float m_fGravity;

		NEffectUpdateDecorator::CBaseDecorator * m_pDecorator;
        // Per-particle curve cursors replace separately allocated decorators.
        unsigned m_curveCursor[6]{};
        bool m_compactUpdate = false;
        BYTE m_updateRotationType = 0, m_updateTextureType = 0;
        void UpdateCompact(float lifePercentage, float elapsed);
	public:
		static CParticleInstance* New();
		static void DestroySystem();

		void Transform(const D3DXMATRIX * c_matLocal= nullptr, bool setColorState = true);
		void Transform(const D3DXMATRIX * c_matLocal, const float c_fZRotation, bool setColorState = true);
		DWORD GetColor() const;
        void PrepareRender(const D3DXMATRIX* local, const TParticleRenderContext& context,
            TParticleRenderGeometry& geometry) const;
        void WriteRenderQuad(const TParticleRenderGeometry& geometry, TPDTVertex* vertices,
            bool rotate = false, float cosine = 1.0f, float sine = 0.0f) const;

		TPTVertex * GetParticleMeshPointer();

		void DeleteThis();

		void Destroy();

	protected:
		void __Initialize();
		TPTVertex			m_ParticleMesh[4];
	public:
		static CDynamicPool<CParticleInstance> ms_kPool;
};

