#include "stdafx.h"
#include "UI.h"
#include "UIScene_AbstractContainerMenu.h"
#include "UISplitScreenHelpers.h"

#include "../../../Minecraft.World/net.minecraft.world.inventory.h"
#include "../../../Minecraft.World/net.minecraft.world.item.h"
#include "../../MultiPlayerLocalPlayer.h"
#include "../../MultiPlayerGameMode.h"
#ifdef _WINDOWS64
#include "../../Windows64/KeyboardMouseInput.h"
#include "Client/RecipeViewer/RecipeViewer.h"	// Hellish Sight: JEI-style recipe viewer
#include "Client/MachineGui/MachineGui.h"
#endif

UIScene_AbstractContainerMenu::UIScene_AbstractContainerMenu(int iPad, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	m_focusSection = eSectionNone;
	// in this scene, we override the press sound with our own for crafting success or fail
	ui.OverrideSFX(m_iPad,ACTION_MENU_A,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_OK,true);
#ifdef __ORBIS__
	ui.OverrideSFX(m_iPad,ACTION_MENU_TOUCHPAD_PRESS,true);
#endif
	ui.OverrideSFX(m_iPad,ACTION_MENU_X,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_Y,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT_SCROLL,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT_SCROLL,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_UP,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_DOWN,true);

	m_bIgnoreInput=false;
}

UIScene_AbstractContainerMenu::~UIScene_AbstractContainerMenu()
{
	app.DebugPrintf("UIScene_AbstractContainerMenu::~UIScene_AbstractContainerMenu\n");
}

void UIScene_AbstractContainerMenu::handleDestroy()
{
	app.DebugPrintf("UIScene_AbstractContainerMenu::handleDestroy\n");

#ifdef _WINDOWS64
	g_KBMInput.SetScreenCursorHidden(false);
	g_KBMInput.SetCursorHiddenForUI(false);
	HSRecipeViewer::sceneClosed(m_iPad);
	HSMachineGui::clear(m_iPad);
#endif

	Minecraft *pMinecraft = Minecraft::GetInstance();
	if( pMinecraft->localgameModes[m_iPad] != nullptr )
	{
		TutorialMode *gameMode = static_cast<TutorialMode *>(pMinecraft->localgameModes[m_iPad]);
		if(gameMode != nullptr) gameMode->getTutorial()->changeTutorialState(m_previousTutorialState);
	}

	// 4J Stu - Fix for #11302 - TCR 001: Network Connectivity: Host crashed after being killed by the client while accessing a chest during burst packet loss.
	// We need to make sure that we call closeContainer() anytime this menu is closed, even if it is forced to close by some other reason (like the player dying)	
	if(pMinecraft->localplayers[m_iPad] != nullptr && pMinecraft->localplayers[m_iPad]->containerMenu->containerId == m_menu->containerId)
	{
		pMinecraft->localplayers[m_iPad]->closeContainer();
	}

	ui.OverrideSFX(m_iPad,ACTION_MENU_A,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_OK,false);
#ifdef __ORBIS__
	ui.OverrideSFX(m_iPad,ACTION_MENU_TOUCHPAD_PRESS,false);
#endif
	ui.OverrideSFX(m_iPad,ACTION_MENU_X,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_Y,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT_SCROLL,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT_SCROLL,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_UP,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_DOWN,false);
}

void UIScene_AbstractContainerMenu::InitDataAssociations(int iPad, AbstractContainerMenu *menu, int startIndex)
{
}

void UIScene_AbstractContainerMenu::PlatformInitialize(int iPad, int startIndex)
{
#ifdef _WINDOWS64
	g_KBMInput.SetScreenCursorHidden(true);
	g_KBMInput.SetCursorHiddenForUI(true);
#endif

	m_labelInventory.init( app.GetString(IDS_INVENTORY) );

	if(startIndex >= 0)
	{
		m_slotListInventory.addSlots(startIndex, 27);
		m_slotListHotbar.addSlots(startIndex + 27, 9);
	}

	// Determine min and max extents for pointer, it needs to be able to move off the container to drop items.
	float fPanelWidth, fPanelHeight;
	float fPanelX, fPanelY;
	float fPointerWidth, fPointerHeight;

	// We may have varying depths of controls here, so base off the pointers parent
#if TO_BE_IMPLEMENTED
	HXUIOBJ parent;
	XuiElementGetBounds( m_pointerControl->m_hObj, &fPointerWidth, &fPointerHeight );	
#else
	fPointerWidth = 50;
	fPointerHeight = 50;
#endif

	fPanelWidth = m_controlBackgroundPanel.getWidth();
	fPanelHeight = m_controlBackgroundPanel.getHeight();
	fPanelX = m_controlBackgroundPanel.getXPos();
	fPanelY = m_controlBackgroundPanel.getYPos();
	// Get size of pointer
	m_fPointerImageOffsetX = 0; //floor(fPointerWidth/2.0f);
	m_fPointerImageOffsetY = 0; //floor(fPointerHeight/2.0f);
	
	m_fPanelMinX = fPanelX;
	m_fPanelMaxX = fPanelX + fPanelWidth;
	m_fPanelMinY = fPanelY;
	m_fPanelMaxY = fPanelY + fPanelHeight;

#ifdef __ORBIS__
	// we need to map the touchpad rectangle to the UI rectangle. While it works great for the creative menu, it is much too sensitive for the smaller menus.
	//X coordinate of the touch point (0 to 1919)    
	//Y coordinate of the touch point (0 to 941: DUALSHOCK�4 wireless controllers and the CUH-ZCT1J/CAP-ZCT1J/CAP-ZCT1U controllers for the PlayStation�4 development tool,    
	//0 to 753: JDX-1000x series controllers for the PlayStation�4 development tool,)     
	m_fTouchPadMulX=fPanelWidth/1919.0f;
	m_fTouchPadMulY=fPanelHeight/941.0f;
	m_fTouchPadDeadZoneX=15.0f*m_fTouchPadMulX;
	m_fTouchPadDeadZoneY=15.0f*m_fTouchPadMulY;

#endif

	// 4J-PB - need to limit this in splitscreen
	if(app.GetLocalPlayerCount()>1)
	{
		// don't let the pointer go into someone's screen
		m_fPointerMinY = floor(fPointerHeight/2.0f);
	}
	else
	{
		m_fPointerMinY = fPanelY -fPointerHeight;
	}
	m_fPointerMinX = fPanelX - fPointerWidth;
	m_fPointerMaxX = m_fPanelMaxX + fPointerWidth;
	m_fPointerMaxY = m_fPanelMaxY + (fPointerHeight/2);

// 	m_hPointerText=nullptr;
// 	m_hPointerTextBkg=nullptr;

	// Put the pointer over first item in use row to start with.
	UIVec2D itemPos;
	UIVec2D itemSize;
	GetItemScreenData( m_eCurrSection, 0, &( itemPos ), &( itemSize ) );

	UIVec2D sectionPos;
	GetPositionOfSection( m_eCurrSection, &( sectionPos ) );

	UIVec2D vPointerPos = sectionPos;
	vPointerPos += itemPos;
	vPointerPos.x += ( itemSize.x / 2.0f );
	vPointerPos.y += ( itemSize.y / 2.0f );

	vPointerPos.x -= m_fPointerImageOffsetX;
	vPointerPos.y -= m_fPointerImageOffsetY;

	//m_pointerControl->SetPosition( &vPointerPos );
	m_pointerPos = vPointerPos;

#ifdef _WINDOWS64
	if ((iPad == 0) && g_KBMInput.IsKBMActive())
	{
		m_pointerPos.x = ((m_fPanelMinX + m_fPanelMaxX) * 0.5f) - m_fPointerImageOffsetX;
		m_pointerPos.y = ((m_fPanelMinY + m_fPanelMaxY) * 0.5f) - m_fPointerImageOffsetY;
	}

	if (m_pointerPos.x < m_fPointerMinX) m_pointerPos.x = m_fPointerMinX;
	if (m_pointerPos.x > m_fPointerMaxX) m_pointerPos.x = m_fPointerMaxX;
	if (m_pointerPos.y < m_fPointerMinY) m_pointerPos.y = m_fPointerMinY;
	if (m_pointerPos.y > m_fPointerMaxY) m_pointerPos.y = m_fPointerMaxY;
#endif

	IggyEvent mouseEvent;
	S32 width, height;
	m_parentLayer->getRenderDimensions(width, height);

	C4JRender::eViewportType vp = m_parentLayer->getViewport();
	if(vp != C4JRender::VIEWPORT_TYPE_FULLSCREEN)
		Fit16x9(width, height);

	S32 x = m_pointerPos.x*(static_cast<float>(width)/m_movieWidth);
	S32 y = m_pointerPos.y*(static_cast<float>(height)/m_movieHeight);
	IggyMakeEventMouseMove( &mouseEvent, x, y);

	IggyEventResult result;
	IggyPlayerDispatchEventRS ( getMovie() , &mouseEvent , &result );

#ifdef USE_POINTER_ACCEL
	m_fPointerVelX = 0.0f;
	m_fPointerVelY = 0.0f;
	m_fPointerAccelX = 0.0f;
	m_fPointerAccelY = 0.0f;
#endif
}

void UIScene_AbstractContainerMenu::tick()
{
	UIScene::tick();

#ifdef _WINDOWS64
	// Hellish Sight: the recipe viewer lives around the panel, so the pointer may go anywhere
	const bool hsViewer = HSRecipeViewer::enabledFor(m_iPad) && m_parentLayer->getViewport() == C4JRender::VIEWPORT_TYPE_FULLSCREEN;
	if (hsViewer && HSRecipeViewer::wantsFreePointer())
	{
		// pointer coordinates are local to MainPanel
		const float mainX = static_cast<float>(m_controlMainPanel.getXPos());
		const float mainY = static_cast<float>(m_controlMainPanel.getYPos());
		m_fPointerMinX = -mainX;
		m_fPointerMinY = -mainY;
		m_fPointerMaxX = static_cast<float>(m_movieWidth) - mainX;
		m_fPointerMaxY = static_cast<float>(m_movieHeight) - mainY;
	}
#endif

	onMouseTick();

#ifdef _WINDOWS64
	if (hsViewer && m_movieWidth > 0 && m_movieHeight > 0)
	{
		S32 rw, rh;
		m_parentLayer->getRenderDimensions(rw, rh);
		const float sx = static_cast<float>(rw) / m_movieWidth, sy = static_cast<float>(rh) / m_movieHeight;

		HSRecipeViewer::SceneInfo info;
		info.pad = m_iPad;
		info.screenW = rw;
		info.screenH = rh;
		// the pointer, the panel and the slots are all in MainPanel's local space
		const float mainX = static_cast<float>(m_controlMainPanel.getXPos());
		const float mainY = static_cast<float>(m_controlMainPanel.getYPos());
		info.panelX0 = (m_fPanelMinX + mainX) * sx;
		info.panelX1 = (m_fPanelMaxX + mainX) * sx;
		info.panelY0 = (m_fPanelMinY + mainY) * sy;
		info.panelY1 = (m_fPanelMaxY + mainY) * sy;
		info.pointerX = (m_pointerPos.x + m_fPointerImageOffsetX + mainX) * sx;
		info.pointerY = (m_pointerPos.y + m_fPointerImageOffsetY + mainY) * sy;
		info.hovered = HS_HoveredItem();
		if (getSceneType() == eUIScene_ClassicCraftingMenu) info.craftGrid = 3;
		else if (getSceneType() == eUIScene_InventoryMenu && app.GetGameSettings(m_iPad, eGameSetting_ClassicCrafting)) info.craftGrid = 2;
		Minecraft *mc = Minecraft::GetInstance();
		info.creative = mc->localgameModes[m_iPad] != nullptr && mc->localgameModes[m_iPad]->hasInfiniteItems();
		HSRecipeViewer::updateScene(info);
	}
#endif

	IggyEvent mouseEvent;
	S32 width, height;
	m_parentLayer->getRenderDimensions(width, height);

	C4JRender::eViewportType vp = m_parentLayer->getViewport();
	if(vp != C4JRender::VIEWPORT_TYPE_FULLSCREEN)
		Fit16x9(width, height);

	S32 x = static_cast<S32>(m_pointerPos.x * (static_cast<float>(width) / m_movieWidth));
	S32 y = static_cast<S32>(m_pointerPos.y * (static_cast<float>(height) / m_movieHeight));

	IggyMakeEventMouseMove( &mouseEvent, x, y);

	// 4J Stu - This seems to be broken on Durango, so do it ourself
#ifdef _DURANGO
	//mouseEvent.x = x;
	//mouseEvent.y = y;
#endif

	IggyEventResult result;
	IggyPlayerDispatchEventRS ( getMovie() , &mouseEvent , &result );
}

void UIScene_AbstractContainerMenu::render(S32 width, S32 height, C4JRender::eViewportType viewpBort)
{
	m_cacheSlotRenders = true;

	m_needsCacheRendered = m_needsCacheRendered || m_menu->needsRendered();

	if(m_needsCacheRendered)
	{
		m_expectedCachedSlotCount = GetBaseSlotCount();
		unsigned int count = m_menu->getSize();
		for(unsigned int i = 0; i < count; ++i)
		{
			if(m_menu->getSlot(i)->hasItem())
			{
				++m_expectedCachedSlotCount;
			}
		}
	}

	UIScene::render(width, height, viewpBort);

	m_needsCacheRendered = false;
}

void UIScene_AbstractContainerMenu::getMouseToSWFScale(float &scaleX, float &scaleY)
{
	extern HWND g_hWnd;
	RECT rc;
	GetClientRect(g_hWnd, &rc);
	int winW = rc.right - rc.left;
	int winH = rc.bottom - rc.top;
	if(winW <= 0 || winH <= 0) { scaleX = 1.0f; scaleY = 1.0f; return; }

	S32 renderW, renderH;
	C4JRender::eViewportType vp = GetParentLayer()->getViewport();
	ui.getRenderDimensions(vp, renderW, renderH);
	if(vp != C4JRender::VIEWPORT_TYPE_FULLSCREEN)
		Fit16x9(renderW, renderH);

	float screenW = (float)ui.getScreenWidth();
	float screenH = (float)ui.getScreenHeight();
	scaleX = static_cast<float>(m_movieWidth) * screenW / (static_cast<float>(renderW) * static_cast<float>(winW));
	scaleY = static_cast<float>(m_movieHeight) * screenH / (static_cast<float>(renderH) * static_cast<float>(winH));
}

void UIScene_AbstractContainerMenu::customDraw(IggyCustomDrawCallbackRegion *region)
{
	Minecraft *pMinecraft = Minecraft::GetInstance();
	if(pMinecraft->localplayers[m_iPad] == nullptr || pMinecraft->localgameModes[m_iPad] == nullptr) return;

	shared_ptr<ItemInstance> item = nullptr;
	int slotId = -1;
	if(wcscmp((wchar_t *)region->name,L"pointerIcon")==0)
	{		
		m_cacheSlotRenders = false;
		item = pMinecraft->localplayers[m_iPad]->inventory->getCarried();
	}
	else
	{
		swscanf(static_cast<wchar_t *>(region->name),L"slot_%d",&slotId);
		if (slotId == -1)
		{
			app.DebugPrintf("This is not the control we are looking for\n");
		}
		else
		{			
			m_cacheSlotRenders = true;
			Slot *slot = m_menu->getSlot(slotId);
			item = slot->getItem();
		}
	}

	if(item != nullptr) customDrawSlotControl(region,m_iPad,item,m_menu->isValidIngredient(item, slotId)?1.0f:0.5f,item->isFoil(),true);
}

void UIScene_AbstractContainerMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	if(m_bIgnoreInput) return;

#ifdef _WINDOWS64
	// Hellish Sight: clicks / keys over the recipe viewer belong to it
	if (HSRecipeViewer::handleAction(iPad, key, pressed, released, repeat))
	{
		handled = true;
		return;
	}
#endif

	//app.DebugPrintf("UIScene_InventoryMenu handling input for pad %d, key %d, down- %s, pressed- %s, released- %s\n", iPad, key, down?"TRUE":"FALSE", pressed?"TRUE":"FALSE", released?"TRUE":"FALSE");
	ui.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

	if(pressed)
	{
		m_bPointerDrivenByMouse = false;
		handled = handleKeyDown(m_iPad, key, repeat);
	}
}

void UIScene_AbstractContainerMenu::SetPointerText(vector<HtmlString> *description, bool newSlot)
{
	m_cursorPath.setLabel(HtmlString::Compose(description), false, newSlot);
}

void UIScene_AbstractContainerMenu::setSectionFocus(ESceneSection eSection, int iPad)
{
	UIControl *newFocus = getSection(eSection);
	if(newFocus) newFocus->setFocus(true);

	if(m_focusSection != eSectionNone)
	{
		UIControl *currentFocus = getSection(m_focusSection);
		// 4J-TomK only set current focus to false if it differs from last (previously this continuously fired iggy functions when they were identical!
		if(currentFocus != newFocus)
			if(currentFocus) currentFocus->setFocus(false);
	}

	m_focusSection = eSection;
}

void UIScene_AbstractContainerMenu::setFocusToPointer(int iPad)
{
	if(m_focusSection != eSectionNone)
	{
		UIControl *currentFocus = getSection(m_focusSection);
		if(currentFocus) currentFocus->setFocus(false);
	}
	m_focusSection = eSectionNone;
}

shared_ptr<ItemInstance> UIScene_AbstractContainerMenu::getSlotItem(ESceneSection eSection, int iSlot)
{
	Slot *slot = m_menu->getSlot( getSectionStartOffset(eSection) + iSlot );
	if(slot) return slot->getItem();
	else return nullptr;
}

Slot *UIScene_AbstractContainerMenu::getSlot(ESceneSection eSection, int iSlot)
{
	Slot *slot = m_menu->getSlot( getSectionStartOffset(eSection) + iSlot );
	if(slot) return slot;
	else return nullptr;
}

bool UIScene_AbstractContainerMenu::isSlotEmpty(ESceneSection eSection, int iSlot)
{
	Slot *slot = m_menu->getSlot( getSectionStartOffset(eSection) + iSlot );
	if(slot) return !slot->hasItem();
	else return false;
}

void UIScene_AbstractContainerMenu::adjustPointerForSafeZone()
{
	// Handled by AS
}

// Hellish Sight: the container item under the pointer (for the recipe viewer's R / U keys)
shared_ptr<ItemInstance> UIScene_AbstractContainerMenu::HS_HoveredItem()
{
	if (m_eCurrSection == eSectionNone || !IsSectionSlotList(m_eCurrSection)) return nullptr;
	const int index = getCurrentIndex(m_eCurrSection) - getSectionStartOffset(m_eCurrSection);
	if (index < 0) return nullptr;

	UIVec2D sectionPos, itemPos, itemSize;
	GetPositionOfSection(m_eCurrSection, &sectionPos);
	GetItemScreenData(m_eCurrSection, index, &itemPos, &itemSize);
	itemPos += sectionPos;
	const float px = m_pointerPos.x + m_fPointerImageOffsetX, py = m_pointerPos.y + m_fPointerImageOffsetY;
	if (px < itemPos.x || py < itemPos.y || px > itemPos.x + itemSize.x || py > itemPos.y + itemSize.y) return nullptr;
	return getSlotItem(m_eCurrSection, index);
}

// Hellish Sight: bounds of everything the container shows (slots, tabs, buttons) plus the panel border,
// in pointer (movie) coordinates. Used to keep the recipe viewer's item list beside the panel.
void UIScene_AbstractContainerMenu::HS_ContentBounds(float &x0, float &y0, float &x1, float &y1)
{
	x0 = y0 = 1e9f;
	x1 = y1 = -1e9f;
	float slotW = 0.0f;
	for (int iSection = m_eFirstSection; iSection < m_eMaxSection; ++iSection)
	{
		const ESceneSection eSection = static_cast<ESceneSection>(iSection);
		if (!IsVisible(eSection)) continue;
		UIVec2D sectionPos;
		GetPositionOfSection(eSection, &sectionPos);
		int count = 1;
		if (IsSectionSlotList(eSection))
		{
			int cols = 0, rows = 0;
			count = GetSectionDimensions(eSection, &cols, &rows);
		}
		for (int i = 0; i < count && i < 256; ++i)
		{
			UIVec2D pos, size;
			GetItemScreenData(eSection, i, &pos, &size);
			pos += sectionPos;
			if (size.x <= 0.0f || size.y <= 0.0f) continue;
			x0 = (std::min)(x0, pos.x);
			y0 = (std::min)(y0, pos.y);
			x1 = (std::max)(x1, pos.x + size.x);
			y1 = (std::max)(y1, pos.y + size.y);
			if (IsSectionSlotList(eSection)) slotW = (std::max)(slotW, size.x);
		}
	}
	if (x1 < x0)
	{
		// nothing found: centred panel
		const float w = m_fPanelMaxX - m_fPanelMinX, h = m_fPanelMaxY - m_fPanelMinY;
		x0 = (m_movieWidth - w) * 0.5f; x1 = x0 + w;
		y0 = (m_movieHeight - h) * 0.5f; y1 = y0 + h;
		return;
	}
	const float border = slotW > 0.0f ? slotW * 0.6f : 16.0f;
	x0 -= border; y0 -= border; x1 += border; y1 += border;
}
