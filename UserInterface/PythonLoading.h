/*
* Title: Client Loading Optimization
* Description: Increases the performance of loading client data.
* Author: Owsap
* Date: 2022.09.23
*
* Version: 1.2
*
* Discord: Owsap#7928
* Skype: owsap.
*
* Web: https://owsap.dev/
* GitHub: https://github.com/Owsap
**/

#pragma once

#if defined(ENABLE_LOADING_PERFORMANCE)
#include "../GameLib/RaceData.h"
#include "../GameLib/RaceMotionData.h"

#include <mutex>
#include <future>
#include <thread>
#include <chrono>
#include <regex>

//#define __LOADING_PERFORMANCE_CHECK__
#define __ALLOW_ESCAPE_LOADING__
#define __WARP_SHOWER__

#include "PythonLoadingUtils.hpp"

class CPythonLoading : public CSingleton<CPythonLoading>
{
public:
	CPythonLoading();
	virtual ~CPythonLoading();

	void BeginThreadLoading();
	void JoinThreadLoading();
	void EndThreadLoading();

	void LoadBackground(const long c_lPlayerX, const long c_lPlayerY);
	void RegisterSkills(const BYTE c_bJob, const BYTE c_bSkillGroupIndex, const BYTE c_bEmpire, const bool c_bEnable678thSkill = true);
	bool LoadGuildBuilding(const char* c_szGuildBuildingList);

private:
	static void __LoadData();

	static void __Initialize();
	static void __LoadSound();
	static void __LoadEffect();
	static void __LoadNPCList();
	static void __LoadRaceHeight();

	static void __LoadRace();
	static void __LoadWarriorEx(const std::string& c_stPathName);
	static void __LoadAssassinEx(const std::string& c_stPathName);
	static void __LoadSuraEx(const std::string& c_stPathName);
	static void __LoadShamanEx(const std::string& c_stPathName);
	static void __LoadWolfmanEx(const std::string& c_stPathName);

	static void __LoadComboType(CRaceData* pRaceData, WORD wMotionMode);
	static void __LoadHorseComboType(CRaceData* pRaceData, WORD wMotionMode);

	static void __SetIntroMotions(CRaceData* pRaceData, const std::string& c_stPathName);
	static void __SetGeneralMotion(CRaceData* pRaceData, const std::string& c_stPathName);
	static void __SetNewGeneralMotion(CRaceData* pRaceData, const std::string& c_stPathName);

	static void __LoadMotionData(CRaceData* pRaceData, MotionDataVector& vMotionData, WORD wMotionMode, const std::string& rstFullPath);
	static void __RegisterCacheMotionData(CRaceData* pRaceData, WORD wMotionMode, WORD wMotionIndex, const std::string& c_stFileName, BYTE bPercentage);

	static void __RegisterEmotionAnis(CRaceData* pRaceData, const std::string& c_stPathName);
	static void __LoadAction(CRaceData* pRaceData, const std::string& c_stPathName);
	static void __LoadWedding(CRaceData* pRaceData, const std::string& c_stPathName);
	static void __LoadFishing(CRaceData* pRaceData, const std::string& c_stPathName);

	static void __RegisterDungeonMapName();
	static void __RegisterTitleName();
#if defined(ENABLE_GUILD_LEADER_GRADE_NAME)
	static void __RegisterGuildLeaderGradeName();
#endif
	static void __RegisterColor();
	static void __RegisterEmotionIcon();

private:
	static std::mutex ms_mutex;

protected:
	std::thread ms_thread;
	std::atomic<bool> ms_thread_load, ms_thread_joined;
	bool m_titles_registered;
};
#endif
