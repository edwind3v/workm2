#pragma once

#include "../eterBase/Singleton.h"
#ifdef ENABLE_FIX_MOBS_LAG
#include <vector>
#include <cstdint>
#endif

class CPythonTextTail : public CSingleton<CPythonTextTail>
{
	public:
		typedef struct STextTail
		{
			CGraphicTextInstance*			pTextInstance{};
			CGraphicTextInstance*			pOwnerTextInstance{};

			CGraphicMarkInstance*			pMarkInstance{};
			CGraphicTextInstance*			pGuildNameTextInstance{};

			CGraphicTextInstance*			pTitleTextInstance{};
			CGraphicTextInstance*			pLevelTextInstance{};

			CGraphicObjectInstance *		pOwner{};

			DWORD							dwVirtualID{};

			float							x{}, y{}, z{};
			float							fDistanceFromPlayer{};
#ifdef ENABLE_FIX_MOBS_LAG
			float distanceSquared{};
			std::uint64_t shownFrame{};
#endif
			D3DXCOLOR						Color{};
			BOOL							bNameFlag{};

			float							xStart{}, yStart{};
			float							xEnd{}, yEnd{};

			DWORD							LivingTime{};

			float							fHeight{};

			STextTail() {}
			virtual ~STextTail() {}
		} TTextTail;

		typedef std::map<DWORD, TTextTail*>		TTextTailMap;
#ifdef ENABLE_FIX_MOBS_LAG
		typedef std::vector<TTextTail*> TTextTailList;
#else
		typedef std::list<TTextTail*>			TTextTailList;
#endif
		typedef TTextTailMap					TChatTailMap;

	public:
		CPythonTextTail(void);
		virtual ~CPythonTextTail(void);

		void GetInfo(std::string* pstInfo);

		void Initialize() const;
		void Destroy();
		void Clear();

		void UpdateAllTextTail();
		void UpdateShowingTextTail();
		void Render();

		void ArrangeTextTail();
		void HideAllTextTail();
		void ShowAllTextTail();
		void ShowCharacterTextTail(DWORD VirtualID);
		void ShowItemTextTail(DWORD VirtualID);

		void RegisterCharacterTextTail(DWORD dwGuildID, DWORD dwVirtualID, const D3DXCOLOR & c_rColor, float fAddHeight=10.0f);
		void RegisterItemTextTail(DWORD VirtualID, const char * c_szText, CGraphicObjectInstance * pOwner);
		void RegisterChatTail(DWORD VirtualID, const char * c_szChat);
		void RegisterInfoTail(DWORD VirtualID, const char * c_szChat);
		void SetCharacterTextTailColor(DWORD VirtualID, const D3DXCOLOR & c_rColor);
		void SetItemTextTailOwner(DWORD dwVID, const char * c_szName);
		void DeleteCharacterTextTail(DWORD VirtualID);
		void DeleteItemTextTail(DWORD VirtualID);

		int Pick(int ixMouse, int iyMouse);
		void SelectItemName(DWORD dwVirtualID);

		bool GetTextTailPosition(DWORD dwVID, float* px, float* py, float* pz);
		bool IsChatTextTail(DWORD dwVID);

		void EnablePKTitle(BOOL bFlag) const;
		void AttachTitle(DWORD dwVID, const char * c_szName, const D3DXCOLOR& c_rColor);
		void DetachTitle(DWORD dwVID);

		void AttachLevel(DWORD dwVID, const char* c_szText, const D3DXCOLOR& c_rColor);
		void DetachLevel(DWORD dwVID);

	protected:
		TTextTail * RegisterTextTail(DWORD dwVirtualID, const char * c_szText, CGraphicObjectInstance * pOwner, float fHeight, const D3DXCOLOR & c_rColor);
		void DeleteTextTail(TTextTail * pTextTail);

		void UpdateTextTail(TTextTail * pTextTail) const;
		void RenderTextTailBox(TTextTail * pTextTail) const;
		void RenderTextTailName(TTextTail * pTextTail) const;
		void UpdateDistance(const TPixelPosition & c_rCenterPosition, TTextTail * pTextTail) const;

		bool isIn(TTextTail * pSource, TTextTail * pTarget) const;
#ifdef ENABLE_FIX_MOBS_LAG
		void ShowCharacterTextTail(TTextTail* tail);
		void ShowItemTextTail(TTextTail* tail);
		std::uint64_t m_visibilityFrame = 1;
#endif

	protected:
		TTextTailMap				m_CharacterTextTailMap;
		TTextTailMap				m_ItemTextTailMap;
		TChatTailMap				m_ChatTailMap;

		TTextTailList				m_CharacterTextTailList;
		TTextTailList				m_ItemTextTailList;

	private:
		CDynamicPool<STextTail>		m_TextTailPool;
};

