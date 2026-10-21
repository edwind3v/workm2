#include "StdAfx.h"
#include "../EterBase/MapPerformance.h"
#include "MapOutdoor.h"
#include "AreaTerrain.h"
#include "AreaLoaderThread.h"
#include "../eterLib/ResourceManager.h"
#include "../EterPack/EterPackManager.h"

//CAreaLoaderThread CMapOutdoor::ms_AreaLoaderThread;

bool CMapOutdoor::Load(float x, float y, float z)
{
	Destroy();

	CEterPackManager& rkPackMgr=CEterPackManager::Instance();
	{
		static std::string s_strOldPathName="";

		const std::string c_rstrNewPathName=GetName()+"\\cache";

		s_strOldPathName=c_rstrNewPathName;
	}

	const std::string strFileName = GetMapDataDirectory() + "\\Setting.txt";

	if (!LoadSetting(strFileName.c_str()))
		TraceError("CMapOutdoor::Load : LoadSetting(%s) Failed", strFileName.c_str());

#ifdef WORLD_EDITOR // @fixme015
	if (!LoadMonsterAreaInfo())
		TraceError("CMapOutdoor::Load - LoadMonsterAreaInfo ERROR");
#endif

	CreateTerrainPatchProxyList();
	BuildQuadTree();
	LoadWaterTexture();
	CreateCharacterShadowTexture();

	m_lOldReadX = -1;

	CSpeedTreeDirectX::Instance().SetRenderingDevice();

	Update(x, y, z);

	__HeightCache_Init();

	// LOCAL_ENVIRONMENT_DATA
	const std::string local_envDataName = GetMapDataDirectory() + "\\" + m_settings_envDataName;
	if (rkPackMgr.isExist(local_envDataName.c_str()))
	{
		m_envDataName = local_envDataName;
	}
	else
	{
		const std::string& c_rstrEnvironmentRoot = "d:/ymir work/environment/";
		const std::string& c_rstrMapName = GetName();
		m_envDataName = c_rstrEnvironmentRoot + m_settings_envDataName;

		if (0 == m_envDataName.compare(c_rstrEnvironmentRoot))
		{
			const std::string& strAppendName = c_rstrMapName.substr(c_rstrMapName.size() - 2, 2);
			m_envDataName = c_rstrEnvironmentRoot + strAppendName + ".msenv";
		}
	}
	// LOCAL_ENVIRONMENT_DATA_END
	return true;
}

std::string& CMapOutdoor::GetEnvironmentDataName()
{
	return m_envDataName;
}

bool CMapOutdoor::isTerrainLoaded(WORD wX, WORD wY) const
{
	for (DWORD i = 0; i < m_TerrainVector.size(); ++i)
	{
		CTerrain * pTerrain = m_TerrainVector[i];
		WORD usCoordX, usCoordY;
		pTerrain->GetCoordinate(&usCoordX, &usCoordY);

		if (usCoordX == wX && usCoordY == wY)
			return true;
	}
	return false;
}

bool CMapOutdoor::isAreaLoaded(WORD wX, WORD wY) const
{
	for (DWORD i = 0; i < m_AreaVector.size(); ++i)
	{
		const CArea * pArea = m_AreaVector[i];
		WORD usCoordX, usCoordY;
		pArea->GetCoordinate(&usCoordX, &usCoordY);

		if (usCoordX == wX && usCoordY == wY)
			return true;
	}
	return false;
}

void CMapOutdoor::AssignTerrainPtr()
{
	OnPreAssignTerrainPtr();

	short sReferenceCoordMinX, sReferenceCoordMaxX, sReferenceCoordMinY, sReferenceCoordMaxY;
	sReferenceCoordMinX = max(m_CurCoordinate.m_sTerrainCoordX - LOAD_SIZE_WIDTH, 0);
	sReferenceCoordMaxX = min(m_CurCoordinate.m_sTerrainCoordX + LOAD_SIZE_WIDTH, m_sTerrainCountX - 1);
	sReferenceCoordMinY = max(m_CurCoordinate.m_sTerrainCoordY - LOAD_SIZE_WIDTH, 0);
	sReferenceCoordMaxY = min(m_CurCoordinate.m_sTerrainCoordY + LOAD_SIZE_WIDTH, m_sTerrainCountY - 1);

	DWORD i;
	for (i = 0; i < AROUND_AREA_NUM; ++i)
	{
		m_pArea[i] = nullptr;
		m_pTerrain[i] = nullptr;
	}

	for (i = 0; i < m_TerrainVector.size(); ++i)
	{
		CTerrain * pTerrain = m_TerrainVector[i];
		WORD usCoordX, usCoordY;
		pTerrain->GetCoordinate(&usCoordX, &usCoordY);

		if (usCoordX >= sReferenceCoordMinX &&
			usCoordX <= sReferenceCoordMaxX &&
			usCoordY >= sReferenceCoordMinY &&
			usCoordY <= sReferenceCoordMaxY)
		{
			m_pTerrain[(usCoordY - m_CurCoordinate.m_sTerrainCoordY + LOAD_SIZE_WIDTH) * 3 +
				(usCoordX - m_CurCoordinate.m_sTerrainCoordX + LOAD_SIZE_WIDTH) ] = pTerrain;
		}
	}

	for (i = 0; i < m_AreaVector.size(); ++i)
	{
		CArea * pArea = m_AreaVector[i];
		WORD usCoordX, usCoordY;
		pArea->GetCoordinate(&usCoordX, &usCoordY);

		if (usCoordX >= sReferenceCoordMinX &&
			usCoordX <= sReferenceCoordMaxX &&
			usCoordY >= sReferenceCoordMinY &&
			usCoordY <= sReferenceCoordMaxY)
		{
			m_pArea[(usCoordY - m_CurCoordinate.m_sTerrainCoordY + LOAD_SIZE_WIDTH) * 3 +
				(usCoordX - m_CurCoordinate.m_sTerrainCoordX + LOAD_SIZE_WIDTH) ] = pArea;
		}
	}
}

bool CMapOutdoor::LoadArea(WORD wAreaCoordX, WORD wAreaCoordY, WORD wCellCoordX, WORD wCellCoordY)
{
	if (isAreaLoaded(wAreaCoordX, wAreaCoordY))
		return true;
	for (auto it = m_AreaDeleteVector.begin(); it != m_AreaDeleteVector.end(); ++it)
	{
		WORD x, y; (*it)->GetCoordinate(&x, &y);
		if (x == wAreaCoordX && y == wAreaCoordY && !(*it)->IsRetiring())
		{ m_AreaVector.push_back(*it); m_AreaDeleteVector.erase(it); return true; }
	}
	MapPerformance::Count(MapPerformance::Counter::AreasLoaded);
	MapPerformance::Scope mapProfile("map.area_load");
	mapProfile.Detail("map=%s sector=(%u,%u)", GetName().c_str(), wAreaCoordX, wAreaCoordY);
#ifdef _DEBUG
	DWORD dwStartTime = ELTimer_GetMSec();
#endif
	const unsigned long ulID = (unsigned long) (wAreaCoordX) * 1000L + (unsigned long) (wAreaCoordY);
	char szAreaPathName[64+1];
	_snprintf(szAreaPathName, sizeof(szAreaPathName), "%s\\%06u\\", GetMapDataDirectory().c_str(), ulID);

	CArea * pArea = CArea::New();
	pArea->SetMapOutDoor(this);
#ifdef _DEBUG
	Tracef("CMapOutdoor::LoadArea1 %d\n", ELTimer_GetMSec() - dwStartTime);
	dwStartTime = ELTimer_GetMSec();
#endif

	pArea->SetCoordinate(wAreaCoordX, wAreaCoordY);
	if ( !pArea->Load(szAreaPathName) )
		TraceError(" CMapOutdoor::LoadArea(%d, %d) LoadShadowMap ERROR", wAreaCoordX, wAreaCoordY);

#ifdef _DEBUG
	Tracef("CMapOutdoor::LoadArea2 %d\n", ELTimer_GetMSec() - dwStartTime);
	dwStartTime = ELTimer_GetMSec();
#endif

	m_AreaVector.push_back(pArea);

	pArea->EnablePortal(m_bEnablePortal);
#ifdef _DEBUG
	Tracef("CMapOutdoor::LoadArea3 %d\n", ELTimer_GetMSec() - dwStartTime);
#endif

	return true;
}

bool CMapOutdoor::LoadTerrain(WORD x, WORD y, WORD, WORD)
{
	if (isTerrainLoaded(x, y)) return true;
	MapPerformance::Scope profile("map.terrain_load");
	profile.Detail("map=%s sector=(%u,%u)", GetName().c_str(), x, y);
	MapPerformance::Count(MapPerformance::Counter::TerrainsLoaded);
	// A fully prepared recent section can be reused without rebuilding it.
	for (auto it = m_TerrainDeleteVector.begin(); it != m_TerrainDeleteVector.end(); ++it)
	{
		WORD oldX, oldY; (*it)->GetCoordinate(&oldX, &oldY);
		if (oldX == x && oldY == y && (*it)->IsReady())
		{
			m_TerrainVector.push_back(*it); m_TerrainDeleteVector.erase(it); return true;
		}
	}
	auto found = std::find_if(m_terrainPreparation.begin(), m_terrainPreparation.end(),
		[&](const STerrainPreparation& t) { return t.x == x && t.y == y; });
	STerrainPreparation local;
	STerrainPreparation* task = &local;
	if (found != m_terrainPreparation.end()) task = &*found;
	else
	{
		local.x = x; local.y = y;
		char suffix[32]; sprintf(suffix, "\\%06u\\", unsigned(x) * 1000u + y);
		local.directory = GetMapDataDirectory() + suffix;
	}
	if (task->stage < 11) MapPerformance::Count(MapPerformance::Counter::StreamingFallbacks);
	while (task->stage < 11 && !task->failed) AdvanceTerrainPreparation(*task, true);
	const bool success = !task->failed && task->terrain;
	if (success) m_TerrainVector.push_back(task->terrain);
	else if (task->terrain) CTerrain::Delete(task->terrain);
	if (found != m_terrainPreparation.end()) m_terrainPreparation.erase(found);
	return success;
}

void CMapOutdoor::RequestSectorFiles(WORD x, WORD y)
{
	char suffix[32]; sprintf(suffix, "\\%06u\\", unsigned(x) * 1000u + y);
	const std::string directory = GetMapDataDirectory() + suffix;
	std::vector<std::string> files;
	for (const char* name : {"AreaProperty.txt", "height.raw", "tile.raw", "attr.atr", "water.wtr", "shadowmap.raw", "shadowmap.dds", "minimap.dds", "AreaData.txt", "AreaAmbienceData.txt"})
		files.push_back(directory + name);
	CEterPackManager::Instance().RequestReadAhead(files);
}

bool CMapOutdoor::AdvanceTerrainPreparation(STerrainPreparation& task, bool required)
{
	MapPerformance::Scope profile(required ? "stream.required_terrain_step" : "stream.prepare_terrain_step");
	profile.Detail("sector=(%u,%u) stage=%u part=%u", task.x, task.y, task.stage, task.part);
	auto ready = [&](const char* name) {
		if (required) return true;
		const auto filename = task.directory + name;
		if (CEterPackManager::Instance().IsReadAheadReady(filename.c_str())) return true;
		CEterPackManager::Instance().RequestReadAhead({filename});
		return false;
	};
	auto filename = [&](const char* name) { return task.directory + name; };
	CTerrain* terrain = task.terrain;
	switch (task.stage)
	{
	case 0:
	{
		if (!ready("AreaProperty.txt")) return false;
		CTokenVectorMap tokens;
		if (!LoadMultipleTextData(filename("AreaProperty.txt").c_str(), tokens) ||
			tokens["scripttype"].empty() || tokens["scripttype"][0] != "AreaProperty" || tokens["areaname"].empty())
		{
			TraceError("STREAM_TERRAIN invalid property %s", filename("AreaProperty.txt").c_str());
			task.failed = true; return false;
		}
		task.terrain = CTerrain::New(); terrain = task.terrain;
		terrain->Clear(); terrain->SetMapOutDoor(this); terrain->SetCoordinate(task.x, task.y);
		terrain->CopySettingFromGlobalSetting(); terrain->SetName(tokens["areaname"][0].c_str());
		break;
	}
	case 1:
		if (!ready("water.wtr")) return false;
		MapPerformance::Measure("terrain.water", task.directory.c_str(), [&] { terrain->LoadWaterMap(filename("water.wtr").c_str()); }); break;
	case 2:
		if (!ready("height.raw")) return false;
		MapPerformance::Measure("terrain.height_and_normals", task.directory.c_str(), [&] { terrain->LoadHeightMap(filename("height.raw").c_str()); }); break;
	case 3:
		if (!ready("attr.atr")) return false;
		terrain->LoadAttrMap(filename("attr.atr").c_str()); break;
	case 4:
		if (!ready("tile.raw")) return false;
		terrain->LoadTileMapDeferred(filename("tile.raw").c_str()); task.part = 1; break;
	case 5:
		if (task.part < terrain->GetBlendMaskCount()) { terrain->PrepareBlendMask(task.part++); return true; }
		task.part = 0; break;
	case 6:
		if (!ready("shadowmap.dds")) return false;
		terrain->LoadShadowTexture(filename("shadowmap.dds").c_str()); break;
	case 7:
		if (!ready("shadowmap.raw")) return false;
		terrain->LoadShadowMap(filename("shadowmap.raw").c_str()); break;
	case 8:
		if (!ready("minimap.dds")) return false;
		terrain->LoadMiniMapTexture(filename("minimap.dds").c_str()); task.part = 0; break;
	case 9:
		if (task.part < terrain->GetGeometryPatchCount()) { terrain->PrepareGeometryPatch(task.part++); return true; }
		break;
	case 10:
		terrain->SetReady(); MapPerformance::Count(MapPerformance::Counter::TerrainPrepared); break;
	default: return false;
	}
	++task.stage;
	return true;
}

void CMapOutdoor::CancelStreaming()
{
	for (auto& task : m_terrainPreparation) if (task.terrain) CTerrain::Delete(task.terrain);
	m_terrainPreparation.clear();
}

void CMapOutdoor::UpdateStreaming(const D3DXVECTOR3& player)
{
	MapPerformance::Scope profile("stream.update");
	const int size = CTerrainImpl::TERRAIN_XSIZE;
	const int x = m_CurCoordinate.m_sTerrainCoordX, y = m_CurCoordinate.m_sTerrainCoordY;
	const float localX = player.x - x * size, localY = fabsf(player.y) - y * size;
	const int nextX = (std::clamp)(x + (localX < size * 0.35f ? -1 : localX > size * 0.65f ? 1 : 0), 0, int(m_sTerrainCountX) - 1);
	const int nextY = (std::clamp)(y + (localY < size * 0.35f ? -1 : localY > size * 0.65f ? 1 : 0), 0, int(m_sTerrainCountY) - 1);
	for (auto it = m_terrainPreparation.begin(); it != m_terrainPreparation.end();)
	{
		if (isTerrainLoaded(it->x, it->y) || abs(int(it->x) - nextX) > 1 || abs(int(it->y) - nextY) > 1)
		{
			if (it->terrain) m_TerrainDeleteVector.push_back(it->terrain);
			it = m_terrainPreparation.erase(it);
		}
		else ++it;
	}
	for (int sy = (std::max)(0, nextY - 1); sy <= (std::min)(int(m_sTerrainCountY) - 1, nextY + 1); ++sy)
		for (int sx = (std::max)(0, nextX - 1); sx <= (std::min)(int(m_sTerrainCountX) - 1, nextX + 1); ++sx)
		{
			if (isTerrainLoaded(sx, sy)) continue;
			if (std::any_of(m_terrainPreparation.begin(), m_terrainPreparation.end(), [&](const STerrainPreparation& t) { return t.x == sx && t.y == sy; })) continue;
			const size_t capacity = IsWorldMemoryPressure() ? 2u : 6u;
			if (m_terrainPreparation.size() >= capacity || m_TerrainDeleteVector.size() >= 6) continue;
			STerrainPreparation task; task.x = WORD(sx); task.y = WORD(sy);
			char suffix[32]; sprintf(suffix, "\\%06u\\", unsigned(sx) * 1000u + sy);
			task.directory = GetMapDataDirectory() + suffix;
			for (auto cached = m_TerrainDeleteVector.begin(); cached != m_TerrainDeleteVector.end(); ++cached)
			{
				WORD oldX, oldY; (*cached)->GetCoordinate(&oldX, &oldY);
				if (oldX == task.x && oldY == task.y && (*cached)->IsReady())
				{ task.terrain = *cached; task.stage = 11; m_TerrainDeleteVector.erase(cached); break; }
			}
			RequestSectorFiles(task.x, task.y); m_terrainPreparation.push_back(std::move(task));
		}
	CFrameWorkBudget budget(1.5);
	bool advanced = true;
	while (budget.Available() && advanced)
	{
		advanced = false;
		for (auto& task : m_terrainPreparation)
		{
			if (!budget.Available()) break;
			if (!task.failed && task.stage < 11) advanced |= AdvanceTerrainPreparation(task, false);
			if (budget.Available() && !isAreaLoaded(task.x, task.y))
			{
				const auto objects = task.directory + "AreaData.txt";
				const auto ambience = task.directory + "AreaAmbienceData.txt";
				if (CEterPackManager::Instance().IsReadAheadReady(objects.c_str()) && CEterPackManager::Instance().IsReadAheadReady(ambience.c_str()))
					LoadArea(task.x, task.y, 0, 0);
			}
		}
	}
}

bool CMapOutdoor::LoadSetting(const char * c_szFileName)
{
	CTokenVectorMap stTokenVectorMap;

	if (!LoadMultipleTextData(c_szFileName, stTokenVectorMap))
	{
		TraceError("MapOutdoor::LoadSetting(c_szFileName=%s) - LoadMultipleTextData", c_szFileName);
		return false;
	}

	if (!stTokenVectorMap.contains("scripttype"))
	{
		TraceError("MapOutdoor::LoadSetting(c_szFileName=%s) - FIND 'scripttype' - FAILED", c_szFileName);
		return false;
	}

	if (!stTokenVectorMap.contains("viewradius"))
	{
		TraceError("MapOutdoor::LoadSetting(c_szFileName=%s) - FIND 'viewradius' - FAILED", c_szFileName);
		return false;
	}

	if (!stTokenVectorMap.contains("cellscale"))
	{
		TraceError("MapOutdoor::LoadSetting(c_szFileName=%s) - FIND 'cellscale' - FAILED", c_szFileName);
		return false;
	}

	if (!stTokenVectorMap.contains("heightscale"))
	{
		TraceError("MapOutdoor::LoadSetting(c_szFileName=%s) - FIND 'heightscale' - FAILED", c_szFileName);
		return false;
	}

	if (!stTokenVectorMap.contains("mapsize"))
	{
		TraceError("MapOutdoor::LoadSetting(c_szFileName=%s) - FIND 'mapsize' - FAILED", c_szFileName);
		return false;
	}

	if (!stTokenVectorMap.contains("textureset"))
	{
		TraceError("MapOutdoor::LoadSetting(c_szFileName=%s) - FIND 'textureset' - FAILED", c_szFileName);
		return false;
	}

	if (stTokenVectorMap.contains("terrainvisible"))
	{
		m_bSettingTerrainVisible = (bool) (atoi(stTokenVectorMap["terrainvisible"][0].c_str()) != 0);
	}
	else
	{
		m_bSettingTerrainVisible = true;
	}

	const std::string & c_rstrType = stTokenVectorMap["scripttype"][0];
	const std::string & c_rstrViewRadius = stTokenVectorMap["viewradius"][0];
	//const std::string & c_rstrCellScale = stTokenVectorMap["cellscale"][0];
	const std::string & c_rstrHeightScale = stTokenVectorMap["heightscale"][0];
	const std::string & c_rstrMapSizeX = stTokenVectorMap["mapsize"][0];
	const std::string & c_rstrMapSizeY = stTokenVectorMap["mapsize"][1];

	std::string strTextureSet;
	const TTokenVector & rkVec_strToken = stTokenVectorMap["textureset"];
	if (!rkVec_strToken.empty())
	{
		strTextureSet = rkVec_strToken[0];
	}

	if (c_rstrType != "MapSetting")
	{
		TraceError("MapOutdoor::LoadSetting(c_szFileName=%s) - Resourse Type ERROR", c_szFileName);
		return false;
	}

	m_lViewRadius = atol(c_rstrViewRadius.c_str());

#ifdef WORLD_EDITOR
	m_lViewRadius <<= 1;
#endif

	if (0L >= m_lViewRadius)
	{
		TraceError("MapOutdoor::LoadSetting(c_szFileName=%s) - VIEWRADIUS IS NOT GREATER THAN 0", c_szFileName);
		return false;
	}

	m_fHeightScale = atof(c_rstrHeightScale.c_str());

	SetTerrainCount(atoi(c_rstrMapSizeX.c_str()), atoi(c_rstrMapSizeY.c_str()));

	m_fTerrainTexCoordBase = 1.0f / (float) (CTerrainImpl::PATCH_XSIZE * CTerrainImpl::CELLSCALE);

	if (stTokenVectorMap.contains("baseposition"))
	{
		const std::string & c_rstrMapBaseX = stTokenVectorMap["baseposition"][0];
		const std::string & c_rstrMapBaseY = stTokenVectorMap["baseposition"][1];
		SetBaseXY((DWORD)atol(c_rstrMapBaseX.c_str()), (DWORD)atol(c_rstrMapBaseY.c_str()));
	}

	std::string stTextureSetFileName = strTextureSet;

	if (0 != stTextureSetFileName.find_first_of("textureset", 0))
		stTextureSetFileName = "textureset\\"+strTextureSet;

 	if (!m_TextureSet.Load(stTextureSetFileName.c_str(), m_fTerrainTexCoordBase))
	{
#ifdef WORLD_EDITOR
		LogBoxf("TextureSet not successfully applied to [%s].\nTexture not found [%s].", c_szFileName, stTextureSetFileName.c_str());
#else
		TraceError("MapOutdoor::LoadSetting(c_szFileName=%s) - LOAD TEXTURE SET(%s) ERROR", c_szFileName, stTextureSetFileName.c_str());
		return false;
#endif
	}

	CTerrain::SetTextureSet(&m_TextureSet);

	if (stTokenVectorMap.contains("environment"))
	{
		const CTokenVector & c_rEnvironmentVector = stTokenVectorMap["environment"];
		if (!c_rEnvironmentVector.empty())
			m_settings_envDataName = c_rEnvironmentVector[0];
		else
			TraceError("CMapOutdoor::LoadSetting(c_szFileName=%s) - Failed to load environment data\n", c_szFileName);
	}

	m_fWaterTexCoordBase = 1.0f / (float)(CTerrainImpl::CELLSCALE * 4);

	D3DXMatrixScaling(&m_matSplatAlpha,
		+m_fTerrainTexCoordBase * 2.0f * (float)(CTerrainImpl::PATCH_XSIZE) / (float)(CTerrainImpl::SPLATALPHA_RAW_XSIZE-2),
		-m_fTerrainTexCoordBase * 2.0f * (float)(CTerrainImpl::PATCH_YSIZE) / (float)(CTerrainImpl::SPLATALPHA_RAW_XSIZE-2),
		0.0f);
	m_matSplatAlpha._41 = m_fTerrainTexCoordBase * 4.6f;
	m_matSplatAlpha._42 = m_fTerrainTexCoordBase * 4.6f;

	D3DXMatrixScaling(&m_matStaticShadow,
		+m_fTerrainTexCoordBase * ((float) CTerrainImpl::PATCH_XSIZE / CTerrainImpl::XSIZE),
		-m_fTerrainTexCoordBase * ((float) CTerrainImpl::PATCH_YSIZE / CTerrainImpl::XSIZE),
		0.0f);
	m_matStaticShadow._41 = 0.0f;
	m_matStaticShadow._42 = 0.0f;

	D3DXMatrixScaling(&m_matDynamicShadowScale, 1.0f / 2550.0f, -1.0f / 2550.0f, 1.0f);
	m_matDynamicShadowScale._41 = 0.5f;
	m_matDynamicShadowScale._42 = 0.5f;

	// Transform
	D3DXMatrixScaling(&m_matBuildingTransparent, 1.0f / ((float)ms_iWidth), -1.0f / ((float)ms_iHeight), 1.0f);
	m_matBuildingTransparent._41 = 0.5f;
	m_matBuildingTransparent._42 = 0.5f;
	return true;
}

bool CMapOutdoor::LoadMonsterAreaInfo()
{
	RemoveAllMonsterAreaInfo();

	char c_szFileName[256];
	sprintf(c_szFileName, "%s\\regen.txt", GetMapDataDirectory().c_str());

	LPCVOID pModelData;
	CMappedFile File;

	if (!CEterPackManager::Instance().Get(File, c_szFileName, &pModelData))
	{
		//TraceError(" CMapOutdoorAccessor::LoadMonsterAreaInfo Load File %s ERROR", c_szFileName);
		return false;
	}

	CMemoryTextFileLoader textFileLoader;
	CTokenVector stTokenVector;

	textFileLoader.Bind(File.Size(), pModelData);

	for (DWORD i = 0; i < textFileLoader.GetLineCount(); ++i)
	{
		if (!textFileLoader.SplitLine(i, &stTokenVector))
			continue;

		stl_lowers(stTokenVector[0]);

		// Start or End
		if (0 == stTokenVector[0].compare("m") || 0 == stTokenVector[0].compare("g"))
		{
			if (stTokenVector.size() < 11)
			{
				TraceError("CMapOutdoorAccessor::LoadMonsterAreaInfo Get MonsterInfo File Format ERROR! continue....");
				continue;
			}

			CMonsterAreaInfo::EMonsterAreaInfoType eMonsterAreaInfoType;
			if (0 == stTokenVector[0].compare("m"))
			{
				eMonsterAreaInfoType = CMonsterAreaInfo::MONSTERAREAINFOTYPE_MONSTER;
			}
			else if (0 == stTokenVector[0].compare("g"))
			{
				eMonsterAreaInfoType = CMonsterAreaInfo::MONSTERAREAINFOTYPE_GROUP;
			}
			else
			{
				TraceError("CMapOutdoorAccessor::LoadMonsterAreaInfo Get MonsterInfo Data ERROR! continue....");
				continue;
			}

			const std::string & c_rstrOriginX	= stTokenVector[1].c_str();
			const std::string & c_rstrOriginY	= stTokenVector[2].c_str();
			const std::string & c_rstrSizeX		= stTokenVector[3].c_str();
			const std::string & c_rstrSizeY		= stTokenVector[4].c_str();
			const std::string & c_rstrZ			= stTokenVector[5].c_str();
			const std::string & c_rstrDir		= stTokenVector[6].c_str();
			const std::string & c_rstrTime		= stTokenVector[7].c_str();
			const std::string & c_rstrPercent	= stTokenVector[8].c_str();
			const std::string & c_rstrCount		= stTokenVector[9].c_str();
			const std::string & c_rstrVID		= stTokenVector[10].c_str();

			long lOriginX, lOriginY, lSizeX, lSizeY, lZ, lTime, lPercent;
			CMonsterAreaInfo::EMonsterDir eMonsterDir;
			DWORD dwMonsterCount;
			DWORD dwMonsterVID;

			lOriginX		= atol(c_rstrOriginX.c_str());
			lOriginY		= atol(c_rstrOriginY.c_str());
			lSizeX			= atol(c_rstrSizeX.c_str());
			lSizeY			= atol(c_rstrSizeY.c_str());
			lZ				= atol(c_rstrZ.c_str());
			eMonsterDir		= (CMonsterAreaInfo::EMonsterDir) atoi(c_rstrDir.c_str());
			lTime			= atol(c_rstrTime.c_str());
			lPercent		= atol(c_rstrPercent.c_str());
			dwMonsterCount	= (DWORD) atoi(c_rstrCount.c_str());
			dwMonsterVID	= (DWORD) atoi(c_rstrVID.c_str());

//			lOriginX -= m_dwBaseX / 100;
//			lOriginY -= m_dwBaseY / 100;

			CMonsterAreaInfo * pMonsterAreaInfo = AddMonsterAreaInfo(lOriginX, lOriginY, lSizeX, lSizeY);
			pMonsterAreaInfo->SetMonsterAreaInfoType(eMonsterAreaInfoType);
			if (CMonsterAreaInfo::MONSTERAREAINFOTYPE_MONSTER == eMonsterAreaInfoType)
				pMonsterAreaInfo->SetMonsterVID(dwMonsterVID);
			else if (CMonsterAreaInfo::MONSTERAREAINFOTYPE_GROUP == eMonsterAreaInfoType)
				pMonsterAreaInfo->SetMonsterGroupID(dwMonsterVID);
			pMonsterAreaInfo->SetMonsterCount(dwMonsterCount);
			pMonsterAreaInfo->SetMonsterDirection(eMonsterDir);
		}
	}

	return true;
}

