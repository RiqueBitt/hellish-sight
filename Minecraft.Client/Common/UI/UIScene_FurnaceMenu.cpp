#include "stdafx.h"
#include "UI.h"
#include "../../../Minecraft.World/net.minecraft.world.level.tile.entity.h"
#include "../../../Minecraft.World/net.minecraft.world.inventory.h"
#include "../../Minecraft.h"
#include "UIScene_FurnaceMenu.h"
#include "Client/ModMachineScreen.h"
#ifdef _WINDOWS64
#include "Client/MachineGui/MachineGui.h"
#endif

UIScene_FurnaceMenu::UIScene_FurnaceMenu(int iPad, void *_initData, UILayer *parentLayer) : UIScene_AbstractContainerMenu(iPad, parentLayer)
{
	// Setup all the Iggy references we need for this scene
	initialiseMovie();

	FurnaceScreenInput *initData = static_cast<FurnaceScreenInput *>(_initData);
	m_furnace = initData->furnace;

	m_labelFurnace.init(m_furnace->getName());
	m_labelIngredient.init(app.GetString(IDS_INGREDIENT));
	m_labelFuel.init(app.GetString(IDS_FUEL));

	m_progressFurnaceFire.init(L"",0,0,12,0);
	m_progressFurnaceArrow.init(L"",0,0,24,0);

	Minecraft *pMinecraft = Minecraft::GetInstance();
	if( pMinecraft->localgameModes[initData->iPad] != nullptr )
	{
		TutorialMode *gameMode = static_cast<TutorialMode *>(pMinecraft->localgameModes[initData->iPad]);
		m_previousTutorialState = gameMode->getTutorial()->getCurrentState();
		gameMode->getTutorial()->changeTutorialState(e_Tutorial_State_Furnace_Menu, this);
	}

	FurnaceMenu* menu = new FurnaceMenu( initData->inventory, initData->furnace );

	Initialize( initData->iPad, menu, true, FurnaceMenu::INV_SLOT_START, eSectionFurnaceUsing, eSectionFurnaceMax );

	m_slotListFuel.addSlots(FurnaceMenu::FUEL_SLOT, 1);
	m_slotListIngredient.addSlots(FurnaceMenu::INGREDIENT_SLOT, 1);
	m_slotListResult.addSlots(FurnaceMenu::RESULT_SLOT, 1);

	app.SetRichPresenceContext(m_iPad,CONTEXT_GAME_STATE_FORGING);

	delete initData;
}

wstring UIScene_FurnaceMenu::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1)
	{
		return L"FurnaceMenuSplit";
	}
	else
	{
		return L"FurnaceMenu";
	}
}

void UIScene_FurnaceMenu::handleReload()
{
	Initialize( m_iPad, m_menu, true, FurnaceMenu::INV_SLOT_START, eSectionFurnaceUsing, eSectionFurnaceMax );

	m_slotListFuel.addSlots(FurnaceMenu::FUEL_SLOT, 1);
	m_slotListIngredient.addSlots(FurnaceMenu::INGREDIENT_SLOT, 1);
	m_slotListResult.addSlots(FurnaceMenu::RESULT_SLOT, 1);
}

void UIScene_FurnaceMenu::tick()
{
	m_progressFurnaceFire.setProgress( m_furnace->getLitProgress( 12 ) );
	m_progressFurnaceArrow.setProgress( m_furnace->getBurnProgress( 24 ) );
	HS_UpdateMachineScreen();
	UIScene_AbstractContainerMenu::tick();
}

namespace
{
	std::wstring hsNum(long long v)
	{
		wchar_t buf[32];
		if (v >= 1000000000LL) swprintf(buf, 32, L"%.2fG", v / 1e9);
		else if (v >= 1000000LL) swprintf(buf, 32, L"%.2fM", v / 1e6);
		else if (v >= 10000LL) swprintf(buf, 32, L"%.1fk", v / 1e3);
		else swprintf(buf, 32, L"%lld", v);
		return buf;
	}
}

// Hellish Sight: the furnace screen doubles as the screen of every mod machine. The
// server fills FurnaceTileEntity::hsData (see ModMachine.h); here it becomes a side
// panel of Mekanism-style gauges plus three status lines.
void UIScene_FurnaceMenu::HS_UpdateMachineScreen()
{
	const int *d = m_furnace->hsData;
	if (d[HSD_KIND] <= 0) return;
	const int kind = d[HSD_KIND] - 1;

	std::string spec;
#ifdef _WINDOWS64
	HSMachineGui::Screen gui;	// Hellish Sight: the mod's own GUI sprites, drawn over the screen
#endif
	auto addBar = [&](long long value, long long max, int color, int barKind) {
		if (max <= 0) return;
		const int frac = static_cast<int>((std::min)(1000LL, value * 1000 / max));
		char buf[64];
		sprintf_s(buf, "%d,%d,%d", frac, color, barKind);
		if (!spec.empty()) spec += ";";
		spec += buf;
#ifdef _WINDOWS64
		HSMachineGui::Bar bar;
		bar.frac = frac / 1000.0f;
		bar.color = color;
		bar.kind = barKind;
		bar.tooltip = hsNum(value) + L" / " + hsNum(max) + (barKind == 0 ? L" FE" : (barKind == 1 ? L" mB" : L" K"));
		gui.bars.push_back(bar);
#endif
	};

	const long long energy = hsGetBig(d, HSD_ENERGY), energyCap = hsGetBig(d, HSD_ENERGY_CAP);
	const long long tankCap = hsGetBig(d, HSD_TANK_CAP);
	const long long heat = hsGetBig(d, HSD_HEAT);
	const int heatMax = d[HSD_HEAT_MAX] * 10;

	std::wstring line1, line2, line3;
	if (energyCap > 0)
	{
		addBar(energy, energyCap, 0x3CC83C, 0);
		line1 = L"Energia: " + hsNum(energy) + L" / " + hsNum(energyCap) + L" FE";
	}
	int tanksShown = 0;
	for (int t = 0; t < 4; t++)
	{
		const int base = HSD_TANK0 + t * 4;
		const int chem = d[base] - 1;
		const long long amount = hsGetBig(d, base + 1);
		std::wstring name;
		int color = 0x4060FF;
		if (chem < 0 || amount <= 0 || !ModMachines::chemicalByIndex(chem, name, color))
		{
			// an empty tank still shows for tanks / pipes so the player sees the capacity
			if (t == 0 && (kind == MachineKind_Tank) && tankCap > 0) addBar(0, tankCap, 0x4060FF, 1);
			continue;
		}
		addBar(amount, tankCap, color, 1);
		std::wstring part = name + L" " + hsNum(amount) + L"/" + hsNum(tankCap) + L" mB";
		std::wstring &target = tanksShown < 2 ? line2 : line3;
		if (!target.empty()) target += L" | ";
		target += part;
		tanksShown++;
	}
	if (heatMax > 0)
	{
		addBar((std::max)(0LL, heat - 300), (std::max)(1, heatMax - 300), heat > heatMax * 0.6 ? 0xFF3C1E : 0xFF9628, 2);
		std::wstring h = L"Temperatura: " + std::to_wstring(heat) + L" K";
		if (kind == MachineKind_Fission)
		{
			h += L" | Dano " + std::to_wstring(d[HSD_DAMAGE]) + L"% | " + ((d[HSD_EXTRA] & 0x4000) ? L"LIGADO " : L"desligado ") + std::to_wstring(d[HSD_EXTRA] & 0x3FFF) + L" mB/t";
		}
		if (line1.empty()) line1 = h;
		else if (line3.empty()) line3 = h;
		else line3 += L" | " + h;
	}

	// structure / misc info goes to the last free line
	std::wstring info;
	if (d[HSD_FORMED] == 1) info = (kind == MachineKind_Teleporter) ? L"Moldura do teleporter incompleta" : L"Estrutura incompleta";
	else if (d[HSD_FORMED] == 2 && kind != MachineKind_Teleporter) info = L"Estrutura formada (" + std::to_wstring(d[HSD_MB_VOLUME]) + L" blocos)";
	if (kind == MachineKind_Fusion) info += std::wstring(info.empty() ? L"" : L" | ") + (d[HSD_EXTRA] ? L"Plasma aceso" : L"Plasma apagado");
	if (kind == MachineKind_Miner) info += std::wstring(info.empty() ? L"" : L" | ") + L"Varredura " + std::to_wstring(d[HSD_EXTRA]) + L"%";
	if (kind == MachineKind_Teleporter || kind == MachineKind_Entangloporter || (kind >= MachineKind_QioDrive && kind <= MachineKind_QioExporter))
		info += std::wstring(info.empty() ? L"" : L" | ") + L"Frequ\u00EAncia " + std::to_wstring(d[HSD_FREQ]);
	const int up = d[HSD_UPGRADES];
	if (up != 0)
	{
		info += std::wstring(info.empty() ? L"" : L" | ") + L"Upgrades V" + std::to_wstring(up & 0xF) + L" E" + std::to_wstring((up >> 4) & 0xF) + L" S" + std::to_wstring((up >> 8) & 0xF);
		if ((up >> 12) & 1) info += L" F";
	}
	if (!info.empty())
	{
		if (line2.empty()) line2 = info;
		else if (line3.empty()) line3 = info;
		else line3 += L" | " + info;
	}

#ifdef _WINDOWS64
	if (HSMachineGui::available())
	{
		S32 rw, rh;
		m_parentLayer->getRenderDimensions(rw, rh);
		if (m_movieWidth > 0 && m_movieHeight > 0)
		{
			const float sx = static_cast<float>(rw) / m_movieWidth, sy = static_cast<float>(rh) / m_movieHeight;
			const float mainX = static_cast<float>(m_controlMainPanel.getXPos()), mainY = static_cast<float>(m_controlMainPanel.getYPos());
			gui.pad = m_iPad;
			gui.screenW = rw;
			gui.screenH = rh;
			gui.panelX0 = (m_fPanelMinX + mainX) * sx;
			gui.panelX1 = (m_fPanelMaxX + mainX) * sx;
			gui.panelY0 = (m_fPanelMinY + mainY) * sy;
			gui.panelY1 = (m_fPanelMaxY + mainY) * sy;
			gui.pointerX = (m_pointerPos.x + m_fPointerImageOffsetX + mainX) * sx;
			gui.pointerY = (m_pointerPos.y + m_fPointerImageOffsetY + mainY) * sy;
			gui.progress = d[HSD_PROGRESS];
			HSMachineGui::update(gui);
			spec.clear();	// the sprites replace the drawn fallback panel
		}
	}
#endif
	if (spec != m_hsLastSpec)
	{
		m_hsLastSpec = spec;
		IggyDataValue result;
		IggyDataValue value[1];
		std::wstring w(spec.begin(), spec.end());
		IggyStringUTF16 str;
		str.string = (IggyUTF16*)w.c_str();
		str.length = static_cast<int>(w.length());
		value[0].type = IGGY_DATATYPE_string_UTF16;
		value[0].string16 = str;
		IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcSetMachinePanel, 1, value);
	}
	if (line1 != m_hsLastIngredient) { m_hsLastIngredient = line1; m_labelIngredient.setLabel(line1.empty() ? app.GetString(IDS_INGREDIENT) : line1.c_str()); }
	if (line2 != m_hsLastFuel) { m_hsLastFuel = line2; m_labelFuel.setLabel(line2.empty() ? app.GetString(IDS_FUEL) : line2.c_str()); }
	if (line3 != m_hsLastInventory) { m_hsLastInventory = line3; m_labelInventory.setLabel(line3.empty() ? app.GetString(IDS_INVENTORY) : line3.c_str()); }
}

int UIScene_FurnaceMenu::getSectionColumns(ESceneSection eSection)
{
	int cols = 0;
	switch( eSection )
	{
	case eSectionFurnaceResult:
		cols = 1;
		break;
	case eSectionFurnaceFuel:
		cols = 1;
		break;
	case eSectionFurnaceIngredient:
		cols = 1;
		break;
	case eSectionFurnaceInventory:
		cols = 9;
		break;
	case eSectionFurnaceUsing:
		cols = 9;
		break;
	default:
		assert( false );
		break;
	}
	return cols;
}

int UIScene_FurnaceMenu::getSectionRows(ESceneSection eSection)
{
	int rows = 0;
	switch( eSection )
	{
	case eSectionFurnaceResult:
		rows = 1;
		break;
	case eSectionFurnaceFuel:
		rows = 1;
		break;
	case eSectionFurnaceIngredient:
		rows = 1;
		break;
	case eSectionFurnaceInventory:
		rows = 3;
		break;
	case eSectionFurnaceUsing:
		rows = 1;
		break;
	default:
		assert( false );
		break;
	}
	return rows;
}

void UIScene_FurnaceMenu::GetPositionOfSection( ESceneSection eSection, UIVec2D* pPosition )
{
	switch( eSection )
	{
	case eSectionFurnaceResult:
		pPosition->x = m_slotListResult.getXPos();
		pPosition->y = m_slotListResult.getYPos();
		break;
	case eSectionFurnaceFuel:
		pPosition->x = m_slotListFuel.getXPos();
		pPosition->y = m_slotListFuel.getYPos();
		break;
	case eSectionFurnaceIngredient:
		pPosition->x = m_slotListIngredient.getXPos();
		pPosition->y = m_slotListIngredient.getYPos();
		break;
	case eSectionFurnaceInventory:
		pPosition->x = m_slotListInventory.getXPos();
		pPosition->y = m_slotListInventory.getYPos();
		break;
	case eSectionFurnaceUsing:
		pPosition->x = m_slotListHotbar.getXPos();
		pPosition->y = m_slotListHotbar.getYPos();
		break;
	default:
		assert( false );
		break;
	}
}

void UIScene_FurnaceMenu::GetItemScreenData( ESceneSection eSection, int iItemIndex, UIVec2D* pPosition, UIVec2D* pSize )
{
	UIVec2D sectionSize;
	switch( eSection )
	{
	case eSectionFurnaceResult:
		sectionSize.x = m_slotListResult.getWidth();
		sectionSize.y = m_slotListResult.getHeight();
		break;
	case eSectionFurnaceFuel:
		sectionSize.x = m_slotListFuel.getWidth();
		sectionSize.y = m_slotListFuel.getHeight();
		break;
	case eSectionFurnaceIngredient:
		sectionSize.x = m_slotListIngredient.getWidth();
		sectionSize.y = m_slotListIngredient.getHeight();
		break;
	case eSectionFurnaceInventory:
		sectionSize.x = m_slotListInventory.getWidth();
		sectionSize.y = m_slotListInventory.getHeight();
		break;
	case eSectionFurnaceUsing:
		sectionSize.x = m_slotListHotbar.getWidth();
		sectionSize.y = m_slotListHotbar.getHeight();
		break;
	default:
		assert( false );
		break;
	}

	int rows = getSectionRows(eSection);
	int cols = getSectionColumns(eSection);

	pSize->x = sectionSize.x/cols;
	pSize->y = sectionSize.y/rows;

	int itemCol = iItemIndex % cols;
	int itemRow = iItemIndex/cols;

	pPosition->x = itemCol * pSize->x;
	pPosition->y = itemRow * pSize->y;
}

void UIScene_FurnaceMenu::setSectionSelectedSlot(ESceneSection eSection, int x, int y)
{
	int cols = getSectionColumns(eSection);

	int index = (y * cols) + x;

	UIControl_SlotList *slotList = nullptr;
	switch( eSection )
	{
	case eSectionFurnaceResult:
		slotList = &m_slotListResult;
		break;
	case eSectionFurnaceFuel:
		slotList = &m_slotListFuel;
		break;
	case eSectionFurnaceIngredient:
		slotList = &m_slotListIngredient;
		break;
	case eSectionFurnaceInventory:
		slotList = &m_slotListInventory;
		break;
	case eSectionFurnaceUsing:
		slotList = &m_slotListHotbar;
		break;
	default:
		assert( false );
		break;
	}

	slotList->setHighlightSlot(index);
}

UIControl *UIScene_FurnaceMenu::getSection(ESceneSection eSection)
{
	UIControl *control = nullptr;
	switch( eSection )
	{
	case eSectionFurnaceResult:
		control = &m_slotListResult;
		break;
	case eSectionFurnaceFuel:
		control = &m_slotListFuel;
		break;
	case eSectionFurnaceIngredient:
		control = &m_slotListIngredient;
		break;
	case eSectionFurnaceInventory:
		control = &m_slotListInventory;
		break;
	case eSectionFurnaceUsing:
		control = &m_slotListHotbar;
		break;
	default:
		assert( false );
		break;
	}
	return control;
}
