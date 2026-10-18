#pragma once

// Hellish Sight - Ruby mod "machines": blocks with an inventory, energy (FE), chemical /
// fluid tanks, upgrades, side configuration and optional multiblock structures.
// Registered from Lua with registerMachine. The screen is the furnace screen
// (input / power-or-extra / output, progress arrow, flame = energy gauge).
//
// Kinds:
//  * processor   recipes: items and/or chemicals in -> items and/or chemicals out
//  * generator   burns items or a chemical (fuelChem), or solar; can exhaust a chemical
//  * storage     energy cube (front outputs), charges rechargeable items in its bottom slot
//  * cable       moves FE between neighbours
//  * tank        chemical / fluid tank (front outputs), refills items (jetpacks...)
//  * pipe        moves chemicals / fluids between neighbours
//  * transporter moves items between inventories (logistical transporter)
//  * pump        pumps fluid source blocks below into its tank
//  * miner       digital miner: mines ores (or everything) in a radius below it
//  * teleporter  teleports a sneaking player standing on it to the next teleporter
//                with the same frequency
//
// Tools (registerMachineTool): configurator (face modes / eject / frequency / miner
// filter), speed / energy / muffling upgrades. Sneak + right click with an empty hand
// prints the machine status in chat.

#include "Tile.h"
#include "BaseEntityTile.h"
#include "FurnaceTileEntity.h"

#include <map>
#include <string>
#include <vector>

class Random;

struct ModChemAmount
{
	std::string name;		// "mekanism:hydrogen", "minecraft:water"
	int amount = 0;			// mB
};

struct ModMachineRecipe
{
	std::string in, out, extra;
	int count = 1;
	int inCount = 1;		// items consumed from the input slot per operation
	int inId = 0, inAux = -1;
	int outId = 0, outAux = 0;
	int extraId = 0, extraAux = -1;
	ModChemAmount chemIn[2], chemOut[2];
	std::string secondary;	// optional second item output (chance)
	int secondaryId = 0, secondaryAux = 0;
	float secondaryChance = 0.0f;
	int secondaryCount = 1;
};

struct ModMachineFuel
{
	std::string item;
	int ticks = 200;
	int id = 0, aux = -1;
};

struct ModMultiblockInner
{
	int capacity = 0;		// FE (or mB for tanks) added per block
	int transfer = 0;		// FE/t added per block
	float speed = 0.0f;		// speed multiplier added per block
};

struct ModMultiblockDef
{
	bool enabled = false;
	std::vector<std::string> frame;		// casing / glass / valves (the controller always counts)
	std::map<std::string, ModMultiblockInner> inner;	// allowed interior blocks
	bool interiorAir = true;			// air allowed inside
	int minSize = 3, maxSize = 18;
	int capacityPerVolume = 0;			// FE per interior block
	int tankPerVolume = 0;				// mB per interior block
	float speedPerVolume = 0.0f;
	float speedPerHeight = 0.0f;
	// resolved
	std::vector<int> frameIds;
	std::map<int, ModMultiblockInner> innerIds;
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

	int kind = 0;
	int energyCapacity = 0;		// FE buffer
	int energyPerTick = 0;		// processor usage while working (pump / miner / teleporter: per operation / per tick)
	int generation = 0;			// generator FE/t
	int generatorType = 0;		// 0 burn (fuel slot or fuelChem), 1 solar (daylight + open sky)
	int transfer = 0;			// max FE/t (or mB/t for pipes, items per move for transporters)

	int tankCapacity = 10000;	// mB per tank
	int parallel = 1;			// factories: operations per cycle
	ModChemAmount fuelChem;		// generators: chemical burnt per tick
	ModChemAmount exhaustChem;	// generators: chemical produced per tick
	int radius = 8;				// miner
	bool upgrades = true;
	ModMultiblockDef multiblock;

	// heat (resistive / fuelwood heaters, conductors, boilers, fission reactors)
	int heatCapacity = 1000;		// heat units per K (multiblocks: per interior block)
	int heatPerTick = 0;			// heaters: heat produced per tick (fuelwood) - resistive heaters convert FE 1:1
	float conductance = 0.2f;		// fraction of the temperature difference moved per tick
	int maxTemperature = 1200;		// fission: damage above this (K)
	long long ignitionEnergy = 0;	// fusion: FE consumed to ignite
	int burnRateMax = 0;			// fission: mB/t per fuel assembly
	std::string portalFrame;		// teleporter: frame block ("modId:id")
	int portalFrameId = 0;
};

enum ModMachineKind
{
	MachineKind_Processor = 0, MachineKind_Generator, MachineKind_Storage, MachineKind_Cable,
	MachineKind_Tank, MachineKind_Pipe, MachineKind_Transporter, MachineKind_Pump,
	MachineKind_Miner, MachineKind_Teleporter,
	MachineKind_Heater, MachineKind_Conductor, MachineKind_Boiler, MachineKind_Fission, MachineKind_Fusion,
	MachineKind_Seismic, MachineKind_QioDrive, MachineKind_QioDashboard, MachineKind_QioImporter,
	MachineKind_QioExporter, MachineKind_Entangloporter, MachineKind_ModStation,
};

enum ModMachineToolRole
{
	MachineTool_None = 0, MachineTool_Configurator, MachineTool_UpgradeSpeed, MachineTool_UpgradeEnergy,
	MachineTool_UpgradeMuffling, MachineTool_UpgradeFilter, MachineTool_SeismicReader, MachineTool_PortableTeleporter,
};

// MekaSuit / tool modules (installed with the Modification Station, stored in the item's
// "HSModules" tag as name -> level)
enum ModArmorSlot { ArmorSlot_Helmet = 1, ArmorSlot_Chest = 2, ArmorSlot_Legs = 4, ArmorSlot_Boots = 8, ArmorSlot_Tool = 16 };
struct ModModuleDef
{
	std::string name;		// "gravitational_modulating", "vision_enhancement", ...
	int slots = 0;			// ModArmorSlot mask
	int maxLevel = 1;
};

// Hellish Sight: layout of FurnaceTileEntity::hsData (machine screen). Values are shorts,
// big numbers are split into three 15-bit chunks.
enum ModMachineScreenData
{
	HSD_KIND = 0,			// machine kind + 1 (0 = not a mod machine)
	HSD_ENERGY = 1,			// 3 chunks
	HSD_ENERGY_CAP = 4,		// 3 chunks
	HSD_TANK_CAP = 7,		// 3 chunks
	HSD_TANK0 = 10,			// per tank: chemical index + 1, then 3 chunks of mB (4 tanks)
	HSD_HEAT = 26,			// 3 chunks (temperature in K)
	HSD_HEAT_MAX = 29,		// max temperature / 10
	HSD_UPGRADES = 30,		// speed | energy << 4 | muffling << 8 | filter << 12
	HSD_FORMED = 31,		// 0 no structure, 1 incomplete, 2 formed
	HSD_FREQ = 32,
	HSD_DAMAGE = 33,		// reactor damage %
	HSD_PROGRESS = 34,		// 0..100
	HSD_EJECT = 35,
	HSD_MB_VOLUME = 36,
	HSD_EXTRA = 37,			// miner: % scanned, reactor: burn rate...
	HSD_COUNT = 38,
};

inline void hsPutBig(int *data, int at, long long v)
{
	if (v < 0) v = 0;
	data[at] = static_cast<int>(v & 0x7FFF);
	data[at + 1] = static_cast<int>((v >> 15) & 0x7FFF);
	data[at + 2] = static_cast<int>((v >> 30) & 0x7FFF);
}

inline long long hsGetBig(const int *data, int at)
{
	return (static_cast<long long>(data[at] & 0x7FFF)) | (static_cast<long long>(data[at + 1] & 0x7FFF) << 15) | (static_cast<long long>(data[at + 2] & 0x7FFF) << 30);
}

struct ModItemRefill
{
	std::string resource;	// "energy" or a chemical name
	int perPoint = 100;		// FE / mB per durability point
};

namespace ModMachines
{
	std::vector<ModMachineDef> &defs();
	ModMachineDef *byTile(int tileId);
	ModMachineDef *byId(const std::string &fullId);
	void resolve(ModMachineDef &def);		// item names -> ids (after every mod registered)
	bool isAnyMachinePowerItem(int id, int aux);
	void registerTileEntityType();			// once, at boot

	// chemicals / fluids
	void registerChemical(const std::string &fullId, const std::wstring &name, int color, bool gas);
	std::wstring chemicalName(const std::string &fullId);
	int chemicalIndex(const std::string &fullId);		// stable index (same on client and server), -1 if unknown
	bool chemicalByIndex(int index, std::wstring &name, int &color);

	// tools and rechargeable items (by item name "modId:id")
	void registerTool(const std::string &itemName, int role);
	int toolRole(int itemId);
	void registerRefill(const std::string &itemName, const std::string &resource, int perPoint);
	const ModItemRefill *refillFor(int itemId);

	// QIO drives, MekaSuit pieces / modules
	void registerQioDrive(const std::string &itemName, int capacity);
	int qioDriveCapacity(int itemId);
	void registerModule(const std::string &itemName, const ModModuleDef &def);
	const ModModuleDef *moduleFor(int itemId);
	void registerSuitPiece(const std::string &itemName, int slot);
	int suitPieceSlot(int itemId);
	int moduleLevel(shared_ptr<ItemInstance> item, const std::string &module);

	bool teleporterFrame(Level *level, int x, int y, int z, ModMachineDef *d, int &axis, int &offset);
	// item use (portable teleporter) and armour tick (MekaSuit modules)
	bool onItemUse(ItemInstance *item, Level *level, shared_ptr<Player> player);
	void armorTick(Player *player);
}

class ModMachineTileEntity : public FurnaceTileEntity
{
public:
	static TileEntity *create() { return new ModMachineTileEntity(); }

	enum { TANK_IN0 = 0, TANK_IN1, TANK_OUT0, TANK_OUT1, TANK_COUNT };
	enum { FACE_DEFAULT = 0, FACE_INPUT, FACE_OUTPUT, FACE_DISABLED, FACE_MODE_COUNT };
	enum { UPG_SPEED = 0, UPG_ENERGY, UPG_MUFFLING, UPG_FILTER, UPG_COUNT };
	static const int MAX_UPGRADES = 8;

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
	virtual void hsUpdateData();
	void hsUpdateDataExtra(ModMachineDef *d);	// heat / reactor / miner extras

	ModMachineDef *def();

	// energy
	int getEnergy() const { return m_energy; }
	int capacity();
	int receiveEnergy(int amount, int intoFace, bool fromCable);	// returns accepted FE

	// chemicals / fluids
	int receiveChem(const std::string &chem, int amount, int intoFace);	// returns accepted mB
	int tankCapacity();

	// items (transporters, ejection)
	int acceptItem(shared_ptr<ItemInstance> item, int intoFace);	// returns count accepted

	// player interaction (server side). Returns true when handled.
	bool interact(shared_ptr<Player> player, int face);
	void dropExtras(Level *level);	// upgrades + buffered items when broken

private:
	void tickProcessor(ModMachineDef *d);
	void tickEnergyBlock(ModMachineDef *d);
	void tickTank(ModMachineDef *d);
	void tickPipe(ModMachineDef *d);
	void tickTransporter(ModMachineDef *d);
	void tickPump(ModMachineDef *d);
	void tickMiner(ModMachineDef *d);
	void tickTeleporter(ModMachineDef *d);
	void scanStructure(ModMachineDef *d);
	// advanced kinds (ModMachineAdvanced.cpp)
	void tickHeater(ModMachineDef *d);
	void tickConductor(ModMachineDef *d);
	void tickBoiler(ModMachineDef *d);
	void tickFission(ModMachineDef *d);
	void tickFusion(ModMachineDef *d);
	void tickSeismic(ModMachineDef *d);
	void tickQio(ModMachineDef *d);
	void tickEntangloporter(ModMachineDef *d);
	void tickModStation(ModMachineDef *d);
	bool scanTeleporterFrame(int &ax, int &ox);
	bool interactAdvanced(shared_ptr<Player> player, int face, shared_ptr<ItemInstance> held, int role);
	void conductHeat(ModMachineTileEntity *other);
	void boilCoolant(int maxMb, int heatPerMb, int inTank, const char *inChem, int outTank, const char *outChem);
	void seismicReport(shared_ptr<Player> player);
	void openQioDashboard(shared_ptr<Player> player);
	void saveAdvanced(CompoundTag *base);
	void loadAdvanced(CompoundTag *base);
public:
	bool hasHeat();
	double heatCapacityNow();
	void addHeat(double h) { m_temp += h / heatCapacityNow(); }
	double temperature() const { return m_temp; }
	int frequency() const { return m_frequency; }
	bool teleporterReady();			// formed portal with enough energy
	void teleporterInterior(int &x0, int &y0, int &z0, int &ax);	// bottom-left interior block + axis
	int entangleEnergyIn(int amount);
private:
	double m_temp = 300.0;			// K
	int m_damage = 0;				// fission: 0..10000
	bool m_enabled = false;			// fission / fusion on
	bool m_ignited = false;			// fusion
	int m_burnRate = 1;				// fission mB/t
	std::vector<int> m_heatSources;	// boiler: x, y, z of heaters touching the structure
	int m_portalAxis = -1, m_portalOffset = 0;	// teleporter frame
	int m_mbMin[3] = { 0, 0, 0 }, m_mbMax[3] = { 0, 0, 0 };	// structure bounds
	int m_meltdownWarned = 0;

	void pushEnergy(int face, int amount);
	int pushChem(int tank, int face, int amount);
	void pushOutputs(ModMachineDef *d);
	void ejectItems(ModMachineDef *d);
	bool storeOutput(shared_ptr<ItemInstance> item);
	void chargeItemInSlot(ModMachineDef *d);
	void drainItemInSlot(ModMachineDef *d);
	void updateGauge(ModMachineDef *d);
	void setActiveState(bool active);
	void sendStatus(shared_ptr<Player> player);

	bool recipeReady(const ModMachineRecipe &r, bool checkOutputs);
	void runRecipe(const ModMachineRecipe &r);
	const ModMachineRecipe *findRecipe(ModMachineRecipe &furnaceOut);
	int powerTicks(shared_ptr<ItemInstance> item);
	int faceMode(int face) const { return (face >= 0 && face < 6) ? m_faceMode[face] : FACE_DEFAULT; }
	int frontFace();
	float speedMultiplier(ModMachineDef *d);
	int energyUse(ModMachineDef *d);
	int inputTankFor(ModMachineDef *d, const std::string &chem);

	int m_energy = 0;
	int m_burn = 0, m_burnTotal = 0;
	long long m_progress = 0;			// work done on the current operation (x1000)
	ModChemAmount m_tanks[TANK_COUNT];
	unsigned char m_faceMode[6] = { 0, 0, 0, 0, 0, 0 };
	bool m_eject = true;
	int m_upgrades[UPG_COUNT] = { 0, 0, 0, 0 };
	int m_frequency = 0;				// teleporter
	bool m_minerAll = false;			// miner: every block instead of ores only
	int m_minerCursor = 0;
	int m_lastFace = -1;				// transporter: where the buffered item came from
	std::vector<shared_ptr<ItemInstance> > m_buffer;	// transporter / miner / secondary outputs
	int m_tickCounter = 0;
	int m_teleportCooldown = 0;

	// multiblock
	int m_formed = -1;					// -1 unknown, 0 invalid, 1 formed
	int m_mbVolume = 0, m_mbHeight = 0;
	int m_mbCapacity = 0, m_mbTransfer = 0, m_mbTank = 0;
	float m_mbSpeed = 1.0f;

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
	virtual void updateShape(LevelSource *level, int x, int y, int z, int forceData = -1, shared_ptr<TileEntity> forceEntity = shared_ptr<TileEntity>());
	virtual bool isSolidRender(bool isServerLevel = false);
	virtual bool isCubeShaped();
	bool isCable() const;		// thin connector shape (cables, pipes, transporters)

	static void setActive(Level *level, int x, int y, int z, bool active);

private:
	int m_defIndex;
	Icon *m_front = nullptr, *m_frontActive = nullptr, *m_top = nullptr, *m_bottom = nullptr;
	Random *m_random;
};
