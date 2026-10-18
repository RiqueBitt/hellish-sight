#include "stdafx.h"
#include "UI.h"
#include "UIScene_CreativeMenu.h"

#include "../Minecraft.World/JavaMath.h"
#include "../../LocalPlayer.h"
#include "../Tutorial/Tutorial.h"
#include "../Tutorial/TutorialMode.h"
#include "../../../Minecraft.World/net.minecraft.world.inventory.h"
#include "../../../Minecraft.World/net.minecraft.world.item.h"

#ifdef __PSVITA__
#define GAME_CREATIVE_TOUCHUPDATE_TIMER_ID 0
#define GAME_CREATIVE_TOUCHUPDATE_TIMER_TIME 100
#endif

UIScene_CreativeMenu::UIScene_CreativeMenu(int iPad, void *_initData, UILayer *parentLayer) : UIScene_AbstractContainerMenu(iPad, parentLayer)
{
	// Setup all the Iggy references we need for this scene
	initialiseMovie();

	InventoryScreenInput *initData = static_cast<InventoryScreenInput *>(_initData);

	shared_ptr<SimpleContainer> creativeContainer = std::make_shared<SimpleContainer>(0, L"", false, TabSpec::MAX_SIZE);
	itemPickerMenu = new ItemPickerMenu(creativeContainer, initData->player->inventory);

	Initialize( initData->iPad, itemPickerMenu, false, -1, eSectionInventoryCreativeUsing, eSectionInventoryCreativeMax, initData->bNavigateBack);
	HS_SetupTabRows();

	m_labelInventory.setLabel( L"" );
	m_bFirstCall=true;

	//m_slotListContainer.addSlots(0,TabSpec::MAX_SIZE);
	//m_slotListHotbar.addSlots(TabSpec::MAX_SIZE,TabSpec::MAX_SIZE + 9);
	for(unsigned int i = 0; i < TabSpec::MAX_SIZE; ++i)
	{
		m_slotListContainer.addSlot(i);
	}

	for(unsigned int i = TabSpec::MAX_SIZE; i < TabSpec::MAX_SIZE + 9; ++i)
	{
		m_slotListHotbar.addSlot(i);
	}

	Minecraft *pMinecraft = Minecraft::GetInstance();
	if( pMinecraft->localgameModes[initData->iPad] != nullptr )
	{
		TutorialMode *gameMode = static_cast<TutorialMode *>(pMinecraft->localgameModes[initData->iPad]);
		m_previousTutorialState = gameMode->getTutorial()->getCurrentState();
		gameMode->getTutorial()->changeTutorialState(e_Tutorial_State_Creative_Inventory_Menu, this);
	}

	if(initData) delete initData;

	m_curTab = static_cast<ECreativeInventoryTabs>(HS_TAB_NONE);
	switchTab(eCreativeInventoryTab_BuildingBlocks);

#ifdef __PSVITA__
	// initialise vita touch controls with ids
	for(unsigned int i = 0; i < ETouchInput_Count; ++i)
	{
		m_TouchInput[i].init(i);
	}
#endif
}

wstring UIScene_CreativeMenu::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1)
	{
		return L"CreativeMenuSplit";
	}
	else
	{
		return L"CreativeMenu";
	}
}

#ifdef __PSVITA__
UIControl* UIScene_CreativeMenu::GetMainPanel()
{
	return &m_controlMainPanel;
}

void UIScene_CreativeMenu::handleTouchInput(unsigned int iPad, S32 x, S32 y, int iId, bool bPressed, bool bRepeat, bool bReleased)
{
	// perform action on release
	if(bReleased)
	{
		int slot = -1;
		if(iId >= ETouchInput_TouchPanel_0 && iId <= ETouchInput_TouchPanel_7) slot = iId - ETouchInput_TouchPanel_0;
		else if(iId >= ETouchInput_HSTab_8 && iId <= ETouchInput_HSTab_15) slot = 8 + iId - ETouchInput_HSTab_8;
		const int tab = HS_TabForSlot(slot);
		if(tab != HS_TAB_NONE)
		{
			switchTab((ECreativeInventoryTabs)tab);
			ui.PlayUISFX(eSFX_Focus);
		}
		else if(iId == ETouchInput_HSPageButton)
		{
			HS_SetTabRowPage(m_tabRowPage + 1);
			ui.PlayUISFX(eSFX_Focus);
		}
	}
	if(bRepeat && iId == ETouchInput_TouchSlider && specs[m_curTab]->getPageCount() > 1)
	{
		// calculate relative touch position on slider
		float fPosition = ((float)y - (float)m_TouchInput[ETouchInput_TouchSlider].getYPos() - m_controlMainPanel.getYPos()) /  (float)m_TouchInput[ETouchInput_TouchSlider].getHeight();
		
		// clamp
		if(fPosition > 1)
			fPosition = 1.0f;
		else if(fPosition < 0)
			fPosition = 0.0f;

		// calculate page position according to page count
		int iCurrentPage = Math::round(fPosition * (specs[m_curTab]->getPageCount() - 1));
		
		// set tab page
		m_tabPage[m_curTab] = iCurrentPage;

		// update tab
		switchTab(m_curTab);
	}
}

void UIScene_CreativeMenu::handleTouchBoxRebuild()
{
	addTimer(GAME_CREATIVE_TOUCHUPDATE_TIMER_ID,GAME_CREATIVE_TOUCHUPDATE_TIMER_TIME);
}

void UIScene_CreativeMenu::handleTimerComplete(int id)
{
	if(id == GAME_CREATIVE_TOUCHUPDATE_TIMER_ID)
	{
		// we cannot rebuild touch boxes in an iggy callback because it requires further iggy calls
		GetMainPanel()->UpdateControl();
		ui.TouchBoxRebuild(this);
		killTimer(GAME_CREATIVE_TOUCHUPDATE_TIMER_ID);
	}
}
#endif

void UIScene_CreativeMenu::handleOtherClicked(int iPad, ESceneSection eSection, int buttonNum, bool quickKey)
{
	if(eSection >= eSectionInventoryCreativeTab_0 && eSection <= eSectionInventoryCreativeTab_15)
	{
		const int tab = HS_TabForSlot(static_cast<int>(eSection) - static_cast<int>(eSectionInventoryCreativeTab_0));
		if(tab != HS_TAB_NONE && tab != m_curTab)
		{
			switchTab(static_cast<ECreativeInventoryTabs>(tab));
			ui.PlayUISFX(eSFX_Focus);
		}
		return;
	}

	switch(eSection)
	{
	case eSectionInventoryCreativePageButton:
		{
			// left half = previous page, right half = next page
			UIControl *button = HS_SectionControl(eSection);
			int dir = 1;
			if(button != nullptr && button->getWidth() > 0)
			{
				const float mid = static_cast<float>(button->getXPos()) + button->getWidth() * 0.5f;
				if(m_pointerPos.x < mid) dir = -1;
			}
			HS_SetTabRowPage(m_tabRowPage + dir);
			ui.PlayUISFX(eSFX_Focus);
		}
		break;
	case eSectionInventoryCreativeSlider:
		ScrollBar(this->m_pointerPos);
		break;
	}
}

bool UIScene_CreativeMenu::IsVisible( ESceneSection eSection )
{
	if(eSection >= eSectionInventoryCreativeTab_0 && eSection <= eSectionInventoryCreativeTab_15)
	{
		return HS_TabForSlot(static_cast<int>(eSection) - static_cast<int>(eSectionInventoryCreativeTab_0)) != HS_TAB_NONE;
	}
	if(eSection == eSectionInventoryCreativePageButton)
	{
		return HS_TabRowPageCount() > 1;
	}
	return true;
}

UIControl *UIScene_CreativeMenu::HS_SectionControl(ESceneSection eSection)
{
	if(eSection >= eSectionInventoryCreativeTab_0 && eSection <= eSectionInventoryCreativeTab_7)
	{
		return &m_TouchInput[ETouchInput_TouchPanel_0 + (eSection - eSectionInventoryCreativeTab_0)];
	}
	if(eSection >= eSectionInventoryCreativeTab_8 && eSection <= eSectionInventoryCreativeTab_15)
	{
		return &m_TouchInput[ETouchInput_HSTab_8 + (eSection - eSectionInventoryCreativeTab_8)];
	}
	if(eSection == eSectionInventoryCreativePageButton) return &m_TouchInput[ETouchInput_HSPageButton];
	if(eSection == eSectionInventoryCreativeSlider) return &m_TouchInput[ETouchInput_TouchSlider];
	return nullptr;
}

// Hellish Sight: the bottom row of tabs and the page button are created by the SWF's
// AS3 outside the background panel. Work out whether the bottom row fits (it does
// not in split screen, then tabs go 8 per page on the top row) and let the pointer
// reach everything.
void UIScene_CreativeMenu::HS_SetupTabRows()
{
	for(int i = 0; i < HS_TAB_SLOTS; ++i) m_hsSentTab[i] = -2;

	UIControl &top = m_TouchInput[ETouchInput_TouchPanel_0];
	UIControl &bottom = m_TouchInput[ETouchInput_HSTab_8];
	top.UpdateControl();
	bottom.UpdateControl();
	m_hsBottomRow = bottom.getWidth() > 0 && bottom.getHeight() > 0 && bottom.getYPos() > top.getYPos() + top.getHeight() / 2;

	for(int i = ETouchInput_HSTab_8; i <= ETouchInput_HSPageButton; ++i)
	{
		if(i <= ETouchInput_HSTab_15 && !m_hsBottomRow) continue;
		UIControl &t = m_TouchInput[i];
		t.UpdateControl();
		if (t.getWidth() <= 0 || t.getHeight() <= 0) continue;
		const float x0 = static_cast<float>(t.getXPos());
		const float y0 = static_cast<float>(t.getYPos());
		const float x1 = x0 + t.getWidth();
		const float y1 = y0 + t.getHeight();
		if (x0 < m_fPanelMinX) m_fPanelMinX = x0;
		if (y0 < m_fPanelMinY) m_fPanelMinY = y0;
		if (x1 > m_fPanelMaxX) m_fPanelMaxX = x1;
		if (y1 > m_fPanelMaxY) m_fPanelMaxY = y1;
	}
	if (m_fPointerMinX > m_fPanelMinX - 4.0f) m_fPointerMinX = m_fPanelMinX - 4.0f;
	if (m_fPointerMinY > m_fPanelMinY - 4.0f && app.GetLocalPlayerCount() <= 1) m_fPointerMinY = m_fPanelMinY - 4.0f;
	if (m_fPointerMaxX < m_fPanelMaxX + 4.0f) m_fPointerMaxX = m_fPanelMaxX + 4.0f;
	if (m_fPointerMaxY < m_fPanelMaxY + 4.0f) m_fPointerMaxY = m_fPanelMaxY + 4.0f;

	// the layout may have changed (reload): keep the current tab visible
	if (m_curTab >= 0) m_tabRowPage = static_cast<int>(m_curTab) / HS_TabsPerPage();
	if (m_tabRowPage >= HS_TabRowPageCount()) m_tabRowPage = 0;
}

void UIScene_CreativeMenu::HS_RefreshTabRow()
{
	IggyDataValue result;
	const int pageCount = HS_TabRowPageCount();
	if (m_tabRowPage >= pageCount) m_tabRowPage = 0;
	const bool nativeRow = (m_tabRowPage == 0);

	// native vanilla art: active tab frame only when a vanilla tab is active on page 0
	{
		IggyDataValue value[1];
		value[0].type = IGGY_DATATYPE_number;
		value[0].number = static_cast<F64>((nativeRow && m_curTab >= 0 && m_curTab < 8) ? m_curTab : -1);
		IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcSetActiveTab , 1 , value );
	}
	{
		IggyDataValue value[4];
		value[0].type = IGGY_DATATYPE_number;
		value[0].number = static_cast<F64>(m_tabRowPage);
		value[1].type = IGGY_DATATYPE_number;
		value[1].number = static_cast<F64>(pageCount);
		value[2].type = IGGY_DATATYPE_boolean;
		value[2].boolval = nativeRow;
		value[3].type = IGGY_DATATYPE_boolean;
		value[3].boolval = m_hsBottomRow;
		IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcSetTabPage , 4 , value );
	}

	for (int slot = 0; slot < HS_TAB_SLOTS; ++slot)
	{
		const int tab = HS_TabForSlot(slot);
		// top-row slots on page 0 are the native vanilla tabs
		const bool visible = tab != HS_TAB_NONE && !(nativeRow && slot < 8);

		std::wstring icon = L"=";	// keep the icon already drawn
		if (visible && m_hsSentTab[slot] != tab)
		{
			const std::string &hex = getTabIconHex(tab);
			icon.assign(hex.begin(), hex.end());
			m_hsSentTab[slot] = tab;
		}

		IggyDataValue value[4];
		value[0].type = IGGY_DATATYPE_number;
		value[0].number = static_cast<F64>(slot);
		value[1].type = IGGY_DATATYPE_boolean;
		value[1].boolval = visible;
		value[2].type = IGGY_DATATYPE_boolean;
		value[2].boolval = visible && tab == m_curTab;
		IggyStringUTF16 str;
		str.string = (IggyUTF16*)icon.c_str();
		str.length = static_cast<int>(icon.length());
		value[3].type = IGGY_DATATYPE_string_UTF16;
		value[3].string16 = str;
		IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcSetTabSlot , 4 , value );
	}
}

void UIScene_CreativeMenu::handleReload()
{
	Initialize( m_iPad, m_menu, false, -1, eSectionInventoryCreativeUsing, eSectionInventoryCreativeMax, m_bNavigateBack );
	HS_SetupTabRows();

	for(unsigned int i = 0; i < TabSpec::MAX_SIZE; ++i)
	{
		m_slotListContainer.addSlot(i);
	}

	for(unsigned int i = TabSpec::MAX_SIZE; i < TabSpec::MAX_SIZE + 9; ++i)
	{
		m_slotListHotbar.addSlot(i);
	}
	
	ECreativeInventoryTabs lastTab = m_curTab;
	m_curTab = static_cast<ECreativeInventoryTabs>(HS_TAB_NONE);
	switchTab(lastTab);
}

void UIScene_CreativeMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	// 4J-PB - going to ignore repeats on this scene
	if(repeat) return;

	//app.DebugPrintf("UIScene_CreativeMenu handling input for pad %d, key %d, down- %s, pressed- %s, released- %s\n", iPad, key, down?"TRUE":"FALSE", pressed?"TRUE":"FALSE", released?"TRUE":"FALSE");
	ui.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

	int dir = 1;
	switch(key)
	{
	case VK_PAD_LSHOULDER:
		dir = -1;
		// Fall through intentional
	case VK_PAD_RSHOULDER:
		HS_CycleTab(dir);
		break;
	case VK_PAD_LTRIGGER:
		// change the potion strength
		{
			++m_tabDynamicPos[m_curTab];
			if(m_tabDynamicPos[m_curTab] >= specs[m_curTab]->m_dynamicGroupsCount) m_tabDynamicPos[m_curTab] = 0;
			switchTab(m_curTab);
		}
		break;
	default:
		UIScene_AbstractContainerMenu::handleInput(iPad,key,repeat,pressed,released,handled);
		break;
	}
}

void UIScene_CreativeMenu::updateTabHighlightAndText(ECreativeInventoryTabs tab)
{
	m_curTab = tab;
	HS_RefreshTabRow();
	m_labelInventory.setLabel(getTabLabel(tab));
}

int UIScene_CreativeMenu::getSectionColumns(ESceneSection eSection)
{
	int cols = 0;
	switch( eSection )
	{
	case eSectionInventoryCreativeSelector:
		cols = 10;
		break;
	case eSectionInventoryCreativeUsing:
		cols = 9;
		break;
	default:
		assert( false );
		break;
	}
	return cols;
}

int UIScene_CreativeMenu::getSectionRows(ESceneSection eSection)
{
	int rows = 0;
	switch( eSection )
	{
	case eSectionInventoryCreativeSelector:
		rows = 5;
		break;
	case eSectionInventoryCreativeUsing:
		rows = 1;
		break;
	default:
		assert( false );
		break;
	}
	return rows;
}

void UIScene_CreativeMenu::GetPositionOfSection( ESceneSection eSection, UIVec2D* pPosition )
{
	switch( eSection )
	{
	case eSectionInventoryCreativeSelector:
		pPosition->x = m_slotListContainer.getXPos();
		pPosition->y = m_slotListContainer.getYPos();
		break;
	case eSectionInventoryCreativeUsing:
		pPosition->x = m_slotListHotbar.getXPos();
		pPosition->y = m_slotListHotbar.getYPos();
		break;
	default:
		if(UIControl *c = HS_SectionControl(eSection))
		{
			pPosition->x = c->getXPos();
			pPosition->y = c->getYPos();
			break;
		}
		assert( false );
		break;
	}
}

void UIScene_CreativeMenu::GetItemScreenData( ESceneSection eSection, int iItemIndex, UIVec2D* pPosition, UIVec2D* pSize )
{
	UIVec2D sectionSize;

	switch( eSection )
	{
	case eSectionInventoryCreativeSelector:
		sectionSize.x = m_slotListContainer.getWidth();
		sectionSize.y = m_slotListContainer.getHeight();
		break;
	case eSectionInventoryCreativeUsing:
		sectionSize.x = m_slotListHotbar.getWidth();
		sectionSize.y = m_slotListHotbar.getHeight();
		break;
	default:
		if(UIControl *c = HS_SectionControl(eSection))
		{
			sectionSize.x = c->getWidth();
			sectionSize.y = c->getHeight();
			break;
		}
		assert( false );
		break;
	}

	if(IsSectionSlotList(eSection))
	{
		int rows = getSectionRows(eSection);
		int cols = getSectionColumns(eSection);

		pSize->x = sectionSize.x/cols;
		pSize->y = sectionSize.y/rows;

		int itemCol = iItemIndex % cols;
		int itemRow = iItemIndex/cols;

		pPosition->x = itemCol * pSize->x;
		pPosition->y = itemRow * pSize->y;
	}
	else
	{
		GetPositionOfSection(eSection, pPosition);
		pSize->x = sectionSize.x;
		pSize->y = sectionSize.y;
	}
}

void UIScene_CreativeMenu::setSectionSelectedSlot(ESceneSection eSection, int x, int y)
{
	int cols = getSectionColumns(eSection);

	int index = (y * cols) + x;

	UIControl_SlotList *slotList = nullptr;
	switch( eSection )
	{
	case eSectionInventoryCreativeSelector:
		slotList = &m_slotListContainer;
		break;
	case eSectionInventoryCreativeUsing:
		slotList = &m_slotListHotbar;
		break;
	default:
		assert( false );
		break;
	}

	slotList->setHighlightSlot(index);
}

UIControl *UIScene_CreativeMenu::getSection(ESceneSection eSection)
{
	UIControl *control = nullptr;
	switch( eSection )
	{
	case eSectionInventoryCreativeSelector:
		control = &m_slotListContainer;
		break;
	case eSectionInventoryCreativeUsing:
		control = &m_slotListHotbar;
		break;
	default:
		control = HS_SectionControl(eSection);
		assert( control != nullptr );
		break;
	}
	return control;
}

void UIScene_CreativeMenu::updateScrollCurrentPage(int currentPage, int pageCount)
{
	IggyDataValue result;
	IggyDataValue value[2];

	value[0].type = IGGY_DATATYPE_number;
	value[0].number = static_cast<F64>(pageCount);

	value[1].type = IGGY_DATATYPE_number;
	value[1].number = static_cast<F64>(currentPage) - 1;

	IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcSetScrollBar , 2 , value );
}