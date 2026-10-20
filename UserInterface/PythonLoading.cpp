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

#include "StdAfx.h"

#if defined(ENABLE_LOADING_PERFORMANCE)
#include "../EterPack/EterPackManager.h"

#include "PythonApplication.h"
#include "PythonCharacterManager.h"
#include "PythonPlayer.h"
#include "PythonLoading.h"
#include "PythonLoading.hpp"

std::mutex CPythonLoading::ms_mutex;

CPythonLoading::CPythonLoading()
{
	ms_thread_load = false;
	ms_thread_joined = false;
	m_titles_registered = false;
}

CPythonLoading::~CPythonLoading()
{
	EndThreadLoading();
}

void CPythonLoading::BeginThreadLoading()
{
	if (!ms_thread_load)
	{
		m_titles_registered = false;
		ms_thread_load = true;
		ms_thread = std::thread([&] { __LoadData(); });
	}

	Sleep(g_iSafeSleep);
}

void CPythonLoading::JoinThreadLoading()
{
	if (ms_thread.joinable() && !ms_thread_joined)
	{
		ms_thread.join();
		ms_thread_joined = true;
	}

	// Locale lookups use CPython and must stay on the interpreter thread.
	if (!m_titles_registered)
	{
		__RegisterTitleName();
#if defined(ENABLE_GUILD_LEADER_GRADE_NAME)
		__RegisterGuildLeaderGradeName();
#endif
		m_titles_registered = true;
	}
}

void CPythonLoading::EndThreadLoading()
{
	const bool bWasLoading = ms_thread_load || ms_thread.joinable();

	if (ms_thread.joinable())
		ms_thread.join();

	ms_thread_load = false;
	ms_thread_joined = false;
	m_titles_registered = false;
	if (!bWasLoading)
		return;

	CResourceManager::Instance().Destroy();
	CResourceManager::Instance().DestroyDeletingList();
}

void CPythonLoading::__LoadData()
{
	std::lock_guard<std::mutex> lock(ms_mutex);

#if defined(__LOADING_PERFORMANCE_CHECK__)
	DWORD dwInitSoundTime = ELTimer_GetMSec();
#endif
	__Initialize();

#if defined(__LOADING_PERFORMANCE_CHECK__)
	DWORD dwLoadSoundTime = ELTimer_GetMSec();
#endif
	__LoadSound();

#if defined(__LOADING_PERFORMANCE_CHECK__)
	DWORD dwLoadNPCTime = ELTimer_GetMSec();
#endif
	__LoadEffect();

#if defined(__LOADING_PERFORMANCE_CHECK__)
	DWORD dwLoadRaceHeightTime = ELTimer_GetMSec();
#endif
	__LoadNPCList();

#if defined(__LOADING_PERFORMANCE_CHECK__)
	DWORD dwLoadRaceTime = ELTimer_GetMSec();
#endif
	__LoadRaceHeight();

#if defined(__LOADING_PERFORMANCE_CHECK__)
	DWORD dwLoadEffectTime = ELTimer_GetMSec();
#endif
	__LoadRace();
	// Title names are registered by JoinThreadLoading on the Python thread.

#if defined(__LOADING_PERFORMANCE_CHECK__)
	DWORD dwRegisterColorTime = ELTimer_GetMSec();
#endif
	__RegisterColor();

#if defined(__LOADING_PERFORMANCE_CHECK__)
	DWORD dwRegisterEmotionIconTime = ELTimer_GetMSec();
#endif
	__RegisterEmotionIcon();

#if defined(__LOADING_PERFORMANCE_CHECK__)
	DWORD dwLastLoadTime = ELTimer_GetMSec();
	if (dwLastLoadTime - dwInitSoundTime > 10)
	{
		static FILE* pFile = fopen("perf_all_load.txt", "a");
		fprintf(pFile, "Initialize Load Time : %d\n", dwLoadSoundTime - dwInitSoundTime);
		fprintf(pFile, "Sound Load Time : %d\n", dwLoadNPCTime - dwLoadSoundTime);
		fprintf(pFile, "Effect Load Time : %d\n", dwLoadRaceHeightTime - dwLoadNPCTime);
		fprintf(pFile, "NPC Load Load Time : %d\n", dwLoadRaceTime - dwLoadRaceHeightTime);
		fprintf(pFile, "Race Height Time : %d\n", dwLoadEffectTime - dwLoadRaceTime);
		fprintf(pFile, "Race Load Time : %d\n", dwRegisterColorTime - dwLoadEffectTime);
		fprintf(pFile, "Register Color Load Time : %d\n", dwRegisterEmotionIconTime - dwRegisterColorTime);
		fprintf(pFile, "Register Emotion Icon Load Time : %d\n", dwLastLoadTime - dwRegisterEmotionIconTime);
		fprintf(pFile, "Total Load Time : %d\n", dwLastLoadTime - dwInitSoundTime);
		fprintf(pFile, "----------------------------------\n");
		fflush(pFile);
	}
#endif
}

void CPythonLoading::LoadBackground(const long c_lPlayerX, const long c_lPlayerY)
{
	JoinThreadLoading();

	CPythonNetworkStream& rkNetStream = CPythonNetworkStream::Instance();
	__RegisterDungeonMapName();
	rkNetStream.Warp(c_lPlayerX, c_lPlayerY);

	BYTE bJob = static_cast<BYTE>(RaceToJob(rkNetStream.GetMainActorRace()));
	BYTE bSkillGroup = static_cast<BYTE>(rkNetStream.GetMainActorSkillGroup());
	BYTE bEmpire = static_cast<BYTE>(rkNetStream.GetMainActorEmpire());

	bool bEnable678thSkill = true;
	PyObject* pConstInfo = PyImport_ImportModule("constInfo");
	if (pConstInfo)
	{
		PyObject* pSkillFlag = PyObject_GetAttrString(pConstInfo, "NEW_678TH_SKILL_ENABLE");
		if (pSkillFlag)
		{
			bEnable678thSkill = PyObject_IsTrue(pSkillFlag) == 1;
			Py_DECREF(pSkillFlag);
		}
		else
			PyErr_Clear();
		Py_DECREF(pConstInfo);
	}
	else
		PyErr_Clear();
	RegisterSkills(bJob, bSkillGroup, bEmpire, bEnable678thSkill);

	CPythonBackground& rkBackground = CPythonBackground::Instance();
	rkBackground.SetViewDistanceSet(CPythonBackground::DISTANCE0, 25600);
	rkBackground.SelectViewDistanceNum(CPythonBackground::DISTANCE0);
	if (rkBackground.IsMapReady())
		CPythonApplication::Instance().SetGlobalCenterPosition(c_lPlayerX, c_lPlayerY);

	rkNetStream.StartGame();
}

void CPythonLoading::__Initialize()
{
	CInstanceBase::SetDustGap(DUST_GAP);
	CInstanceBase::SetHorseDustGap(HORSE_DUST_GAP);
	CPythonPlayer::Instance().RegisterEffect(CPythonPlayer::EFFECT_PICK, "d:/ymir work/effect/etc/click/click.mse", true);
}

void CPythonLoading::__LoadSound()
{
	for (const SSoundData& it : g_vUseSoundData)
		CPythonItem::Instance().SetUseSoundFileName(it.dwItemType, it.stFileName);

	for (const SSoundData& it : g_vDropSoundData)
		CPythonItem::Instance().SetDropSoundFileName(it.dwItemType, it.stFileName);
}

// ����Ʈ�� ���� �����̴�. CInstanceBaseŬ������ static DWORD ms_adwCRCAffectEffect[EFFECT_NUM] �̶�� �迭���ٰ� ������ ���´�.
void CPythonLoading::__LoadEffect()
{
	const std::string stLocalePath = LocaleService_GetLocalePath();
	static const char* newCibnEmpireEffects[] =
	{
		"locale/newcibn/effect/empire/empire_A.mse",
		"locale/newcibn/effect/empire/empire_B.mse",
		"locale/newcibn/effect/empire/empire_C.mse",
	};
	for (const SEffectData& it : g_vEffectData)
	{
		std::string stEffectFileName = it.stEftFileName;
		if (it.iEftType == CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_YMIR)
			stEffectFileName = stLocalePath + "/effect/gm.mse";
		else if (stLocalePath == "locale/newcibn" &&
			it.iEftType >= CInstanceBase::EFFECT_EMPIRE + 1 &&
			it.iEftType <= CInstanceBase::EFFECT_EMPIRE + 3)
			stEffectFileName = newCibnEmpireEffects[it.iEftType - CInstanceBase::EFFECT_EMPIRE - 1];
		CInstanceBase::RegisterEffect(it.iEftType, it.stEftAttachBone.c_str(), stEffectFileName.c_str(), it.bCache);
	}

	for (const SFlyEffectData& it : g_vFlyEffectData)
		CFlyingManager::Instance().RegisterIndexedFlyData(it.dwIndex, it.byType, it.stFileName.c_str());

	for (const SEmoticonEffectData& it : g_vEmotionEffectData)
	{
		CInstanceBase::RegisterEffect(it.iEftType, it.stEftAttachBone.c_str(), it.stEftFileName.c_str(), it.bCache);
		CPythonNetworkStream::Instance().RegisterEmoticonString(it.stCommand.c_str());
	}
}

void CPythonLoading::__LoadNPCList()
{
	CMappedFile kMappedFile;
	const void* pvData;

	if (!CEterPackManager::Instance().Get(kMappedFile, g_stNPCList_FileName.c_str(), &pvData))
	{
		Tracef("CPythonLoading::LoadNPCList(): Failed to load file %s", g_stNPCList_FileName.c_str());
		return;
	}

	CMemoryTextFileLoader kMemTextFileLoader;
	kMemTextFileLoader.Bind(kMappedFile.Size(), pvData);

	CTokenVector kTokenVector;
	for (DWORD dwLine = 0; dwLine < kMemTextFileLoader.GetLineCount(); ++dwLine)
	{
		if (!kMemTextFileLoader.SplitLineByTab(dwLine, &kTokenVector))
			continue;

		if (kTokenVector.size() <= NPC_TOKEN_TYPE_MODEL_NAME || !IsNumber(kTokenVector[NPC_TOKEN_TYPE_VNUM]))
		{
			Tracef("CPythonLoading::LoadNPCList(): Strange Token Count [Line:%d / TokenCount:%d]\n", dwLine, kTokenVector.size());
			continue;
		}

		DWORD dwVnum = std::stol(kTokenVector[NPC_TOKEN_TYPE_VNUM].c_str());

		std::string& stModelName = kTokenVector[NPC_TOKEN_TYPE_MODEL_NAME];
		stModelName.erase(remove(stModelName.begin(), stModelName.end(), ' '), stModelName.end());

		if (dwVnum == 0)
		{
			if (kTokenVector.size() <= NPC_LIST_TOKEN_MODEL_SRC_NAME)
				continue;
			std::string& stModelSrcName = kTokenVector[NPC_LIST_TOKEN_MODEL_SRC_NAME];
			stModelSrcName.erase(remove(stModelSrcName.begin(), stModelSrcName.end(), ' '), stModelSrcName.end());

			CRaceManager::Instance().RegisterRaceSrcName(stModelName.c_str(), stModelSrcName.c_str());
		}
		else
		{
			CRaceManager::Instance().RegisterRaceName(dwVnum, stModelName.c_str());
		}
	}
}

void CPythonLoading::__LoadRaceHeight()
{
	CMappedFile kMappedFile;
	const void* pvData;

	if (!CEterPackManager::Instance().Get(kMappedFile, g_stRaceHeight_FileName.c_str(), &pvData))
	{
		Tracef("CPythonLoading::LoadRaceHeight(): Failed to load file %s", g_stRaceHeight_FileName.c_str());
		return;
	}

	CMemoryTextFileLoader kMemTextFileLoader;
	kMemTextFileLoader.Bind(kMappedFile.Size(), pvData);

	CTokenVector kTokenVector;
	for (DWORD dwLine = 0; dwLine < kMemTextFileLoader.GetLineCount(); ++dwLine)
	{
		if (!kMemTextFileLoader.SplitLineByTab(dwLine, &kTokenVector))
			continue;

		if (kTokenVector.size() < 2)
		{
			Tracef("CPythonLoading::LoadRaceHeight(): Strange Token Count [Line:%d / TokenCount:%d]\n", dwLine, kTokenVector.size());
			continue;
		}

		DWORD dwRaceIndex = std::stol(kTokenVector[0].c_str());
		float fRaceHeight = std::stof(kTokenVector[1].c_str());

		CRaceManager::Instance().SetRaceHeight(dwRaceIndex, fRaceHeight);
	}
}

void CPythonLoading::__LoadRace()
{
	for (const SRaceData& it : g_vRaceData)
	{
		CRaceManager::Instance().CreateRace(it.dwRaceIndex);
		CRaceManager::Instance().SelectRace(it.dwRaceIndex);

		CRaceData* pRaceData = nullptr;
		if (!CRaceManager::Instance().GetRaceDataPointer(it.dwRaceIndex, &pRaceData))
		{
			TraceError("CPythonLoading::LoadRace(): No race data selected by %d.", it.dwRaceIndex);
			break;
		}

		if (!pRaceData->LoadRaceData(it.stFileName.c_str()))
		{
			TraceError("CPythonLoading::LoadRace(): Failed to load race (vnum %d) data %s", it.dwRaceIndex, it.stFileName.c_str());
			break;
		}

		switch (it.dwRaceIndex)
		{
		case CRaceData::RACE_WARRIOR_M:
		case CRaceData::RACE_WARRIOR_W:
			__LoadWarriorEx(it.stPathName.c_str());
			break;

		case CRaceData::RACE_ASSASSIN_M:
		case CRaceData::RACE_ASSASSIN_W:
			__LoadAssassinEx(it.stPathName.c_str());
			break;

		case CRaceData::RACE_SURA_M:
		case CRaceData::RACE_SURA_W:
			__LoadSuraEx(it.stPathName.c_str());
			break;

		case CRaceData::RACE_SHAMAN_M:
		case CRaceData::RACE_SHAMAN_W:
			__LoadShamanEx(it.stPathName.c_str());
			break;

#if defined(ENABLE_WOLFMAN_CHARACTER)
		case CRaceData::RACE_WOLFMAN_M:
			__LoadWolfmanEx(it.stPathName.c_str());
			break;
#endif

		default:
			break;
		}
	}
}

void CPythonLoading::__LoadWarriorEx(const std::string& c_stPathName)
{
	CRaceData* pRaceData = CRaceManager::Instance().GetSelectedRaceDataPointer();
	if (pRaceData == nullptr)
		return;

	// GENERAL MODE
	{
		__SetIntroMotions(pRaceData, c_stPathName);
		__SetGeneralMotion(pRaceData, c_stPathName);

		pRaceData->SetMotionRandomWeight(CRaceMotionData::MODE_GENERAL, CRaceMotionData::NAME_WAIT, 0, 70);

		MotionDataVector vGeneralMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 30 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack.msa", 50 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack_1.msa", 50 },
		};

		__LoadMotionData(pRaceData, vGeneralMotionData, CRaceMotionData::MODE_GENERAL, (c_stPathName + "general/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_GENERAL);
	}

	// SKILL
	{
		MotionDataVector vSkillMotionData;
		vSkillMotionData.clear();

		std::vector<SkillGradeMotionData> vSkillGradeMotionData
		{
			{ 1, "samyeon" },
			{ 2, "palbang" },
			{ 3, "jeongwi" },
			{ 4, "geomgyeong" },
			{ 5, "tanhwan" },
#if defined(ENABLE_678TH_SKILL)
			{ 6, "gihyeol" },
#endif
			{ 9, "finish" },
			{ 16, "gigongcham" },
			{ 17, "gyeoksan" },
			{ 18, "daejin" },
			{ 19, "cheongeun" },
			{ 20, "geompung" },
#if defined(ENABLE_678TH_SKILL)
			{ 21, "noegeom" },
#endif
			{ 24, "finish" },
		};

		// Skill Grade Motions
		for (BYTE bSkillEffectIndex = 0; bSkillEffectIndex < CPythonSkill::SKILL_EFFECT_COUNT; bSkillEffectIndex++)
		{
			for (SkillGradeMotionData& it : vSkillGradeMotionData)
			{
				std::string stEndString = "";
				if (bSkillEffectIndex != 0)
					stEndString = "_" + std::to_string(bSkillEffectIndex + 1);

				SMotionData MotionData;
				MotionData.wMotionIndex = CRaceMotionData::NAME_SKILL + (bSkillEffectIndex * CPythonSkill::SKILL_GRADEGAP) + it.first;
				MotionData.stFileName = std::string(it.second + stEndString + ".msa");
				MotionData.bPercentage = 0;

				vSkillMotionData.emplace_back(MotionData);
			}
		}

		// Guild Skill Motions
		for (const SMotionData& it : g_vGuildSkillMotionData)
			vSkillMotionData.emplace_back(it);

		__LoadMotionData(pRaceData, vSkillMotionData, CRaceMotionData::MODE_GENERAL, (c_stPathName + "skill/"));
	}

	// EMOTION
	__RegisterEmotionAnis(pRaceData, c_stPathName);

	// ONEHAND_SWORD BATTLE
	{
		MotionDataVector vOneHandSwordMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 50 },
			{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 50 },
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE, "damage_1.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_2.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_3.msa", 50 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_4, "combo_04.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_5, "combo_05.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_6, "combo_06.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_7, "combo_07.msa", 0 },
		};

		__LoadMotionData(pRaceData, vOneHandSwordMotionData, CRaceMotionData::MODE_ONEHAND_SWORD, (c_stPathName + "onehand_sword/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_ONEHAND_SWORD);
	}

	// TWOHAND_SWORD BATTLE
	{
		MotionDataVector vTwoHandSwordMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 70 },
			{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 30 },
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE, "damage_1.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_2.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_3.msa", 50 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_4, "combo_04.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_5, "combo_05.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_6, "combo_06.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_7, "combo_07.msa", 0 },
		};

		__LoadMotionData(pRaceData, vTwoHandSwordMotionData, CRaceMotionData::MODE_TWOHAND_SWORD, (c_stPathName + "twohand_sword/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_TWOHAND_SWORD);
	}

	// FISHING
	__LoadFishing(pRaceData, c_stPathName);

	// HORSE
	{
		MotionDataVector vHorseMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 90 },
			{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 9 },
			{ CRaceMotionData::NAME_WAIT, "wait_2.msa", 1 },
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage.msa", 0 },
			{ CRaceMotionData::NAME_DEAD, "dead.msa", 0 },
			{ HORSE_SKILL_CHARGE, "skill_charge.msa", 0 },
		};

		__LoadMotionData(pRaceData, vHorseMotionData, CRaceMotionData::MODE_HORSE, (c_stPathName + "horse/"));

		// HORSE_ONEHAND_SWORD
		{
			MotionDataVector vHorseOneHandSwordMotionData
			{
				{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
				{ HORSE_SKILL_WILDATTACK, "skill_wildattack.msa", 0 },
				{ HORSE_SKILL_SPLASH, "skill_splash.msa", 0 },
			};

			__LoadMotionData(pRaceData, vHorseOneHandSwordMotionData, CRaceMotionData::MODE_HORSE_ONEHAND_SWORD, (c_stPathName + "horse_onehand_sword/"));
			__LoadHorseComboType(pRaceData, CRaceMotionData::MODE_HORSE_ONEHAND_SWORD);
		}

		// HORSE_TWOHAND_SWORD
		{
			MotionDataVector vHorseTwoHandSwordMotionData
			{
				{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
				{ HORSE_SKILL_WILDATTACK, "skill_wildattack.msa", 0 },
				{ HORSE_SKILL_SPLASH, "skill_splash.msa", 0 },
			};

			__LoadMotionData(pRaceData, vHorseTwoHandSwordMotionData, CRaceMotionData::MODE_HORSE_TWOHAND_SWORD, (c_stPathName + "horse_twohand_sword/"));
			__LoadHorseComboType(pRaceData, CRaceMotionData::MODE_HORSE_TWOHAND_SWORD);
		}
	}

	// BONE
	pRaceData->RegisterAttachingBoneName(CRaceData::EParts::PART_WEAPON, "equip_right_hand");
#if defined(ENABLE_ACCE_COSTUME_SYSTEM)
	pRaceData->RegisterAttachingBoneName(CRaceData::EParts::PART_ACCE, "Bip01 Spine2");
#endif
}

void CPythonLoading::__LoadAssassinEx(const std::string& c_stPathName)
{
	CRaceData* pRaceData = CRaceManager::Instance().GetSelectedRaceDataPointer();
	if (pRaceData == nullptr)
		return;

	// GENERAL MODE
	{
		__SetIntroMotions(pRaceData, c_stPathName);
		__SetGeneralMotion(pRaceData, c_stPathName);

		pRaceData->SetMotionRandomWeight(CRaceMotionData::MODE_GENERAL, CRaceMotionData::NAME_WAIT, 0, 70);

		MotionDataVector vGeneralMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 30 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack.msa", 50 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack_1.msa", 50 },
		};

		__LoadMotionData(pRaceData, vGeneralMotionData, CRaceMotionData::MODE_GENERAL, (c_stPathName + "general/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_GENERAL);
	}

	// SKILL
	{
		MotionDataVector vSkillMotionData;
		vSkillMotionData.clear();

		std::vector<SkillGradeMotionData> vSkillGradeMotionData
		{
			{ 1, "amseup" },
			{ 2, "gungsin" },
			{ 3, "charyun" },
			{ 4, "eunhyeong" },
			{ 5, "sangong" },
			{ 6, "seomjeon" },
			{ 9, "ilgangpyo" },
			{ 16, "yeonsa" },
			{ 17, "gwangyeok" },
			{ 18, "hwajo" },
			{ 19, "gyeonggong" },
			{ 20, "dokgigung" },
			{ 21, "seomgwang" },
			{ 24, "pungraepo" },
		};

		// Skill Grade Motions
		for (BYTE bSkillEffectIndex = 0; bSkillEffectIndex < CPythonSkill::SKILL_EFFECT_COUNT; bSkillEffectIndex++)
		{
			for (SkillGradeMotionData& it : vSkillGradeMotionData)
			{
				std::string stEndString = "";
				if (bSkillEffectIndex != 0)
					stEndString = "_" + std::to_string(bSkillEffectIndex + 1);

				SMotionData MotionData;
				MotionData.wMotionIndex = CRaceMotionData::NAME_SKILL + (bSkillEffectIndex * CPythonSkill::SKILL_GRADEGAP) + it.first;
				MotionData.stFileName = std::string(it.second + stEndString + ".msa");
				MotionData.bPercentage = 0;

				vSkillMotionData.emplace_back(MotionData);
			}
		}

		// Guild Skill Motions
		for (const SMotionData& it : g_vGuildSkillMotionData)
			vSkillMotionData.emplace_back(it);

		__LoadMotionData(pRaceData, vSkillMotionData, CRaceMotionData::MODE_GENERAL, (c_stPathName + "skill/"));
	}

	// EMOTION
	__RegisterEmotionAnis(pRaceData, c_stPathName);

	// ONEHAND_SWORD BATTLE
	{
		MotionDataVector vOneHandSwordMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 70 },
			{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 30 },
			{ CRaceMotionData::NAME_WALK, "walk.msa" , 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa" , 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE, "damage_1.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_2.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_3.msa", 50 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_4, "combo_04.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_5, "combo_05.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_6, "combo_06.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_7, "combo_07.msa" , 0 },
		};

		__LoadMotionData(pRaceData, vOneHandSwordMotionData, CRaceMotionData::MODE_ONEHAND_SWORD, (c_stPathName + "onehand_sword/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_ONEHAND_SWORD);
	}

	// DUALHAND_SWORD BATTLE
	{
		MotionDataVector vDualHandSwordMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 70 },
			{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 30 },
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE, "damage_1.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_2.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_3.msa", 50 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_4, "combo_04.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_5, "combo_05.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_6, "combo_06.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_7, "combo_07.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_8, "combo_08.msa", 0 },
		};

		__LoadMotionData(pRaceData, vDualHandSwordMotionData, CRaceMotionData::MODE_DUALHAND_SWORD, (c_stPathName + "dualhand_sword/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_DUALHAND_SWORD);
	}

	// BOW BATTLE
	{
		MotionDataVector vBowMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 70 },
			{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 30},
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 50},
			{ CRaceMotionData::NAME_DAMAGE, "damage_1.msa", 50},
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_2.msa", 50},
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_3.msa", 50},
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack.msa", 0 },
		};

		__LoadMotionData(pRaceData, vBowMotionData, CRaceMotionData::MODE_BOW, (c_stPathName + "bow/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_BOW);
	}

	// FISHING
	__LoadFishing(pRaceData, c_stPathName);

	// HORSE
	{
		MotionDataVector vHorseMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 90 },
			{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 9 },
			{ CRaceMotionData::NAME_WAIT, "wait_2.msa", 1 },
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage.msa", 0 },
			{ CRaceMotionData::NAME_DEAD, "dead.msa", 0 },
			{ HORSE_SKILL_CHARGE, "skill_charge.msa", 0 },
		};

		__LoadMotionData(pRaceData, vHorseMotionData, CRaceMotionData::MODE_HORSE, (c_stPathName + "horse/"));

		// HORSE_ONEHAND_SWORD
		{
			MotionDataVector vHorseOneHandSwordMotionData
			{
				{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
				{ HORSE_SKILL_WILDATTACK, "skill_wildattack.msa", 0 },
				{ HORSE_SKILL_SPLASH, "skill_splash.msa", 0 },
			};

			__LoadMotionData(pRaceData, vHorseOneHandSwordMotionData, CRaceMotionData::MODE_HORSE_ONEHAND_SWORD, (c_stPathName + "horse_onehand_sword/"));
			__LoadHorseComboType(pRaceData, CRaceMotionData::MODE_HORSE_ONEHAND_SWORD);
		}

		// HORSE_DUALHAND_SWORD
		{
			MotionDataVector vHorseTwoHandSwordMotionData
			{
				{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
				{ HORSE_SKILL_WILDATTACK, "skill_wildattack.msa", 0 },
				{ HORSE_SKILL_SPLASH, "skill_splash.msa", 0 },
			};

			__LoadMotionData(pRaceData, vHorseTwoHandSwordMotionData, CRaceMotionData::MODE_HORSE_DUALHAND_SWORD, (c_stPathName + "horse_dualhand_sword/"));
			__LoadHorseComboType(pRaceData, CRaceMotionData::MODE_HORSE_DUALHAND_SWORD);
		}

		// HORSE_BOW
		{
			MotionDataVector vHorseBowMotionData
			{
				{ CRaceMotionData::NAME_WAIT, "wait.msa", 90 },
				{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 9 },
				{ CRaceMotionData::NAME_WAIT, "wait_2.msa", 1 },
				{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
				{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 0 },
				{ CRaceMotionData::NAME_DEAD, "dead.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack.msa", 0 },
				{ HORSE_SKILL_WILDATTACK, "skill_wildattack.msa", 0 },
				{ HORSE_SKILL_SPLASH, "skill_splash.msa", 0 },
			};

			__LoadMotionData(pRaceData, vHorseBowMotionData, CRaceMotionData::MODE_HORSE_BOW, (c_stPathName + "horse_bow/"));
			__LoadHorseComboType(pRaceData, CRaceMotionData::MODE_HORSE_BOW);
		}
	}

	// BONE
	pRaceData->RegisterAttachingBoneName(CRaceData::PART_WEAPON, "equip_right");
	pRaceData->RegisterAttachingBoneName(CRaceData::PART_WEAPON_LEFT, "equip_left");
#if defined(ENABLE_ACCE_COSTUME_SYSTEM)
	pRaceData->RegisterAttachingBoneName(CRaceData::PART_ACCE, "Bip01 Spine2");
#endif
}

void CPythonLoading::__LoadSuraEx(const std::string& c_stPathName)
{
	CRaceData* pRaceData = CRaceManager::Instance().GetSelectedRaceDataPointer();
	if (pRaceData == nullptr)
		return;

	// GENERAL MODE
	{
		__SetIntroMotions(pRaceData, c_stPathName);
		__SetGeneralMotion(pRaceData, c_stPathName);

		MotionDataVector vGeneralMotionData
		{
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack.msa", 50 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack_1.msa", 50 },
		};

		__LoadMotionData(pRaceData, vGeneralMotionData, CRaceMotionData::MODE_GENERAL, (c_stPathName + "general/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_GENERAL);
	}

	// SKILL
	{
		MotionDataVector vSkillMotionData;
		vSkillMotionData.clear();

		std::vector<SkillGradeMotionData> vSkillGradeMotionData
		{
			{ 1, "swaeryeong" },
			{ 2, "yonggwon" },
			{ 3, "gwigeom" },
			{ 4, "gongpo" },
			{ 5, "jumagap" },
			{ 6, "pabeop" },
			{ 9, "akgi" },
			{ 16, "maryeong" },
			{ 17, "hwayeom" },
			{ 18, "muyeong" },
			{ 19, "heuksin" },
			{ 20, "tusok" },
			{ 21, "mahwan" },
			{ 24, "akgi_dark" },
		};

		// Skill Grade Motions
		for (BYTE bSkillEffectIndex = 0; bSkillEffectIndex < CPythonSkill::SKILL_EFFECT_COUNT; bSkillEffectIndex++)
		{
			for (SkillGradeMotionData& it : vSkillGradeMotionData)
			{
				std::string stEndString = "";
				if (bSkillEffectIndex != 0)
					stEndString = "_" + std::to_string(bSkillEffectIndex + 1);

				SMotionData MotionData;
				MotionData.wMotionIndex = CRaceMotionData::NAME_SKILL + (bSkillEffectIndex * CPythonSkill::SKILL_GRADEGAP) + it.first;
				MotionData.stFileName = std::string(it.second + stEndString + ".msa");
				MotionData.bPercentage = 0;

				vSkillMotionData.emplace_back(MotionData);
			}
		}

		// Guild Skill Motions
		for (const SMotionData& it : g_vGuildSkillMotionData)
			vSkillMotionData.emplace_back(it);

		__LoadMotionData(pRaceData, vSkillMotionData, CRaceMotionData::MODE_GENERAL, (c_stPathName + "skill/"));
	}

	// EMOTION
	__RegisterEmotionAnis(pRaceData, c_stPathName);

	// ONEHAND_SWORD BATTLE
	{
		MotionDataVector vOneHandSwordMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 0 },
			{ CRaceMotionData::NAME_WALK, "walk.msa" , 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa" , 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE, "damage_1.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_2.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_3.msa", 50 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_4, "combo_04.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_5, "combo_05.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_6, "combo_06.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_7, "combo_07.msa" , 0 },
		};

		__LoadMotionData(pRaceData, vOneHandSwordMotionData, CRaceMotionData::MODE_ONEHAND_SWORD, (c_stPathName + "onehand_sword/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_ONEHAND_SWORD);
	}

	// FISHING
	__LoadFishing(pRaceData, c_stPathName);

	// HORSE
	{
		MotionDataVector vHorseMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 90 },
			{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 9 },
			{ CRaceMotionData::NAME_WAIT, "wait_2.msa", 1 },
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage.msa", 0 },
			{ CRaceMotionData::NAME_DEAD, "dead.msa", 0 },
			{ HORSE_SKILL_CHARGE, "skill_charge.msa", 0 },
			{ HORSE_SKILL_SPLASH, "skill_splash.msa", 0 },
		};

		__LoadMotionData(pRaceData, vHorseMotionData, CRaceMotionData::MODE_HORSE, (c_stPathName + "horse/"));

		// HORSE_ONEHAND_SWORD
		{
			MotionDataVector vHorseOneHandSwordMotionData
			{
				{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
				{ HORSE_SKILL_WILDATTACK, "skill_wildattack.msa", 0 },
			};

			__LoadMotionData(pRaceData, vHorseOneHandSwordMotionData, CRaceMotionData::MODE_HORSE_ONEHAND_SWORD, (c_stPathName + "horse_onehand_sword/"));
			__LoadHorseComboType(pRaceData, CRaceMotionData::MODE_HORSE_ONEHAND_SWORD);
		}
	}

	// BONE
	pRaceData->RegisterAttachingBoneName(CRaceData::PART_WEAPON, "equip_right");
#if defined(ENABLE_ACCE_COSTUME_SYSTEM)
	pRaceData->RegisterAttachingBoneName(CRaceData::PART_ACCE, "Bip01 Spine2");
#endif
}

void CPythonLoading::__LoadShamanEx(const std::string& c_stPathName)
{
	CRaceData* pRaceData = CRaceManager::Instance().GetSelectedRaceDataPointer();
	if (pRaceData == nullptr)
		return;

	// GENERAL MODE
	{
		__SetIntroMotions(pRaceData, c_stPathName);
		__SetGeneralMotion(pRaceData, c_stPathName);

		MotionDataVector vGeneralMotionData
		{
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack.msa", 50 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack_1.msa", 50 },
		};

		__LoadMotionData(pRaceData, vGeneralMotionData, CRaceMotionData::MODE_GENERAL, (c_stPathName + "general/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_GENERAL);
	}

	// SKILL
	{
		MotionDataVector vSkillMotionData
		{
			{ CRaceMotionData::NAME_SKILL + 1, "bipabu.msa" },
			{ CRaceMotionData::NAME_SKILL + 2, "yongpa.msa" },
			{ CRaceMotionData::NAME_SKILL + 3, "paeryong.msa" },
			{ CRaceMotionData::NAME_SKILL + 4, "hosin_target.msa" },
			{ CRaceMotionData::NAME_SKILL + 5, "boho_target.msa" },
			{ CRaceMotionData::NAME_SKILL + 6, "gicheon_target.msa" },
			{ CRaceMotionData::NAME_SKILL + 16, "noejeon.msa" },
			{ CRaceMotionData::NAME_SKILL + 17, "byeorak.msa" },
			{ CRaceMotionData::NAME_SKILL + 18, "pokroe.msa" },
			{ CRaceMotionData::NAME_SKILL + 19, "jeongeop_target.msa" },
			{ CRaceMotionData::NAME_SKILL + 20, "kwaesok_target.msa" },
			{ CRaceMotionData::NAME_SKILL + 21, "jeungryeok_target.msa" },
		};

		std::vector<SkillGradeMotionData> vSkillGradeMotionData
		{
			{ 1, "bipabu" },
			{ 2, "yongpa" },
			{ 3, "paeryong" },
			{ 4, "hosin" },
			{ 5, "boho" },
			{ 6, "gicheon" },
			{ 9, "meteo" },
			{ 16, "noejeon" },
			{ 17, "byeorak" },
			{ 18, "pokroe" },
			{ 19, "jeongeop" },
			{ 20, "kwaesok" },
			{ 21, "jeungryeok" },
			{ 24, "chunwoon" },
		};

		// Grade Skills
		for (BYTE bSkillEffectIndex = 1; bSkillEffectIndex < CPythonSkill::SKILL_EFFECT_COUNT; bSkillEffectIndex++)
		{
			for (SkillGradeMotionData& it : vSkillGradeMotionData)
			{
				std::string stEndString = "_" + std::to_string(bSkillEffectIndex + 1);

				SMotionData MotionData;
				MotionData.wMotionIndex = CRaceMotionData::NAME_SKILL + (bSkillEffectIndex * CPythonSkill::SKILL_GRADEGAP) + it.first;
				MotionData.stFileName = std::string(it.second + stEndString + ".msa");
				MotionData.bPercentage = 0;

				vSkillMotionData.emplace_back(MotionData);
			}
		}

		// Guild Skill Motions
		for (const SMotionData& it : g_vGuildSkillMotionData)
			vSkillMotionData.emplace_back(it);

		__LoadMotionData(pRaceData, vSkillMotionData, CRaceMotionData::MODE_GENERAL, (c_stPathName + "skill/"));
	}

	// EMOTION
	__RegisterEmotionAnis(pRaceData, c_stPathName);

	// FAN BATTLE
	{
		MotionDataVector vFanMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 0 },
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 50,},
			{ CRaceMotionData::NAME_DAMAGE, "damage_1.msa", 50, },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_2.msa", 50,},
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_3.msa", 50, },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_4, "combo_04.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_5, "combo_05.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_6, "combo_06.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_7, "combo_07.msa", 0 },
		};

		__LoadMotionData(pRaceData, vFanMotionData, CRaceMotionData::MODE_FAN, (c_stPathName + "fan/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_FAN);
	}

	// BELL BATTLE
	{
		MotionDataVector vBellMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 0 },
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 50,},
			{ CRaceMotionData::NAME_DAMAGE, "damage_1.msa", 50, },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_2.msa", 50,},
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_3.msa", 50, },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_4, "combo_04.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_5, "combo_05.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_6, "combo_06.msa", 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_7, "combo_07.msa", 0 },
		};

		__LoadMotionData(pRaceData, vBellMotionData, CRaceMotionData::MODE_BELL, (c_stPathName + "bell/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_BELL);
	}

	// FISHING
	__LoadFishing(pRaceData, c_stPathName);

	// HORSE
	{
		MotionDataVector vHorseMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 90 },
			{ CRaceMotionData::NAME_WAIT, "wait_1.msa", 9 },
			{ CRaceMotionData::NAME_WAIT, "wait_2.msa", 1 },
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "damage.msa", 0 },
			{ CRaceMotionData::NAME_DEAD, "dead.msa", 0 },
			{ HORSE_SKILL_CHARGE, "skill_charge.msa", 0 },
		};

		__LoadMotionData(pRaceData, vHorseMotionData, CRaceMotionData::MODE_HORSE, (c_stPathName + "horse/"));

		// HORSE_FAN
		{
			MotionDataVector vHorseFanMotionData
			{
				{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
				{ HORSE_SKILL_WILDATTACK, "skill_wildattack.msa", 0 },
			};

			__LoadMotionData(pRaceData, vHorseFanMotionData, CRaceMotionData::MODE_HORSE_FAN, (c_stPathName + "horse_fan/"));
			__LoadHorseComboType(pRaceData, CRaceMotionData::MODE_HORSE_FAN);
		}

		// HORSE_BELL
		{
			MotionDataVector vHorseBellMotionData
			{
				{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
				{ HORSE_SKILL_WILDATTACK, "skill_wildattack.msa", 0 },
			};

			__LoadMotionData(pRaceData, vHorseBellMotionData, CRaceMotionData::MODE_HORSE_BELL, (c_stPathName + "horse_bell/"));
			__LoadHorseComboType(pRaceData, CRaceMotionData::MODE_HORSE_BELL);
		}
	}

	// BONE
	pRaceData->RegisterAttachingBoneName(CRaceData::PART_WEAPON, "equip_right");
	pRaceData->RegisterAttachingBoneName(CRaceData::PART_WEAPON_LEFT, "equip_left");
#if defined(ENABLE_ACCE_COSTUME_SYSTEM)
	pRaceData->RegisterAttachingBoneName(CRaceData::PART_ACCE, "Bip01 Spine2");
#endif
}

#if defined(ENABLE_WOLFMAN_CHARACTER)
void CPythonLoading::__LoadWolfmanEx(const std::string& c_stPathName)
{
	CRaceData* pRaceData = CRaceManager::Instance().GetSelectedRaceDataPointer();
	if (pRaceData == nullptr)
		return;

	// GENERAL MODE
	{
		__SetIntroMotions(pRaceData, c_stPathName);
		__SetNewGeneralMotion(pRaceData, c_stPathName);

		pRaceData->SetMotionRandomWeight(CRaceMotionData::MODE_GENERAL, CRaceMotionData::NAME_WAIT, 0, 50);

		MotionDataVector vGeneralMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait1.msa", 30 },
			{ CRaceMotionData::NAME_WAIT, "wait2.msa", 30 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack1.msa", 50 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "attack2.msa", 50 },
		};

		__LoadMotionData(pRaceData, vGeneralMotionData, CRaceMotionData::MODE_GENERAL, (c_stPathName + "general/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_GENERAL);
	}

	// SKILL
	{
		MotionDataVector vSkillMotionData;
		vSkillMotionData.clear();

		std::vector<SkillGradeMotionData> vSkillGradeMotionData
		{
			{ 1, "split_Slash" },
			{ 2, "wind_death" },
			{ 3, "reef_attack" },
			{ 4, "wreckage" },
			{ 5, "red_possession" },
			{ 6, "blue_possession" },
			{ 9, "pungwoo" },
		};

		// Skill Grade Motions
		for (BYTE bSkillEffectIndex = 0; bSkillEffectIndex < CPythonSkill::SKILL_EFFECT_COUNT; bSkillEffectIndex++)
		{
			for (SkillGradeMotionData& it : vSkillGradeMotionData)
			{
				std::string stEndString = "";
				if (bSkillEffectIndex != 0)
					stEndString = "_" + std::to_string(bSkillEffectIndex);

				SMotionData MotionData;
				MotionData.wMotionIndex = CRaceMotionData::NAME_SKILL + (bSkillEffectIndex * CPythonSkill::SKILL_GRADEGAP) + it.first;
				MotionData.stFileName = std::string(it.second + stEndString + ".msa");
				MotionData.bPercentage = 0;

				vSkillMotionData.emplace_back(MotionData);
			}
		}

		// Guild Skill Motions
		for (const SMotionData& it : g_vGuildSkillMotionData)
			vSkillMotionData.emplace_back(it);

		__LoadMotionData(pRaceData, vSkillMotionData, CRaceMotionData::MODE_CLAW, (c_stPathName + "skill/"));
	}

	// EMOTION
	__RegisterEmotionAnis(pRaceData, c_stPathName);

	// CLAW BATTLE
	{
		MotionDataVector vClawMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 50 },
			{ CRaceMotionData::NAME_WAIT, "wait1.msa", 30 },
			{ CRaceMotionData::NAME_WAIT, "wait2.msa", 20 },
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "front_damage.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE, "front_damage1.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "back_damage.msa", 50 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "back_damage1.msa", 50 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_4, "combo_04.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_5, "combo_05.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_6, "combo_06.msa" , 0 },
			{ CRaceMotionData::NAME_COMBO_ATTACK_7, "combo_07.msa" , 0 },
		};

		__LoadMotionData(pRaceData, vClawMotionData, CRaceMotionData::MODE_CLAW, (c_stPathName + "claw/"));
		__LoadComboType(pRaceData, CRaceMotionData::MODE_CLAW);
	}

	// FISHING
	__LoadFishing(pRaceData, c_stPathName);

	// HORSE
	{
		MotionDataVector vHorseMotionData
		{
			{ CRaceMotionData::NAME_WAIT, "wait.msa", 90 },
			{ CRaceMotionData::NAME_WAIT, "wait1.msa", 9 },
			{ CRaceMotionData::NAME_WAIT, "wait2.msa", 1 },
			{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
			{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE, "front_damage.msa", 0 },
			{ CRaceMotionData::NAME_DAMAGE_BACK, "front_damage.msa", 0 },
			{ CRaceMotionData::NAME_DEAD, "dead.msa", 0 },
			{ HORSE_SKILL_CHARGE, "skill_charge.msa", 0 },
			{ HORSE_SKILL_SPLASH, "skill_splash.msa", 0 },
		};

		__LoadMotionData(pRaceData, vHorseMotionData, CRaceMotionData::MODE_HORSE_CLAW, (c_stPathName + "horse/"));

		// HORSE_CLAW
		{
			MotionDataVector vHorseClawMotionData
			{
				{ CRaceMotionData::NAME_COMBO_ATTACK_1, "combo_01.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_2, "combo_02.msa", 0 },
				{ CRaceMotionData::NAME_COMBO_ATTACK_3, "combo_03.msa", 0 },
				{ HORSE_SKILL_WILDATTACK, "skill_wildattack.msa", 0 },
			};

			__LoadMotionData(pRaceData, vHorseClawMotionData, CRaceMotionData::MODE_HORSE_CLAW, (c_stPathName + "horse_claw/"));
			__LoadHorseComboType(pRaceData, CRaceMotionData::MODE_HORSE_CLAW);
		}
	}

	// BONE
	pRaceData->RegisterAttachingBoneName(CRaceData::PART_WEAPON, "equip_right_weapon");
	pRaceData->RegisterAttachingBoneName(CRaceData::PART_WEAPON_LEFT, "equip_left_weapon");
#if defined(ENABLE_ACCE_COSTUME_SYSTEM)
	pRaceData->RegisterAttachingBoneName(CRaceData::PART_ACCE, "Bip01 Spine2");
#endif
}
#endif

void CPythonLoading::__LoadMotionData(CRaceData* pRaceData, MotionDataVector& vMotionData, WORD wMotionMode, const std::string& rstFullPath)
{
	if (vMotionData.empty())
		return;

	CRaceManager::Instance().SetPathName(rstFullPath.c_str());
	pRaceData->RegisterMotionMode(wMotionMode);
	for (const SMotionData& it : vMotionData)
		__RegisterCacheMotionData(pRaceData, wMotionMode, it.wMotionIndex, it.stFileName.c_str(), it.bPercentage);
}

void CPythonLoading::__RegisterCacheMotionData(CRaceData* pRaceData, WORD wMotionMode, WORD wMotionIndex, const std::string& c_stFileName, BYTE bPercentage)
{
	const char* c_szFullFileName = CRaceManager::Instance().GetFullPathFileName(c_stFileName.c_str());
	const CGraphicThing* c_pMotionThing = pRaceData->RegisterMotionData(wMotionMode, wMotionIndex, c_szFullFileName, bPercentage);
	if (c_pMotionThing && g_bCacheMotionData)
		CResourceManager::Instance().LoadStaticCache(c_pMotionThing->GetFileName());
}

void CPythonLoading::__SetIntroMotions(CRaceData* pRaceData, const std::string& c_stPathName)
{
	CRaceManager::Instance().SetPathName((c_stPathName + "intro/").c_str());
	pRaceData->RegisterMotionMode(CRaceMotionData::MODE_GENERAL);
	for (const SMotionData& it : g_vIntroMotionData)
		__RegisterCacheMotionData(pRaceData, CRaceMotionData::MODE_GENERAL, it.wMotionIndex, it.stFileName.c_str(), it.bPercentage);
}

void CPythonLoading::__SetGeneralMotion(CRaceData* pRaceData, const std::string& c_stPathName)
{
	CRaceManager::Instance().SetPathName((c_stPathName + "general/").c_str());
	pRaceData->RegisterMotionMode(CRaceMotionData::MODE_GENERAL);
	for (const SMotionData& it : g_vGeneralMotionData)
		__RegisterCacheMotionData(pRaceData, CRaceMotionData::MODE_GENERAL, it.wMotionIndex, it.stFileName, it.bPercentage);
}

void CPythonLoading::__SetNewGeneralMotion(CRaceData* pRaceData, const std::string& c_stPathName)
{
	CRaceManager::Instance().SetPathName((c_stPathName + "general/").c_str());
	pRaceData->RegisterMotionMode(CRaceMotionData::MODE_GENERAL);
	for (const SMotionData& it : g_vNewGeneralMotionData)
		__RegisterCacheMotionData(pRaceData, CRaceMotionData::MODE_GENERAL, it.wMotionIndex, it.stFileName, it.bPercentage);
}

// NOTE : Loads Actions and Wedding Actions
void CPythonLoading::__RegisterEmotionAnis(CRaceData* pRaceData, const std::string& c_stPathName)
{
	__LoadAction(pRaceData, c_stPathName);
	__LoadWedding(pRaceData, c_stPathName);
}

void CPythonLoading::__LoadAction(CRaceData* pRaceData, const std::string& c_stPathName)
{
	CRaceManager::Instance().SetPathName((c_stPathName + "action/").c_str());
	pRaceData->RegisterMotionMode(CRaceMotionData::MODE_GENERAL);

	// Actions
	for (const SMotionData& it : g_vActionMotionData)
		__RegisterCacheMotionData(pRaceData, CRaceMotionData::MODE_GENERAL, it.wMotionIndex, it.stFileName, it.bPercentage);

	// Wedding Actions
	pRaceData->RegisterMotionMode(CRaceMotionData::MODE_WEDDING_DRESS);
	for (const SMotionData& it : g_vActionMotionData)
		__RegisterCacheMotionData(pRaceData, CRaceMotionData::MODE_WEDDING_DRESS, it.wMotionIndex, it.stFileName, it.bPercentage);
}

void CPythonLoading::__LoadWedding(CRaceData* pRaceData, const std::string& c_stPathName)
{
	CRaceManager::Instance().SetPathName((c_stPathName + "wedding/").c_str());
	pRaceData->RegisterMotionMode(CRaceMotionData::MODE_WEDDING_DRESS);

	// Wedding
	for (const SMotionData& it : g_vWeddingMotionData)
		__RegisterCacheMotionData(pRaceData, CRaceMotionData::MODE_WEDDING_DRESS, it.wMotionIndex, it.stFileName, it.bPercentage);
}

void CPythonLoading::__LoadFishing(CRaceData* pRaceData, const std::string& c_stPathName)
{
	CRaceManager::Instance().SetPathName((c_stPathName + "fishing/").c_str());
	pRaceData->RegisterMotionMode(CRaceMotionData::MODE_FISHING);

	for (const SMotionData& it : g_vFishingMotionData)
		__RegisterCacheMotionData(pRaceData, CRaceMotionData::MODE_FISHING, it.wMotionIndex, it.stFileName, it.bPercentage);
}

void CPythonLoading::__LoadComboType(CRaceData* pRaceData, WORD wMotionMode)
{
	switch (wMotionMode)
	{
	case CRaceMotionData::MODE_GENERAL:
	{
		// Combo Type 1
		pRaceData->ReserveComboAttack(CRaceMotionData::MODE_GENERAL, COMBO_TYPE_1, 1);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_GENERAL, COMBO_TYPE_1, COMBO_INDEX_1, CRaceMotionData::NAME_COMBO_ATTACK_1);
	}
	break;
	case CRaceMotionData::MODE_ONEHAND_SWORD:
	case CRaceMotionData::MODE_TWOHAND_SWORD:
	case CRaceMotionData::MODE_FAN:
	case CRaceMotionData::MODE_BELL:
#if defined(ENABLE_WOLFMAN_CHARACTER)
	case CRaceMotionData::MODE_CLAW:
#endif
	{
		// Combo Type 1
		pRaceData->ReserveComboAttack(wMotionMode, COMBO_TYPE_1, 4);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_1, COMBO_INDEX_1, CRaceMotionData::NAME_COMBO_ATTACK_1);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_1, COMBO_INDEX_2, CRaceMotionData::NAME_COMBO_ATTACK_2);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_1, COMBO_INDEX_3, CRaceMotionData::NAME_COMBO_ATTACK_3);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_1, COMBO_INDEX_4, CRaceMotionData::NAME_COMBO_ATTACK_4);
		// Combo Type 2
		pRaceData->ReserveComboAttack(wMotionMode, COMBO_TYPE_2, 5);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_2, COMBO_INDEX_1, CRaceMotionData::NAME_COMBO_ATTACK_1);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_2, COMBO_INDEX_2, CRaceMotionData::NAME_COMBO_ATTACK_2);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_2, COMBO_INDEX_3, CRaceMotionData::NAME_COMBO_ATTACK_3);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_2, COMBO_INDEX_4, CRaceMotionData::NAME_COMBO_ATTACK_5);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_2, COMBO_INDEX_5, CRaceMotionData::NAME_COMBO_ATTACK_7);
		// Combo Type 3
		pRaceData->ReserveComboAttack(wMotionMode, COMBO_TYPE_3, 6);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_3, COMBO_INDEX_1, CRaceMotionData::NAME_COMBO_ATTACK_1);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_3, COMBO_INDEX_2, CRaceMotionData::NAME_COMBO_ATTACK_2);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_3, COMBO_INDEX_3, CRaceMotionData::NAME_COMBO_ATTACK_3);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_3, COMBO_INDEX_4, CRaceMotionData::NAME_COMBO_ATTACK_5);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_3, COMBO_INDEX_5, CRaceMotionData::NAME_COMBO_ATTACK_6);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_3, COMBO_INDEX_6, CRaceMotionData::NAME_COMBO_ATTACK_4);
	}
	break;
	case CRaceMotionData::MODE_DUALHAND_SWORD:
	{
		// Combo Type 1
		pRaceData->ReserveComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_1, 4);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_1, COMBO_INDEX_1, CRaceMotionData::NAME_COMBO_ATTACK_1);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_1, COMBO_INDEX_2, CRaceMotionData::NAME_COMBO_ATTACK_2);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_1, COMBO_INDEX_3, CRaceMotionData::NAME_COMBO_ATTACK_3);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_1, COMBO_INDEX_4, CRaceMotionData::NAME_COMBO_ATTACK_4);
		// Combo Type 2
		pRaceData->ReserveComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_2, 5);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_2, COMBO_INDEX_1, CRaceMotionData::NAME_COMBO_ATTACK_1);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_2, COMBO_INDEX_2, CRaceMotionData::NAME_COMBO_ATTACK_2);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_2, COMBO_INDEX_3, CRaceMotionData::NAME_COMBO_ATTACK_3);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_2, COMBO_INDEX_4, CRaceMotionData::NAME_COMBO_ATTACK_5);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_2, COMBO_INDEX_5, CRaceMotionData::NAME_COMBO_ATTACK_7);
		// Combo Type 3
		pRaceData->ReserveComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_3, 6);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_3, COMBO_INDEX_1, CRaceMotionData::NAME_COMBO_ATTACK_1);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_3, COMBO_INDEX_2, CRaceMotionData::NAME_COMBO_ATTACK_2);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_3, COMBO_INDEX_3, CRaceMotionData::NAME_COMBO_ATTACK_3);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_3, COMBO_INDEX_4, CRaceMotionData::NAME_COMBO_ATTACK_5);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_3, COMBO_INDEX_5, CRaceMotionData::NAME_COMBO_ATTACK_6);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_DUALHAND_SWORD, COMBO_TYPE_3, COMBO_INDEX_6, CRaceMotionData::NAME_COMBO_ATTACK_8);
	}
	break;
	case CRaceMotionData::MODE_BOW:
	{
		// Combo Type 1
		pRaceData->ReserveComboAttack(CRaceMotionData::MODE_BOW, COMBO_TYPE_1, 1);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_BOW, COMBO_TYPE_1, COMBO_INDEX_1, CRaceMotionData::NAME_COMBO_ATTACK_1);
	}
	break;
	}
}

void CPythonLoading::__LoadHorseComboType(CRaceData* pRaceData, WORD wMotionMode)
{
	switch (wMotionMode)
	{
	case CRaceMotionData::MODE_HORSE_ONEHAND_SWORD:
	case CRaceMotionData::MODE_HORSE_TWOHAND_SWORD:
	case CRaceMotionData::MODE_HORSE_DUALHAND_SWORD:
	case CRaceMotionData::MODE_HORSE_FAN:
	case CRaceMotionData::MODE_HORSE_BELL:
#if defined(ENABLE_WOLFMAN_CHARACTER)
	case CRaceMotionData::MODE_HORSE_CLAW:
#endif
	{
		// Horse Combo Type 1
		pRaceData->ReserveComboAttack(wMotionMode, COMBO_TYPE_1, 3);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_1, COMBO_INDEX_1, CRaceMotionData::NAME_COMBO_ATTACK_1);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_1, COMBO_INDEX_2, CRaceMotionData::NAME_COMBO_ATTACK_2);
		pRaceData->RegisterComboAttack(wMotionMode, COMBO_TYPE_1, COMBO_INDEX_3, CRaceMotionData::NAME_COMBO_ATTACK_3);
	}
	break;
	case CRaceMotionData::MODE_HORSE_BOW:
	{
		// Horse Combo Type 1
		pRaceData->ReserveComboAttack(CRaceMotionData::MODE_HORSE_BOW, COMBO_TYPE_1, 3);
		pRaceData->RegisterComboAttack(CRaceMotionData::MODE_HORSE_BOW, COMBO_TYPE_1, COMBO_INDEX_1, CRaceMotionData::NAME_COMBO_ATTACK_1);
	}
	break;
	}
}

void CPythonLoading::__RegisterDungeonMapName()
{
	CPythonBackground& rkBackground = CPythonBackground::Instance();
	if (!rkBackground.IsMapReady())
		return;

	for (const std::string& stMapName : g_vDungeonMapName)
		rkBackground.RegisterDungeonMapName(stMapName.c_str());
}

void CPythonLoading::__RegisterTitleName()
{
	BYTE bIndex = 0;
#if defined(ENABLE_GENDER_ALIGNMENT)
	for (const PairedTitleName& it : g_vTitleName)
	{
		const std::string stMaleTitle = GetLocaleInfoString(it.first);
		const std::string stFemaleTitle = GetLocaleInfoString(it.second);
		CInstanceBase::RegisterTitleName(bIndex++, stMaleTitle.c_str());
		CInstanceBase::RegisterTitleName(bIndex++, stFemaleTitle.c_str());
	}
#else
	for (const std::string& stTitle : g_vTitleName)
	{
		const std::string stLocalizedTitle = GetLocaleInfoString(stTitle);
		CInstanceBase::RegisterTitleName(bIndex++, stLocalizedTitle.c_str());
	}
#endif
}

#if defined(ENABLE_GUILD_LEADER_GRADE_NAME)
void CPythonLoading::__RegisterGuildLeaderGradeName()
{
	BYTE bIndex = 0;
	for (const std::string& stTitle : g_vGuildLeaderGradeTitleName)
	{
		const std::string stLocalizedTitle = GetLocaleInfoString(stTitle);
		CInstanceBase::RegisterGuildLeaderGradeName(bIndex++, stLocalizedTitle.c_str());
	}
}
#endif

void CPythonLoading::__RegisterColor()
{
	for (const std::pair<const BYTE, D3DCOLORVALUE>& it : g_mNameColor)
		CInstanceBase::RegisterNameColor(it.first, it.second.r, it.second.g, it.second.b);

	for (const std::pair<const BYTE, D3DCOLORVALUE>& it : g_mTitleNameColor)
		CInstanceBase::RegisterTitleColor(it.first, it.second.r, it.second.g, it.second.b);
}

void CPythonLoading::__RegisterEmotionIcon()
{
	for (const std::pair<const WORD, std::string>& it : g_vEmotionIcon)
	{
		CGraphicImage* pImage = dynamic_cast<CGraphicImage*>(CResourceManager::Instance().GetResourcePointer(it.second.c_str()));
		m_kMap_iEmotionIndex_pkIconImage.emplace(std::make_pair(it.first, pImage));
	}
}

bool CPythonLoading::LoadGuildBuilding(const char* c_szGuildBuildingList)
{
	CMappedFile kMappedFile;
	const void* pvData;

	if (!CEterPackManager::Instance().Get(kMappedFile, c_szGuildBuildingList, &pvData))
	{
		Tracef("CPythonLoading::LoadGuildBuilding(): Failed to load file %s", c_szGuildBuildingList);
		return false;
	}

	CMemoryTextFileLoader kMemTextFileLoader;
	kMemTextFileLoader.Bind(kMappedFile.Size(), pvData);

	PyObject* ppyAppendingList = PyList_New(0);

	CTokenVector kTokenVector;
	for (DWORD dwLine = 0; dwLine < kMemTextFileLoader.GetLineCount(); ++dwLine)
	{
		if (!kMemTextFileLoader.SplitLineByTab(dwLine, &kTokenVector))
			continue;

		const std::string& stVNum = kTokenVector[GUIILD_BUILDING_TOKEN_VNUM];
		if (!IsNumber(stVNum))
			continue;

		if (kTokenVector.size() < GUIILD_BUILDING_LIMIT_TOKEN_COUNT)
		{
			Tracef("CPythonLoading::LoadGuildBuilding(): Strange Token Count [Line:%d / TokenCount:%d]\n", dwLine, kTokenVector.size());
			continue;
		}

		const DWORD& dwVNum = std::stol(stVNum);
		const std::string_view stvType = kTokenVector[GUIILD_BUILDING_TOKEN_TYPE];
		const std::string& stName = kTokenVector[GUIILD_BUILDING_TOKEN_NAME];
		const std::string& stLocaleName = kTokenVector[GUIILD_BUILDING_TOKEN_LOCAL_NAME];

		int xRotLimit = std::stoi(kTokenVector[GUIILD_BUILDING_TOKEN_X_ROT_LIMIT]);
		int yRotLimit = std::stoi(kTokenVector[GUIILD_BUILDING_TOKEN_Y_ROT_LIMIT]);
		int zRotLimit = std::stoi(kTokenVector[GUIILD_BUILDING_TOKEN_Z_ROT_LIMIT]);

		const DWORD& dwPrice = std::stol(kTokenVector[GUIILD_BUILDING_TOKEN_PRICE]);
		std::string stMaterialTokenList = kTokenVector[GUIILD_BUILDING_TOKEN_MATERIAL];
		stMaterialTokenList.erase(std::remove(stMaterialTokenList.begin(), stMaterialTokenList.end(), '"'), stMaterialTokenList.end());
		std::string_view stvMaterialTokenList = stMaterialTokenList;

		const BOOL& bEnable = std::stoi(kTokenVector[GUIILD_BUILDING_TOKEN_ENABLE_FLAG]);

		constexpr BOOL ENABLE_FLAG_TYPE_NOT_USE = FALSE;
		constexpr BOOL ENABLE_FLAG_TYPE_USE = TRUE;
		constexpr BOOL ENABLE_FLAG_TYPE_USE_BUT_HIDE = 2;

		if (ENABLE_FLAG_TYPE_NOT_USE == bEnable)
			continue;

		// GuildSymbol �� �Ϲ� NPC ��� �Բ� ����Ѵ�.
		GuildBuildingMap::const_iterator it = g_mGuildBuilding.find(stvType);
		if (it == g_mGuildBuilding.end())
			continue;

		CRaceManager::Instance().RegisterRaceSrcName(stName.c_str(), it->second.c_str());
		CRaceManager::Instance().RegisterRaceName(dwVNum, stName.c_str());

		// Material List
		{
			PyObject* ppyMaterialList = PyList_New(g_bGuildMaterialMaxNum);
			for (BYTE bIndex = 0; bIndex < g_bGuildMaterialMaxNum; ++bIndex)
				PyList_SetItem(ppyMaterialList, bIndex, Py_BuildValue("s", "0"));

			std::vector<std::string_view> vMaterialToken;
			SplitString(stvMaterialTokenList, '/', [&](const std::string_view& c_rString, std::size_t nFrom, std::size_t nTo)
				{
					// Emplace back material token delimited by '/'.
					vMaterialToken.emplace_back(c_rString.substr(nFrom, nTo - nFrom));
				});

			for (const std::string_view& stToken : vMaterialToken)
			{
				std::vector<std::string_view> vMaterialItem;
				SplitString(stToken, ',', [&](const std::string_view& c_rString, std::size_t nFrom, std::size_t nTo)
					{
						vMaterialItem.emplace_back(c_rString.substr(nFrom, nTo - nFrom));
					});

				if (vMaterialItem.size() != 2)
					continue;

				const DWORD dwMaterialVNum = std::atol(vMaterialItem[0].data());
				const short sMaterialIndex = GetGuildMaterialIndex(dwMaterialVNum);
				if (sMaterialIndex < 0)
					continue;

				const DWORD c_dwMaterialCount = std::atol(vMaterialItem[1].data());
				PyList_SetItem(ppyMaterialList, sMaterialIndex, Py_BuildValue("s", std::to_string(c_dwMaterialCount).c_str()));
			}

			PyObject* ppyDict = PyDict_New();
			PyDict_SetItem(ppyDict, Py_BuildValue("s", "VNUM"), Py_BuildValue("i", dwVNum));
			PyDict_SetItem(ppyDict, Py_BuildValue("s", "TYPE"), Py_BuildValue("s", stvType.data()));
			PyDict_SetItem(ppyDict, Py_BuildValue("s", "NAME"), Py_BuildValue("s", stName.c_str()));
			PyDict_SetItem(ppyDict, Py_BuildValue("s", "LOCAL_NAME"), Py_BuildValue("s", stLocaleName.c_str()));
			PyDict_SetItem(ppyDict, Py_BuildValue("s", "X_ROT_LIMIT"), Py_BuildValue("i", xRotLimit));
			PyDict_SetItem(ppyDict, Py_BuildValue("s", "Y_ROT_LIMIT"), Py_BuildValue("i", yRotLimit));
			PyDict_SetItem(ppyDict, Py_BuildValue("s", "Z_ROT_LIMIT"), Py_BuildValue("i", zRotLimit));
			PyDict_SetItem(ppyDict, Py_BuildValue("s", "PRICE"), Py_BuildValue("i", dwPrice));
			PyDict_SetItem(ppyDict, Py_BuildValue("s", "MATERIAL"), Py_BuildValue("O", ppyMaterialList));
			PyDict_SetItem(ppyDict, Py_BuildValue("s", "SHOW"), Py_BuildValue("i", ENABLE_FLAG_TYPE_USE_BUT_HIDE == bEnable ? FALSE : TRUE));
			PyList_Append(ppyAppendingList, ppyDict);
		}
	}

	CPythonGuild::Instance().SetGuildBuildingList(ppyAppendingList);
	Py_DECREF(ppyAppendingList);
	return true;
}

void CPythonLoading::RegisterSkills(const BYTE c_bJob, const BYTE c_bSkillGroupIndex, const BYTE c_bEmpire, const bool c_bEnable678thSkill)
{
	if (c_bJob >= NRaceData::JOB_MAX_NUM)
		return;

	// Player Skills
	const SkillIndexVectorMap& c_rSkillMap = c_bEnable678thSkill ? g_vMapPlayerSkillIndex : g_vMapPlayerSkillIndexLegacy;
	if (c_rSkillMap.find(c_bJob) != c_rSkillMap.end() &&
		c_rSkillMap.at(c_bJob).find(c_bSkillGroupIndex) != c_rSkillMap.at(c_bJob).end())
	{
		const SkillIndexVector& c_vPlayerSkillIndex = c_rSkillMap.at(c_bJob).at(c_bSkillGroupIndex);
		for (std::size_t nIndex = 0; nIndex < c_vPlayerSkillIndex.size(); nIndex++)
		{
			// 7�� 8�� ��ų�� ���⼭ �����ϸ� �ȵ�
			if (nIndex != 6 && nIndex != 7)
				CPythonPlayer::Instance().SetSkill(nIndex + 1, c_vPlayerSkillIndex[nIndex]);
		}
	}

	// Support Skills
	const SkillIndexVector& c_vSupportSkillIndex = c_bEnable678thSkill ? g_vSupportSkillIndex : g_vSupportSkillIndexLegacy;
	for (std::size_t nIndex = 0; nIndex < c_vSupportSkillIndex.size(); nIndex++)
		CPythonPlayer::Instance().SetSkill(nIndex + 100 + 1, c_vSupportSkillIndex[nIndex]);

	if (c_bEmpire != 0)
	{
		BYTE bLanguageSkillIndex = 0;
		for (BYTE bIndex = 0; bIndex < 3; ++bIndex)
		{
			if (bIndex + 1 == c_bEmpire)
				continue;
			CPythonPlayer::Instance().SetSkill(107 + bLanguageSkillIndex, c_iSkillIndex_Language1 + bIndex);
			++bLanguageSkillIndex;
		}
	}

	// Guild Skills
	{
		// Passive
		const SkillIndexVector& c_vPassiveGuildSkillIndex = g_vMapGuildSkillIndex.at("PASSIVE");
		for (std::size_t nIndex = 0; nIndex < c_vPassiveGuildSkillIndex.size(); nIndex++)
			CPythonPlayer::Instance().SetSkill(200 + nIndex, c_vPassiveGuildSkillIndex[nIndex]);

		// Active
		const SkillIndexVector& c_vActiveGuildSkillIndex = g_vMapGuildSkillIndex.at("ACTIVE");
		for (std::size_t nIndex = 0; nIndex < c_vActiveGuildSkillIndex.size(); nIndex++)
			CPythonPlayer::Instance().SetSkill(210 + nIndex, c_vActiveGuildSkillIndex[nIndex]);
	}
}

PyObject* loadingRegisterSkill(PyObject* poSelf, PyObject* poArgs)
{
	BYTE bRace;
	if (!PyTuple_GetByte(poArgs, 0, &bRace))
		return Py_BuildException();

	BYTE bSkillGroup;
	if (!PyTuple_GetByte(poArgs, 1, &bSkillGroup))
		return Py_BuildException();

	BYTE bEmpire;
	if (!PyTuple_GetByte(poArgs, 2, &bEmpire))
		return Py_BuildException();

	bool bEnable678thSkill = true;
	if (PyTuple_Size(poArgs) > 3)
	{
		const int iSkillFlag = PyObject_IsTrue(PyTuple_GetItem(poArgs, 3));
		if (iSkillFlag < 0)
			return Py_BuildException();
		bEnable678thSkill = iSkillFlag != 0;
	}
	CPythonLoading::Instance().RegisterSkills(static_cast<BYTE>(RaceToJob(bRace)), bSkillGroup, bEmpire, bEnable678thSkill);
	return Py_BuildNone();
}

PyObject* loadingLoadGuildBuildingList(PyObject* poSelf, PyObject* poArgs)
{
	char* szFileName;
	if (!PyTuple_GetString(poArgs, 0, &szFileName))
		return Py_BuildException();

	return Py_BuildValue("i", CPythonLoading::Instance().LoadGuildBuilding(szFileName));
}

PyObject* loadingIsWorldReady(PyObject* poSelf, PyObject* poArgs)
{
	const bool isReady = CPythonNetworkStream::Instance().IsGamePhase() &&
		CPythonBackground::Instance().IsMapReady() &&
		CPythonCharacterManager::Instance().GetMainInstancePtr() != nullptr;
	return Py_BuildValue("i", isReady);
}

void initLoading()
{
	static PyMethodDef s_methods[] =
	{
		{ "RegisterSkill", loadingRegisterSkill, METH_VARARGS, },
		{ "LoadGuildBuildingList", loadingLoadGuildBuildingList, METH_VARARGS, },
		{ "IsWorldReady", loadingIsWorldReady, METH_NOARGS, },
		{ nullptr, nullptr, 0 },
	};

	PyObject* poModule = Py_InitModule("loading", s_methods);

	PyModule_AddIntConstant(poModule, "JOB_WARRIOR", NRaceData::JOB_WARRIOR);
	PyModule_AddIntConstant(poModule, "JOB_ASSASSIN", NRaceData::JOB_ASSASSIN);
	PyModule_AddIntConstant(poModule, "JOB_SURA", NRaceData::JOB_SURA);
	PyModule_AddIntConstant(poModule, "JOB_SHAMAN", NRaceData::JOB_SHAMAN);
#if defined(ENABLE_WOLFMAN_CHARACTER)
	PyModule_AddIntConstant(poModule, "JOB_WOLFMAN", NRaceData::JOB_WOLFMAN);
#endif

	PyModule_AddIntConstant(poModule, "RACE_WARRIOR_M", CRaceData::RACE_WARRIOR_M);
	PyModule_AddIntConstant(poModule, "RACE_ASSASSIN_W", CRaceData::RACE_ASSASSIN_W);
	PyModule_AddIntConstant(poModule, "RACE_SURA_M", CRaceData::RACE_SURA_M);
	PyModule_AddIntConstant(poModule, "RACE_SHAMAN_W", CRaceData::RACE_SHAMAN_W);
	PyModule_AddIntConstant(poModule, "RACE_WARRIOR_W", CRaceData::RACE_WARRIOR_W);
	PyModule_AddIntConstant(poModule, "RACE_ASSASSIN_M", CRaceData::RACE_ASSASSIN_M);
	PyModule_AddIntConstant(poModule, "RACE_SURA_W", CRaceData::RACE_SURA_W);
	PyModule_AddIntConstant(poModule, "RACE_SHAMAN_M", CRaceData::RACE_SHAMAN_M);
#if defined(ENABLE_WOLFMAN_CHARACTER)
	PyModule_AddIntConstant(poModule, "RACE_WOLFMAN_M", CRaceData::RACE_WOLFMAN_M);
#endif
	PyModule_AddIntConstant(poModule, "RACE_MAX_NUM", CRaceData::RACE_MAX_NUM);

	//PyModule_AddIntConstant(poModule, "COMBO_TYPE_1", CPythonLoading::COMBO_TYPE_1);
	//PyModule_AddIntConstant(poModule, "COMBO_TYPE_2", CPythonLoading::COMBO_TYPE_2);
	//PyModule_AddIntConstant(poModule, "COMBO_TYPE_3", CPythonLoading::COMBO_TYPE_3);
	//
	//PyModule_AddIntConstant(poModule, "COMBO_INDEX_1", CPythonLoading::COMBO_INDEX_1);
	//PyModule_AddIntConstant(poModule, "COMBO_INDEX_2", CPythonLoading::COMBO_INDEX_2);
	//PyModule_AddIntConstant(poModule, "COMBO_INDEX_3", CPythonLoading::COMBO_INDEX_3);
	//PyModule_AddIntConstant(poModule, "COMBO_INDEX_4", CPythonLoading::COMBO_INDEX_4);
	//PyModule_AddIntConstant(poModule, "COMBO_INDEX_5", CPythonLoading::COMBO_INDEX_5);
	//PyModule_AddIntConstant(poModule, "COMBO_INDEX_6", CPythonLoading::COMBO_INDEX_6);
	//
	PyModule_AddIntConstant(poModule, "HORSE_SKILL_WILDATTACK", EHorseSkill::HORSE_SKILL_WILDATTACK);
	PyModule_AddIntConstant(poModule, "HORSE_SKILL_CHARGE", EHorseSkill::HORSE_SKILL_CHARGE);
	PyModule_AddIntConstant(poModule, "HORSE_SKILL_SPLASH", EHorseSkill::HORSE_SKILL_SPLASH);
	//
	//PyModule_AddIntConstant(poModule, "GUILD_SKILL_DRAGONBLOOD", CPythonLoading::GUILD_SKILL_DRAGONBLOOD);
	//PyModule_AddIntConstant(poModule, "GUILD_SKILL_DRAGONBLESS", CPythonLoading::GUILD_SKILL_DRAGONBLESS);
	//PyModule_AddIntConstant(poModule, "GUILD_SKILL_BLESSARMOR", CPythonLoading::GUILD_SKILL_BLESSARMOR);
	//PyModule_AddIntConstant(poModule, "GUILD_SKILL_SPPEDUP", CPythonLoading::GUILD_SKILL_SPPEDUP);
	//PyModule_AddIntConstant(poModule, "GUILD_SKILL_DRAGONWRATH", CPythonLoading::GUILD_SKILL_DRAGONWRATH);
	//PyModule_AddIntConstant(poModule, "GUILD_SKILL_MAGICUP", CPythonLoading::GUILD_SKILL_MAGICUP);
	//
	//
	PyModule_AddIntConstant(poModule, "LOADING_TYPE_INTRO", 0);
	PyModule_AddIntConstant(poModule, "LOADING_TYPE_GAME", 1);
	PyModule_AddIntConstant(poModule, "LOADING_TYPE_WARP", 2);

	PyModule_AddIntConstant(poModule, "INTRO_LOADING", 0);
}
#endif
