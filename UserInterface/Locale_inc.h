#pragma once

//////////////////////////////////////////////////////////////////////////
// ### ServiceDefs Macros ###
//#define _IMPROVED_PACKET_ENCRYPTION_
#ifndef _IMPROVED_PACKET_ENCRYPTION_
#define USE_NO_PACKET_ENCRYPTION
#endif

//#define ENABLE_SEQUENCE_SYSTEM
//#define __PERFORMANCE_CHECK__
// ### ServiceDefs Macros ###
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// ### Default Ymir Macros ###
#define LOCALE_SERVICE_EUROPE
#define ENABLE_COSTUME_SYSTEM
#define ENABLE_ENERGY_SYSTEM
#define ENABLE_DRAGON_SOUL_SYSTEM
#define ENABLE_NEW_EQUIPMENT_SYSTEM
// ### Default Ymir Macros ###
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// ### New From LocaleInc ###
#define ENABLE_PACK_GET_CHECK
#define ENABLE_CANSEEHIDDENTHING_FOR_GM
#define ENABLE_PROTOSTRUCT_AUTODETECT
#define ENABLE_PLAYER_PER_ACCOUNT5
#define ENABLE_LEVEL_IN_TRADE
#define ENABLE_DICE_SYSTEM
#define ENABLE_EXTEND_INVEN_SYSTEM
#define ENABLE_LVL115_ARMOR_EFFECT
#define ENABLE_SLOT_WINDOW_EX
#define ENABLE_TEXT_LEVEL_REFRESH
#define ENABLE_USE_COSTUME_ATTR
// #define ENABLE_MAGIC_REDUCTION_SYSTEM
#define ENABLE_MOUNT_COSTUME_SYSTEM
#define ENABLE_WEAPON_COSTUME_SYSTEM
#define ENABLE_DISCORD_RPC
#define ENABLE_PET_SYSTEM_EX
#define ENABLE_LOCALE_COMMON
#define ENABLE_NO_DSS_QUALIFICATION
//#define ENABLE_NO_SELL_PRICE_DIVIDED_BY_5
#define ENABLE_PENDANT_SYSTEM
#define ENABLE_GLOVE_SYSTEM
#define ENABLE_MOVE_CHANNEL
#define ENABLE_QUIVER_SYSTEM
#define ENABLE_RACE_HEIGHT
#define ENABLE_ELEMENTAL_TARGET
#define ENABLE_INGAME_CONSOLE
#define ENABLE_4TH_AFF_SKILL_DESC
#define ENABLE_GUILD_TOKEN_AUTH
#define ENABLE_EMOTION_HIDE_WEAPON
#define ENABLE_MULTI_ITEM_PICK
#define ENABLE_CONQUEROR_UI
#define ENABLE_DS_GRADE_MYTH
#define ENABLE_MESSENGER_REMOVE_SYNC
// #define ENABLE_MODEL_LOD_LOAD
#define ENABLE_ITEM_GROUND_EX

// Preload character, effect, sound, and NPC data through the native C++ loader.
#define ENABLE_LOADING_PERFORMANCE

// Font batching and its device-reset corrections share this switch.
#define ENABLE_FIX_MOBS_LAG

// Remove dog colision for test
#define REMOVE_DOG_COLISION

#define ENABLE_NEW_EVENT_STRUCT
#ifdef ENABLE_NEW_EVENT_STRUCT
#define USE_NEW_EVENT_TEXT_AUTO_Y
#endif

#define WJ_SHOW_MOB_INFO
#ifdef WJ_SHOW_MOB_INFO
#define ENABLE_SHOW_MOBAIFLAG
#define ENABLE_SHOW_MOBLEVEL
#define WJ_SHOW_MOB_INFO_EX
#endif

#define ENABLE_WOLFMAN_CHARACTER
#ifdef ENABLE_WOLFMAN_CHARACTER
// #define DISABLE_WOLFMAN_ON_CREATE
#endif
// ### New From LocaleInc ###
//////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////
// ### New System Defines - Extended Version ###
#define ENABLE_ACCE_COSTUME_SYSTEM
#ifdef ENABLE_ACCE_COSTUME_SYSTEM
// #define USE_ACCE_ABSORB_WITH_NO_NEGATIVE_BONUS
#endif

#define ENABLE_HIGHLIGHT_NEW_ITEM //if you want to see highlighted a new item when dropped or when exchanged
#ifdef ENABLE_HIGHLIGHT_NEW_ITEM
#define BL_ENABLE_PICKUP_ITEM_EFFECT // enable some extra highlight features from official
#define __BL_ENABLE_PICKUP_ITEM_EFFECT__ // alias
#endif

#define __BL_OFFICIAL_LOOT_FILTER__
#if defined(__BL_OFFICIAL_LOOT_FILTER__)
//#define ENABLE_PREMIUM_LOOT_FILTER // Enable Premium Usage of the Loot Filter System
#endif

#define ENABLE_MOUSEWHEEL_EVENT // if you want use SetMouseWheelScrollEvent or you want use mouse wheel to move the scrollbar
#define ENABLE_EMOJI_SYSTEM // it shows emojis in the textlines
#define __ENABLE_STEALTH_FIX__ // effects while hidden won't show up
#define ENABLE_MINIMAP_WHITEMARK_CIRCLE // circle dots in minimap instead of squares
// #define ENABLE_PRINT_RECV_PACKET_DEBUG // for debug: print received packets
#define ENABLE_MINIMAP_TELEPORT_CLICK // click on minimap as gm to warp directly
#define ENABLE_ATLAS_MARK_ON_WARP_SCROLLS // warp scrolls tooltips will show the mark on the atlas

// enable the won system as a currency
#define ENABLE_CHEQUE_SYSTEM
#ifdef ENABLE_CHEQUE_SYSTEM
#define DISABLE_CHEQUE_DROP
#define ENABLE_WON_EXCHANGE_WINDOW
#endif

// reversed official code
#define ENABLE_PLAYER_CHECKAFFECT
#define ENABLE_BL_APP_GET_TEXT
#define ENABLE_BL_TRACEBACK
#define __BL_MOUSE_WHEEL_TOP_WINDOW__
#define ENABLE_UI_CIRCLE
#define ENABLE_UI_MOVING
#define ENABLE_FONT_EX
#define __BL_CLIP_MASK__
#define ENABLE_AUTO_L2R
#define ENABLE_AREA_OPTIMIZATION
// ### New System Defines - Extended Version ###
//////////////////////////////////////////////////////////////////////////


// Native DX11 backend; entity visibility and animation cadence are preserved.
#define ENABLE_DIRECTX11
// ### DIRECTX11 Configuration ###
#define ENABLE_MSAA
#define MSAA_DEFAULT_SAMPLE_COUNT 4

#define ENABLE_BLOOM
#define BLOOM_DEFAULT_THRESHOLD 0.8f
#define BLOOM_DEFAULT_INTENSITY 1.0f

#define ENABLE_GODRAYS

#define ENABLE_SSAO


// Celestial Body (sun/moon disk rendered on skybox)
#define ENABLE_CELESTIAL_BODY

#define ENABLE_HEIGHT_FOG


// D3D11 Feature Level - minimum required
// Maximum constants for shader system
#define MAX_LIGHTS 8

// ### DIRECTX11 Configuration ###

#define ENABLE_FRUSTUM_CULLING

#define ENABLE_CHAR_RENDER_LIMIT
#define CHAR_RENDER_LIMIT_DEFAULT       0        // Max characters rendered (0 = no limit)

// Effect Limit - Limit total active effect instances
#define ENABLE_EFFECT_LIMIT
#define EFFECT_LIMIT_DEFAULT            0      // Max effects total (0 = no limit)
#define EFFECT_LIMIT_DISTANCE_DEFAULT   3000.0f  // Skip far effects when at limit


// Optional: #define ENABLE_UPDATE_CULLING
#define UPDATE_CULLING_DISTANCE_FAR     15000.0f  // Very far: skip most updates
#define UPDATE_CULLING_DISTANCE_MID     8000.0f   // Mid range: reduce update frequency
#define UPDATE_CULLING_FRAME_SKIP_FAR   8         // Update every N frames when far
#define UPDATE_CULLING_FRAME_SKIP_MID   4         // Update every N frames when mid

// Optional: #define ENABLE_DEFORM_CULLING

// Optional: #define ENABLE_ANIMATION_LOD

#define ENABLE_SHADOW_RENDER_LIMIT
#define SHADOW_RENDER_LIMIT_DEFAULT     0       // Max shadow casters (0 = no limit)
#define ANIMATION_LOD_DISTANCE_HIGH     3000.0f   // Full quality animations
#define ANIMATION_LOD_DISTANCE_MID      8000.0f   // Reduced quality
#define ANIMATION_LOD_SKIP_FRAMES_MID   2         // Update every N frames at mid distance
#define ANIMATION_LOD_SKIP_FRAMES_FAR   4         // Update every N frames at far distance

#define ENABLE_RENDER_MODE_GROUPING


// ### RENDERING OPTIMIZATIONS ###
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// ### SHADOW CAMERA CONFIGURATION ###

#define SHADOW_PROJECTION_SIZE          2550.0f

#define SHADOW_LIGHT_OFFSET             1250.0f

// Shadow projection near plane (minimum render distance)
#define SHADOW_NEAR_PLANE               1.0f

#define SHADOW_FAR_PLANE                15000.0f

#define SHADOW_LIGHT_DIR_X              1.732f
#define SHADOW_LIGHT_DIR_Z              (2.0f * 1.732f)

// ### SHADOW CAMERA CONFIGURATION ###
//////////////////////////////////////////////////////////////////////////


// Shadow map coverage sizes (world units)
#define SHADOW_BIG_MAP_SIZE             5000.0f
#define SHADOW_LOCAL_MAP_SIZE           600.0f

#define SHADOW_OPACITY_DEFAULT          80.0f

// ### DUAL SHADOW MAP CONFIGURATION ###
//////////////////////////////////////////////////////////////////////////

//martysama0134's dcf42890919f0da1c0e6dbb7f15bc7ec
