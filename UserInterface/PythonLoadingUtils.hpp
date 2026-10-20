/*
* Title: Client Loading Optimization
* Description: Increases the performance of loading client data.
* Author: Owsap
* Date: 2022.10.10
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

#if defined(ENABLE_LOADING_PERFORMANCE) && !defined(__INC_PYTHON_LOADING_UTILS__)
#define __INC_PYTHON_LOADING_UTILS__

// Sound
using SoundData = struct SSoundData
{
	DWORD dwItemType;
	std::string stFileName;
};
using SoundDataVector = std::vector<SSoundData>;

// Effect
using EffectData = struct SEffectData
{
	UINT iEftType;
	std::string stEftAttachBone;
	std::string stEftFileName;
	bool bCache = false;
};
using EffectDataVector = std::vector<SEffectData>;

// Fly Effect
using FlyEffectData = struct SFlyEffectData
{
	DWORD dwIndex;
	BYTE byType;
	std::string stFileName;
};
using FlyEffectDataVector = std::vector<SFlyEffectData>;

// Emotion
using EmoticonEffectData = struct SEmoticonEffectData : public EffectData
{
	std::string stCommand;
};
using EmotionEffectDataVector = std::vector<EmoticonEffectData>;

// Race
using RaceData = struct SRaceData
{
	DWORD dwRaceIndex;
	std::string stFileName;
	std::string stPathName;
};
using RaceDataVector = std::vector<SRaceData>;

// Motions
using MotionData = struct SMotionData
{
	WORD wMotionIndex;
	std::string stFileName;
	BYTE bPercentage;
};
using MotionDataVector = std::vector<SMotionData>;
using SkillGradeMotionData = std::pair<BYTE, std::string>;

// Skills
using SkillIndexVector = std::vector<DWORD>;
using SkillIndexVectorMap = std::map<BYTE, std::map<BYTE, SkillIndexVector>>;
using SupportSkillIndexVector = SkillIndexVector;
using GuildSkillIndexVectorMap = std::map<std::string, SkillIndexVector>;

// Dungeon Map Name
using DungeonMapNameVector = std::vector<std::string>;

// Title Name
#if defined(ENABLE_GENDER_ALIGNMENT)
using PairedTitleName = std::pair<std::string, std::string>;
using PairedTitleNameVector = std::vector<PairedTitleName>;
#endif
using TitleNameVector = std::vector<std::string>;

// Name & Title Color
using NameColorMap = std::map<BYTE, D3DCOLORVALUE>;
using TitleColorMap = NameColorMap;

// Emotion Icon
using EmotionIconVector = std::map<WORD, std::string>;

// Guild Building
using GuildBuildingMap = std::map<std::string_view, std::string>;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Python Object
std::string GetLocaleInfoString(const std::string& c_rstToken);

///////////////////////////////////////////////////////////////////////////////////////////////////
// String Functions
template<typename StringFunction>
void SplitString(const std::string_view& c_rString, char szDelimiter, StringFunction stFunc)
{
	std::size_t nFrom = 0;
	for (std::size_t nSize = 0; nSize < c_rString.size(); ++nSize)
	{
		if (c_rString[nSize] == szDelimiter)
		{
			stFunc(c_rString, nFrom, nSize);
			nFrom = nSize + 1;
		}
	}
	if (nFrom <= c_rString.size())
		stFunc(c_rString, nFrom, c_rString.size());
}
#endif // __INC_PYTHON_LOADING_UTILS__
