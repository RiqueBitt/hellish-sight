#pragma once

// Hellish Sight - Ruby mod "machines": processing blocks with an inventory and the
// furnace screen (input / power / output). Registered from Lua with registerMachine.
//
//  * Recipes: input item -> output item (count), optionally consuming an "extra"
//    item from the power slot (infusers, injection chambers). Recipes that name an
//    extra need no power.
//  * Power: items from the machine's fuel list (or every furnace fuel when
//    furnaceFuels = true) burn for N ticks; a recipe only runs while powered.
//  * One operation takes processTicks (default 200, like a furnace).
//  * The block faces the player, shows its "active" front while working and keeps its
//    contents in the world save (tile entity id "HSMachine").

#include "Tile.h"
#include "BaseEntityTile.h"
#include "FurnaceTileEntity.h"

#include <string>
#include <vector>

class Random;

struct ModMachineRecipe
{
	std::string in, out, extra;
	int count = 1;
	int inCount = 1;		// items consumed from the input slot per operation
	int inId = 0, inAux = -1;
	int outId = 0, outAux = 0;
	int extraId = 0, extraAux = -1;
};

struct ModMachineFuel
{
	std::string item;
	int ticks = 200;
	int id = 0, aux = -1;
};

struct ModMachineDef
{
	std::string fullId;			// "modId:id"
	std::wstring name;
	int tileId = -1;
	int processTicks = 200;
	bool furnaceRecipes = false;	// also accept every furnace (smelting) recipe
	bool furnaceFuels = false;		// also accept every furnace fuel as power
	bool needsPower = true;
	std::vector<ModMachineRecipe> recipes;
	std::vector<ModMachineFuel> fuels;
	std::wstring iconFront, iconFrontActive, iconSide, iconTop, iconBottom;
	bool resolved = false;
};

namespace ModMachines
{
	std::vector<ModMachineDef> &defs();
	ModMachineDef *byTile(int tileId);
	ModMachineDef *byId(const std::string &fullId);
	void resolve(ModMachineDef &def);		// item names -> ids (after every mod registered)
	bool isAnyMachinePowerItem(int id, int aux);
	void registerTileEntityType();			// once, at boot
}

class ModMachineTileEntity : public FurnaceTileEntity
{
public:
	static TileEntity *create() { return new ModMachineTileEntity(); }

	ModMachineTileEntity() {}
	explicit ModMachineTileEntity(const std::string &machineId) : m_machineId(machineId) {}

	virtual void tick();
	virtual void load(CompoundTag *base);
	virtual void save(CompoundTag *base);
	virtual wstring getName();
	virtual wstring getCustomName();
	virtual bool hasCustomName();
	virtual bool canPlaceItem(int slot, shared_ptr<ItemInstance> item);
	virtual shared_ptr<TileEntity> clone();

	ModMachineDef *def();

private:
	const ModMachineRecipe *findRecipe(shared_ptr<ItemInstance> input, const ModMachineRecipe &tmp, ModMachineRecipe &furnaceOut);
	int powerTicks(shared_ptr<ItemInstance> item);

	std::string m_machineId;
	ModMachineDef *m_def = nullptr;
	bool m_active = false;
};

class ModMachineTile : public BaseEntityTile
{
public:
	ModMachineTile(int id, int defIndex, float hardness);

	virtual Icon *getTexture(int face, int data);
	virtual void registerIcons(IconRegister *iconRegister);
	virtual shared_ptr<TileEntity> newTileEntity(Level *level);
	virtual bool TestUse();
	virtual bool use(Level *level, int x, int y, int z, shared_ptr<Player> player, int clickedFace, float clickX, float clickY, float clickZ, bool soundOnly = false);
	virtual void setPlacedBy(Level *level, int x, int y, int z, shared_ptr<LivingEntity> by, shared_ptr<ItemInstance> itemInstance);
	virtual void onRemove(Level *level, int x, int y, int z, int id, int data);
	virtual void animateTick(Level *level, int xt, int yt, int zt, Random *random);

	static void setActive(Level *level, int x, int y, int z, bool active);

private:
	int m_defIndex;
	Icon *m_front = nullptr, *m_frontActive = nullptr, *m_top = nullptr, *m_bottom = nullptr;
	Random *m_random;
};
