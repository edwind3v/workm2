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

#if defined(ENABLE_LOADING_PERFORMANCE)

extern std::map<int, CGraphicImage*> m_kMap_iEmotionIndex_pkIconImage;

enum EEmotionIcon : WORD
{
	EMOTION_CLAP = 1,

	EMOTION_CONGRATULATION,
	EMOTION_FORGIVE,
	EMOTION_ANGRY,

	EMOTION_ATTRACTIVE,
	EMOTION_SAD,
	EMOTION_SHY,
	EMOTION_CHEERUP,
	EMOTION_BANTER,
	EMOTION_JOY,

	EMOTION_CHEERS_1,
	EMOTION_CHEERS_2,

	EMOTION_DANCE_1,
	EMOTION_DANCE_2,
	EMOTION_DANCE_3,
	EMOTION_DANCE_4,
	EMOTION_DANCE_5,
	EMOTION_DANCE_6, // °­³²½ºÅ¸ÀÏ

	EMOTION_KISS = 51,
	EMOTION_FRENCH_KISS,
	EMOTION_SLAP,
#if defined(ENABLE_EXPRESSING_EMOTION)
	EMOTION_PUSH_UP,
	EMOTION_DANCE_7,
	EMOTION_EXERCISE,
	EMOTION_DOZE,
	EMOTION_SELFIE,
	EMOTION_CHARGING,
	EMOTION_NOSAY,
	EMOTION_WEATHER_1,
	EMOTION_WEATHER_2,
	EMOTION_WEATHER_3,
	EMOTION_HUNGRY,
	EMOTION_SIREN,
	EMOTION_LETTER,
	EMOTION_CALL,
	EMOTION_CELEBRATION,
	EMOTION_ALCOHOL,
	EMOTION_BUSY,
	EMOTION_WHIRL,
#endif
};

enum EComboType : BYTE
{
	COMBO_TYPE_1,
	COMBO_TYPE_2,
	COMBO_TYPE_3,
	COMBO_TYPE_MAX,
};

enum EComboIndex : BYTE
{
	COMBO_INDEX_1,
	COMBO_INDEX_2,
	COMBO_INDEX_3,
	COMBO_INDEX_4,
	COMBO_INDEX_5,
	COMBO_INDEX_6,
};

enum EDustGap : WORD
{
	DUST_GAP = 250,
	HORSE_DUST_GAP = 500,
};

enum EHorseSkill : DWORD
{
	HORSE_SKILL_WILDATTACK = CRaceMotionData::NAME_SKILL + 121,
	HORSE_SKILL_CHARGE = CRaceMotionData::NAME_SKILL + 122,
	HORSE_SKILL_SPLASH = CRaceMotionData::NAME_SKILL + 123,
};

enum EGuildSkill : DWORD
{
	GUILD_SKILL_DRAGONBLOOD = CRaceMotionData::NAME_SKILL + 101,
	GUILD_SKILL_DRAGONBLESS = CRaceMotionData::NAME_SKILL + 102,
	GUILD_SKILL_BLESSARMOR = CRaceMotionData::NAME_SKILL + 103,
	GUILD_SKILL_SPPEDUP = CRaceMotionData::NAME_SKILL + 104,
	GUILD_SKILL_DRAGONWRATH = CRaceMotionData::NAME_SKILL + 105,
	GUILD_SKILL_MAGICUP = CRaceMotionData::NAME_SKILL + 106,
};

enum ELanguageSkill : DWORD
{
	EMPIRE_LANGUAGE_1 = 126,
	EMPIRE_LANGUAGE_2 = 127,
	EMPIRE_LANGUAGE_3 = 128,
};

enum ENPCListToken : BYTE
{
	NPC_TOKEN_TYPE_VNUM,
	NPC_TOKEN_TYPE_MODEL_NAME,
	NPC_LIST_TOKEN_MODEL_SRC_NAME,
	NPC_LIST_TOKEN_MAX,
};

enum EGuildBuildingToken : BYTE
{
	GUIILD_BUILDING_TOKEN_VNUM,
	GUIILD_BUILDING_TOKEN_TYPE,
	GUIILD_BUILDING_TOKEN_NAME,
	GUIILD_BUILDING_TOKEN_LOCAL_NAME,
	GUIILD_BUILDING_NO_USE_TOKEN_SIZE_1,
	GUIILD_BUILDING_NO_USE_TOKEN_SIZE_2,
	GUIILD_BUILDING_NO_USE_TOKEN_SIZE_3,
	GUIILD_BUILDING_NO_USE_TOKEN_SIZE_4,
	GUIILD_BUILDING_TOKEN_X_ROT_LIMIT,
	GUIILD_BUILDING_TOKEN_Y_ROT_LIMIT,
	GUIILD_BUILDING_TOKEN_Z_ROT_LIMIT,
	GUIILD_BUILDING_TOKEN_PRICE,
	GUIILD_BUILDING_TOKEN_MATERIAL,
	GUIILD_BUILDING_TOKEN_NPC,
	GUIILD_BUILDING_TOKEN_GROUP,
	GUIILD_BUILDING_TOKEN_DEPEND_GROUP,
	GUIILD_BUILDING_TOKEN_ENABLE_FLAG,
#if defined(ENABLE_CONQUEROR_LEVEL)
	GUIILD_BUILDING_TOKEN_NEW_WORLD,
#endif
	GUIILD_BUILDING_LIMIT_TOKEN_COUNT,
};

enum EGuildBuildingMaterialItem : DWORD
{
	MATERIAL_STONE_ID = 90010,
	MATERIAL_LOG_ID = 90011,
	MATERIAL_PLYWOOD_ID = 90012
};

enum EGuildBuildingMaterialIndex : BYTE
{
	MATERIAL_STONE_INDEX,
	MATERIAL_LOG_INDEX,
	MATERIAL_PLYWOOD_INDEX
};

static constexpr int g_iSafeSleep = 1500; //1500
static constexpr bool g_bCacheMotionData = true;

static const std::string g_stNPCList_FileName = "npclist.txt";
static const std::string g_stRaceHeight_FileName = "race_height.txt";

// Sound Data
static const SoundDataVector g_vUseSoundData
{
	{ CPythonItem::USESOUND_DEFAULT, "sound/ui/drop.wav" },
	{ CPythonItem::USESOUND_ACCESSORY, "sound/ui/equip_ring_amulet.wav" },
	{ CPythonItem::USESOUND_ARMOR, "sound/ui/equip_metal_armor.wav" },
	{ CPythonItem::USESOUND_BOW, "sound/ui/equip_bow.wav" },
	{ CPythonItem::USESOUND_WEAPON, "sound/ui/equip_metal_weapon.wav" },
	{ CPythonItem::USESOUND_POTION, "sound/ui/eat_potion.wav" },
	{ CPythonItem::USESOUND_PORTAL, "sound/ui/potal_scroll.wav" },
} /*11*/;
static const SoundDataVector g_vDropSoundData
{
	{ CPythonItem::DROPSOUND_DEFAULT, "sound/ui/drop.wav" },
	{ CPythonItem::DROPSOUND_ACCESSORY, "sound/ui/equip_ring_amulet.wav" },
	{ CPythonItem::DROPSOUND_ARMOR, "sound/ui/equip_metal_armor.wav" },
	{ CPythonItem::DROPSOUND_BOW, "sound/ui/equip_bow.wav" },
	{ CPythonItem::DROPSOUND_WEAPON, "sound/ui/equip_metal_weapon.wav" },
};

// Effect
static const EffectDataVector g_vEffectData
{
	{ CInstanceBase::EFFECT_DUST, "", "d:/ymir work/effect/etc/dust/dust.mse", false },
	{ CInstanceBase::EFFECT_HORSE_DUST, "", "d:/ymir work/effect/etc/dust/running_dust.mse", false },
	{ CInstanceBase::EFFECT_HIT, "", "d:/ymir work/effect/hit/blow_1/blow_1_low.mse", false },

	{ CInstanceBase::EFFECT_HPUP_RED, "", "d:/ymir work/effect/etc/recuperation/drugup_red.mse", false },
	{ CInstanceBase::EFFECT_SPUP_BLUE, "", "d:/ymir work/effect/etc/recuperation/drugup_blue.mse", false },
	{ CInstanceBase::EFFECT_SPEEDUP_GREEN, "", "d:/ymir work/effect/etc/recuperation/drugup_green.mse", false },
	{ CInstanceBase::EFFECT_DXUP_PURPLE, "", "d:/ymir work/effect/etc/recuperation/drugup_purple.mse", false },

	// ÀÚµ¿¹°¾à HP, SP
	{ CInstanceBase::EFFECT_AUTO_HPUP, "", "d:/ymir work/effect/etc/recuperation/autodrugup_red.mse", false },
	{ CInstanceBase::EFFECT_AUTO_SPUP, "", "d:/ymir work/effect/etc/recuperation/autodrugup_blue.mse", false },

	// ¶ó¸¶´Ü ÃÊ½Â´ÞÀÇ ¹ÝÁö(71135) Âø¿ë¼ø°£ ¹ßµ¿ ÀÌÆåÆ®
	{ CInstanceBase::EFFECT_RAMADAN_RING_EQUIP, "", "d:/ymir work/effect/etc/buff/buff_item1.mse", false },

	// ÇÒ·ÎÀ© »çÅÁ Âø¿ë¼ø°£ ¹ßµ¿ ÀÌÆåÆ®
	{ CInstanceBase::EFFECT_HALLOWEEN_CANDY_EQUIP, "", "d:/ymir work/effect/etc/buff/buff_item2.mse", false },

	// Çàº¹ÀÇ ¹ÝÁö Âø¿ë¼ø°£ ¹ßµ¿ ÀÌÆåÆ®
	{ CInstanceBase::EFFECT_HAPPINESS_RING_EQUIP, "", "d:/ymir work/effect/etc/buff/buff_item3.mse", false },

	// »ç¶ûÀÇ ÆÒ´øÆ® Âø¿ë¼ø°£ ¹ßµ¿ ÀÌÆåÆ®
	{ CInstanceBase::EFFECT_LOVE_PENDANT_EQUIP, "", "d:/ymir work/effect/etc/buff/buff_item4.mse", false },

	//{ CInstanceBase::EFFECT_MAGIC_RING_EQUIP, "", "d:/ymir work/effect/etc/buff/buff_item5.mse", false },

#if defined(ENABLE_ACCE_COSTUME_SYSTEM)
	{ CInstanceBase::EFFECT_ACCE_SUCESS_ABSORB, "", "d:/ymir work/effect/etc/buff/buff_item6.mse", false },
	{ CInstanceBase::EFFECT_ACCE_EQUIP, "", "d:/ymir work/effect/etc/buff/buff_item7.mse", false },
#endif

	// ºÎÈ°Àý Äµµð(71188) Âø¿ë¼ø°£ ¹ßµ¿ ÀÌÆåÆ®
	//{ CInstanceBase::EFFECT_EASTER_CANDY_EQIP, "", "d:/ymir work/effect/etc/buff/buff_item8.mse", false },

	{ CInstanceBase::EFFECT_PENETRATE, "Bip01", "d:/ymir work/effect/hit/gwantong.mse", false },
	//{ CInstanceBase::EFFECT_BLOCK, "", "d:/ymir work/effect/etc/", false },
	//{ CInstanceBase::EFFECT_DODGE, "", "d:/ymir work/effect/etc/", false },
	{ CInstanceBase::EFFECT_FIRECRACKER, "", "d:/ymir work/effect/etc/firecracker/newyear_firecracker.mse", false },
	{ CInstanceBase::EFFECT_SPIN_TOP, "", "d:/ymir work/effect/etc/firecracker/paing_i.mse", false },
	{ CInstanceBase::EFFECT_SELECT, "", "d:/ymir work/effect/etc/click/click_select.mse", false },
	{ CInstanceBase::EFFECT_TARGET, "", "d:/ymir work/effect/etc/click/click_glow_select.mse", false },
	{ CInstanceBase::EFFECT_STUN, "Bip01 Head", "d:/ymir work/effect/etc/stun/stun.mse", false },
	{ CInstanceBase::EFFECT_CRITICAL, "Bip01 R Hand", "d:/ymir work/effect/hit/critical.mse", false },

	{ CInstanceBase::EFFECT_DAMAGE_TARGET, "", "d:/ymir work/effect/affect/damagevalue/target.mse", false },
	{ CInstanceBase::EFFECT_DAMAGE_NOT_TARGET, "", "d:/ymir work/effect/affect/damagevalue/nontarget.mse", false },
	{ CInstanceBase::EFFECT_DAMAGE_SELFDAMAGE, "", "d:/ymir work/effect/affect/damagevalue/damage.mse", false },
	{ CInstanceBase::EFFECT_DAMAGE_SELFDAMAGE2, "", "d:/ymir work/effect/affect/damagevalue/damage_1.mse", false },
	{ CInstanceBase::EFFECT_DAMAGE_POISON, "", "d:/ymir work/effect/affect/damagevalue/poison.mse", false },
	{ CInstanceBase::EFFECT_DAMAGE_MISS, "", "d:/ymir work/effect/affect/damagevalue/miss.mse", false },
	{ CInstanceBase::EFFECT_DAMAGE_TARGETMISS, "", "d:/ymir work/effect/affect/damagevalue/target_miss.mse", false },
	{ CInstanceBase::EFFECT_DAMAGE_CRITICAL, "", "d:/ymir work/effect/affect/damagevalue/critical.mse", false },

	//{ CInstanceBase::EFFECT_SUCCESS, "", "d:/ymir work/effect/success.mse", false },
	//{ CInstanceBase::EFFECT_FAIL, "", "d:/ymir work/effect/fail.mse", false },

	{ CInstanceBase::EFFECT_LEVELUP_ON_14_FOR_GERMANY, "","season1/effect/paymessage_warning.mse", false }, // ¹º§¾÷ 14ÀÏ¶§ ( µ¶ÀÏÀü¿ë )
	{ CInstanceBase::EFFECT_LEVELUP_UNDER_15_FOR_GERMANY, "", "season1/effect/paymessage_decide.mse", false }, //·¹º§¾÷ 15ÀÏ¶§ ( µ¶ÀÏÀü¿ë )

	{ CInstanceBase::EFFECT_PERCENT_DAMAGE1, "", "d:/ymir work/effect/hit/percent_damage1.mse", false },
	{ CInstanceBase::EFFECT_PERCENT_DAMAGE2, "", "d:/ymir work/effect/hit/percent_damage2.mse", false },
	{ CInstanceBase::EFFECT_PERCENT_DAMAGE3, "", "d:/ymir work/effect/hit/percent_damage3.mse", false },

	//{ CInstanceBase::EFFECT_THUNDER_AREA, "", "D:/ymir work/effect/monster/light_emissive3.mse", false },
	//{ CInstanceBase::EFFECT_THUNDER, "", "D:/ymir work/effect/monster/yellow_tigerman_24_1.mse", false },
	//{ CInstanceBase::EFFECT_HEAL, "", "D:/ymir work/pc/shaman/effect/jeongeop_2.mse", false },

	// ENABLE_2016_VALENTINE
	//{ CInstanceBase::EFFECT_CHOCOLATE_PENDANT, "", "D:/ymir work/effect/etc/buff/buff_item10.mse", false },

	// ENABLE_PEPSI_EVENT
	//{ CInstanceBase::EFFECT_PEPSI_EVENT, "", "D:/ymir work/effect/etc/buff/buff_item11.mse", false },

#if defined(ENABLE_GUILD_DRAGONLAIR_SYSTEM)
	{ CInstanceBase::EFFECT_DRAGONLAIR_STONE_UNBEATABLE_1, "", "d:/ymir work/effect/monster2/redd_moojuk.mse", false },
	{ CInstanceBase::EFFECT_DRAGONLAIR_STONE_UNBEATABLE_2, "", "d:/ymir work/effect/monster2/redd_moojuk.mse", false },
	{ CInstanceBase::EFFECT_DRAGONLAIR_STONE_UNBEATABLE_3, "", "d:/ymir work/effect/monster2/redd_moojuk_blue.mse", false },
	{ CInstanceBase::EFFECT_DRAGONLAIR_STONE_UNBEATABLE_4, "", "d:/ymir work/effect/monster2/redd_moojuk_green.mse", false },
#endif

#if defined(ENABLE_BALANCE_IMPROVING)
	{ CInstanceBase::EFFECT_FEATHER_WALK, "", "d:/ymir work/effect/hit/gyeonggong_boom.mse", false },
#endif

	// ENABLE_BATTLE_FIELD
	//{ CInstanceBase::EFFECT_BATTLE_POTION, "", "D:/ymir work/effect/etc/buff/buff_item12.mse", false },

#if defined(ENABLE_AI_FLAG_REFLECT)
	{ CInstanceBase::EFFECT_REFLECT, "", "D:/ymir work/effect/hit/blow_4/blow_4_ref.mse", false },
#endif

	// ENABLE_FLOWER_EVENT
	//{ CInstanceBase::EFFECT_FLOWER_EVENT, "", "d:/ymir work/effect/etc/buff/buff_item15_flower.mse", false },

	//// ENABLE_GEM_SYSTEM
	//{ CInstanceBase::EFFECT_GEM_PENDANT, "", "d:/ymir work/effect/etc/buff/buff_item16.mse", false },

	//{ CInstanceBase::EFFECT_NAZAR_PENDANT, "", "D:/ymir work/effect/etc/buff/buff_item13.mse", false },
	//{ CInstanceBase::EFFECT_GUARDIAN_PENDANT, "", "D:/ymir work/effect/etc/buff/buff_item14.mse", false },

#if defined(ENABLE_SHIP_DEFENSE)
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_DEFENSE_WAVE_LASER, "", "d:/ymir work/effect/monster2/defense_mini_laser.mse", false },
#endif
	////////////////////////////////////////////////////////////////////////////////////////////////////

	{ CInstanceBase::EFFECT_SPAWN_APPEAR, "Bip01", "d:/ymir work/effect/etc/appear_die/monster_appear.mse", false },
	{ CInstanceBase::EFFECT_SPAWN_DISAPPEAR, "Bip01", "d:/ymir work/effect/etc/appear_die/monster_die.mse", false },
	{ CInstanceBase::EFFECT_FLAME_ATTACK, "equip_right_hand", "d:/ymir work/effect/hit/blow_flame/flame_3_weapon.mse", false },
	{ CInstanceBase::EFFECT_FLAME_HIT, "", "d:/ymir work/effect/hit/blow_flame/flame_3_blow.mse", false },
	{ CInstanceBase::EFFECT_FLAME_ATTACH, "", "d:/ymir work/effect/hit/blow_flame/flame_3_body.mse", false },
	{ CInstanceBase::EFFECT_ELECTRIC_ATTACK, "equip_right", "d:/ymir work/effect/hit/blow_electric/light_1_weapon.mse", false },
	{ CInstanceBase::EFFECT_ELECTRIC_HIT, "", "d:/ymir work/effect/hit/blow_electric/light_1_blow.mse", false },
	{ CInstanceBase::EFFECT_ELECTRIC_ATTACH, "", "d:/ymir work/effect/hit/blow_electric/light_1_body.mse", false },

	{ CInstanceBase::EFFECT_LEVELUP, "", "d:/ymir work/effect/etc/levelup_1/level_up.mse", false },
	{ CInstanceBase::EFFECT_SKILLUP, "", "d:/ymir work/effect/etc/skillup/skillup_1.mse", false },

	{ CInstanceBase::EFFECT_EMPIRE + 1, "Bip01", "d:/ymir work/effect/etc/empire/empire_A.mse", false },
	{ CInstanceBase::EFFECT_EMPIRE + 2, "Bip01", "d:/ymir work/effect/etc/empire/empire_B.mse", false },
	{ CInstanceBase::EFFECT_EMPIRE + 3, "Bip01", "d:/ymir work/effect/etc/empire/empire_C.mse", false },

	{ CInstanceBase::EFFECT_WEAPON + 1, "equip_right_hand", "d:/ymir work/pc/warrior/effect/geom_sword_loop.mse", false },
	{ CInstanceBase::EFFECT_WEAPON + 2, "equip_right_hand", "d:/ymir work/pc/warrior/effect/geom_spear_loop.mse", false },

	// LOCALE
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_YMIR, "Bip01", "locale/common/effect/gm.mse", false },
	// END_OF_LOCALE

	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_POISON, "Bip01", "d:/ymir work/effect/hit/blow_poison/poison_loop.mse", false }, // Áßµ¶
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::NEW_AFFECT_POISON, "Bip01", "d:/ymir work/effect/hit/blow_poison/poison_loop.mse", false }, // Áßµ¶

	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_SLOW, "", "d:/ymir work/effect/affect/slow.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::NEW_AFFECT_SLOW, "", "d:/ymir work/effect/affect/slow.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_STUN, "Bip01 Head", "d:/ymir work/effect/etc/stun/stun_loop.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::NEW_AFFECT_STUN, "Bip01 Head", "d:/ymir work/effect/etc/stun/stun_loop.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_DUNGEON_READY, "", "d:/ymir work/effect/etc/ready/ready.mse", false },

	//{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_BUILDING_CONSTRUCTION_SMALL, "", "d:/ymir work/guild/effect/10_construction.mse", false },
	//{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_BUILDING_CONSTRUCTION_LARGE, "", "d:/ymir work/guild/effect/20_construction.mse", false },
	//{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_BUILDING_UPGRADE, "", "d:/ymir work/guild/effect/20_upgrade.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_CHEONGEUN, "", "d:/ymir work/pc/warrior/effect/gyeokgongjang_loop.mse", false }, // Ãµ±ÙÃß (¹Ø¿¡µµ ÀÖµû-_-)
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_GYEONGGONG, "", "d:/ymir work/pc/assassin/effect/gyeonggong_loop.mse", false }, // ÀÚ°´ - °æ°ø
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_EUNHYEONG, "", "d:/ymir work/pc/assassin/effect/eunhyeongbeop.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_GWIGEOM, "Bip01 R Finger2", "d:/ymir work/pc/sura/effect/gwigeom_loop.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_GONGPO, "", "d:/ymir work/pc/sura/effect/fear_loop.mse", false }, // ¼ö¶ó - °øÆ÷
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_JUMAGAP, "", "d:/ymir work/pc/sura/effect/jumagap_loop.mse", false }, // ¼ö¶ó - ÁÖ¸¶°©
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_HOSIN, "", "d:/ymir work/pc/shaman/effect/3hosin_loop.mse", false }, // ¹«´ç - È£½Å
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_BOHO, "", "d:/ymir work/pc/shaman/effect/boho_loop.mse", false }, // ¹«´ç - º¸È£
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_KWAESOK, "", "d:/ymir work/pc/shaman/effect/10kwaesok_loop.mse", false }, // ¹«´ç - Äè¼Ó
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_HEUKSIN, "", "d:/ymir work/pc/sura/effect/heuksin_loop.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_MUYEONG, "", "d:/ymir work/pc/sura/effect/muyeong_loop.mse", false },

	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_FIRE, "Bip01", "d:/ymir work/effect/hit/blow_flame/flame_loop.mse", false },

#if defined(ENABLE_WOLFMAN_CHARACTER)
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_BLEEDING, "Bip01", "d:/ymir work/effect/hit/blow_poison/bleeding_loop.mse", false },
#endif
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_RED_POSSESSION, "Bip01", "d:/ymir work/effect/hit/blow_flame/flame_loop_w.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_GICHEON, "Bip01 R Hand", "d:/ymir work/pc/shaman/effect/6gicheon_hand.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_JEUNGRYEOK, "Bip01 L Hand", "d:/ymir work/pc/shaman/effect/jeungryeok_hand.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_PABEOP, "Bip01 Head", "d:/ymir work/pc/sura/effect/pabeop_loop.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_BLUE_POSSESSION, "", "d:/ymir work/pc3/common/effect/gyeokgongjang_loop_w.mse", false },

	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_FALLEN_CHEONGEUN, "", "d:/ymir work/pc/warrior/effect/gyeokgongjang_loop.mse", false }, // Ãµ±ÙÃß(Fallen)

	// 34 Polymoph
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_WAR_FLAG1, "", "d:/ymir work/effect/etc/guild_war_flag/flag_red.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_WAR_FLAG2, "", "d:/ymir work/effect/etc/guild_war_flag/flag_blue.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_WAR_FLAG3, "", "d:/ymir work/effect/etc/guild_war_flag/flag_yellow.mse", false },

#if defined(ENABLE_BATTLE_FIELD)
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_TARGET_VICTIM, "", "d:/Ymir Work/effect/etc/direction/direction_land_dragonroom.mse", false },
#endif

#ifdef __BUFFI_SUPPORT__
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_BUFFI_HOSIN, "", "d:/ymir work/pc/shaman/effect/3hosin_loop.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_BUFFI_GICHEON, "Bip01 R Hand", "d:/ymir work/pc/shaman/effect/6gicheon_hand.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_BUFFI_KWAESOK, "", "d:/ymir work/pc/shaman/effect/10kwaesok_loop.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_BUFFI_JEUNGRYEO, "Bip01 L Hand", "d:/ymir work/pc/shaman/effect/jeungryeok_hand.mse", false },
#endif

	// SWORD
	{ CInstanceBase::EFFECT_REFINED + 1, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_7.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 2, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_8.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 3, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_9.mse", false },

	// BOW
	{ CInstanceBase::EFFECT_REFINED + 4, "PART_WEAPON_LEFT", "D:/ymir work/pc/common/effect/sword/sword_7_b.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 5, "PART_WEAPON_LEFT", "D:/ymir work/pc/common/effect/sword/sword_8_b.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 6, "PART_WEAPON_LEFT", "D:/ymir work/pc/common/effect/sword/sword_9_b.mse", false },

	// FAN
	{ CInstanceBase::EFFECT_REFINED + 7, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_7_f.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 8, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_8_f.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 9, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_9_f.mse", false },

	// DEGER RIGHT
	{ CInstanceBase::EFFECT_REFINED + 10, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_7_s.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 11, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_8_s.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 12, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_9_s.mse", false },

	// DEGER LEFT
	{ CInstanceBase::EFFECT_REFINED + 13, "PART_WEAPON_LEFT", "D:/ymir work/pc/common/effect/sword/sword_7_s.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 14, "PART_WEAPON_LEFT", "D:/ymir work/pc/common/effect/sword/sword_8_s.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 15, "PART_WEAPON_LEFT", "D:/ymir work/pc/common/effect/sword/sword_9_s.mse", false },


	// BODY
	{ CInstanceBase::EFFECT_REFINED + 16, "Bip01", "D:/ymir work/pc/common/effect/armor/armor_7.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 17, "Bip01", "D:/ymir work/pc/common/effect/armor/armor_8.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 18, "Bip01", "D:/ymir work/pc/common/effect/armor/armor_9.mse", false },
	// BODY SPECIAL
	{ CInstanceBase::EFFECT_REFINED + 19, "Bip01", "D:/ymir work/pc/common/effect/armor/armor-4-2-1.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 20, "Bip01", "D:/ymir work/pc/common/effect/armor/armor-4-2-2.mse", false },
#if defined(ENABLE_LVL115_ARMOR_EFFECT)
	{ CInstanceBase::EFFECT_REFINED + 21, "Bip01", "D:/ymir work/pc/common/effect/armor/armor-5-1.mse", false },
#endif
#if defined(ENABLE_WOLFMAN_CHARACTER)
	{ CInstanceBase::EFFECT_REFINED + 22, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_7_w.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 23, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_8_w.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 24, "PART_WEAPON", "D:/ymir work/pc/common/effect/sword/sword_9_w.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 25, "PART_WEAPON_LEFT", "D:/ymir work/pc/common/effect/sword/sword_7_w.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 26, "PART_WEAPON_LEFT", "D:/ymir work/pc/common/effect/sword/sword_8_w.mse", false },
	{ CInstanceBase::EFFECT_REFINED + 27, "PART_WEAPON_LEFT", "D:/ymir work/pc/common/effect/sword/sword_9_w.mse", false },
#endif

#ifdef ENABLE_OFFLINESHOP_SYSTEM
	{ CInstanceBase::EFFECT_REFINED + CInstanceBase::EFFECT_MYOFFSHOP, "Bip01", "d:/ymir work/effect/etc/myshop/2.mse", false },
#endif
#ifdef METINSTONES_QUEUE
	{ CInstanceBase::EFFECT_REFINED + CInstanceBase::EFFECT_METIN_QUEUE, "", "d:/ymir work/effect/npc2/metin_queue.mse", false },
#endif
#if defined(ENABLE_ACCE_COSTUME_SYSTEM)
	{ CInstanceBase::EFFECT_ACCE_BACK, "Bip01", "D:/ymir work/pc/common/effect/armor/acc_01.mse", false },
#endif

#if defined(ENABLE_SOUL_SYSTEM)
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_SOUL_RED, "", "d:/ymir work/effect/etc/soul/soul_red_001.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_SOUL_BLUE, "", "d:/ymir work/effect/etc/soul/soul_blue_001.mse", false },
	{ CInstanceBase::EFFECT_AFFECT + CInstanceBase::AFFECT_SOUL_MIX, "", "d:/ymir work/effect/etc/soul/soul_mix_001.mse", false },
#endif




};

// Fly Effect
static const FlyEffectDataVector g_vFlyEffectData
{
	{ FLY_EXP, CFlyingManager::INDEX_FLY_TYPE_NORMAL, "d:/ymir work/effect/etc/gathering/ga_piece_yellow_small2.msf" }, // ³ë¶õ»ö (EXP)
	{ FLY_HP_MEDIUM, CFlyingManager::INDEX_FLY_TYPE_NORMAL, "d:/ymir work/effect/etc/gathering/ga_piece_red_small.msf" }, // »¡°£»ö (HP) ÀÛÀº°Å
	{ FLY_HP_BIG, CFlyingManager::INDEX_FLY_TYPE_NORMAL, "d:/ymir work/effect/etc/gathering/ga_piece_red_big.msf" }, // »¡°£»ö (HP) Å«°Å
	{ FLY_SP_SMALL, CFlyingManager::INDEX_FLY_TYPE_NORMAL, "d:/ymir work/effect/etc/gathering/ga_piece_blue_warrior_small.msf" }, // ÆÄ¶õ»ö ²¿¸®¸¸ ÀÖ´Â°Å
	{ FLY_SP_MEDIUM, CFlyingManager::INDEX_FLY_TYPE_NORMAL, "d:/ymir work/effect/etc/gathering/ga_piece_blue_small.msf" }, // ÆÄ¶õ»ö ÀÛÀº°Å
	{ FLY_SP_BIG, CFlyingManager::INDEX_FLY_TYPE_NORMAL, "d:/ymir work/effect/etc/gathering/ga_piece_blue_big.msf" }, // ÆÄ¶õ»ö Å«°Å
	{ FLY_FIREWORK1, CFlyingManager::INDEX_FLY_TYPE_FIRE_CRACKER, "d:/ymir work/effect/etc/firecracker/firecracker_1.msf" }, // ÆøÁ× 1
	{ FLY_FIREWORK2, CFlyingManager::INDEX_FLY_TYPE_FIRE_CRACKER, "d:/ymir work/effect/etc/firecracker/firecracker_2.msf" }, // ÆøÁ× 2
	{ FLY_FIREWORK3, CFlyingManager::INDEX_FLY_TYPE_FIRE_CRACKER, "d:/ymir work/effect/etc/firecracker/firecracker_3.msf" }, // ÆøÁ× 3
	{ FLY_FIREWORK4, CFlyingManager::INDEX_FLY_TYPE_FIRE_CRACKER, "d:/ymir work/effect/etc/firecracker/firecracker_4.msf" }, // ÆøÁ× 4
	{ FLY_FIREWORK5, CFlyingManager::INDEX_FLY_TYPE_FIRE_CRACKER, "d:/ymir work/effect/etc/firecracker/firecracker_5.msf" }, // ÆøÁ× 5
	{ FLY_FIREWORK6, CFlyingManager::INDEX_FLY_TYPE_FIRE_CRACKER, "d:/ymir work/effect/etc/firecracker/firecracker_6.msf" }, // ÆøÁ× 6
	{ FLY_FIREWORK_XMAS, CFlyingManager::INDEX_FLY_TYPE_FIRE_CRACKER, "d:/ymir work/effect/etc/firecracker/firecracker_xmas.msf" }, // ÆøÁ× X-Mas
	{ FLY_CHAIN_LIGHTNING, CFlyingManager::INDEX_FLY_TYPE_NORMAL, "d:/ymir work/pc/shaman/effect/pokroe.msf" }, // Æø·Ú°Ý
	{ FLY_HP_SMALL, CFlyingManager::INDEX_FLY_TYPE_NORMAL, "d:/ymir work/effect/etc/gathering/ga_piece_red_smallest.msf" }, // »¡°£»ö ¸Å¿ì ÀÛÀº°Å
	{ FLY_SKILL_MUYEONG, CFlyingManager::INDEX_FLY_TYPE_AUTO_FIRE, "d:/ymir work/pc/sura/effect/muyeong_fly.msf" }, // ¹«¿µÁø
#if defined(ENABLE_QUIVER_SYSTEM)
	{ FLY_QUIVER_ATTACK_NORMAL, CFlyingManager::INDEX_FLY_TYPE_NORMAL, "d:/ymir work/pc/assassin/effect/arrow_02.msf" }, // ÀÏ¹Ý È°°ø°Ý ÀÌÆåÆ® 1
#endif
	//{ FLY_CONQUEROR_EXP, CFlyingManager::INDEX_FLY_TYPE_NORMAL, "d:/ymir work/effect/etc/gathering/ga_piece_orange_small.msf" },
};

// Emotion
static const EmotionEffectDataVector g_vEmotionEffectData
{
	{ CInstanceBase::EFFECT_EMOTICON + 0, "", "d:/ymir work/effect/etc/emoticon/sweat.mse", false, "(È²´ç)" },
	{ CInstanceBase::EFFECT_EMOTICON + 1, "", "d:/ymir work/effect/etc/emoticon/money.mse", false, "(µ·)" },
	{ CInstanceBase::EFFECT_EMOTICON + 2, "", "d:/ymir work/effect/etc/emoticon/happy.mse", false, "(±â»Ý)" },
	{ CInstanceBase::EFFECT_EMOTICON + 3, "", "d:/ymir work/effect/etc/emoticon/love_s.mse", false, "(ÁÁ¾Æ)" },
	{ CInstanceBase::EFFECT_EMOTICON + 4, "", "d:/ymir work/effect/etc/emoticon/love_l.mse", false, "(»ç¶û)" },
	{ CInstanceBase::EFFECT_EMOTICON + 5, "", "d:/ymir work/effect/etc/emoticon/angry.mse", false, "(ºÐ³ë)" },
	{ CInstanceBase::EFFECT_EMOTICON + 6, "", "d:/ymir work/effect/etc/emoticon/aha.mse", false, "(¾ÆÇÏ)" },
	{ CInstanceBase::EFFECT_EMOTICON + 7, "", "d:/ymir work/effect/etc/emoticon/gloom.mse", false, "(¿ì¿ï)" },
	{ CInstanceBase::EFFECT_EMOTICON + 8, "", "d:/ymir work/effect/etc/emoticon/sorry.mse", false, "(ÁË¼Û)" },
	{ CInstanceBase::EFFECT_EMOTICON + 9, "", "d:/ymir work/effect/etc/emoticon/!_mix_back.mse", false, "(!)" },
	{ CInstanceBase::EFFECT_EMOTICON + 10, "", "d:/ymir work/effect/etc/emoticon/question.mse", false, "(?)" },
	{ CInstanceBase::EFFECT_EMOTICON + 11, "", "d:/ymir work/effect/etc/emoticon/fish.mse", false, "(fish)" },

#if defined(ENABLE_EXPRESSING_EMOTION)
	{ CInstanceBase::EFFECT_EMOTICON + 12, "", "d:/ymir work/effect/etc/emoticon/charging.mse", false, "(charging)" },
	{ CInstanceBase::EFFECT_EMOTICON + 13, "", "d:/ymir work/effect/etc/emoticon/nosay.mse", false, "(nosay)" },
	{ CInstanceBase::EFFECT_EMOTICON + 14, "", "d:/ymir work/effect/etc/emoticon/weather1.mse", false, "(weather1)" },
	{ CInstanceBase::EFFECT_EMOTICON + 15, "", "d:/ymir work/effect/etc/emoticon/weather2.mse", false, "(weather2)" },
	{ CInstanceBase::EFFECT_EMOTICON + 16, "", "d:/ymir work/effect/etc/emoticon/weather3.mse", false, "(weather3)" },
	{ CInstanceBase::EFFECT_EMOTICON + 17, "", "d:/ymir work/effect/etc/emoticon/hungry.mse", false, "(hungry)" },
	{ CInstanceBase::EFFECT_EMOTICON + 18, "", "d:/ymir work/effect/etc/emoticon/siren.mse", false, "(siren)" },
	{ CInstanceBase::EFFECT_EMOTICON + 19, "", "d:/ymir work/effect/etc/emoticon/letter.mse", false, "(letter)" },
	{ CInstanceBase::EFFECT_EMOTICON + 20, "", "d:/ymir work/effect/etc/emoticon/call.mse", false, "(call)" },
	{ CInstanceBase::EFFECT_EMOTICON + 21, "", "d:/ymir work/effect/etc/emoticon/celebration.mse", false, "(celebration)" },
	{ CInstanceBase::EFFECT_EMOTICON + 22, "", "d:/ymir work/effect/etc/emoticon/alcohol.mse", false, "(alcohol)" },
	{ CInstanceBase::EFFECT_EMOTICON + 23, "", "d:/ymir work/effect/etc/emoticon/busy.mse", false, "(busy)" },
	{ CInstanceBase::EFFECT_EMOTICON + 24, "", "d:/ymir work/effect/etc/emoticon/whirl.mse", false, "(whirl)" },
#endif
};

// Race
static RaceDataVector g_vRaceData
{
	{ CRaceData::RACE_WARRIOR_M, "warrior_m.msm", "d:/ymir work/pc/warrior/" },
	{ CRaceData::RACE_WARRIOR_W, "warrior_w.msm", "d:/ymir work/pc2/warrior/" },
	{ CRaceData::RACE_ASSASSIN_W, "assassin_w.msm", "d:/ymir work/pc/assassin/" },
	{ CRaceData::RACE_ASSASSIN_M, "assassin_m.msm", "d:/ymir work/pc2/assassin/" },
	{ CRaceData::RACE_SURA_M , "sura_m.msm", "d:/ymir work/pc/sura/" },
	{ CRaceData::RACE_SURA_W, "sura_w.msm", "d:/ymir work/pc2/sura/" },
	{ CRaceData::RACE_SHAMAN_W, "shaman_w.msm", "d:/ymir work/pc/shaman/" },
	{ CRaceData::RACE_SHAMAN_M, "shaman_m.msm", "d:/ymir work/pc2/shaman/" },
	{ CRaceData::RACE_WOLFMAN_M, "wolfman_m.msm", "d:/ymir work/pc3/wolfman/" },
};

// Intro Motions
static const MotionDataVector g_vIntroMotionData
{
	{ CRaceMotionData::NAME_INTRO_WAIT, "wait.msa", 0 },
	{ CRaceMotionData::NAME_INTRO_SELECTED, "selected.msa", 0 },
	{ CRaceMotionData::NAME_INTRO_NOT_SELECTED, "not_selected.msa", 0 },
};

// General Motion Data
static const MotionDataVector g_vGeneralMotionData
{
	{ CRaceMotionData::NAME_WAIT, "wait.msa", 0 },
	{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
	{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
	{ CRaceMotionData::NAME_DAMAGE, "damage.msa", 50 },
	{ CRaceMotionData::NAME_DAMAGE, "damage_1.msa", 50 },
	{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_2.msa", 50 },
	{ CRaceMotionData::NAME_DAMAGE_BACK, "damage_3.msa", 50 },
	{ CRaceMotionData::NAME_DAMAGE_FLYING, "damage_flying.msa", 0 },
	{ CRaceMotionData::NAME_STAND_UP, "falling_stand.msa", 0 },
	{ CRaceMotionData::NAME_DAMAGE_FLYING_BACK, "back_damage_flying.msa", 0 },
	{ CRaceMotionData::NAME_STAND_UP_BACK, "back_falling_stand.msa", 0 },
	{ CRaceMotionData::NAME_DEAD, "dead.msa", 0 },
	{ CRaceMotionData::NAME_DIG, "dig.msa", 0 },
};

// New General Motion Data (Used by Wolfman)
static const MotionDataVector g_vNewGeneralMotionData
{
	{ CRaceMotionData::NAME_WAIT, "wait.msa", 0 },
	{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
	{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
	{ CRaceMotionData::NAME_DAMAGE, "front_damage.msa", 50 },
	{ CRaceMotionData::NAME_DAMAGE, "front_damage1.msa", 50 },
	{ CRaceMotionData::NAME_DAMAGE_BACK, "back_damage.msa", 50 },
	{ CRaceMotionData::NAME_DAMAGE_BACK, "back_damage1.msa", 50 },
	{ CRaceMotionData::NAME_DAMAGE_FLYING, "front_damage_flying.msa", 0 },
	{ CRaceMotionData::NAME_STAND_UP, "front_falling_standup.msa", 0 },
	{ CRaceMotionData::NAME_DAMAGE_FLYING_BACK, "back_damage_flying.msa", 0 },
	{ CRaceMotionData::NAME_STAND_UP_BACK, "back_falling_standup.msa", 0 },
	{ CRaceMotionData::NAME_DEAD, "dead.msa", 0 },
	{ CRaceMotionData::NAME_DIG, "dig.msa", 0 },
};

// Action Motion Data
static const MotionDataVector g_vActionMotionData
{
	{ CRaceMotionData::NAME_CLAP, "clap.msa", 0 },
	{ CRaceMotionData::NAME_CHEERS_1, "cheers_1.msa", 0 },
	{ CRaceMotionData::NAME_CHEERS_2, "cheers_2.msa", 0 },
	{ CRaceMotionData::NAME_DANCE_1, "dance_1.msa", 0 },
	{ CRaceMotionData::NAME_DANCE_2, "dance_2.msa", 0 },
	{ CRaceMotionData::NAME_DANCE_3, "dance_3.msa", 0 },
	{ CRaceMotionData::NAME_DANCE_4, "dance_4.msa", 0 },
	{ CRaceMotionData::NAME_DANCE_5, "dance_5.msa", 0 },
	{ CRaceMotionData::NAME_DANCE_6, "dance_6.msa", 0 },
	{ CRaceMotionData::NAME_CONGRATULATION, "congratulation.msa", 0 },
	{ CRaceMotionData::NAME_FORGIVE, "forgive.msa", 0 },
	{ CRaceMotionData::NAME_ANGRY, "angry.msa", 0 },
	{ CRaceMotionData::NAME_ATTRACTIVE, "attractive.msa", 0 },
	{ CRaceMotionData::NAME_SAD, "sad.msa", 0 },
	{ CRaceMotionData::NAME_SHY, "shy.msa", 0 },
	{ CRaceMotionData::NAME_CHEERUP, "cheerup.msa", 0 },
	{ CRaceMotionData::NAME_BANTER, "banter.msa", 0 },
	{ CRaceMotionData::NAME_JOY, "joy.msa", 0 },
	{ CRaceMotionData::NAME_FRENCH_KISS_WITH_WARRIOR, "french_kiss_with_warrior.msa", 0 },
	{ CRaceMotionData::NAME_FRENCH_KISS_WITH_ASSASSIN, "french_kiss_with_assassin.msa", 0 },
	{ CRaceMotionData::NAME_FRENCH_KISS_WITH_SURA, "french_kiss_with_sura.msa", 0 },
	{ CRaceMotionData::NAME_FRENCH_KISS_WITH_SHAMAN, "french_kiss_with_shaman.msa", 0 },
#if defined(ENABLE_WOLFMAN_CHARACTER)
	{ CRaceMotionData::NAME_FRENCH_KISS_WITH_WOLFMAN, "french_kiss_with_wolfman.msa", 0 },
#endif
	{ CRaceMotionData::NAME_KISS_WITH_WARRIOR, "kiss_with_warrior.msa", 0 },
	{ CRaceMotionData::NAME_KISS_WITH_ASSASSIN, "kiss_with_assassin.msa", 0 },
	{ CRaceMotionData::NAME_KISS_WITH_SURA, "kiss_with_sura.msa", 0 },
	{ CRaceMotionData::NAME_KISS_WITH_SHAMAN, "kiss_with_shaman.msa", 0 },
#if defined(ENABLE_WOLFMAN_CHARACTER)
	{ CRaceMotionData::NAME_KISS_WITH_WOLFMAN, "kiss_with_wolfman.msa", 0 },
#endif
	{ CRaceMotionData::NAME_SLAP_HIT_WITH_WARRIOR, "slap_hit.msa", 0 },
	{ CRaceMotionData::NAME_SLAP_HIT_WITH_ASSASSIN, "slap_hit.msa", 0 },
	{ CRaceMotionData::NAME_SLAP_HIT_WITH_SURA, "slap_hit.msa", 0 },
	{ CRaceMotionData::NAME_SLAP_HIT_WITH_SHAMAN, "slap_hit.msa", 0 },
#if defined(ENABLE_WOLFMAN_CHARACTER)
	{ CRaceMotionData::NAME_SLAP_HIT_WITH_WOLFMAN, "slap_hit.msa", 0 },
#endif
	{ CRaceMotionData::NAME_SLAP_HURT_WITH_WARRIOR, "slap_hurt.msa", 0 },
	{ CRaceMotionData::NAME_SLAP_HURT_WITH_ASSASSIN, "slap_hurt.msa", 0 },
	{ CRaceMotionData::NAME_SLAP_HURT_WITH_SURA, "slap_hurt.msa", 0 },
	{ CRaceMotionData::NAME_SLAP_HURT_WITH_SHAMAN, "slap_hurt.msa", 0 },
#if defined(ENABLE_WOLFMAN_CHARACTER)
	{ CRaceMotionData::NAME_SLAP_HURT_WITH_WOLFMAN, "slap_hurt.msa", 0 },
#endif
#if defined(ENABLE_EXPRESSING_EMOTION)
	{ CRaceMotionData::NAME_PUSH_UP, "pushup.msa", 0 },
	{ CRaceMotionData::NAME_DANCE_7, "dance_7.msa", 0 },
	{ CRaceMotionData::NAME_EXERCISE, "exercise.msa", 0 },
	{ CRaceMotionData::NAME_DOZE, "doze.msa", 0 },
	{ CRaceMotionData::NAME_SELFIE, "selfie.msa", 0 },
#endif
};

// Wedding Motions
static const MotionDataVector g_vWeddingMotionData
{
	{ CRaceMotionData::NAME_WAIT, "wait.msa", 0 },
	{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
	{ CRaceMotionData::NAME_RUN, "walk.msa", 0 },
};

// Fishing Motions
static const MotionDataVector g_vFishingMotionData
{
	{ CRaceMotionData::NAME_WAIT, "wait.msa", 0 },
	{ CRaceMotionData::NAME_WALK, "walk.msa", 0 },
	{ CRaceMotionData::NAME_RUN, "run.msa", 0 },
	{ CRaceMotionData::NAME_FISHING_THROW, "throw.msa", 0 },
	{ CRaceMotionData::NAME_FISHING_WAIT, "fishing_wait.msa", 0 },
	{ CRaceMotionData::NAME_FISHING_STOP, "fishing_cancel.msa", 0 },
	{ CRaceMotionData::NAME_FISHING_REACT, "fishing_react.msa", 0 },
	{ CRaceMotionData::NAME_FISHING_CATCH, "fishing_catch.msa", 0 },
	{ CRaceMotionData::NAME_FISHING_FAIL, "fishing_fail.msa", 0 },
};

// Guild Skill Motions
static const MotionDataVector g_vGuildSkillMotionData
{
	{ GUILD_SKILL_DRAGONBLOOD, "guild_yongsinuipi.msa", 0 },
	{ GUILD_SKILL_DRAGONBLESS, "guild_yongsinuichukbok.msa", 0 },
	{ GUILD_SKILL_BLESSARMOR, "guild_seonghwigap.msa", 0 },
	{ GUILD_SKILL_SPPEDUP, "guild_gasokhwa.msa", 0 },
	{ GUILD_SKILL_DRAGONWRATH, "guild_yongsinuibunno.msa", 0 },
	{ GUILD_SKILL_MAGICUP, "guild_jumunsul.msa", 0 },
};

// Player skill lists mirror playersettingmodule.DefineSkillIndexDict.
static const SkillIndexVectorMap g_vMapPlayerSkillIndex
{
	{ NRaceData::JOB_WARRIOR, {{ 1, { 1, 2, 3, 4, 5, 6, 0, 0, 137, 0, 138, 0, 139, 0, }}, { 2, { 16, 17, 18, 19, 20, 21, 0, 0, 137, 0, 138, 0, 139, 0, }}}},
	{ NRaceData::JOB_ASSASSIN, {{ 1, { 31, 32, 33, 34, 35, 36, 0, 0, 137, 0, 138, 0, 139, 0, 140, }}, { 2, { 46, 47, 48, 49, 50, 51, 0, 0, 137, 0, 138, 0, 139, 0, 140, }}}},
	{ NRaceData::JOB_SURA, {{ 1, { 61, 62, 63, 64, 65, 66, 0, 0, 137, 0, 138, 0, 139, 0, }}, { 2, { 76, 77, 78, 79, 80, 81, 0, 0, 137, 0, 138, 0, 139, 0, }}}},
	{ NRaceData::JOB_SHAMAN, {{ 1, { 91, 92, 93, 94, 95, 96, 0, 0, 137, 0, 138, 0, 139, 0, }}, { 2, { 106, 107, 108, 109, 110, 111, 0, 0, 137, 0, 138, 0, 139, 0, }}}},
#if defined(ENABLE_WOLFMAN_CHARACTER)
	{ NRaceData::JOB_WOLFMAN, {{ 1, { 170, 171, 172, 173, 174, 175, 0, 0, 137, 0, 138, 0, 139, 0, }}, { 2, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, }}}},
#endif
};

static const SkillIndexVectorMap g_vMapPlayerSkillIndexLegacy
{
	{ NRaceData::JOB_WARRIOR, {{ 1, { 1, 2, 3, 4, 5, 0, 0, 0, 137, 0, 138, 0, 139, 0, }}, { 2, { 16, 17, 18, 19, 20, 0, 0, 0, 137, 0, 138, 0, 139, 0, }}}},
	{ NRaceData::JOB_ASSASSIN, {{ 1, { 31, 32, 33, 34, 35, 0, 0, 0, 137, 0, 138, 0, 139, 0, 140, }}, { 2, { 46, 47, 48, 49, 50, 0, 0, 0, 137, 0, 138, 0, 139, 0, 140, }}}},
	{ NRaceData::JOB_SURA, {{ 1, { 61, 62, 63, 64, 65, 66, 0, 0, 137, 0, 138, 0, 139, 0, }}, { 2, { 76, 77, 78, 79, 80, 81, 0, 0, 137, 0, 138, 0, 139, 0, }}}},
	{ NRaceData::JOB_SHAMAN, {{ 1, { 91, 92, 93, 94, 95, 96, 0, 0, 137, 0, 138, 0, 139, 0, }}, { 2, { 106, 107, 108, 109, 110, 111, 0, 0, 137, 0, 138, 0, 139, 0, }}}},
#if defined(ENABLE_WOLFMAN_CHARACTER)
	{ NRaceData::JOB_WOLFMAN, {{ 1, { 170, 171, 172, 173, 174, 175, 0, 0, 137, 0, 138, 0, 139, 0, }}, { 2, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, }}}},
#endif
};

static const SkillIndexVector g_vSupportSkillIndex
{
	122, 123, 121, 124, 125, 129, 0, 0, 130, 131, 0, 0,
};

static const SkillIndexVector g_vSupportSkillIndexLegacy
{
	122, 123, 121, 124, 125, 129, 0, 0, 130, 131,
};
// Guild Skills
static const GuildSkillIndexVectorMap g_vMapGuildSkillIndex
{
	{ "PASSIVE", { 151, }},
	{ "ACTIVE", { 152, 153, 154, 155, 156, 157, }},
};

// Dungeon Map Name
static const DungeonMapNameVector g_vDungeonMapName
{
	{ "metin2_map_spiderdungeon" },
	{ "metin2_map_monkeydungeon" },
	{ "metin2_map_monkeydungeon_02" },
	{ "metin2_map_monkeydungeon_03" },
	{ "metin2_map_deviltower1" },
};

// Title Name
static const TitleNameVector g_vTitleName
{
	"PVP_LEVEL0",
	"PVP_LEVEL1",
	"PVP_LEVEL2",
	"PVP_LEVEL3",
	"PVP_LEVEL4",
	"PVP_LEVEL5",
	"PVP_LEVEL6",
	"PVP_LEVEL7",
	"PVP_LEVEL8"
};

#if defined(ENABLE_GUILD_LEADER_GRADE_NAME)
static const TitleNameVector g_vGuildLeaderGradeTitleName
{
	"GUILD_LEADER_GRADE0",
	"GUILD_LEADER_GRADE1"
};
#endif

// Name Color
static const NameColorMap g_mNameColor
{
	{ CInstanceBase::NAMECOLOR_NORMAL_PC, { 255, 215, 76 }},
	{ CInstanceBase::NAMECOLOR_NORMAL_NPC, { 122, 231, 93 }},
	{ CInstanceBase::NAMECOLOR_NORMAL_MOB, { 235, 22, 9 }},
	{ CInstanceBase::NAMECOLOR_PVP, { 238, 54, 223 }},
	{ CInstanceBase::NAMECOLOR_PK, { 180, 100, 0 }},
	{ CInstanceBase::NAMECOLOR_PARTY, { 128, 192, 255 }},
	{ CInstanceBase::NAMECOLOR_WARP, { 136, 218, 241 }},
	{ CInstanceBase::NAMECOLOR_WAYPOINT, { 255, 255, 255 }},
	{ CInstanceBase::NAMECOLOR_EMPIRE_MOB, { 235, 22, 9 }},
	{ CInstanceBase::NAMECOLOR_EMPIRE_NPC, { 122, 231, 93 }},
	{ CInstanceBase::NAMECOLOR_EMPIRE_PC + 1, { 157, 0, 0 }},
	{ CInstanceBase::NAMECOLOR_EMPIRE_PC + 2, { 222, 160, 47 }},
	{ CInstanceBase::NAMECOLOR_EMPIRE_PC + 3, { 23, 30, 138 }},
};

// Title Color
static const TitleColorMap g_mTitleNameColor
{
	{ 0, { 0, 204, 255 }},
	{ 1, { 0, 144, 255 }},
	{ 2, { 92, 110, 255 }},
	{ 3, { 155, 155, 255 }},
	{ 4, { 255, 255, 255 }},
	{ 5, { 207, 117, 0 }},
	{ 6, { 235, 83, 0 }},
	{ 7, { 227, 0, 0 }},
	{ 8, { 255, 0, 0 }},
};

// Emotion Icon
static const EmotionIconVector g_vEmotionIcon
{
	{ EMOTION_CLAP, "d:/ymir work/ui/game/windows/emotion_clap.sub" },
	{ EMOTION_CHEERS_1, "d:/ymir work/ui/game/windows/emotion_cheers_1.sub" },
	{ EMOTION_CHEERS_2, "d:/ymir work/ui/game/windows/emotion_cheers_2.sub" },
	{ EMOTION_DANCE_1, "icon/action/dance1.tga" },
	{ EMOTION_DANCE_2, "icon/action/dance2.tga" },
	{ EMOTION_CONGRATULATION, "icon/action/congratulation.tga" },
	{ EMOTION_FORGIVE, "icon/action/forgive.tga" },
	{ EMOTION_ANGRY, "icon/action/angry.tga" },
	{ EMOTION_ATTRACTIVE, "icon/action/attractive.tga" },
	{ EMOTION_SAD, "icon/action/sad.tga" },
	{ EMOTION_SHY, "icon/action/shy.tga" },
	{ EMOTION_CHEERUP, "icon/action/cheerup.tga" },
	{ EMOTION_BANTER, "icon/action/banter.tga" },
	{ EMOTION_JOY, "icon/action/joy.tga" },
	{ EMOTION_DANCE_1, "icon/action/dance1.tga" },
	{ EMOTION_DANCE_2, "icon/action/dance2.tga" },
	{ EMOTION_DANCE_3, "icon/action/dance3.tga" },
	{ EMOTION_DANCE_4, "icon/action/dance4.tga" },
	{ EMOTION_DANCE_5, "icon/action/dance5.tga" },
	{ EMOTION_DANCE_6, "icon/action/dance6.tga" },
	{ EMOTION_KISS, "d:/ymir work/ui/game/windows/emotion_kiss.sub" },
	{ EMOTION_FRENCH_KISS, "d:/ymir work/ui/game/windows/emotion_french_kiss.sub" },
	{ EMOTION_SLAP, "d:/ymir work/ui/game/windows/emotion_slap.sub" },
#if defined(ENABLE_EXPRESSING_EMOTION)
	{ EMOTION_PUSH_UP, "icon/action/pushup.tga" },
	{ EMOTION_DANCE_7, "icon/action/dance7.tga" },
	{ EMOTION_EXERCISE, "icon/action/exercise.tga" },
	{ EMOTION_DOZE, "icon/action/doze.tga" },
	{ EMOTION_SELFIE, "icon/action/selfie.tga" },
	{ EMOTION_CHARGING, "icon/action/charging.tga" },
	{ EMOTION_NOSAY, "icon/action/nosay.tga" },
	{ EMOTION_WEATHER_1, "icon/action/weather1.tga" },
	{ EMOTION_WEATHER_2, "icon/action/weather2.tga" },
	{ EMOTION_WEATHER_3, "icon/action/weather3.tga" },
	{ EMOTION_HUNGRY, "icon/action/hungry.tga" },
	{ EMOTION_SIREN, "icon/action/siren.tga" },
	{ EMOTION_LETTER, "icon/action/letter.tga" },
	{ EMOTION_CALL, "icon/action/call.tga" },
	{ EMOTION_CELEBRATION, "icon/action/celebration.tga" },
	{ EMOTION_ALCOHOL, "icon/action/alcohol.tga" },
	{ EMOTION_BUSY, "icon/action/busy.tga" },
	{ EMOTION_WHIRL, "icon/action/whirl.tga" }
#endif
};

// Guild Building
static constexpr BYTE g_bGuildMaterialMaxNum = 3;
static const GuildBuildingMap g_mGuildBuilding
{
	{ "HEADQUARTER", "headquarter" },
	{ "FACILITY", "facility" },
	{ "OBJECT", "object" },
	{ "WALL", "fence" },
};

static const short GetGuildMaterialIndex(const DWORD c_dwVNum)
{
	switch (c_dwVNum)
	{
	case MATERIAL_STONE_ID: // ÃÊ¼®
	case MATERIAL_LOG_ID: // Åë³ª¹«
	case MATERIAL_PLYWOOD_ID: // ÇÕÆÇ
		return c_dwVNum - MATERIAL_STONE_ID;
	}
	return -1;
}

std::string GetLocaleInfoString(const std::string& c_rstToken)
{
	PyObject* ppyModule = PyImport_ImportModule("localeInfo");
	if (!ppyModule)
	{
		PyErr_Clear();
		return "NoName";
	}

	PyObject* ppyObject = PyObject_GetAttrString(ppyModule, c_rstToken.c_str());
	Py_DECREF(ppyModule);
	if (!ppyObject)
	{
		PyErr_Clear();
		return "NoName";
	}

	if (!PyString_Check(ppyObject))
	{
		Py_DECREF(ppyObject);
		PyErr_Clear();
		return "NoName";
	}

	const char* szText = PyString_AsString(ppyObject);
	if (!szText)
	{
		Py_DECREF(ppyObject);
		PyErr_Clear();
		return "NoName";
	}

	const std::string stText = szText[0] == '\0' ? "NoName" : szText;
	Py_DECREF(ppyObject);
	return stText;
}

bool IsNumber(const std::string& c_rString)
{
	return !c_rString.empty() && std::find_if(c_rString.begin(), c_rString.end(),
		[](unsigned char c)
		{
			return !std::isdigit(c);
		}) == c_rString.end();
}
#endif // ENABLE_LOADING_PERFORMANCE
