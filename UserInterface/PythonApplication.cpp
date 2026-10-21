#include "StdAfx.h"
#include "../EterBase/MapPerformance.h"
#include "../eterBase/Error.h"
#include "../eterlib/Camera.h"
#include "../eterlib/AttributeInstance.h"
#include "../eterLib/DX11Probe.h"
#include "../eterLib/DX11GrannyProbe.h"
#include "../gamelib/AreaTerrain.h"
#include "../EterGrnLib/Material.h"
#include "../CWebBrowser/CWebBrowser.h"

#include "resource.h"
#include "PythonApplication.h"
#include "PythonLoading.h"
#include "PythonCharacterManager.h"

#include "ProcessScanner.h"

#include "CheckLatestFiles.h"

extern void GrannyCreateSharedDeformBuffer();
extern void GrannyDestroySharedDeformBuffer();

float MIN_FOG = 2400.0f;
double g_specularSpd=0.007f;

CPythonApplication * CPythonApplication::ms_pInstance;

float c_fDefaultCameraRotateSpeed = 1.5f;
float c_fDefaultCameraPitchSpeed = 1.5f;
float c_fDefaultCameraZoomSpeed = 0.05f;

#ifdef ENABLE_AREA_OPTIMIZATION
DWORD GetUpdateFPS() { return CPythonApplication::Instance().GetUpdateFPS(); }
#endif

CPythonApplication::CPythonApplication() :
m_bCursorVisible(TRUE),
m_bLiarCursorOn(false),
m_iCursorMode(CURSOR_MODE_HARDWARE),
m_isWindowed(false),
m_isFrameSkipDisable(false),
m_poMouseHandler(nullptr),
m_dwUpdateFPS(0),
m_dwRenderFPS(0),
m_fAveRenderTime(0.0f),
m_dwFaceCount(0),
m_fGlobalTime(0.0f),
m_fGlobalElapsedTime(0.0f),
m_dwLButtonDownTime(0),
m_dwLastIdleTime(0)
{
	m_dx11SharedPreviewTexture = nullptr;
	m_dx11ModelPreviewTexture = nullptr;
	m_dx11ModelPreviewAttempted = false;
#ifndef _DEBUG
	SetEterExceptionHandler();
#endif

	CTimer::Instance().UseCustomTime();
	m_dwWidth = 800;
	m_dwHeight = 600;

	ms_pInstance = this;
	m_isWindowFullScreenEnable = FALSE;

	m_v3CenterPosition = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	m_dwStartLocalTime = ELTimer_GetMSec();
	m_tServerTime = 0;
	m_tLocalStartTime = 0;

	m_iPort = 0;
	m_iFPS = 60;

	m_isActivateWnd = false;
	m_isMinimizedWnd = true;

	m_fRotationSpeed = 0.0f;
	m_fPitchSpeed = 0.0f;
	m_fZoomSpeed = 0.0f;

	m_fFaceSpd=0.0f;

	m_dwFaceAccCount=0;
	m_dwFaceAccTime=0;

	m_dwFaceSpdSum=0;
	m_dwFaceSpdCount=0;

	m_FlyingManager.SetMapManagerPtr(&m_pyBackground);

	m_iCursorNum = CURSOR_SHAPE_NORMAL;
	m_iContinuousCursorNum = CURSOR_SHAPE_NORMAL;

	m_isSpecialCameraMode = FALSE;
	m_fCameraRotateSpeed = c_fDefaultCameraRotateSpeed;
	m_fCameraPitchSpeed = c_fDefaultCameraPitchSpeed;
	m_fCameraZoomSpeed = c_fDefaultCameraZoomSpeed;

	m_iCameraMode = CAMERA_MODE_NORMAL;
	m_fBlendCameraStartTime = 0.0f;
	m_fBlendCameraBlendTime = 0.0f;

	m_iForceSightRange = -1;

	CCameraManager::Instance().AddCamera(EVENT_CAMERA_NUMBER);
}

CPythonApplication::~CPythonApplication()
{
	if (m_dx11ModelPreviewTexture)
	{
		m_dx11ModelPreviewTexture->Release();
		m_dx11ModelPreviewTexture = nullptr;
	}
	if (m_dx11SharedPreviewTexture)
	{
		m_dx11SharedPreviewTexture->Release();
		m_dx11SharedPreviewTexture = nullptr;
	}
}

void CPythonApplication::GetMousePosition(POINT* ppt)
{
	CMSApplication::GetMousePosition(ppt);
}

void CPythonApplication::SetMinFog(float fMinFog) const
{
	MIN_FOG = fMinFog;
}

void CPythonApplication::SetFrameSkip(bool isEnable)
{
	if (isEnable)
		m_isFrameSkipDisable=false;
	else
		m_isFrameSkipDisable=true;
}

void CPythonApplication::NotifyHack(const char* c_szFormat, ...)
{
	char szBuf[1024];

	va_list args;
	va_start(args, c_szFormat);
	_vsnprintf(szBuf, sizeof(szBuf), c_szFormat, args);
	va_end(args);
	m_pyNetworkStream.NotifyHack(szBuf);
}

void CPythonApplication::GetInfo(UINT eInfo, std::string* pstInfo)
{
	switch (eInfo)
	{
	case INFO_ACTOR:
		m_kChrMgr.GetInfo(pstInfo);
		break;
	case INFO_EFFECT:
		m_kEftMgr.GetInfo(pstInfo);
		break;
	case INFO_ITEM:
		m_pyItem.GetInfo(pstInfo);
		break;
	case INFO_TEXTTAIL:
		m_pyTextTail.GetInfo(pstInfo);
		break;
	}
}

#ifdef ENABLE_BL_TRACEBACK
void PrintTraceStack()
{
	PyThreadState* tstate = PyThreadState_GET();
	if (tstate)
	{
		for (PyFrameObject* frame = tstate->frame; frame; frame = frame->f_back)
		{
			PyCodeObject* f_code = frame->f_code;
			if (!f_code || !f_code->co_filename || !f_code->co_name)
				continue;

			const char* filename = PyString_AsString(f_code->co_filename);
			const char* funcname = PyString_AsString(f_code->co_name);
			const auto firstline = f_code->co_firstlineno;
			const auto line = PyFrame_GetLineNumber(frame);
			TraceError("filename=%s, name=%s, line=%d", filename, funcname, line);
		}
	}
}
#endif

void CPythonApplication::Abort() const
{
	TraceError("============================================================================================================");
	TraceError("Abort!!!!\n\n");

	#ifdef ENABLE_BL_TRACEBACK
	PrintTraceStack();
	#endif

	PostQuitMessage(0);
}

void CPythonApplication::Exit() const
{
	PostQuitMessage(0);
}

bool PERF_CHECKER_RENDER_GAME = false;

void CPythonApplication::RenderGame()
{
	MapPerformance::Scope mapProfile("game.render");
	if (!PERF_CHECKER_RENDER_GAME)
	{
		const float fAspect=m_kWndMgr.GetAspect();
		const float fFarClip=m_pyBackground.GetFarClip();

		m_pyGraphic.SetPerspective(30.0f, fAspect, 100.0, fFarClip);

		MapPerformance::Measure("render.culling", nullptr, [&] { CCullingManager::Instance().Process(); });

		MapPerformance::Measure("render.character_deform", nullptr, [&] { m_kChrMgr.Deform(); });

		MapPerformance::Measure("render.character_shadows", nullptr, [&] { m_pyBackground.RenderCharacterShadowToTexture(); });

		m_pyGraphic.SetGameRenderState();
		m_pyGraphic.PushState();

		{
			long lx, ly;
			m_kWndMgr.GetMousePosition(lx, ly);
			m_pyGraphic.SetCursorPosition(lx, ly);
		}

		m_pyBackground.RenderSky();

		m_pyBackground.RenderBeforeLensFlare();

		m_pyBackground.RenderCloud();

		m_pyBackground.BeginEnvironment();
		MapPerformance::Measure("render.terrain_and_objects", nullptr, [&] { m_pyBackground.Render(); });

		m_pyBackground.SetCharacterDirLight();
		MapPerformance::Measure("render.characters", nullptr, [&] { m_kChrMgr.Render(); });

		m_pyBackground.SetBackgroundDirLight();
		MapPerformance::Measure("render.water", nullptr, [&] { m_pyBackground.RenderWater(); });
		m_pyBackground.RenderSnow();
		MapPerformance::Measure("render.map_effects", nullptr, [&] { m_pyBackground.RenderEffect(); });

		m_pyBackground.EndEnvironment();

		MapPerformance::Measure("render.effects", nullptr, [&] { m_kEftMgr.Render(); });
		m_pyItem.Render();
		m_FlyingManager.Render();

		m_pyBackground.BeginEnvironment();
		MapPerformance::Measure("render.blockers", nullptr, [&] { m_pyBackground.RenderPCBlocker(); });
		m_pyBackground.EndEnvironment();

		m_pyBackground.RenderAfterLensFlare();

		return;
	}

	const DWORD t1=ELTimer_GetMSec();
	MapPerformance::Measure("render.character_deform", nullptr, [&] { m_kChrMgr.Deform(); });
	const DWORD t2=ELTimer_GetMSec();
	const DWORD t3=ELTimer_GetMSec();
	MapPerformance::Measure("render.character_shadows", nullptr, [&] { m_pyBackground.RenderCharacterShadowToTexture(); });
	const DWORD t4=ELTimer_GetMSec();

	m_pyGraphic.SetGameRenderState();
	m_pyGraphic.PushState();

	const float fAspect=m_kWndMgr.GetAspect();
	const float fFarClip=m_pyBackground.GetFarClip();

	m_pyGraphic.SetPerspective(30.0f, fAspect, 100.0, fFarClip);

	const DWORD t5=ELTimer_GetMSec();

	MapPerformance::Measure("render.culling", nullptr, [&] { CCullingManager::Instance().Process(); });

	const DWORD t6=ELTimer_GetMSec();

	{
		long lx, ly;
		m_kWndMgr.GetMousePosition(lx, ly);
		m_pyGraphic.SetCursorPosition(lx, ly);
	}

	m_pyBackground.RenderSky();
	const DWORD t7=ELTimer_GetMSec();
	m_pyBackground.RenderBeforeLensFlare();
	const DWORD t8=ELTimer_GetMSec();
	m_pyBackground.RenderCloud();
	const DWORD t9=ELTimer_GetMSec();
	m_pyBackground.BeginEnvironment();
	MapPerformance::Measure("render.terrain_and_objects", nullptr, [&] { m_pyBackground.Render(); });

	m_pyBackground.SetCharacterDirLight();
	const DWORD t10=ELTimer_GetMSec();
	MapPerformance::Measure("render.characters", nullptr, [&] { m_kChrMgr.Render(); });
	const DWORD t11=ELTimer_GetMSec();

	m_pyBackground.SetBackgroundDirLight();
	MapPerformance::Measure("render.water", nullptr, [&] { m_pyBackground.RenderWater(); });
	const DWORD t12=ELTimer_GetMSec();
	MapPerformance::Measure("render.map_effects", nullptr, [&] { m_pyBackground.RenderEffect(); });
	const DWORD t13=ELTimer_GetMSec();
	m_pyBackground.EndEnvironment();
	MapPerformance::Measure("render.effects", nullptr, [&] { m_kEftMgr.Render(); });
	const DWORD t14=ELTimer_GetMSec();
	m_pyItem.Render();
	const DWORD t15=ELTimer_GetMSec();
	m_FlyingManager.Render();
	const DWORD t16=ELTimer_GetMSec();
	m_pyBackground.BeginEnvironment();
	MapPerformance::Measure("render.blockers", nullptr, [&] { m_pyBackground.RenderPCBlocker(); });
	m_pyBackground.EndEnvironment();
	const DWORD t17=ELTimer_GetMSec();
	m_pyBackground.RenderAfterLensFlare();
	const DWORD t18=ELTimer_GetMSec();
	const DWORD tEnd=ELTimer_GetMSec();

	if (GetAsyncKeyState(VK_Z))
		STATEMANAGER.SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);

	if (tEnd-t1<3)
		return;

	static FILE* fp=fopen("perf_game_render.txt", "w");

	fprintf(fp, "GR.Total %d (Time %d)\n", tEnd-t1, ELTimer_GetMSec());
	fprintf(fp, "GR.DFM %d\n", t2-t1);
	fprintf(fp, "GR.EFT.UP %d\n", t3-t2);
	fprintf(fp, "GR.SHW %d\n", t4-t3);
	fprintf(fp, "GR.STT %d\n", t5-t4);
	fprintf(fp, "GR.CLL %d\n", t6-t5);
	fprintf(fp, "GR.BG.SKY %d\n", t7-t6);
	fprintf(fp, "GR.BG.LEN %d\n", t8-t7);
	fprintf(fp, "GR.BG.CLD %d\n", t9-t8);
	fprintf(fp, "GR.BG.MAIN %d\n", t10-t9);
	fprintf(fp, "GR.CHR %d\n",	t11-t10);
	fprintf(fp, "GR.BG.WTR %d\n", t12-t11);
	fprintf(fp, "GR.BG.EFT %d\n", t13-t12);
	fprintf(fp, "GR.EFT %d\n", t14-t13);
	fprintf(fp, "GR.ITM %d\n", t15-t14);
	fprintf(fp, "GR.FLY %d\n", t16-t15);
	fprintf(fp, "GR.BG.BLK %d\n", t17-t16);
	fprintf(fp, "GR.BG.LEN %d\n", t18-t17);

	fflush(fp);
}

void CPythonApplication::UpdateGame()
{
	MapPerformance::Scope mapProfile("game.update");
	const DWORD t1=ELTimer_GetMSec();
	POINT ptMouse;
	GetMousePosition(&ptMouse);

	CGraphicTextInstance::Hyperlink_UpdateMousePos(ptMouse.x, ptMouse.y);

	const DWORD t2=ELTimer_GetMSec();

	//if (m_isActivateWnd)
	{
		CScreen s;
		const float fAspect = UI::CWindowManager::Instance().GetAspect();
		const float fFarClip = CPythonBackground::Instance().GetFarClip();

		s.SetPerspective(30.0f,fAspect, 100.0f, fFarClip);
		s.BuildViewFrustum();
	}

	const DWORD t3=ELTimer_GetMSec();
	TPixelPosition kPPosMainActor;
	m_pyPlayer.NEW_GetMainActorPosition(&kPPosMainActor);

	const DWORD t4=ELTimer_GetMSec();
	MapPerformance::Measure("game.background_update", nullptr, [&] { m_pyBackground.Update(kPPosMainActor.x, kPPosMainActor.y, kPPosMainActor.z); });
#ifdef ENABLE_AREA_OPTIMIZATION
	CPythonNetworkStream::Instance().TryRefreshHeightsAfterWarp();
#endif

	const DWORD t5=ELTimer_GetMSec();
	m_GameEventManager.SetCenterPosition(kPPosMainActor.x, kPPosMainActor.y, kPPosMainActor.z);
	m_GameEventManager.Update();

	const DWORD t6=ELTimer_GetMSec();
	MapPerformance::Measure("game.characters_update", nullptr, [&] { m_kChrMgr.Update(); });
	const DWORD t7=ELTimer_GetMSec();
	MapPerformance::Measure("game.effects_update", nullptr, [&] { m_kEftMgr.Update(); }); //@fixme029
	m_kEftMgr.UpdateSound();
	const DWORD t8=ELTimer_GetMSec();
	m_FlyingManager.Update();
	const DWORD t9=ELTimer_GetMSec();
	m_pyItem.Update(ptMouse);
	const DWORD t10=ELTimer_GetMSec();
	m_pyPlayer.Update();
	const DWORD t11=ELTimer_GetMSec();

	m_pyPlayer.NEW_GetMainActorPosition(&kPPosMainActor);
	SetCenterPosition(kPPosMainActor.x, kPPosMainActor.y, kPPosMainActor.z);
	const DWORD t12=ELTimer_GetMSec();

	if (PERF_CHECKER_RENDER_GAME)
	{
		if (t12-t1>5)
		{
			static FILE* fp=fopen("perf_game_update.txt", "w");

			fprintf(fp, "GU.Total %d (Time %d)\n", t12-t1, ELTimer_GetMSec());
			fprintf(fp, "GU.GMP %d\n", t2-t1);
			fprintf(fp, "GU.SCR %d\n", t3-t2);
			fprintf(fp, "GU.MPS %d\n", t4-t3);
			fprintf(fp, "GU.BG %d\n", t5-t4);
			fprintf(fp, "GU.GEM %d\n", t6-t5);
			fprintf(fp, "GU.CHR %d\n", t7-t6);
			fprintf(fp, "GU.EFT %d\n", t8-t7);
			fprintf(fp, "GU.FLY %d\n", t9-t8);
			fprintf(fp, "GU.ITM %d\n", t10-t9);
			fprintf(fp, "GU.PLR %d\n", t11-t10);
			fprintf(fp, "GU.POS %d\n", t12-t11);
			fflush(fp);
		}
	}
}

void CPythonApplication::SkipRenderBuffering(DWORD dwSleepMSec)
{
	m_dwBufSleepSkipTime=ELTimer_GetMSec()+dwSleepMSec;
}

bool CPythonApplication::Process()
{
#if defined(CHECK_LATEST_DATA_FILES)
	if (CheckLatestFiles_PollEvent())
		return false;
#endif
	// Diagnostic migration step: load one genuine game texture via the pack
	// manager and preview it through the independent DX11 resource path.
	// This runs once only when explicitly requested, never during normal play.
	static bool s_dx11AssetAttempted = false;
	if (!s_dx11AssetAttempted)
	{
		s_dx11AssetAttempted = true;
		char assetPath[512] = {};
		const DWORD assetPathLength = GetEnvironmentVariableA(
			"M2_DX11_ASSET_PATH", assetPath, ARRAYSIZE(assetPath));
		if (assetPathLength >= ARRAYSIZE(assetPath))
		{
			TraceError("DX11_ASSET_FAILED stage=AssetPathTooLong hr=0x%08X game_renderer=DX9Ex",
				static_cast<unsigned>(HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)));
		}
		else if (assetPathLength > 0 && m_isWindowed)
		{
			char sharedPreviewFlag[8] = {};
			const DWORD sharedFlagLength = GetEnvironmentVariableA(
				"M2_DX11_SHARED_PREVIEW", sharedPreviewFlag, ARRAYSIZE(sharedPreviewFlag));
			const bool requestSharedPreview = sharedFlagLength == 1 && sharedPreviewFlag[0] == '1';
			graphics::DX11ProbeResult assetResult;
			graphics::DX11SharedProbeResult sharedResult;
			const HRESULT assetHr = graphics::RunDX11AssetProbe(
				GetWindowHandle(), m_grpDevice.GetDevice(), assetPath,
				L"dx11_asset_probe.bmp", &assetResult,
				requestSharedPreview ? m_grpDevice.GetDevice() : nullptr,
				requestSharedPreview ? &sharedResult : nullptr);
			if (SUCCEEDED(assetHr))
			{
				TraceError("DX11_ASSET_DRAW path=\"%s\" source=%ux%u center_rgba=%u,%u,%u,%u image_saved=%u game_renderer=DX9Ex",
					assetPath, assetResult.sourceWidth, assetResult.sourceHeight,
					assetResult.centerPixelRGBA[0], assetResult.centerPixelRGBA[1],
					assetResult.centerPixelRGBA[2], assetResult.centerPixelRGBA[3],
					assetResult.bitmapSaved ? 1 : 0);
			}
			else
			{
				TraceError("DX11_ASSET_FAILED path=\"%s\" stage=%s hr=0x%08X game_renderer=DX9Ex",
					assetPath, assetResult.stage, static_cast<unsigned>(assetHr));
			}
			if (requestSharedPreview)
			{
				if (SUCCEEDED(sharedResult.status) && sharedResult.texture)
				{
					m_dx11SharedPreviewTexture = sharedResult.texture;
					TraceError("DX11_SHARED_READY path=\"%s\" format=%s size=256x256 center_rgba=%u,%u,%u,%u game_renderer=DX9Ex",
						assetPath, sharedResult.format, sharedResult.centerPixelRGBA[0],
						sharedResult.centerPixelRGBA[1], sharedResult.centerPixelRGBA[2],
						sharedResult.centerPixelRGBA[3]);
				}
				else if (SUCCEEDED(assetHr))
				{
					TraceError("DX11_SHARED_FAILED path=\"%s\" stage=%s hr=0x%08X game_renderer=DX9Ex",
						assetPath, sharedResult.stage, static_cast<unsigned>(sharedResult.status));
				}
			}
		}
	}
	// A real animated/rigid Granny mesh may only become available after the
	// character scene starts rendering. Consume its one-shot CPU snapshot on
	// the next frame, outside the D3D9 BeginScene / EndScene interval.
	if (m_isWindowed && !m_dx11ModelPreviewAttempted)
	{
		static bool modelCaptureConfigReported = false;
		if (!modelCaptureConfigReported)
		{
			modelCaptureConfigReported = true;
			const char* captureMode = graphics::GetDX11GrannyCaptureMode();
			if (strcmp(captureMode, "off") != 0)
				TraceError("DX11_MODEL_CAPTURE_CONFIG mode=%s retry_ms=500 max_attempts=32 game_renderer=DX9Ex",
					captureMode);
		}
		graphics::DX11GrannySnapshot snapshot;
		if (graphics::TakeDX11GrannySnapshot(&snapshot))
		{
			m_dx11ModelPreviewAttempted = true;
			unsigned char modelPixel[4] = {};
			UINT modelCoveredPixels = 0;
			UINT modelTexturesLoaded = 0;
			UINT modelTexturesMissing = 0;
			const char* modelStage = "NotAttempted";
			const char* sharedFormat = "none";
			const HRESULT modelHr = graphics::RunDX11GrannyPreview(
				GetWindowHandle(), m_grpDevice.GetDevice(), snapshot,
				&m_dx11ModelPreviewTexture, modelPixel, &modelCoveredPixels,
				&modelTexturesLoaded, &modelTexturesMissing,
				&modelStage, &sharedFormat);
			if (SUCCEEDED(modelHr))
			{
				TraceError("DX11_MODEL_READY source=%s mesh=\"%.80s\" meshes=%u vertices=%u indices=%u material_groups=%u textures_loaded=%u textures_missing=%u format=%s drawn_pixels=%u center_rgba=%u,%u,%u,%u game_renderer=DX9Ex",
					snapshot.skinned ? "granny_deformed" : "granny_rigid",
					snapshot.meshName.c_str(),
					static_cast<unsigned>(snapshot.meshCount),
					static_cast<unsigned>(snapshot.vertices.size()),
					static_cast<unsigned>(snapshot.indices.size()),
					static_cast<unsigned>(snapshot.groups.size()),
					modelTexturesLoaded, modelTexturesMissing, sharedFormat,
					modelCoveredPixels,
					modelPixel[0], modelPixel[1], modelPixel[2], modelPixel[3]);
				if (!snapshot.groups.empty())
				{
					TraceError("DX11_MODEL_MATERIAL first_diffuse=\"%.180s\" first_opacity=\"%.180s\"",
						snapshot.groups[0].diffusePath.c_str(),
						snapshot.groups[0].opacityPath.c_str());
					UINT blendedGroups = 0;
					UINT twoSidedGroups = 0;
					UINT specularGroups = 0;
					for (const auto& group : snapshot.groups)
					{
						if (group.alphaBlend) ++blendedGroups;
						if (group.twoSided) ++twoSidedGroups;
						if (group.specularEnabled) ++specularGroups;
					}
					TraceError("DX11_MODEL_MATERIAL_STATE blend_groups=%u two_sided_groups=%u specular_groups=%u model_params_bytes=48 sphere_map=unimplemented",
						blendedGroups, twoSidedGroups, specularGroups);
				}
			}
			else
			{
				TraceError("DX11_MODEL_FAILED stage=%s hr=0x%08X game_renderer=DX9Ex",
					modelStage, static_cast<unsigned>(modelHr));
			}
		}
	}
	MapPerformance::Frame mapProfileFrame;
	ELTimer_SetFrameMSec();

	// 	m_Profiler.Clear();
	const DWORD dwStart = ELTimer_GetMSec();

	///////////////////////////////////////////////////////////////////////////////////////////////////
	static DWORD	s_dwUpdateFrameCount = 0;
	static DWORD	s_dwRenderFrameCount = 0;
	static DWORD	s_dwFaceCount = 0;
	static UINT		s_uiLoad = 0;
	static DWORD	s_dwCheckTime = ELTimer_GetMSec();

    const DWORD fpsNow = ELTimer_GetMSec();
    const DWORD fpsElapsed = fpsNow - s_dwCheckTime;
	if (fpsElapsed >= 1000)
	{
        m_dwUpdateFPS = static_cast<DWORD>((static_cast<unsigned long long>(s_dwUpdateFrameCount) * 1000 + fpsElapsed / 2) / fpsElapsed);
        m_dwRenderFPS = static_cast<DWORD>((static_cast<unsigned long long>(s_dwRenderFrameCount) * 1000 + fpsElapsed / 2) / fpsElapsed);
		m_dwLoad			= s_uiLoad;

		m_dwFaceCount		= s_dwFaceCount / max(1, s_dwRenderFrameCount);

		s_dwCheckTime = fpsNow;

		s_uiLoad = s_dwFaceCount = s_dwUpdateFrameCount = s_dwRenderFrameCount = 0;
	}

	// Update Time
	static BOOL s_bFrameSkip = false;
	static UINT s_uiNextFrameTime = ELTimer_GetMSec();

#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime1=ELTimer_GetMSec();
#endif
	CTimer& rkTimer=CTimer::Instance();
	rkTimer.Advance();

	m_fGlobalTime = rkTimer.GetCurrentSecond();
	m_fGlobalElapsedTime = rkTimer.GetElapsedSecond();

	const UINT uiFrameTime = rkTimer.GetElapsedMilliecond();
	s_uiNextFrameTime += uiFrameTime;

	const DWORD updatestart = ELTimer_GetMSec();
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime2=ELTimer_GetMSec();
#endif
	// Network I/O
	MapPerformance::Measure("app.network", nullptr, [&] { m_pyNetworkStream.Process(); });

	m_kGuildMarkUploader.Process();

	m_kGuildMarkDownloader.Process();
	m_kAccountConnector.Process();

#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime3=ELTimer_GetMSec();
#endif
	//////////////////////
	// Input Process
	// Keyboard
	UpdateKeyboard();
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime4=ELTimer_GetMSec();
#endif
	// Mouse
	POINT Point;
	if (GetCursorPos(&Point))
	{
		ScreenToClient(m_hWnd, &Point);
		OnMouseMove(Point.x, Point.y);
	}
	//////////////////////
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime5=ELTimer_GetMSec();
#endif
	//if (m_isActivateWnd)
	__UpdateCamera();
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime6=ELTimer_GetMSec();
#endif
	// Update Game Playing
	MapPerformance::Measure("app.resource_update", nullptr, [&] { CResourceManager::Instance().Update(); });
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime7=ELTimer_GetMSec();
#endif
	OnCameraUpdate();
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime8=ELTimer_GetMSec();
#endif
	OnMouseUpdate();
#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime9=ELTimer_GetMSec();
#endif
	MapPerformance::Measure("app.ui_update", nullptr, [&] { OnUIUpdate(); });

#ifdef __PERFORMANCE_CHECK__
	DWORD dwUpdateTime10=ELTimer_GetMSec();

	if (dwUpdateTime10-dwUpdateTime1>10)
	{
		static FILE* fp=fopen("perf_app_update.txt", "w");

		fprintf(fp, "AU.Total %d (Time %d)\n", dwUpdateTime9-dwUpdateTime1, ELTimer_GetMSec());
		fprintf(fp, "AU.TU %d\n", dwUpdateTime2-dwUpdateTime1);
		fprintf(fp, "AU.NU %d\n", dwUpdateTime3-dwUpdateTime2);
		fprintf(fp, "AU.KU %d\n", dwUpdateTime4-dwUpdateTime3);
		fprintf(fp, "AU.MP %d\n", dwUpdateTime5-dwUpdateTime4);
		fprintf(fp, "AU.CP %d\n", dwUpdateTime6-dwUpdateTime5);
		fprintf(fp, "AU.RU %d\n", dwUpdateTime7-dwUpdateTime6);
		fprintf(fp, "AU.CU %d\n", dwUpdateTime8-dwUpdateTime7);
		fprintf(fp, "AU.MU %d\n", dwUpdateTime9-dwUpdateTime8);
		fprintf(fp, "AU.UU %d\n", dwUpdateTime10-dwUpdateTime9);
		fprintf(fp, "----------------------------------\n");
		fflush(fp);
	}
#endif

	m_dwCurUpdateTime = ELTimer_GetMSec() - updatestart;

	const DWORD dwCurrentTime = ELTimer_GetMSec();
	BOOL  bCurrentLateUpdate = FALSE;

	s_bFrameSkip = false;

	if (dwCurrentTime > s_uiNextFrameTime)
	{
		const int dt = dwCurrentTime - s_uiNextFrameTime;
		const int nAdjustTime = ((float)dt / (float)uiFrameTime) * uiFrameTime;

		if ( dt >= 500 )
		{
			s_uiNextFrameTime += nAdjustTime;
			printf("FrameSkip Adjusting... %d\n",nAdjustTime);
			CTimer::Instance().Adjust(nAdjustTime);
		}
		s_bFrameSkip = true;
		bCurrentLateUpdate = TRUE;
	}

	if (m_isFrameSkipDisable)
		s_bFrameSkip = false;

	if (!s_bFrameSkip)
	{
		CGrannyMaterial::TranslateSpecularMatrix(g_specularSpd, g_specularSpd, 0.0f);

		const DWORD dwRenderStartTime = ELTimer_GetMSec();

		bool canRender = true;

		if (m_isMinimizedWnd)
		{
			canRender = false;
		}
		else
		{
#ifdef ENABLE_FIX_MOBS_LAG
			// CheckDeviceState owns resource release, reset, and reconstruction.
			canRender = (DEVICE_STATE_OK == CheckDeviceState());
#else
			if (m_pyGraphic.IsLostDevice())
			{
				// Do not hold an external D3D9Ex render-target texture across
				// a graphics-device restore. The preview can be re-enabled by
				// restarting the client after recovery.
				if (m_dx11SharedPreviewTexture)
				{
					m_dx11SharedPreviewTexture->Release();
					m_dx11SharedPreviewTexture = nullptr;
				}
				if (m_dx11ModelPreviewTexture)
				{
					m_dx11ModelPreviewTexture->Release();
					m_dx11ModelPreviewTexture = nullptr;
				}
				CPythonBackground& rkBG = CPythonBackground::Instance();
				rkBG.ReleaseCharacterShadowTexture();

				if (m_pyGraphic.RestoreDevice())
					rkBG.CreateCharacterShadowTexture();
				else
					canRender = false;
			}
#endif
		}

		if (!IsActive())
		{
			SkipRenderBuffering(3000);
		}

		if (!canRender)
		{
			SkipRenderBuffering(3000);
		}
		else
		{
			// RestoreLostDevice
			CCullingManager::Instance().Update();
			if (m_pyGraphic.Begin())
			{
				mapProfileFrame.Rendered();
				m_pyGraphic.ClearDepthBuffer();

#ifdef _DEBUG
				m_pyGraphic.SetClearColor(0.3f, 0.3f, 0.3f);
				m_pyGraphic.Clear();
#endif

				/////////////////////
				// Interface
				m_pyGraphic.SetInterfaceRenderState();

				MapPerformance::Measure("app.ui_render", nullptr, [&] { OnUIRender(); });
				if (m_dx11ModelPreviewTexture)
				{
					// Make the single-mesh diagnostic large enough to inspect.
					// DX11GrannyProbe fits the model itself to the render texture.
					const HRESULT modelDraw = graphics::DrawDX11SharedPreview(
						m_grpDevice.GetDevice(), m_dx11ModelPreviewTexture, 128, 320);
					if (modelDraw == S_OK)
					{
						static bool s_modelPresented = false;
						if (!s_modelPresented)
						{
							s_modelPresented = true;
							TraceError("DX11_MODEL_PRESENT source=Granny/DX11 target=DX9Ex position=top_right size=320x320");
						}
					}
					if (FAILED(modelDraw))
					{
						TraceError("DX11_MODEL_FAILED stage=DrawModelOverlay hr=0x%08X game_renderer=DX9Ex",
							static_cast<unsigned>(modelDraw));
						m_dx11ModelPreviewTexture->Release();
						m_dx11ModelPreviewTexture = nullptr;
					}
				}
				if (m_dx11SharedPreviewTexture)
				{
					const HRESULT sharedDraw = graphics::DrawDX11SharedPreview(
						m_grpDevice.GetDevice(), m_dx11SharedPreviewTexture);
					if (sharedDraw == S_OK)
					{
						static bool s_sharedPresented = false;
						if (!s_sharedPresented)
						{
							s_sharedPresented = true;
							TraceError("DX11_SHARED_PRESENT source=DX11 target=DX9Ex position=top_right size=96x96");
						}
					}
					if (FAILED(sharedDraw))
					{
						TraceError("DX11_SHARED_FAILED stage=DrawD3D9Overlay hr=0x%08X game_renderer=DX9Ex",
							static_cast<unsigned>(sharedDraw));
						m_dx11SharedPreviewTexture->Release();
						m_dx11SharedPreviewTexture = nullptr;
					}
				}
				OnMouseRender();
				/////////////////////

				m_pyGraphic.End();

				MapPerformance::Measure("app.present", nullptr, [&] { m_pyGraphic.Show(); });

				const DWORD dwRenderEndTime = ELTimer_GetMSec();

				static DWORD s_dwRenderCheckTime = dwRenderEndTime;
				static DWORD s_dwRenderRangeTime = 0;
				static DWORD s_dwRenderRangeFrame = 0;

				m_dwCurRenderTime = dwRenderEndTime - dwRenderStartTime;
				s_dwRenderRangeTime += m_dwCurRenderTime;
				++s_dwRenderRangeFrame;

				if (dwRenderEndTime-s_dwRenderCheckTime>1000)
				{
					m_fAveRenderTime=float(double(s_dwRenderRangeTime)/double(s_dwRenderRangeFrame));

					s_dwRenderCheckTime=ELTimer_GetMSec();
					s_dwRenderRangeTime=0;
					s_dwRenderRangeFrame=0;
				}

				const DWORD dwCurFaceCount=m_pyGraphic.GetFaceCount();
				m_pyGraphic.ResetFaceCount();
				s_dwFaceCount += dwCurFaceCount;

				if (dwCurFaceCount > 5000)
				{
					if (dwRenderEndTime > m_dwBufSleepSkipTime)
					{
						static float s_fBufRenderTime = 0.0f;

						const float fCurRenderTime = m_dwCurRenderTime;

						if (fCurRenderTime > s_fBufRenderTime)
						{
							const float fRatio = fMAX(0.5f, (fCurRenderTime - s_fBufRenderTime) / 30.0f);
							s_fBufRenderTime = (s_fBufRenderTime * (100.0f - fRatio) + (fCurRenderTime + 5) * fRatio) / 100.0f;
						}
						else
						{
							const float fRatio = 0.5f;
							s_fBufRenderTime = (s_fBufRenderTime * (100.0f - fRatio) + fCurRenderTime * fRatio) / 100.0f;
						}

						if (s_fBufRenderTime > 100.0f)
							s_fBufRenderTime = 100.0f;

						DWORD dwBufRenderTime = s_fBufRenderTime;

						if (m_isWindowed)
						{
							if (dwBufRenderTime>58)
								dwBufRenderTime=64;
							else if (dwBufRenderTime>42)
								dwBufRenderTime=48;
							else if (dwBufRenderTime>26)
								dwBufRenderTime=32;
							else if (dwBufRenderTime>10)
								dwBufRenderTime=16;
							else
								dwBufRenderTime=8;
						}

						m_fAveRenderTime=s_fBufRenderTime;
					}

					m_dwFaceAccCount += dwCurFaceCount;
					m_dwFaceAccTime += m_dwCurRenderTime;

					m_fFaceSpd=(m_dwFaceAccCount/m_dwFaceAccTime);

					if (-1 == m_iForceSightRange)
					{
						static float s_fAveRenderTime = 16.0f;
						const float fRatio=0.3f;
						s_fAveRenderTime=(s_fAveRenderTime*(100.0f-fRatio)+max(16.0f, m_dwCurRenderTime)*fRatio)/100.0f;

						const float fFar=25600.0f;
						const float fNear=MIN_FOG;
						const double dbAvePow=double(1000.0f/s_fAveRenderTime);
						const double dbMaxPow=60.0;
						const float fDistance=max(fNear+(fFar-fNear)*(dbAvePow)/dbMaxPow, fNear);
						m_pyBackground.SetViewDistanceSet(0, fDistance);
					}
					else
					{
						m_pyBackground.SetViewDistanceSet(0, float(m_iForceSightRange));
					}
				}
				else
				{
					m_pyBackground.SetViewDistanceSet(0, 25600.0f);
				}

				++s_dwRenderFrameCount;
			}
		}
	}

	// Finish before intentional frame-pacing sleep; profiling I/O is outside the capture.
	mapProfileFrame.Finish(IsActive());
	const int rest = s_uiNextFrameTime - ELTimer_GetMSec();

	if (rest > 0 && !bCurrentLateUpdate )
	{
		s_uiLoad -= rest;
		Sleep(rest);
	}

	++s_dwUpdateFrameCount;

	s_uiLoad += ELTimer_GetMSec() - dwStart;
	//m_Profiler.ProfileByScreen();
	return true;
}

void CPythonApplication::UpdateClientRect()
{
	RECT rcApp;
	GetClientRect(&rcApp);
	OnSizeChange(rcApp.right - rcApp.left, rcApp.bottom - rcApp.top);
}

void CPythonApplication::SetMouseHandler(PyObject* poMouseHandler)
{
	m_poMouseHandler = poMouseHandler;
}

int CPythonApplication::CheckDeviceState()
{
	const CGraphicDevice::EDeviceState e_deviceState = m_grpDevice.GetDeviceState();

	switch (e_deviceState)
	{
	case CGraphicDevice::DEVICESTATE_NULL:
		return DEVICE_STATE_FALSE;

	case CGraphicDevice::DEVICESTATE_BROKEN:
		return DEVICE_STATE_SKIP;

	case CGraphicDevice::DEVICESTATE_NEEDS_RESET:
#ifdef ENABLE_FIX_MOBS_LAG
		// This client has shadow targets and DX11 preview textures, but no
		// CRenderTargetManager. Release its actual external targets here.
		if (m_dx11SharedPreviewTexture)
		{
			m_dx11SharedPreviewTexture->Release();
			m_dx11SharedPreviewTexture = nullptr;
		}
		if (m_dx11ModelPreviewTexture)
		{
			m_dx11ModelPreviewTexture->Release();
			m_dx11ModelPreviewTexture = nullptr;
		}
		m_pyBackground.ReleaseCharacterShadowTexture();
#endif
		if (!m_grpDevice.Reset())
			return DEVICE_STATE_SKIP;
#ifdef ENABLE_FIX_MOBS_LAG
		m_pyBackground.CreateCharacterShadowTexture();
		// Resume drawing on the following frame after resource recovery.
		return DEVICE_STATE_SKIP;
#endif
		break;
	}

	return DEVICE_STATE_OK;
}

bool CPythonApplication::CreateDevice(int width, int height, int Windowed, int bit /* = 32*/, int frequency /* = 0*/)
{
	int iRet;

	m_grpDevice.InitBackBufferCount(2);
	m_grpDevice.RegisterWarningString(CGraphicDevice::CREATE_BAD_DRIVER, ApplicationStringTable_GetStringz(IDS_WARN_BAD_DRIVER, "WARN_BAD_DRIVER"));
	m_grpDevice.RegisterWarningString(CGraphicDevice::CREATE_NO_TNL, ApplicationStringTable_GetStringz(IDS_WARN_NO_TNL, "WARN_NO_TNL"));

	iRet = m_grpDevice.Create(GetWindowHandle(), width, height, Windowed ? true : false, bit,frequency, m_pySystem.GetAntialiasing());

	switch (iRet)
	{
	case CGraphicDevice::CREATE_OK:
		return true;

	case CGraphicDevice::CREATE_REFRESHRATE:
		return true;

	case CGraphicDevice::CREATE_ENUM:
	case CGraphicDevice::CREATE_DETECT:
		SET_EXCEPTION(CREATE_NO_APPROPRIATE_DEVICE);
		TraceError("CreateDevice: Enum & Detect failed");
		return false;

	case CGraphicDevice::CREATE_NO_DIRECTX:
		SET_EXCEPTION(CREATE_NO_DIRECTX);
		TraceError("CreateDevice: DirectX 8.1 or greater required to run game");
		return false;

	case CGraphicDevice::CREATE_DEVICE:
		SET_EXCEPTION(CREATE_DEVICE);
		TraceError("CreateDevice: GraphicDevice create failed");
		return false;

	case CGraphicDevice::CREATE_FORMAT:
		SET_EXCEPTION(CREATE_FORMAT);
		TraceError("CreateDevice: Change the screen format");
		return false;

	case CGraphicDevice::CREATE_GET_DEVICE_CAPS:
		PyErr_SetString(PyExc_RuntimeError, "GetDevCaps failed");
		TraceError("CreateDevice: GetDevCaps failed");
		return false;

	case CGraphicDevice::CREATE_GET_DEVICE_CAPS2:
		PyErr_SetString(PyExc_RuntimeError, "GetDevCaps2 failed");
		TraceError("CreateDevice: GetDevCaps2 failed");
		return false;

	default:
		if (iRet & CGraphicDevice::CREATE_OK)
		{
			if (iRet & CGraphicDevice::CREATE_NO_TNL)
				CGrannyLODController::SetMinLODMode(true);
			return true;
		}

		SET_EXCEPTION(UNKNOWN_ERROR);
		TraceError("CreateDevice: Unknown Error!");
		return false;
	}
}

void CPythonApplication::Loop()
{
	while (1)
	{
		if (IsMessage())
		{
			if (!MessageProcess())
				break;
		}
		else
		{
			if (!Process())
				break;

			m_dwLastIdleTime=ELTimer_GetMSec();
		}
	}
}

#define ENABLE_LOAD_ITEM_LIST_FROM_ROOT
#define ENABLE_LOAD_ITEM_SCALE_FROM_ROOT
#define ENABLE_LOAD_SKILL_TABLE_FROM_ROOT
bool LoadLocaleData(const char* localePath)
{
	CPythonNonPlayer&	rkNPCMgr	= CPythonNonPlayer::Instance();
	CItemManager&		rkItemMgr	= CItemManager::Instance();
	CPythonSkill&		rkSkillMgr	= CPythonSkill::Instance();
	CPythonNetworkStream& rkNetStream = CPythonNetworkStream::Instance();

	char szItemList[256];
	char szItemProto[256];
	char szItemDesc[256];
	char szMobProto[256];
	char szSkillDescFileName[256];
	char szSkillTableFileName[256];
	char szInsultList[256];
#ifdef ENABLE_LOAD_ITEM_LIST_FROM_ROOT
	snprintf(szItemList,	sizeof(szItemList) ,	"item_list.txt");
#else
	snprintf(szItemList,	sizeof(szItemList) ,	"%s/item_list.txt",	localePath);
#endif
	snprintf(szItemProto,	sizeof(szItemProto),	"%s/item_proto",	localePath);
	snprintf(szItemDesc,	sizeof(szItemDesc),	"%s/itemdesc.txt",	localePath);
	snprintf(szMobProto,	sizeof(szMobProto),	"%s/mob_proto",		localePath);
	snprintf(szSkillDescFileName, sizeof(szSkillDescFileName),	"%s/SkillDesc.txt", localePath);
	#ifdef ENABLE_LOAD_SKILL_TABLE_FROM_ROOT
	snprintf(szSkillTableFileName, sizeof(szSkillTableFileName),	"SkillTable.txt");
	#else
	snprintf(szSkillTableFileName, sizeof(szSkillTableFileName),	"%s/SkillTable.txt", localePath);
	#endif
	snprintf(szInsultList,	sizeof(szInsultList),	"%s/insult.txt", localePath);

	rkNPCMgr.Destroy();
	rkItemMgr.Destroy();
	rkSkillMgr.Destroy();

	if (!rkItemMgr.LoadItemList(szItemList))
	{
		TraceError("LoadLocaleData - LoadItemList(%s) Error", szItemList);
	}

	if (!rkItemMgr.LoadItemTable(szItemProto))
	{
		TraceError("LoadLocaleData - LoadItemProto(%s) Error", szItemProto);
		return false;
	}

	if (!rkItemMgr.LoadItemDesc(szItemDesc))
	{
		Tracenf("LoadLocaleData - LoadItemDesc(%s) Error", szItemDesc);
	}

	if (!rkNPCMgr.LoadNonPlayerData(szMobProto))
	{
		TraceError("LoadLocaleData - LoadMobProto(%s) Error", szMobProto);
		return false;
	}

	if (!rkSkillMgr.RegisterSkillDesc(szSkillDescFileName))
	{
		TraceError("LoadLocaleData - RegisterSkillDesc(%s) Error", szSkillDescFileName); //@warme670
		return false;
	}

	if (!rkSkillMgr.RegisterSkillTable(szSkillTableFileName))
	{
		TraceError("LoadLocaleData - RegisterSkillTable(%s) Error", szSkillTableFileName); //@warme670
		return false;
	}

	if (!rkNetStream.LoadInsultList(szInsultList))
	{
		Tracenf("CPythonApplication - CPythonNetworkStream::LoadInsultList(%s)", szInsultList);
	}

#if defined(ENABLE_LOADING_PERFORMANCE)
	char szGuildBuildingList[256] = { 0 };
	snprintf(szGuildBuildingList, sizeof(szGuildBuildingList), "%s/guildbuildinglist.txt", localePath);
	if (!CPythonLoading::Instance().LoadGuildBuilding(szGuildBuildingList))
		Tracenf("LoadLocaleData - LoadGuildBuilding(%s) Error", szGuildBuildingList);
#endif

#ifdef ENABLE_ACCE_COSTUME_SYSTEM
	char szItemScale[256]{};
	#ifdef ENABLE_LOAD_ITEM_SCALE_FROM_ROOT
	snprintf(szItemScale, sizeof(szItemScale), "item_scale.txt");
	#else
	snprintf(szItemScale, sizeof(szItemScale), "%s/item_scale.txt", localePath);
	#endif

	if (!rkItemMgr.LoadItemScale(szItemScale))
	{
		Tracenf("LoadLocaleData: error while loading %s.", szItemScale);
		return false;
	}
#endif

	return true;
}

unsigned __GetWindowMode(bool windowed)
{
	if (windowed)
		return WS_OVERLAPPED | WS_CAPTION |   WS_SYSMENU | WS_MINIMIZEBOX;

	return WS_POPUP;
}

static POINT GetCurrentMonitorPosition() noexcept
{
	POINT cursorPos;
	GetCursorPos(&cursorPos);

	HMONITOR hMonitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTONEAREST);

	MONITORINFO monitorInfo = { sizeof(monitorInfo) };
	if (GetMonitorInfo(hMonitor, &monitorInfo)) {
		POINT monitorPos = {
			monitorInfo.rcMonitor.left,
			monitorInfo.rcMonitor.top
		};
		return monitorPos;
	}
	return {};
}

bool CPythonApplication::Create(PyObject * poSelf, const char * c_szName, int width, int height, int Windowed)
{
	Windowed = CPythonSystem::Instance().IsWindowed() ? 1 : 0;

	bool bAnotherWindow = false;

	if (FindWindow(nullptr, c_szName))
		bAnotherWindow = true;

	m_dwWidth = width;
	m_dwHeight = height;

	// Window
	const UINT WindowMode = __GetWindowMode(Windowed ? true : false);

	if (!CMSWindow::Create(c_szName, 4, 0, WindowMode, ::LoadIcon( GetInstance(), MAKEINTRESOURCE( IDI_METIN2 ) ), IDC_CURSOR_NORMAL))
	{
		TraceError("CMSWindow::Create failed");
		SET_EXCEPTION(CREATE_WINDOW);
		return false;
	}

	if (m_pySystem.IsUseDefaultIME())
		CPythonIME::Instance().UseDefaultIME();

	#ifdef ENABLE_DISCORD_RPC
	m_pyNetworkStream.Discord_Start();
	#endif

	if (!m_pySystem.IsWindowed())
	{
		m_isWindowed = false;
		m_isWindowFullScreenEnable = TRUE;
		__SetFullScreenWindow(GetWindowHandle(), width, height, m_pySystem.GetBPP());

		Windowed = true;
	}
	else
	{
		AdjustSize(m_pySystem.GetWidth(), m_pySystem.GetHeight());

		if (Windowed)
		{
			m_isWindowed = true;
			// @fixme031 BEGIN
			RECT rc{};
			GetClientRect(&rc);
			const auto windowWidth = rc.right - rc.left;
			const auto windowHeight = (rc.bottom - rc.top);
			RECT rc2{};
			GetWindowRect(&rc2);
			const auto windowWidth2 = rc2.right - rc2.left;
			const auto windowHeight2 = (rc2.bottom - rc2.top);
			const auto windowWidthDiff = windowWidth2 - windowWidth;
			const auto windowHeightDiff = windowHeight2 - windowHeight;
			const auto dropshadowSize = (windowWidthDiff / 2 != 0) ? (windowWidthDiff / 2 - 1) : 0;
			constexpr auto taskbarSize = 73;
			constexpr auto titlebarSize = 10;
			if (bAnotherWindow)
				CMSApplication::SetPosition(GetScreenWidth() - windowWidth - dropshadowSize, GetScreenHeight() - windowHeight - taskbarSize);
			else
				SetPosition(-dropshadowSize, (m_pySystem.GetHeight() >= 1000) ? -titlebarSize : 0);
			// @fixme031 END
		}
		else
		{
			m_isWindowed = false;
			SetPosition(0, 0);
		}
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	// Cursor
	if (!CreateCursors())
	{
		TraceError("CMSWindow::Cursors Create Error");
		SET_EXCEPTION("CREATE_CURSOR");
		return false;
	}

	if (!m_pySystem.IsNoSoundCard())
	{
		// Sound
		if (!m_SoundManager.Create())
		{
			// LogBox(ApplicationStringTable_GetStringz(IDS_WARN_NO_SOUND_DEVICE));
		}
	}

	extern bool GRAPHICS_CAPS_SOFTWARE_TILING;

	if (!m_pySystem.IsAutoTiling())
		GRAPHICS_CAPS_SOFTWARE_TILING = m_pySystem.IsSoftwareTiling();

	// Device
	if (!CreateDevice(m_pySystem.GetWidth(), m_pySystem.GetHeight(), Windowed, m_pySystem.GetBPP(), m_pySystem.GetFrequency()))
		return false;

	GrannyCreateSharedDeformBuffer();

	if (m_pySystem.IsAutoTiling())
	{
		if (m_grpDevice.IsFastTNL())
			m_pyBackground.ReserveSoftwareTilingEnable(false);
		else
			m_pyBackground.ReserveSoftwareTilingEnable(true);
	}
	else
		m_pyBackground.ReserveSoftwareTilingEnable(m_pySystem.IsSoftwareTiling());

	SetVisibleMode(true);

	if (m_isWindowFullScreenEnable)
		SetWindowPos(GetWindowHandle(), HWND_TOP, 0, 0, width, height, SWP_SHOWWINDOW);

	if (!InitializeKeyboard(GetWindowHandle()))
		return false;

	m_pySystem.GetDisplaySettings();

	// Mouse
	if (m_pySystem.IsSoftwareCursor())
		SetCursorMode(CURSOR_MODE_SOFTWARE);
	else
		SetCursorMode(CURSOR_MODE_HARDWARE);

	// Network
	if (!m_netDevice.Create())
	{
		//PyErr_SetString(PyExc_RuntimeError, "NetDevice::Create failed");
		TraceError("NetDevice::Create failed");
		SET_EXCEPTION("CREATE_NETWORK");
		return false;
	}

	if (!m_grpDevice.IsFastTNL())
		CGrannyLODController::SetMinLODMode(true);

	m_pyItem.Create();

	// Other Modules
	DefaultFont_Startup();

	CPythonIME::Instance().Create(GetWindowHandle());
	CPythonIME::Instance().SetText("", 0);
	CPythonTextTail::Instance().Initialize();

	// Light Manager
	m_LightManager.Initialize();

	CGraphicImageInstance::CreateSystem(32);

	STICKYKEYS sStickKeys;
	memset(&sStickKeys, 0, sizeof(sStickKeys));
	sStickKeys.cbSize = sizeof(sStickKeys);
	SystemParametersInfo( SPI_GETSTICKYKEYS, sizeof(sStickKeys), &sStickKeys, 0 );
	m_dwStickyKeysFlag = sStickKeys.dwFlags;

	sStickKeys.dwFlags &= ~(SKF_AVAILABLE|SKF_HOTKEYACTIVE);
	SystemParametersInfo( SPI_SETSTICKYKEYS, sizeof(sStickKeys), &sStickKeys, 0 );

	// SphereMap
	CGrannyMaterial::CreateSphereMap(0, "d:/ymir work/special/spheremap.jpg");
	CGrannyMaterial::CreateSphereMap(1, "d:/ymir work/special/spheremap01.jpg");
#if defined(ENABLE_LOADING_PERFORMANCE)
	m_pyLoading.BeginThreadLoading();
#endif
	return true;
}

void CPythonApplication::SetGlobalCenterPosition(LONG x, LONG y) const
{
	const CPythonBackground& rkBG=CPythonBackground::Instance();
	rkBG.GlobalPositionToLocalPosition(x, y);

	const float z = CPythonBackground::Instance().GetHeight(x, y);

	CPythonApplication::Instance().SetCenterPosition(x, y, z);
}

void CPythonApplication::SetCenterPosition(float fx, float fy, float fz)
{
	m_v3CenterPosition.x = +fx;
	m_v3CenterPosition.y = -fy;
	m_v3CenterPosition.z = +fz;
}

void CPythonApplication::GetCenterPosition(TPixelPosition * pPixelPosition) const
{
	pPixelPosition->x = +m_v3CenterPosition.x;
	pPixelPosition->y = -m_v3CenterPosition.y;
	pPixelPosition->z = +m_v3CenterPosition.z;
}

void CPythonApplication::SetServerTime(time_t tTime)
{
	m_dwStartLocalTime	= ELTimer_GetMSec();
	m_tServerTime		= tTime;
	m_tLocalStartTime	= time(nullptr);
}

time_t CPythonApplication::GetServerTime() const
{
	return (ELTimer_GetMSec() - m_dwStartLocalTime) + m_tServerTime;
}

time_t CPythonApplication::GetServerTimeStamp() const
{
	return (time(nullptr) - m_tLocalStartTime) + m_tServerTime;
}

float CPythonApplication::GetGlobalTime()
{
	return m_fGlobalTime;
}

float CPythonApplication::GetGlobalElapsedTime()
{
	return m_fGlobalElapsedTime;
}

void CPythonApplication::SetFPS(int iFPS)
{
	m_iFPS = iFPS;
}

int CPythonApplication::GetWidth() const
{
	return m_dwWidth;
}

int CPythonApplication::GetHeight() const
{
	return m_dwHeight;
}

void CPythonApplication::SetConnectData(const char * c_szIP, int iPort)
{
	m_strIP = c_szIP;
	m_iPort = iPort;
}

void CPythonApplication::GetConnectData(std::string & rstIP, int & riPort) const
{
	rstIP	= m_strIP;
	riPort	= m_iPort;
}

void CPythonApplication::EnableSpecialCameraMode()
{
	m_isSpecialCameraMode = TRUE;
}

void CPythonApplication::SetCameraSpeed(int iPercentage)
{
	m_fCameraRotateSpeed = c_fDefaultCameraRotateSpeed * float(iPercentage) / 100.0f;
	m_fCameraPitchSpeed = c_fDefaultCameraPitchSpeed * float(iPercentage) / 100.0f;
	m_fCameraZoomSpeed = c_fDefaultCameraZoomSpeed * float(iPercentage) / 100.0f;
}

void CPythonApplication::SetForceSightRange(int iRange)
{
	m_iForceSightRange = iRange;
}

void CPythonApplication::Clear()
{
#if defined(ENABLE_LOADING_PERFORMANCE)
	// Release the native reference while Python is still initialized.
	m_pyGuild.SetGuildBuildingList(nullptr);
#endif
	m_pySystem.Clear();
}

void CPythonApplication::Destroy()
{
	MapPerformance::Flush();
#if defined(ENABLE_LOADING_PERFORMANCE)
	m_pyLoading.EndThreadLoading();
#endif
	if (m_dx11ModelPreviewTexture)
	{
		m_dx11ModelPreviewTexture->Release();
		m_dx11ModelPreviewTexture = nullptr;
	}
	if (m_dx11SharedPreviewTexture)
	{
		m_dx11SharedPreviewTexture->Release();
		m_dx11SharedPreviewTexture = nullptr;
	}
	WebBrowser_Destroy();

	// SphereMap
	CGrannyMaterial::DestroySphereMap();

	m_kWndMgr.Destroy();

	CPythonSystem::Instance().SaveConfig();

	DestroyCollisionInstanceSystem();

	m_pySystem.SaveInterfaceStatus();

	m_pyEventManager.Destroy();
	m_FlyingManager.Destroy();

	m_pyMiniMap.Destroy();

	m_pyTextTail.Destroy();
	m_pyChat.Destroy();
	m_kChrMgr.Destroy();
	m_RaceManager.Destroy();

	m_pyItem.Destroy();
	m_kItemMgr.Destroy();

	m_pyBackground.Destroy();

	m_kEftMgr.Destroy();
	m_LightManager.Destroy();

	// DEFAULT_FONT
	DefaultFont_Cleanup();
	// END_OF_DEFAULT_FONT

	GrannyDestroySharedDeformBuffer();

	m_pyGraphic.Destroy();

	#ifdef ENABLE_DISCORD_RPC
	m_pyNetworkStream.Discord_Close();
	#endif

	m_pyRes.Destroy();

	m_kGuildMarkDownloader.Disconnect();

	CGrannyModelInstance::DestroySystem();
	CGraphicImageInstance::DestroySystem();

	m_SoundManager.Destroy();
	m_grpDevice.Destroy();

	//CSpeedTreeForestDirectX8::Instance().Clear();

	CAttributeInstance::DestroySystem();
	CTextFileLoader::DestroySystem();
	DestroyCursors();

	CMSApplication::Destroy();

	STICKYKEYS sStickKeys;
	memset(&sStickKeys, 0, sizeof(sStickKeys));
	sStickKeys.cbSize = sizeof(sStickKeys);
	sStickKeys.dwFlags = m_dwStickyKeysFlag;
	SystemParametersInfo( SPI_SETSTICKYKEYS, sizeof(sStickKeys), &sStickKeys, 0 );
}

