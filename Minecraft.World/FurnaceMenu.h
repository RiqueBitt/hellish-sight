#pragma once

#include "AbstractContainerMenu.h"

class FurnaceTileEntity;

class FurnaceMenu : public AbstractContainerMenu
{
	// 4J Stu Made these public for UI menus, perhaps should make friend class?
public:
	static const int INGREDIENT_SLOT = 0;
	static const int FUEL_SLOT = 1;
	static const int RESULT_SLOT = 2;
	static const int INV_SLOT_START = FurnaceMenu::RESULT_SLOT + 1;
	static const int INV_SLOT_END = FurnaceMenu::INV_SLOT_START + 9 * 3;
	static const int USE_ROW_SLOT_START = FurnaceMenu::INV_SLOT_END;
	static const int USE_ROW_SLOT_END = FurnaceMenu::USE_ROW_SLOT_START + 9;

private:
	shared_ptr<FurnaceTileEntity> furnace;

public:
	FurnaceMenu(shared_ptr<Inventory> inventory, shared_ptr<FurnaceTileEntity> furnace);

private:
	int tc;
	int lt;
	int ld;
	int hsSent[48];		// Hellish Sight: last machine data sent

public:
	virtual void addSlotListener(ContainerListener *listener);
	virtual void broadcastChanges();
	// Hellish Sight: the server adds its listener before the client has the menu (open packet
	// sent after), so the first data would be dropped: resend everything on the next broadcast
	void hsResendAll() { for (int i = 0; i < 48; i++) hsSent[i] = 0x7fffffff; tc = -1; lt = -1; ld = -1; }
	virtual void setData(int id, int value);
	virtual bool stillValid(shared_ptr<Player> player);
	virtual shared_ptr<ItemInstance> quickMoveStack(shared_ptr<Player> player, int slotIndex);

	// 4J Added looped param
	virtual shared_ptr<ItemInstance> clicked(int slotIndex, int buttonNum, int clickType, shared_ptr<Player> player, bool looped = false);
};
