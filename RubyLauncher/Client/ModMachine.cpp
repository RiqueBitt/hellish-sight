// Hellish Sight - Ruby mod machines. See ModMachine.h.

#include "stdafx.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.entity.item.h"
#include "net.minecraft.world.entity.player.h"
#include "net.minecraft.world.item.h"
#include "net.minecraft.world.level.tile.entity.h"
#include "net.minecraft.world.h"
#include "com.mojang.nbt.h"
#include "FurnaceRecipes.h"
#include "Facing.h"
#include "Mth.h"
#include "ModMachine.h"
#include "Rendering/ModModels.h"
#include "Rendering/MeshRender.h"
#include "HSModel.h"
#include "Registry/IDs.h"
#include "ServerPlayer.h"
#include "PlayerConnection.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

// ======================================================================================
// Registries and helpers
// ======================================================================================

namespace
{
	struct ChemInfo { std::wstring name; int color; bool gas; };
	std::map<std::string, ChemInfo> &chems()
	{
		static std::map<std::string, ChemInfo> s;
		return s;
	}

	struct NamedRole { std::string name; int role; };
	std::vector<NamedRole> &toolNames() { static std::vector<NamedRole> s; return s; }
	std::map<int, int> &toolIds() { static std::map<int, int> s; return s; }
	bool &toolsDirty() { static bool s = true; return s; }

	struct NamedRefill { std::string name; ModItemRefill refill; };
	std::vector<NamedRefill> &refillNames() { static std::vector<NamedRefill> s; return s; }
	std::map<int, ModItemRefill> &refillIds() { static std::map<int, ModItemRefill> s; return s; }
	bool &refillsDirty() { static bool s = true; return s; }

	Random &rng() { static Random s; return s; }

	int lookupId(const std::string &name)
	{
		if (name.empty()) return 0;
		return IDMapping::get()->getID(name).id;
	}

	struct TeleEntry { Level *level; int x, y, z; int freq; };
	std::vector<TeleEntry> &teleporters() { static std::vector<TeleEntry> s; return s; }

	std::wstring fmtNumLocal(long long v)
	{
		wchar_t buf[32];
		if (v >= 1000000000LL) swprintf(buf, 32, L"%.2fG", v / 1e9);
		else if (v >= 1000000LL) swprintf(buf, 32, L"%.2fM", v / 1e6);
		else if (v >= 10000LL) swprintf(buf, 32, L"%.1fk", v / 1e3);
		else swprintf(buf, 32, L"%lld", v);
		return buf;
	}

	const int kFaceDx[6] = { 0, 0, 0, 0, -1, 1 };
	const int kFaceDy[6] = { -1, 1, 0, 0, 0, 0 };
	const int kFaceDz[6] = { 0, 0, -1, 1, 0, 0 };
	int oppositeFace(int f) { return f ^ 1; }

	long long posKey(int a, int b, int c)
	{
		return ((long long)(a + 30000000) << 34) ^ ((long long)(b & 0xFF) << 26) ^ (long long)(c + 30000000);
	}

	bool isConnectorKind(int kind)
	{
		return kind == MachineKind_Cable || kind == MachineKind_Pipe || kind == MachineKind_Transporter || kind == MachineKind_Conductor;
	}

	// generic "put this stack into that inventory"; returns how many items went in
	int insertInto(shared_ptr<Container> c, shared_ptr<ItemInstance> item)
	{
		if (c == nullptr || item == nullptr || item->count <= 0) return 0;
		int moved = 0;
		const unsigned int size = c->getContainerSize();
		for (int pass = 0; pass < 2 && item->count > moved; pass++)
		{
			for (unsigned int s = 0; s < size && item->count > moved; s++)
			{
				if (!c->canPlaceItem(s, item)) continue;
				shared_ptr<ItemInstance> cur = c->getItem(s);
				const int max = (std::min)(c->getMaxStackSize(), item->getMaxStackSize());
				if (pass == 0)
				{
					// merge first
					if (cur == nullptr || cur->id != item->id || cur->getAuxValue() != item->getAuxValue() || cur->hasTag() || item->hasTag()) continue;
					const int room = max - cur->count;
					if (room <= 0) continue;
					const int n = (std::min)(room, item->count - moved);
					cur->count += n;
					moved += n;
				}
				else
				{
					if (cur != nullptr) continue;
					const int n = (std::min)(max, item->count - moved);
					shared_ptr<ItemInstance> copy = item->copy();
					copy->count = n;
					c->setItem(s, copy);
					moved += n;
				}
			}
		}
		if (moved > 0) c->setChanged();
		return moved;
	}
}

namespace ModMachines
{
	std::vector<ModMachineDef> &defs()
	{
		static std::vector<ModMachineDef> s_defs;
		return s_defs;
	}

	ModMachineDef *byTile(int tileId)
	{
		for (auto &d : defs()) if (d.tileId == tileId) return &d;
		return nullptr;
	}

	ModMachineDef *byId(const std::string &fullId)
	{
		for (auto &d : defs()) if (d.fullId == fullId) return &d;
		return nullptr;
	}

	static void lookup(const std::string &name, int &id, int &aux)
	{
		if (name.empty()) { id = 0; return; }
		IDMapping::MappedItem m = IDMapping::get()->getID(name);
		id = m.id;
		aux = m.aux;
	}

	void resolve(ModMachineDef &def)
	{
		if (def.resolved) return;
		def.resolved = true;
		for (auto &r : def.recipes)
		{
			if (!r.in.empty())
			{
				int aux = -1;
				lookup(r.in, r.inId, aux);
				r.inAux = aux > 0 ? aux : -1;	// names of aux variants (lapis, charcoal...) only match that variant
			}
			if (!r.out.empty())
			{
				int outAux = 0;
				lookup(r.out, r.outId, outAux);
				r.outAux = outAux < 0 ? 0 : outAux;
			}
			if (!r.extra.empty())
			{
				int ea = -1;
				lookup(r.extra, r.extraId, ea);
				r.extraAux = ea > 0 ? ea : -1;
			}
			if (!r.secondary.empty())
			{
				int sa = 0;
				lookup(r.secondary, r.secondaryId, sa);
				r.secondaryAux = sa < 0 ? 0 : sa;
			}
		}
		for (auto &f : def.fuels)
		{
			int a = -1;
			lookup(f.item, f.id, a);
			f.aux = a > 0 ? a : -1;
		}
		ModMultiblockDef &mb = def.multiblock;
		if (mb.enabled)
		{
			mb.frameIds.clear();
			mb.innerIds.clear();
			for (auto &n : mb.frame) { const int id = lookupId(n); if (id > 0) mb.frameIds.push_back(id); }
			for (auto &kv : mb.inner) { const int id = lookupId(kv.first); if (id > 0) mb.innerIds[id] = kv.second; }
		}
	}

	bool isAnyMachinePowerItem(int id, int aux)
	{
		for (auto &d : defs())
		{
			resolve(d);
			for (auto &f : d.fuels) if (f.id == id) return true;
			for (auto &r : d.recipes) if (r.extraId != 0 && r.extraId == id) return true;
		}
		return refillFor(id) != nullptr;
	}

	void registerTileEntityType()
	{
		static bool done = false;
		if (done) return;
		done = true;
		TileEntity::registerExternalId(ModMachineTileEntity::create, L"HSMachine");
		// furnace screens on the client accept our power items in the fuel slot
		FurnaceTileEntity::s_extraFuelCheck = [](shared_ptr<ItemInstance> item) -> bool {
			return item != nullptr && isAnyMachinePowerItem(item->id, item->getAuxValue());
		};
		Player::s_hsArmorTick = &ModMachines::armorTick;
		registerChemical("minecraft:water", L"\u00C1gua", 0x3F76E4, false);
		registerChemical("minecraft:lava", L"Lava", 0xFF6A00, false);
	}

	void registerChemical(const std::string &fullId, const std::wstring &name, int color, bool gas)
	{
		ChemInfo info;
		info.name = name;
		info.color = color;
		info.gas = gas;
		chems()[fullId] = info;
	}

	std::wstring chemicalName(const std::string &fullId)
	{
		auto it = chems().find(fullId);
		if (it != chems().end() && !it->second.name.empty()) return it->second.name;
		return std::wstring(fullId.begin(), fullId.end());
	}

	int chemicalIndex(const std::string &fullId)
	{
		int i = 0;
		for (auto &kv : chems())
		{
			if (kv.first == fullId) return i;
			i++;
		}
		return -1;
	}

	bool chemicalByIndex(int index, std::wstring &name, int &color)
	{
		int i = 0;
		for (auto &kv : chems())
		{
			if (i++ != index) continue;
			name = kv.second.name;
			color = kv.second.color;
			return true;
		}
		return false;
	}

	void registerTool(const std::string &itemName, int role)
	{
		NamedRole r;
		r.name = itemName;
		r.role = role;
		toolNames().push_back(r);
		toolsDirty() = true;
	}

	int toolRole(int itemId)
	{
		if (toolsDirty())
		{
			toolsDirty() = false;
			toolIds().clear();
			for (auto &t : toolNames())
			{
				const int id = lookupId(t.name);
				if (id > 0) toolIds()[id] = t.role;
				else toolsDirty() = true;
			}
		}
		auto it = toolIds().find(itemId);
		return it != toolIds().end() ? it->second : static_cast<int>(MachineTool_None);
	}

	static int itemForRole(int role)
	{
		toolRole(0);
		for (auto &kv : toolIds()) if (kv.second == role) return kv.first;
		return 0;
	}

	void registerRefill(const std::string &itemName, const std::string &resource, int perPoint)
	{
		NamedRefill r;
		r.name = itemName;
		r.refill.resource = resource;
		r.refill.perPoint = (std::max)(1, perPoint);
		refillNames().push_back(r);
		refillsDirty() = true;
	}

	const ModItemRefill *refillFor(int itemId)
	{
		if (refillsDirty())
		{
			refillsDirty() = false;
			refillIds().clear();
			for (auto &r : refillNames())
			{
				const int id = lookupId(r.name);
				if (id > 0) refillIds()[id] = r.refill;
				else refillsDirty() = true;
			}
		}
		auto it = refillIds().find(itemId);
		return it != refillIds().end() ? &it->second : nullptr;
	}
}

std::wstring hsFmtNum(long long v) { return fmtNumLocal(v); }

// ======================================================================================
// Tile entity: basics
// ======================================================================================

ModMachineDef *ModMachineTileEntity::def()
{
	if (m_def == nullptr && !m_machineId.empty()) m_def = ModMachines::byId(m_machineId);
	if (m_def == nullptr && level != nullptr)
	{
		m_def = ModMachines::byTile(level->getTile(x, y, z));
		if (m_def) m_machineId = m_def->fullId;
	}
	if (m_def) ModMachines::resolve(*m_def);
	return m_def;
}

int ModMachineTileEntity::frontFace()
{
	if (level == nullptr) return Facing::SOUTH;
	const int f = level->getData(x, y, z) & 7;
	return (f >= 2 && f < 6) ? f : Facing::SOUTH;
}

float ModMachineTileEntity::speedMultiplier(ModMachineDef *d)
{
	float s = 1.0f + static_cast<float>(m_upgrades[UPG_SPEED]);
	if (d != nullptr && d->multiblock.enabled) s *= m_mbSpeed;
	return s;
}

int ModMachineTileEntity::energyUse(ModMachineDef *d)
{
	if (d == nullptr || d->energyPerTick <= 0) return 0;
	const double use = d->energyPerTick * (1.0 + m_upgrades[UPG_SPEED]) / (1.0 + 0.5 * m_upgrades[UPG_ENERGY]);
	return (std::max)(1, static_cast<int>(use + 0.5));
}

int ModMachineTileEntity::capacity()
{
	ModMachineDef *d = def();
	if (d == nullptr) return 0;
	if (d->kind == MachineKind_Cable) return (std::max)(1, d->transfer * 2);
	long long cap = d->energyCapacity;
	if (d->kind == MachineKind_Processor || d->kind == MachineKind_Pump || d->kind == MachineKind_Miner || d->kind == MachineKind_Teleporter)
		cap = cap * (1 + m_upgrades[UPG_ENERGY]);
	if (d->multiblock.enabled) cap += m_mbCapacity;
	if (cap > 2000000000LL) cap = 2000000000LL;
	return static_cast<int>(cap);
}

int ModMachineTileEntity::tankCapacity()
{
	ModMachineDef *d = def();
	if (d == nullptr) return 0;
	if (d->kind == MachineKind_Pipe) return (std::max)(100, d->transfer * 2);
	long long cap = d->tankCapacity;
	if (d->multiblock.enabled) cap += m_mbTank;
	if (cap > 2000000000LL) cap = 2000000000LL;
	return static_cast<int>(cap);
}

int ModMachineTileEntity::powerTicks(shared_ptr<ItemInstance> item)
{
	ModMachineDef *d = def();
	if (d == nullptr || item == nullptr) return 0;
	for (auto &f : d->fuels)
	{
		if (f.id != 0 && f.id == item->id && (f.aux < 0 || f.aux == item->getAuxValue())) return f.ticks;
	}
	if (d->furnaceFuels) return FurnaceTileEntity::getBurnDuration(item);
	return 0;
}

void ModMachineTileEntity::setActiveState(bool active)
{
	if (active == m_active) return;
	m_active = active;
	ModMachineTile::setActive(level, x, y, z, active);
}

void ModMachineTileEntity::updateGauge(ModMachineDef *d)
{
	// the furnace "flame" shows the stored energy (the tank for fluid tanks)
	litDuration = 1000;
	if (d->kind == MachineKind_Tank)
	{
		const int cap = tankCapacity();
		litTime = cap > 0 ? static_cast<int>(static_cast<long long>(m_tanks[TANK_IN0].amount) * 1000 / cap) : 0;
		return;
	}
	const int cap = capacity();
	litTime = cap > 0 ? static_cast<int>(static_cast<long long>(m_energy) * 1000 / cap) : 0;
}

// ======================================================================================
// Tick
// ======================================================================================

void ModMachineTileEntity::tick()
{
	if (level == nullptr) return;
	if (level->isClientSide)
	{
		if (litTime > 0 && m_active) litTime--;
		return;
	}
	ModMachineDef *d = def();
	if (d == nullptr) return;
	m_tickCounter++;
	if (m_teleportCooldown > 0) m_teleportCooldown--;

	if (d->multiblock.enabled)
	{
		if (m_formed < 0 || (m_tickCounter % 40) == 1) scanStructure(d);
		if (m_formed != 1)
		{
			setActiveState(false);
			return;
		}
		if (m_energy > capacity()) m_energy = capacity();
		for (auto &t : m_tanks) if (t.amount > tankCapacity()) t.amount = tankCapacity();
	}

	switch (d->kind)
	{
	case MachineKind_Processor: tickProcessor(d); break;
	case MachineKind_Generator:
	case MachineKind_Storage:
	case MachineKind_Cable: tickEnergyBlock(d); break;
	case MachineKind_Tank: tickTank(d); break;
	case MachineKind_Pipe: tickPipe(d); break;
	case MachineKind_Transporter: tickTransporter(d); break;
	case MachineKind_Pump: tickPump(d); break;
	case MachineKind_Miner: tickMiner(d); break;
	case MachineKind_Teleporter: tickTeleporter(d); break;
	case MachineKind_Heater: tickHeater(d); break;
	case MachineKind_Conductor: tickConductor(d); break;
	case MachineKind_Boiler: tickBoiler(d); break;
	case MachineKind_Fission: tickFission(d); break;
	case MachineKind_Fusion: tickFusion(d); break;
	case MachineKind_Seismic: tickSeismic(d); break;
	case MachineKind_QioDrive:
	case MachineKind_QioDashboard:
	case MachineKind_QioImporter:
	case MachineKind_QioExporter: tickQio(d); break;
	case MachineKind_Entangloporter: tickEntangloporter(d); break;
	case MachineKind_ModStation: tickModStation(d); break;
	default: break;
	}

	if (!isConnectorKind(d->kind))
	{
		pushOutputs(d);
		ejectItems(d);
	}
}

// --------------------------------------------------------------------------------------
// Recipes
// --------------------------------------------------------------------------------------

bool ModMachineTileEntity::recipeReady(const ModMachineRecipe &r, bool checkOutputs)
{
	bool anyInput = false;
	if (r.inId != 0)
	{
		shared_ptr<ItemInstance> in = getItem(SLOT_INPUT);
		if (in == nullptr || in->id != r.inId || in->count < (std::max)(1, r.inCount)) return false;
		if (r.inAux >= 0 && in->getAuxValue() != r.inAux) return false;
		anyInput = true;
	}
	if (r.extraId != 0)
	{
		shared_ptr<ItemInstance> ex = getItem(SLOT_FUEL);
		if (ex == nullptr || ex->id != r.extraId || ex->count <= 0) return false;
		if (r.extraAux >= 0 && ex->getAuxValue() != r.extraAux) return false;
	}
	for (int k = 0; k < 2; k++)
	{
		if (r.chemIn[k].name.empty()) continue;
		const ModChemAmount &t = m_tanks[TANK_IN0 + k];
		if (t.name != r.chemIn[k].name || t.amount < r.chemIn[k].amount) return false;
		anyInput = true;
	}
	if (!anyInput) return false;
	if (!checkOutputs) return true;

	if (r.outId != 0)
	{
		shared_ptr<ItemInstance> out = getItem(SLOT_RESULT);
		if (out != nullptr)
		{
			if (out->id != r.outId || out->getAuxValue() != r.outAux) return false;
			const int max = (std::min)(getMaxStackSize(), out->getMaxStackSize());
			if (out->count + r.count > max) return false;
		}
	}
	const int cap = tankCapacity();
	for (int k = 0; k < 2; k++)
	{
		if (r.chemOut[k].name.empty()) continue;
		const ModChemAmount &t = m_tanks[TANK_OUT0 + k];
		if (!t.name.empty() && t.amount > 0 && t.name != r.chemOut[k].name) return false;
		if (t.amount + r.chemOut[k].amount > cap) return false;
	}
	if (m_buffer.size() > 4) return false;	// secondary outputs piling up
	return true;
}

void ModMachineTileEntity::runRecipe(const ModMachineRecipe &r)
{
	if (r.inId != 0)
	{
		shared_ptr<ItemInstance> in = getItem(SLOT_INPUT);
		in->count -= (std::max)(1, r.inCount);
		if (in->count <= 0) setItem(SLOT_INPUT, nullptr);
	}
	if (r.extraId != 0)
	{
		shared_ptr<ItemInstance> ex = getItem(SLOT_FUEL);
		ex->count--;
		if (ex->count <= 0) setItem(SLOT_FUEL, nullptr);
	}
	for (int k = 0; k < 2; k++)
	{
		if (r.chemIn[k].name.empty()) continue;
		ModChemAmount &t = m_tanks[TANK_IN0 + k];
		t.amount -= r.chemIn[k].amount;
		if (t.amount <= 0) { t.amount = 0; t.name.clear(); }
	}
	if (r.outId != 0)
	{
		shared_ptr<ItemInstance> out = getItem(SLOT_RESULT);
		if (out == nullptr) setItem(SLOT_RESULT, std::make_shared<ItemInstance>(r.outId, r.count, r.outAux));
		else out->count += r.count;
	}
	for (int k = 0; k < 2; k++)
	{
		if (r.chemOut[k].name.empty()) continue;
		ModChemAmount &t = m_tanks[TANK_OUT0 + k];
		t.name = r.chemOut[k].name;
		t.amount += r.chemOut[k].amount;
	}
	if (r.secondaryId != 0 && rng().nextFloat() < r.secondaryChance)
	{
		storeOutput(std::make_shared<ItemInstance>(r.secondaryId, (std::max)(1, r.secondaryCount), r.secondaryAux));
	}
}

const ModMachineRecipe *ModMachineTileEntity::findRecipe(ModMachineRecipe &furnaceOut)
{
	ModMachineDef *d = def();
	if (d == nullptr) return nullptr;
	const ModMachineRecipe *partial = nullptr;
	for (auto &r : d->recipes)
	{
		if (r.outId == 0 && r.chemOut[0].name.empty() && r.chemOut[1].name.empty()) continue;
		if (!recipeReady(r, false)) continue;
		if (recipeReady(r, true)) return &r;
		if (partial == nullptr) partial = &r;
	}
	if (partial) return partial;
	shared_ptr<ItemInstance> input = getItem(SLOT_INPUT);
	if (d->furnaceRecipes && input != nullptr)
	{
		ItemInstance *res = FurnaceRecipes::getInstance()->getResult(input->id, input->getAuxValue());
		if (res != nullptr)
		{
			furnaceOut = ModMachineRecipe();
			furnaceOut.inId = input->id;
			furnaceOut.outId = res->id;
			furnaceOut.outAux = res->getAuxValue();
			furnaceOut.count = res->count > 0 ? res->count : 1;
			return &furnaceOut;
		}
	}
	return nullptr;
}

int ModMachineTileEntity::inputTankFor(ModMachineDef *d, const std::string &chem)
{
	for (int k = 0; k < 2; k++)
	{
		for (auto &r : d->recipes) if (r.chemIn[k].name == chem) return TANK_IN0 + k;
	}
	return -1;
}

// --------------------------------------------------------------------------------------
// Processor
// --------------------------------------------------------------------------------------

void ModMachineTileEntity::tickProcessor(ModMachineDef *d)
{
	bool changed = false;
	ModMachineRecipe furnaceTmp;
	const ModMachineRecipe *r = findRecipe(furnaceTmp);
	const bool fits = r != nullptr && recipeReady(*r, true);

	const bool usesPower = d->needsPower && (r == nullptr || r->extraId == 0);
	const bool electric = d->energyPerTick > 0;

	if (electric)
	{
		drainItemInSlot(d);
		// power items in the bottom slot charge the FE buffer
		shared_ptr<ItemInstance> fuel = getItem(SLOT_FUEL);
		const int ticks = (r == nullptr || r->extraId == 0 || fuel == nullptr || fuel->id != r->extraId) ? powerTicks(fuel) : 0;
		const int fe = ticks * d->energyPerTick;
		if (fe > 0 && m_energy + fe <= capacity())
		{
			m_energy += fe;
			fuel->count--;
			if (fuel->count <= 0) setItem(SLOT_FUEL, nullptr);
			changed = true;
		}
	}

	// refuel burning machines when empty and there is work to do
	if (!electric && usesPower && r != nullptr && fits && litTime <= 0)
	{
		shared_ptr<ItemInstance> fuel = getItem(SLOT_FUEL);
		const int ticks = powerTicks(fuel);
		if (ticks > 0)
		{
			litTime = litDuration = ticks;
			fuel->count--;
			if (fuel->count <= 0)
			{
				Item *remaining = fuel->getItem() ? fuel->getItem()->getCraftingRemainingItem() : nullptr;
				setItem(SLOT_FUEL, remaining != nullptr ? std::make_shared<ItemInstance>(remaining) : nullptr);
			}
			changed = true;
		}
	}

	int parallelOps = 1;
	if (r != nullptr && d->parallel > 1 && r->inId != 0)
	{
		shared_ptr<ItemInstance> in = getItem(SLOT_INPUT);
		if (in != nullptr) parallelOps = (std::max)(1, (std::min)(d->parallel, in->count / (std::max)(1, r->inCount)));
	}
	const int use = energyUse(d) * parallelOps;

	const bool running = r != nullptr && fits && (electric ? m_energy >= use : (!usesPower || litTime > 0));
	if (running)
	{
		if (electric) m_energy -= use;
		else if (usesPower) litTime--;
		else { litTime = litDuration = 1; }
		const long long need = static_cast<long long>((std::max)(1, d->processTicks)) * 1000;
		m_progress += static_cast<long long>(1000.0f * speedMultiplier(d));
		if (m_progress >= need)
		{
			m_progress = 0;
			int ops = 0;
			while (ops < parallelOps && recipeReady(*r, true)) { runRecipe(*r); ops++; }
			changed = true;
		}
		tickCount = static_cast<int>((std::min)(199LL, m_progress * 200 / need));
	}
	else
	{
		if (tickCount != 0) changed = true;
		tickCount = 0;
		m_progress = 0;
		if (!usesPower && !electric) litTime = 0;
	}
	if (electric) updateGauge(d);
	setActiveState(running);
	if (changed) setChanged();
}

// --------------------------------------------------------------------------------------
// Energy blocks
// --------------------------------------------------------------------------------------

int ModMachineTileEntity::receiveEnergy(int amount, int intoFace, bool fromCable)
{
	ModMachineDef *d = def();
	if (d == nullptr || amount <= 0) return 0;
	if (d->multiblock.enabled && m_formed != 1) return 0;
	if (faceMode(intoFace) == FACE_DISABLED || faceMode(intoFace) == FACE_OUTPUT) return 0;
	switch (d->kind)
	{
	case MachineKind_Processor:
		if (d->energyPerTick <= 0) return 0;
		break;
	case MachineKind_Generator:
	case MachineKind_Tank:
	case MachineKind_Pipe:
	case MachineKind_Transporter:
	case MachineKind_Conductor:
	case MachineKind_Boiler:
	case MachineKind_Fission:
	case MachineKind_QioDrive:
	case MachineKind_QioDashboard:
		return 0;
	case MachineKind_Heater:
		if (d->energyPerTick <= 0) return 0;	// fuelwood heaters burn items
		break;
	case MachineKind_Fusion:
		if (m_ignited) return 0;				// only charges up for ignition
		break;
	case MachineKind_Entangloporter:
		return entangleEnergyIn(amount);
	case MachineKind_Storage:
		if (level && intoFace == (level->getData(x, y, z) & 7) && faceMode(intoFace) != FACE_INPUT) return 0;	// the front only outputs
		break;
	default:
		break;
	}
	const int room = capacity() - m_energy;
	const int accepted = (std::max)(0, (std::min)(room, amount));
	m_energy += accepted;
	return accepted;
}

void ModMachineTileEntity::pushEnergy(int face, int amount)
{
	if (amount <= 0 || m_energy <= 0 || level == nullptr) return;
	if (faceMode(face) == FACE_DISABLED || faceMode(face) == FACE_INPUT) return;
	const int nx = x + kFaceDx[face], ny = y + kFaceDy[face], nz = z + kFaceDz[face];
	shared_ptr<ModMachineTileEntity> other = dynamic_pointer_cast<ModMachineTileEntity>(level->getTileEntity(nx, ny, nz));
	if (other == nullptr) return;
	ModMachineDef *od = other->def();
	ModMachineDef *d = def();
	if (od == nullptr || d == nullptr) return;
	int send = (std::min)(amount, m_energy);
	if (d->kind == MachineKind_Cable && od->kind == MachineKind_Cable)
	{
		// cables share their charge: move half of the difference
		const int diff = m_energy - other->m_energy;
		if (diff <= 1) return;
		send = (std::min)(send, diff / 2);
	}
	const int moved = other->receiveEnergy(send, oppositeFace(face), d->kind == MachineKind_Cable);
	m_energy -= moved;
	if (moved > 0) other->setChanged();
}

void ModMachineTileEntity::chargeItemInSlot(ModMachineDef *d)
{
	// energy cubes charge rechargeable items, tanks refill items that run on their chemical
	shared_ptr<ItemInstance> item = getItem(SLOT_FUEL);
	if (item == nullptr || !item->isDamageableItem() || item->getDamageValue() <= 0) return;
	const ModItemRefill *rf = ModMachines::refillFor(item->id);
	if (rf == nullptr) return;
	int points = 0;
	if (rf->resource == "energy")
	{
		if (d->kind != MachineKind_Storage) return;
		const int budget = (std::max)(rf->perPoint, d->transfer + m_mbTransfer);
		points = (std::min)(item->getDamageValue(), (std::min)(budget, m_energy) / rf->perPoint);
		m_energy -= points * rf->perPoint;
	}
	else
	{
		if (d->kind != MachineKind_Tank) return;
		ModChemAmount &t = m_tanks[TANK_IN0];
		if (t.name != rf->resource) return;
		points = (std::min)(item->getDamageValue(), (std::min)(t.amount, 1000) / rf->perPoint);
		t.amount -= points * rf->perPoint;
		if (t.amount <= 0) { t.amount = 0; t.name.clear(); }
	}
	if (points > 0)
	{
		item->setAuxValue(item->getDamageValue() - points);
		setChanged();
	}
}

void ModMachineTileEntity::drainItemInSlot(ModMachineDef *d)
{
	// energy tablets & co. in the bottom slot of electric machines power them
	shared_ptr<ItemInstance> item = getItem(SLOT_FUEL);
	if (item == nullptr || !item->isDamageableItem()) return;
	const ModItemRefill *rf = ModMachines::refillFor(item->id);
	if (rf == nullptr || rf->resource != "energy") return;
	const int left = item->getMaxDamage() - item->getDamageValue();
	if (left <= 0) return;
	const int room = capacity() - m_energy;
	const int points = (std::min)(left, (std::min)(room, (std::max)(rf->perPoint, energyUse(d) * 4)) / rf->perPoint);
	if (points <= 0) return;
	item->setAuxValue(item->getDamageValue() + points);
	m_energy += points * rf->perPoint;
	setChanged();
}

void ModMachineTileEntity::tickEnergyBlock(ModMachineDef *d)
{
	const int before = m_energy;
	bool active = false;

	if (d->kind == MachineKind_Generator)
	{
		const int gen = static_cast<int>(d->generation * (d->multiblock.enabled ? m_mbSpeed : 1.0f));
		if (d->generatorType == 2)
		{
			// wind: open sky, more power the higher it stands (Mekanism: y 24 -> 255)
			if (level->canSeeSky(x, y + 1, z))
			{
				const float h = (std::max)(0.0f, (std::min)(1.0f, (y - 24) / 231.0f));
				const float weather = level->isThundering() ? 1.5f : (level->isRaining() ? 1.25f : 1.0f);
				const int windGen = static_cast<int>(gen * h * weather);
				if (windGen > 0)
				{
					m_energy = (std::min)(capacity(), m_energy + windGen);
					active = true;
				}
			}
		}
		else if (d->generatorType == 1)
		{
			// solar: daylight, open sky, no rain
			if (level->isDay() && level->canSeeSky(x, y + 1, z) && !level->isRainingAt(x, y + 1, z))
			{
				m_energy = (std::min)(capacity(), m_energy + gen);
				active = true;
			}
		}
		else if (!d->fuelChem.name.empty())
		{
			// chemical generators (gas-burning generator, turbine, fusion reactor)
			ModChemAmount &fuel = m_tanks[TANK_IN0];
			const int per = (std::max)(1, static_cast<int>(d->fuelChem.amount * (d->multiblock.enabled ? m_mbSpeed : 1.0f)));
			bool exhaustFits = true;
			if (!d->exhaustChem.name.empty())
			{
				const ModChemAmount &ex = m_tanks[TANK_OUT0];
				exhaustFits = (ex.amount == 0 || ex.name == d->exhaustChem.name) && ex.amount + d->exhaustChem.amount <= tankCapacity();
			}
			if (fuel.name == d->fuelChem.name && fuel.amount >= per && m_energy < capacity() && exhaustFits)
			{
				fuel.amount -= per;
				if (fuel.amount <= 0) { fuel.amount = 0; fuel.name.clear(); }
				m_energy = (std::min)(capacity(), m_energy + gen);
				if (!d->exhaustChem.name.empty())
				{
					m_tanks[TANK_OUT0].name = d->exhaustChem.name;
					m_tanks[TANK_OUT0].amount += d->exhaustChem.amount;
				}
				active = true;
			}
			tickCount = active ? (m_tickCounter * 10) % 200 : 0;
		}
		else
		{
			if (m_burn <= 0 && m_energy < capacity())
			{
				shared_ptr<ItemInstance> fuel = getItem(SLOT_FUEL);
				const int ticks = powerTicks(fuel);
				if (ticks > 0)
				{
					m_burn = m_burnTotal = ticks;
					fuel->count--;
					if (fuel->count <= 0)
					{
						Item *remaining = fuel->getItem() ? fuel->getItem()->getCraftingRemainingItem() : nullptr;
						setItem(SLOT_FUEL, remaining != nullptr ? std::make_shared<ItemInstance>(remaining) : nullptr);
					}
				}
			}
			if (m_burn > 0)
			{
				m_burn--;
				m_energy = (std::min)(capacity(), m_energy + gen);
				active = true;
			}
			// burn progress on the arrow
			tickCount = m_burnTotal > 0 ? 200 - m_burn * 200 / m_burnTotal : 0;
		}
		const int each = (std::max)(1, d->transfer + m_mbTransfer);
		for (int f = 0; f < 6; f++) pushEnergy(f, each);
	}
	else if (d->kind == MachineKind_Storage)
	{
		chargeItemInSlot(d);
		// power items (redstone...) placed in the bottom slot become FE
		shared_ptr<ItemInstance> fuel = getItem(SLOT_FUEL);
		const int fe = powerTicks(fuel) * 20;
		if (fe > 0 && m_energy + fe <= capacity())
		{
			m_energy += fe;
			fuel->count--;
			if (fuel->count <= 0) setItem(SLOT_FUEL, nullptr);
		}
		const int front = level->getData(x, y, z) & 7;
		const int out = d->transfer + m_mbTransfer;
		pushEnergy(front < 6 ? front : 3, out);
		// extra output faces chosen with the configurator
		for (int f = 0; f < 6; f++) if (f != front && faceMode(f) == FACE_OUTPUT) pushEnergy(f, out);
		active = m_energy > 0;
	}
	else if (d->kind == MachineKind_Cable)
	{
		const int each = (std::max)(1, d->transfer / 2);
		for (int f = 0; f < 6; f++) pushEnergy(f, each);
	}

	if (d->kind != MachineKind_Cable)
	{
		updateGauge(d);
		setActiveState(active);
	}
	if (m_energy != before) setChanged();
}

// --------------------------------------------------------------------------------------
// Chemicals / fluids
// --------------------------------------------------------------------------------------

int ModMachineTileEntity::receiveChem(const std::string &chem, int amount, int intoFace)
{
	ModMachineDef *d = def();
	if (d == nullptr || amount <= 0 || chem.empty()) return 0;
	if (d->multiblock.enabled && m_formed != 1) return 0;
	const int mode = faceMode(intoFace);
	if (mode == FACE_DISABLED || mode == FACE_OUTPUT) return 0;

	int tank = -1;
	switch (d->kind)
	{
	case MachineKind_Processor:
		tank = inputTankFor(d, chem);
		break;
	case MachineKind_Generator:
		if (d->fuelChem.name == chem) tank = TANK_IN0;
		break;
	case MachineKind_Tank:
		if (intoFace == frontFace() && mode != FACE_INPUT) return 0;	// the front only outputs
		tank = TANK_IN0;
		break;
	case MachineKind_Pipe:
		tank = TANK_IN0;
		break;
	case MachineKind_Boiler:
		if (chem == "minecraft:water") tank = TANK_IN0;
		else if (chem == "mekanism:superheated_sodium") tank = TANK_IN1;
		break;
	case MachineKind_Fission:
		if (chem == d->fuelChem.name) tank = TANK_IN0;
		else if (chem == "minecraft:water" || chem == "mekanism:sodium") tank = TANK_IN1;
		break;
	case MachineKind_Fusion:
		if (chem == d->fuelChem.name) tank = TANK_IN0;
		break;
	case MachineKind_Entangloporter:
		tank = TANK_IN0;	// redirected to the shared frequency buffer in tickEntangloporter
		break;
	default:
		return 0;
	}
	if (tank < 0) return 0;
	ModChemAmount &t = m_tanks[tank];
	if (t.amount > 0 && t.name != chem) return 0;
	const int room = tankCapacity() - t.amount;
	const int n = (std::max)(0, (std::min)(room, amount));
	if (n > 0)
	{
		t.name = chem;
		t.amount += n;
		setChanged();
	}
	return n;
}

int ModMachineTileEntity::pushChem(int tank, int face, int amount)
{
	ModChemAmount &t = m_tanks[tank];
	if (t.amount <= 0 || amount <= 0 || level == nullptr) return 0;
	shared_ptr<ModMachineTileEntity> other = dynamic_pointer_cast<ModMachineTileEntity>(level->getTileEntity(x + kFaceDx[face], y + kFaceDy[face], z + kFaceDz[face]));
	if (other == nullptr) return 0;
	const std::string name = t.name;
	const int moved = other->receiveChem(name, (std::min)(amount, t.amount), oppositeFace(face));
	t.amount -= moved;
	if (t.amount <= 0) { t.amount = 0; t.name.clear(); }
	return moved;
}

void ModMachineTileEntity::pushOutputs(ModMachineDef *d)
{
	// output tanks go to pipes / tanks around, and to machines on output faces
	const int rate = (std::max)(250, d->transfer > 0 ? d->transfer : 1000);
	for (int tank = TANK_OUT0; tank <= TANK_OUT1; tank++)
	{
		if (m_tanks[tank].amount <= 0) continue;
		for (int f = 0; f < 6 && m_tanks[tank].amount > 0; f++)
		{
			const int mode = faceMode(f);
			if (mode == FACE_DISABLED || mode == FACE_INPUT) continue;
			shared_ptr<ModMachineTileEntity> other = dynamic_pointer_cast<ModMachineTileEntity>(level->getTileEntity(x + kFaceDx[f], y + kFaceDy[f], z + kFaceDz[f]));
			if (other == nullptr || other->def() == nullptr) continue;
			const int ok = other->def()->kind;
			if (mode != FACE_OUTPUT && ok != MachineKind_Pipe && ok != MachineKind_Tank) continue;
			pushChem(tank, f, rate);
		}
	}
}

void ModMachineTileEntity::tickTank(ModMachineDef *d)
{
	chargeItemInSlot(d);
	// buckets placed in the input slot are emptied into the tank
	shared_ptr<ItemInstance> in = getItem(SLOT_INPUT);
	if (in != nullptr && (in->id == Item::bucket_water_Id || in->id == Item::bucket_lava_Id) && getItem(SLOT_RESULT) == nullptr)
	{
		const std::string chem = in->id == Item::bucket_water_Id ? "minecraft:water" : "minecraft:lava";
		ModChemAmount &t = m_tanks[TANK_IN0];
		if ((t.amount == 0 || t.name == chem) && t.amount + 1000 <= tankCapacity())
		{
			t.name = chem;
			t.amount += 1000;
			setItem(SLOT_INPUT, nullptr);
			setItem(SLOT_RESULT, std::make_shared<ItemInstance>(Item::bucket_empty_Id, 1, 0));
			setChanged();
		}
	}
	// the front (and output faces) feed the next block
	const int rate = (std::max)(1000, d->transfer);
	const int front = frontFace();
	for (int f = 0; f < 6; f++)
	{
		if ((f == front && faceMode(f) != FACE_INPUT) || faceMode(f) == FACE_OUTPUT) pushChem(TANK_IN0, f, rate);
	}
	updateGauge(d);
	setActiveState(m_tanks[TANK_IN0].amount > 0);
}

void ModMachineTileEntity::tickPipe(ModMachineDef *d)
{
	ModChemAmount &t = m_tanks[TANK_IN0];
	if (t.amount <= 0) return;
	const int rate = (std::max)(10, d->transfer);
	for (int f = 0; f < 6 && t.amount > 0; f++)
	{
		if (faceMode(f) == FACE_DISABLED) continue;
		shared_ptr<ModMachineTileEntity> other = dynamic_pointer_cast<ModMachineTileEntity>(level->getTileEntity(x + kFaceDx[f], y + kFaceDy[f], z + kFaceDz[f]));
		if (other == nullptr || other->def() == nullptr) continue;
		if (other->def()->kind == MachineKind_Pipe)
		{
			const ModChemAmount &o = other->m_tanks[TANK_IN0];
			if (o.amount > 0 && o.name != t.name) continue;
			const int diff = t.amount - o.amount;
			if (diff <= 1) continue;
			pushChem(TANK_IN0, f, (std::min)(rate, diff / 2));
		}
		else
		{
			pushChem(TANK_IN0, f, rate);
		}
	}
}

// --------------------------------------------------------------------------------------
// Items: transporters and ejection
// --------------------------------------------------------------------------------------

int ModMachineTileEntity::acceptItem(shared_ptr<ItemInstance> item, int intoFace)
{
	ModMachineDef *d = def();
	if (d == nullptr || item == nullptr || item->count <= 0) return 0;
	const int mode = faceMode(intoFace);
	if (d->kind == MachineKind_Transporter)
	{
		if (!m_buffer.empty()) return 0;
		shared_ptr<ItemInstance> copy = item->copy();
		m_buffer.push_back(copy);
		m_lastFace = intoFace;
		return copy->count;
	}
	if (mode == FACE_OUTPUT || mode == FACE_DISABLED) return 0;
	switch (d->kind)
	{
	case MachineKind_Processor:
	case MachineKind_Generator:
	case MachineKind_Storage:
	case MachineKind_Tank:
	case MachineKind_Pump:
	case MachineKind_Miner:
	case MachineKind_Teleporter:
	case MachineKind_Heater:
	case MachineKind_Seismic:
	case MachineKind_ModStation:
	case MachineKind_QioImporter:
	case MachineKind_Entangloporter:
		break;
	default:
		return 0;
	}
	int moved = 0;
	for (int slot = SLOT_INPUT; slot <= SLOT_FUEL && moved < item->count; slot++)
	{
		if (!canPlaceItem(slot, item)) continue;
		shared_ptr<ItemInstance> cur = getItem(slot);
		const int max = (std::min)(getMaxStackSize(), item->getMaxStackSize());
		if (cur == nullptr)
		{
			shared_ptr<ItemInstance> copy = item->copy();
			copy->count = (std::min)(max, item->count - moved);
			setItem(slot, copy);
			moved += copy->count;
		}
		else if (cur->id == item->id && cur->getAuxValue() == item->getAuxValue() && !cur->hasTag() && !item->hasTag())
		{
			const int n = (std::min)(max - cur->count, item->count - moved);
			if (n > 0) { cur->count += n; moved += n; }
		}
	}
	if (moved > 0) setChanged();
	return moved;
}

bool ModMachineTileEntity::storeOutput(shared_ptr<ItemInstance> item)
{
	if (item == nullptr) return false;
	for (auto &b : m_buffer)
	{
		if (b->id == item->id && b->getAuxValue() == item->getAuxValue() && !b->hasTag() && !item->hasTag() && b->count + item->count <= b->getMaxStackSize())
		{
			b->count += item->count;
			return true;
		}
	}
	m_buffer.push_back(item);
	return true;
}

void ModMachineTileEntity::ejectItems(ModMachineDef *d)
{
	if ((m_tickCounter % 8) != 0) return;
	const bool miner = d->kind == MachineKind_Miner;
	for (int f = 0; f < 6; f++)
	{
		const int mode = faceMode(f);
		if (mode == FACE_DISABLED || mode == FACE_INPUT) continue;
		shared_ptr<TileEntity> te = level->getTileEntity(x + kFaceDx[f], y + kFaceDy[f], z + kFaceDz[f]);
		if (te == nullptr) continue;
		shared_ptr<ModMachineTileEntity> mm = dynamic_pointer_cast<ModMachineTileEntity>(te);
		const bool transporter = mm != nullptr && mm->def() != nullptr && mm->def()->kind == MachineKind_Transporter;
		// transporters always take outputs; other inventories only on output faces (miners: any side)
		if (!transporter && !(m_eject && (mode == FACE_OUTPUT || miner))) continue;
		if (mm != nullptr && !transporter && mm->def() != nullptr && mm->def()->kind == MachineKind_Miner) continue;

		auto give = [&](shared_ptr<ItemInstance> item) -> int {
			if (mm != nullptr) return mm->acceptItem(item, oppositeFace(f));
			return insertInto(dynamic_pointer_cast<Container>(te), item);
		};

		// buffered (secondary / mined) items first
		for (size_t i = 0; i < m_buffer.size(); )
		{
			const int n = give(m_buffer[i]);
			m_buffer[i]->count -= n;
			if (n > 0) setChanged();
			if (m_buffer[i]->count <= 0) m_buffer.erase(m_buffer.begin() + i);
			else ++i;
		}
		shared_ptr<ItemInstance> out = getItem(SLOT_RESULT);
		if (out != nullptr && d->kind != MachineKind_Tank)
		{
			const int n = give(out);
			if (n > 0)
			{
				out->count -= n;
				if (out->count <= 0) setItem(SLOT_RESULT, nullptr);
				setChanged();
			}
		}
	}
}

void ModMachineTileEntity::tickTransporter(ModMachineDef *d)
{
	const int interval = (std::max)(1, 8 / (std::max)(1, d->transfer / 8 + 1));
	if ((m_tickCounter % interval) != 0) return;

	// pull from inventories on faces set to "input" with the configurator
	if (m_buffer.empty())
	{
		for (int f = 0; f < 6 && m_buffer.empty(); f++)
		{
			if (faceMode(f) != FACE_INPUT) continue;
			shared_ptr<TileEntity> te = level->getTileEntity(x + kFaceDx[f], y + kFaceDy[f], z + kFaceDz[f]);
			shared_ptr<Container> c = dynamic_pointer_cast<Container>(te);
			if (c == nullptr) continue;
			const bool furnaceLike = dynamic_pointer_cast<FurnaceTileEntity>(te) != nullptr;
			for (unsigned int s = 0; s < c->getContainerSize(); s++)
			{
				if (furnaceLike && s != static_cast<unsigned int>(SLOT_RESULT)) continue;
				shared_ptr<ItemInstance> it = c->getItem(s);
				if (it == nullptr || it->count <= 0) continue;
				const int n = (std::min)(it->count, (std::max)(1, d->transfer));
				shared_ptr<ItemInstance> taken = c->removeItem(s, n);
				if (taken != nullptr)
				{
					m_buffer.push_back(taken);
					m_lastFace = f;
					c->setChanged();
				}
				break;
			}
		}
	}
	if (m_buffer.empty()) return;

	shared_ptr<ItemInstance> item = m_buffer[0];
	// 1) deliver into a real inventory next to us
	for (int f = 0; f < 6 && item->count > 0; f++)
	{
		if (f == m_lastFace || faceMode(f) == FACE_INPUT || faceMode(f) == FACE_DISABLED) continue;
		shared_ptr<TileEntity> te = level->getTileEntity(x + kFaceDx[f], y + kFaceDy[f], z + kFaceDz[f]);
		if (te == nullptr) continue;
		shared_ptr<ModMachineTileEntity> mm = dynamic_pointer_cast<ModMachineTileEntity>(te);
		int n = 0;
		if (mm != nullptr)
		{
			if (mm->def() != nullptr && mm->def()->kind == MachineKind_Transporter) continue;
			n = mm->acceptItem(item, oppositeFace(f));
		}
		else n = insertInto(dynamic_pointer_cast<Container>(te), item);
		item->count -= n;
	}
	if (item->count <= 0) { m_buffer.clear(); m_lastFace = -1; setChanged(); return; }

	// 2) pass it along the transporter line (round robin)
	for (int i = 0; i < 6; i++)
	{
		const int f = (m_tickCounter / interval + i) % 6;
		if (f == m_lastFace || faceMode(f) == FACE_DISABLED) continue;
		shared_ptr<ModMachineTileEntity> mm = dynamic_pointer_cast<ModMachineTileEntity>(level->getTileEntity(x + kFaceDx[f], y + kFaceDy[f], z + kFaceDz[f]));
		if (mm == nullptr || mm->def() == nullptr || mm->def()->kind != MachineKind_Transporter) continue;
		if (mm->acceptItem(item, oppositeFace(f)) > 0)
		{
			m_buffer.clear();
			m_lastFace = -1;
			setChanged();
			return;
		}
	}
	// dead end: allow going back next time
	m_lastFace = -1;
}

// --------------------------------------------------------------------------------------
// Electric pump
// --------------------------------------------------------------------------------------

void ModMachineTileEntity::tickPump(ModMachineDef *d)
{
	drainItemInSlot(d);
	ModChemAmount &t = m_tanks[TANK_OUT0];
	const int opCost = (std::max)(1, energyUse(d));
	const bool canWork = t.amount + 1000 <= tankCapacity() && m_energy >= opCost;
	bool active = false;
	if (canWork)
	{
		m_energy -= opCost;
		m_progress += static_cast<long long>(1000.0f * speedMultiplier(d));
		active = true;
		const long long need = static_cast<long long>((std::max)(1, d->processTicks)) * 1000;
		if (m_progress >= need)
		{
			m_progress = 0;
			// breadth-first search through the liquid below for a source block
			std::vector<int> queue;
			std::vector<long long> seen;
			queue.push_back(x); queue.push_back(y - 1); queue.push_back(z);
			seen.push_back(posKey(x, y - 1, z));
			size_t head = 0;
			bool done = false;
			while (head < queue.size() && seen.size() < 2048 && !done)
			{
				const int px = queue[head], py = queue[head + 1], pz = queue[head + 2];
				head += 3;
				const int id = level->getTile(px, py, pz);
				const bool water = id == Tile::water_Id || id == Tile::calmWater_Id;
				const bool lava = id == Tile::lava_Id || id == Tile::calmLava_Id;
				if (!water && !lava) continue;
				if (std::abs(px - x) > d->radius * 4 || std::abs(pz - z) > d->radius * 4) continue;
				const std::string chem = water ? "minecraft:water" : "minecraft:lava";
				if (water && m_upgrades[UPG_FILTER] > 0 && level->getData(px, py, pz) == 0)
				{
					// filter upgrade: separate heavy water out of the water, the source block stays
					if (t.amount == 0 || t.name == "mekanism:heavy_water")
					{
						t.name = "mekanism:heavy_water";
						t.amount += 10;
						done = true;
					}
					break;
				}
				if (level->getData(px, py, pz) == 0 && (t.amount == 0 || t.name == chem))
				{
					level->setTileAndUpdate(px, py, pz, 0);
					t.name = chem;
					t.amount += 1000;
					done = true;
					break;
				}
				for (int f = 0; f < 6; f++)
				{
					if (f == Facing::UP && py + 1 >= y) continue;
					const int nx = px + kFaceDx[f], ny = py + kFaceDy[f], nz = pz + kFaceDz[f];
					const long long k = posKey(nx, ny, nz);
					if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
					seen.push_back(k);
					queue.push_back(nx); queue.push_back(ny); queue.push_back(nz);
				}
			}
			if (!done) active = false;
			setChanged();
		}
	}
	updateGauge(d);
	setActiveState(active);
}

// --------------------------------------------------------------------------------------
// Digital miner
// --------------------------------------------------------------------------------------

static bool isOreTile(int id)
{
	switch (id)
	{
	case 14: case 15: case 16: case 21: case 56: case 73: case 74: case 129: case 153:
		return true;
	default:
		break;
	}
	if (id >= 256)
	{
		const std::string name = IDMapping::get()->getByID(id);
		return name.find("ore") != std::string::npos && name.find("store") == std::string::npos;
	}
	return false;
}

void ModMachineTileEntity::tickMiner(ModMachineDef *d)
{
	drainItemInSlot(d);
	const int r = (std::max)(1, d->radius);
	const int side = r * 2 + 1;
	const int layers = (std::max)(0, y - 1);
	const int total = side * side * layers;
	const int opCost = (std::max)(1, energyUse(d));
	bool active = false;
	if (m_minerCursor < total && m_buffer.size() < 9 && m_energy >= opCost)
	{
		m_energy -= opCost;
		active = true;
		m_progress += static_cast<long long>(1000.0f * speedMultiplier(d));
		const long long need = static_cast<long long>((std::max)(1, d->processTicks)) * 1000;
		while (m_progress >= need && m_buffer.size() < 9 && m_minerCursor < total)
		{
			m_progress -= need;
			for (int scanned = 0; scanned < 512 && m_minerCursor < total; scanned++)
			{
				const int idx = m_minerCursor++;
				const int layer = idx / (side * side);
				const int rem = idx % (side * side);
				const int px = x - r + rem % side, pz = z - r + rem / side, py = y - 1 - layer;
				if (py < 1) continue;
				const int id = level->getTile(px, py, pz);
				if (id <= 0 || id == Tile::unbreakable_Id || id == Tile::water_Id || id == Tile::calmWater_Id || id == Tile::lava_Id || id == Tile::calmLava_Id) continue;
				Tile *tile = Tile::tiles[id];
				if (tile == nullptr || tile->isEntityTile()) continue;
				if (!m_minerAll && !isOreTile(id)) continue;
				const int data = level->getData(px, py, pz);
				const int count = tile->getResourceCount(level->random);
				const int res = tile->getResource(data, level->random, 0);
				if (res > 0 && count > 0) storeOutput(std::make_shared<ItemInstance>(res, count, tile->getSpawnResourcesAuxValue(data)));
				level->setTileAndUpdate(px, py, pz, 0);
				break;
			}
		}
		tickCount = total > 0 ? static_cast<int>((long long)m_minerCursor * 199 / total) : 0;
		setChanged();
	}
	updateGauge(d);
	setActiveState(active);
}

// --------------------------------------------------------------------------------------
// Multiblocks
// --------------------------------------------------------------------------------------

void ModMachineTileEntity::scanStructure(ModMachineDef *d)
{
	const ModMultiblockDef &mb = d->multiblock;
	const int selfTile = level->getTile(x, y, z);
	auto isFrame = [&](int id) -> bool {
		if (id == selfTile) return true;
		return std::find(mb.frameIds.begin(), mb.frameIds.end(), id) != mb.frameIds.end();
	};

	// flood fill the frame blocks connected to the controller
	const int maxSize = (std::max)(3, mb.maxSize);
	int minX = x, maxX = x, minY = y, maxY = y, minZ = z, maxZ = z;
	std::vector<int> queue;
	std::vector<long long> seen;
	queue.push_back(x); queue.push_back(y); queue.push_back(z);
	seen.push_back(posKey(x, y, z));
	size_t head = 0;
	while (head < queue.size() && seen.size() < 6000)
	{
		const int px = queue[head], py = queue[head + 1], pz = queue[head + 2];
		head += 3;
		minX = (std::min)(minX, px); maxX = (std::max)(maxX, px);
		minY = (std::min)(minY, py); maxY = (std::max)(maxY, py);
		minZ = (std::min)(minZ, pz); maxZ = (std::max)(maxZ, pz);
		for (int f = 0; f < 6; f++)
		{
			const int nx = px + kFaceDx[f], ny = py + kFaceDy[f], nz = pz + kFaceDz[f];
			if (std::abs(nx - x) >= maxSize || std::abs(ny - y) >= maxSize || std::abs(nz - z) >= maxSize) continue;
			const long long k = posKey(nx, ny, nz);
			if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
			if (!isFrame(level->getTile(nx, ny, nz))) continue;
			seen.push_back(k);
			queue.push_back(nx); queue.push_back(ny); queue.push_back(nz);
		}
	}

	const int sx = maxX - minX + 1, sy = maxY - minY + 1, sz = maxZ - minZ + 1;
	const int minSize = (std::max)(3, mb.minSize);
	bool ok = sx >= minSize && sy >= minSize && sz >= minSize && sx <= maxSize && sy <= maxSize && sz <= maxSize;
	int volume = 0;
	long long cap = 0, tank = 0, transfer = 0;
	float speed = 0.0f;
	for (int px = minX; ok && px <= maxX; px++)
	{
		for (int py = minY; ok && py <= maxY; py++)
		{
			for (int pz = minZ; ok && pz <= maxZ; pz++)
			{
				const bool boundary = px == minX || px == maxX || py == minY || py == maxY || pz == minZ || pz == maxZ;
				const int id = level->getTile(px, py, pz);
				if (boundary)
				{
					if (!isFrame(id)) ok = false;
					continue;
				}
				volume++;
				if (id == 0)
				{
					if (!mb.interiorAir) ok = false;
					continue;
				}
				auto it = mb.innerIds.find(id);
				if (it == mb.innerIds.end()) { ok = false; continue; }
				if (d->kind == MachineKind_Tank) tank += it->second.capacity;
				else cap += it->second.capacity;
				transfer += it->second.transfer;
				speed += it->second.speed;
			}
		}
	}

	const int was = m_formed;
	m_formed = ok ? 1 : 0;
	m_mbMin[0] = minX; m_mbMin[1] = minY; m_mbMin[2] = minZ;
	m_mbMax[0] = maxX; m_mbMax[1] = maxY; m_mbMax[2] = maxZ;
	if (ok)
	{
		m_mbVolume = volume;
		m_mbHeight = sy;
		m_mbCapacity = static_cast<int>((std::min)(2000000000LL, cap + (long long)mb.capacityPerVolume * volume));
		m_mbTank = static_cast<int>((std::min)(2000000000LL, tank + (long long)mb.tankPerVolume * volume));
		m_mbTransfer = static_cast<int>((std::min)(2000000000LL, transfer));
		m_mbSpeed = (std::max)(0.05f, 1.0f + speed + mb.speedPerVolume * volume + mb.speedPerHeight * (sy - 3));
	}
	if (was != m_formed) setChanged();
}

// --------------------------------------------------------------------------------------
// Player interaction
// --------------------------------------------------------------------------------------

static const wchar_t *faceModeName(int mode)
{
	switch (mode)
	{
	case ModMachineTileEntity::FACE_INPUT: return L"entrada";
	case ModMachineTileEntity::FACE_OUTPUT: return L"sa\u00EDda";
	case ModMachineTileEntity::FACE_DISABLED: return L"desligada";
	default: return L"padr\u00E3o";
	}
}

static const wchar_t *faceName(int face)
{
	static const wchar_t *names[6] = { L"baixo", L"cima", L"norte", L"sul", L"oeste", L"leste" };
	return (face >= 0 && face < 6) ? names[face] : L"?";
}

void ModMachineTileEntity::sendStatus(shared_ptr<Player> player)
{
	ModMachineDef *d = def();
	if (d == nullptr || player == nullptr) return;
	player->sendMessage(L"== " + d->name + L" ==");
	if (d->multiblock.enabled)
	{
		if (m_formed == 1) player->sendMessage(L"Estrutura: formada (" + std::to_wstring(m_mbVolume) + L" blocos internos, altura " + std::to_wstring(m_mbHeight) + L")");
		else player->sendMessage(L"Estrutura: incompleta");
	}
	if (capacity() > 0 && d->kind != MachineKind_Tank && d->kind != MachineKind_Pipe && d->kind != MachineKind_Transporter)
		player->sendMessage(L"Energia: " + fmtNumLocal(m_energy) + L" / " + fmtNumLocal(capacity()) + L" FE");
	for (int t = 0; t < TANK_COUNT; t++)
	{
		if (m_tanks[t].amount <= 0) continue;
		player->sendMessage(std::wstring(t < TANK_OUT0 ? L"Tanque: " : L"Sa\u00EDda: ") + ModMachines::chemicalName(m_tanks[t].name) + L" " + fmtNumLocal(m_tanks[t].amount) + L" / " + fmtNumLocal(tankCapacity()) + L" mB");
	}
	if (d->kind == MachineKind_Processor && m_progress > 0)
	{
		const long long need = static_cast<long long>((std::max)(1, d->processTicks)) * 1000;
		player->sendMessage(L"Progresso: " + std::to_wstring(static_cast<int>(m_progress * 100 / need)) + L"%");
	}
	if (m_upgrades[UPG_SPEED] || m_upgrades[UPG_ENERGY] || m_upgrades[UPG_MUFFLING])
		player->sendMessage(L"Upgrades: velocidade " + std::to_wstring(m_upgrades[UPG_SPEED]) + L", energia " + std::to_wstring(m_upgrades[UPG_ENERGY]) + L", silenciador " + std::to_wstring(m_upgrades[UPG_MUFFLING]));
	if (hasHeat()) player->sendMessage(L"Temperatura: " + std::to_wstring(static_cast<int>(m_temp)) + L" K");
	if (d->kind == MachineKind_Fission) player->sendMessage(std::wstring(L"Reator: ") + (m_enabled ? L"ligado" : L"desligado") + L" | queima " + std::to_wstring(m_burnRate) + L" mB/t | dano " + std::to_wstring(m_damage / 100) + L"%");
	if (d->kind == MachineKind_Fusion) player->sendMessage(std::wstring(L"Plasma: ") + (m_ignited ? std::wstring(L"aceso") : L"apagado (precisa de " + fmtNumLocal(d->ignitionEnergy) + L" FE para acender)"));
	if (d->kind == MachineKind_Teleporter || d->kind == MachineKind_Entangloporter || (d->kind >= MachineKind_QioDrive && d->kind <= MachineKind_QioExporter)) player->sendMessage(L"Frequ\u00EAncia: " + std::to_wstring(m_frequency));
	if (d->kind == MachineKind_Miner) player->sendMessage(std::wstring(L"Filtro: ") + (m_minerAll ? L"todos os blocos" : L"s\u00F3 min\u00E9rios") + L" | " + std::to_wstring(m_minerCursor) + L" blocos verificados");
	if (!m_buffer.empty()) player->sendMessage(L"Itens aguardando: " + std::to_wstring(m_buffer.size()) + L" pilha(s)");
	if (!isConnectorKind(d->kind) && d->kind != MachineKind_Storage && d->kind != MachineKind_Tank)
		player->sendMessage(std::wstring(L"Auto-eject: ") + (m_eject ? L"ligado" : L"desligado"));
	std::wstring faces;
	for (int f = 0; f < 6; f++) if (m_faceMode[f] != FACE_DEFAULT) faces += std::wstring(faces.empty() ? L"" : L", ") + faceName(f) + L"=" + faceModeName(m_faceMode[f]);
	if (!faces.empty()) player->sendMessage(L"Faces: " + faces);
}

bool ModMachineTileEntity::interact(shared_ptr<Player> player, int face)
{
	ModMachineDef *d = def();
	if (d == nullptr || player == nullptr) return false;
	shared_ptr<ItemInstance> held = player->inventory->getSelected();
	const bool creative = player->abilities.instabuild;

	if (held == nullptr && d->kind == MachineKind_QioDashboard && !player->isSneaking())
	{
		openQioDashboard(player);
		return true;
	}
	if (held == nullptr)
	{
		if (player->isSneaking() || isConnectorKind(d->kind))
		{
			sendStatus(player);
			return true;
		}
		return false;
	}

	// buckets
	if (held->id == Item::bucket_water_Id || held->id == Item::bucket_lava_Id)
	{
		const std::string chem = held->id == Item::bucket_water_Id ? "minecraft:water" : "minecraft:lava";
		int tank = -1;
		if (d->kind == MachineKind_Tank) tank = TANK_IN0;
		else if (d->kind == MachineKind_Processor) tank = inputTankFor(d, chem);
		else if (d->kind == MachineKind_Generator && d->fuelChem.name == chem) tank = TANK_IN0;
		if (tank < 0) return false;
		ModChemAmount &t = m_tanks[tank];
		if ((t.amount == 0 || t.name == chem) && t.amount + 1000 <= tankCapacity())
		{
			t.name = chem;
			t.amount += 1000;
			if (!creative) player->inventory->setItem(player->inventory->selected, std::make_shared<ItemInstance>(Item::bucket_empty_Id, 1, 0));
			setChanged();
		}
		return true;
	}
	if (held->id == Item::bucket_empty_Id)
	{
		for (int t = 0; t < TANK_COUNT; t++)
		{
			ModChemAmount &tk = m_tanks[t];
			if (tk.amount < 1000 || (tk.name != "minecraft:water" && tk.name != "minecraft:lava")) continue;
			if (d->kind != MachineKind_Tank && t < TANK_OUT0) continue;
			const int filled = tk.name == "minecraft:water" ? Item::bucket_water_Id : Item::bucket_lava_Id;
			tk.amount -= 1000;
			if (tk.amount <= 0) { tk.amount = 0; tk.name.clear(); }
			if (!creative)
			{
				if (held->count <= 1) player->inventory->setItem(player->inventory->selected, std::make_shared<ItemInstance>(filled, 1, 0));
				else
				{
					held->count--;
					shared_ptr<ItemInstance> full = std::make_shared<ItemInstance>(filled, 1, 0);
					if (!player->inventory->add(full)) player->drop(full);
				}
			}
			setChanged();
			return true;
		}
		return false;
	}

	const int role = ModMachines::toolRole(held->id);
	if (interactAdvanced(player, face, held, role)) return true;
	if (role == MachineTool_Configurator)
	{
		if (d->kind == MachineKind_Teleporter || d->kind == MachineKind_Entangloporter || (d->kind >= MachineKind_QioDrive && d->kind <= MachineKind_QioExporter))
		{
			m_frequency = (m_frequency + 1) % 16;
			player->sendMessage(L"Frequ\u00EAncia: " + std::to_wstring(m_frequency));
		}
		else if (d->kind == MachineKind_Miner && face == Facing::UP)
		{
			m_minerAll = !m_minerAll;
			m_minerCursor = 0;
			player->sendMessage(std::wstring(L"Digital Miner: ") + (m_minerAll ? L"minerando todos os blocos" : L"minerando s\u00F3 min\u00E9rios") + L" (reiniciado)");
		}
		else if (!isConnectorKind(d->kind) && face == frontFace() && d->kind != MachineKind_Storage && d->kind != MachineKind_Tank)
		{
			m_eject = !m_eject;
			player->sendMessage(std::wstring(L"Auto-eject: ") + (m_eject ? L"ligado" : L"desligado"));
		}
		else if (face >= 0 && face < 6)
		{
			const int modes = d->kind == MachineKind_Transporter ? 2 : static_cast<int>(FACE_MODE_COUNT);	// transporters: default / pull
			m_faceMode[face] = static_cast<unsigned char>((m_faceMode[face] + 1) % modes);
			const bool pull = d->kind == MachineKind_Transporter && m_faceMode[face] == FACE_INPUT;
			player->sendMessage(std::wstring(L"Face ") + faceName(face) + L": " + (pull ? L"puxar itens" : faceModeName(m_faceMode[face])));
		}
		setChanged();
		return true;
	}
	if (role == MachineTool_UpgradeSpeed || role == MachineTool_UpgradeEnergy || role == MachineTool_UpgradeMuffling || role == MachineTool_UpgradeFilter)
	{
		if (!d->upgrades || isConnectorKind(d->kind) || d->kind == MachineKind_Tank) return false;
		const int u = role == MachineTool_UpgradeSpeed ? UPG_SPEED : role == MachineTool_UpgradeEnergy ? UPG_ENERGY : role == MachineTool_UpgradeMuffling ? UPG_MUFFLING : UPG_FILTER;
		const int maxU = u == UPG_FILTER ? 1 : MAX_UPGRADES;
		if (m_upgrades[u] >= maxU)
		{
			player->sendMessage(L"Este upgrade j\u00E1 est\u00E1 no m\u00E1ximo (" + std::to_wstring(maxU) + L")");
			return true;
		}
		m_upgrades[u]++;
		if (!creative)
		{
			held->count--;
			if (held->count <= 0) player->inventory->setItem(player->inventory->selected, nullptr);
		}
		player->sendMessage(L"Upgrade instalado (" + std::to_wstring(m_upgrades[u]) + L"/" + std::to_wstring(maxU) + L")");
		setChanged();
		return true;
	}
	return false;
}

void ModMachineTileEntity::dropExtras(Level *lvl)
{
	auto drop = [&](shared_ptr<ItemInstance> item) {
		if (item == nullptr || item->count <= 0) return;
		shared_ptr<ItemEntity> e = std::make_shared<ItemEntity>(lvl, x + 0.5, y + 0.5, z + 0.5, item);
		lvl->addEntity(e);
	};
	const int roles[UPG_COUNT] = { MachineTool_UpgradeSpeed, MachineTool_UpgradeEnergy, MachineTool_UpgradeMuffling, MachineTool_UpgradeFilter };
	for (int u = 0; u < UPG_COUNT; u++)
	{
		const int id = ModMachines::itemForRole(roles[u]);
		if (id > 0 && m_upgrades[u] > 0) drop(std::make_shared<ItemInstance>(id, m_upgrades[u], 0));
		m_upgrades[u] = 0;
	}
	for (auto &b : m_buffer) drop(b);
	m_buffer.clear();
}

// --------------------------------------------------------------------------------------
// Save / load
// --------------------------------------------------------------------------------------

void ModMachineTileEntity::load(CompoundTag *base)
{
	FurnaceTileEntity::load(base);
	const wstring id = base->getString(L"HSMachineId");
	m_machineId.assign(id.begin(), id.end());
	m_def = nullptr;
	m_energy = base->getInt(L"HSEnergy");
	m_burn = base->getInt(L"HSBurn");
	m_burnTotal = base->getInt(L"HSBurnTotal");
	for (int t = 0; t < TANK_COUNT; t++)
	{
		const wstring n = base->getString(L"HSTank" + std::to_wstring(t));
		m_tanks[t].name.assign(n.begin(), n.end());
		m_tanks[t].amount = base->getInt(L"HSTankAmt" + std::to_wstring(t));
		if (m_tanks[t].name.empty()) m_tanks[t].amount = 0;
	}
	for (int f = 0; f < 6; f++) m_faceMode[f] = static_cast<unsigned char>(base->getInt(L"HSFace" + std::to_wstring(f)) % FACE_MODE_COUNT);
	m_eject = !base->getBoolean(L"HSNoEject");
	m_upgrades[UPG_SPEED] = (std::min)(static_cast<int>(MAX_UPGRADES), base->getInt(L"HSUpgSpeed"));
	m_upgrades[UPG_ENERGY] = (std::min)(static_cast<int>(MAX_UPGRADES), base->getInt(L"HSUpgEnergy"));
	m_upgrades[UPG_MUFFLING] = (std::min)(static_cast<int>(MAX_UPGRADES), base->getInt(L"HSUpgMuffling"));
	m_frequency = base->getInt(L"HSFreq");
	m_minerAll = base->getBoolean(L"HSMinerAll");
	m_minerCursor = base->getInt(L"HSMinerCursor");
	m_upgrades[UPG_FILTER] = (std::min)(1, base->getInt(L"HSUpgFilter"));
	loadAdvanced(base);
	m_buffer.clear();
	if (base->contains(L"HSBuffer"))
	{
		ListTag<CompoundTag> *buf = (ListTag<CompoundTag> *)base->getList(L"HSBuffer");
		for (int i = 0; buf != nullptr && i < buf->size(); i++)
		{
			shared_ptr<ItemInstance> it = ItemInstance::fromTag(buf->get(i));
			if (it != nullptr) m_buffer.push_back(it);
		}
	}
}

void ModMachineTileEntity::save(CompoundTag *base)
{
	FurnaceTileEntity::save(base);
	base->putString(L"id", L"HSMachine");
	base->putString(L"HSMachineId", wstring(m_machineId.begin(), m_machineId.end()));
	base->putInt(L"HSEnergy", m_energy);
	base->putInt(L"HSBurn", m_burn);
	base->putInt(L"HSBurnTotal", m_burnTotal);
	for (int t = 0; t < TANK_COUNT; t++)
	{
		base->putString(L"HSTank" + std::to_wstring(t), wstring(m_tanks[t].name.begin(), m_tanks[t].name.end()));
		base->putInt(L"HSTankAmt" + std::to_wstring(t), m_tanks[t].amount);
	}
	for (int f = 0; f < 6; f++) base->putInt(L"HSFace" + std::to_wstring(f), m_faceMode[f]);
	base->putBoolean(L"HSNoEject", !m_eject);
	base->putInt(L"HSUpgSpeed", m_upgrades[UPG_SPEED]);
	base->putInt(L"HSUpgEnergy", m_upgrades[UPG_ENERGY]);
	base->putInt(L"HSUpgMuffling", m_upgrades[UPG_MUFFLING]);
	base->putInt(L"HSFreq", m_frequency);
	base->putBoolean(L"HSMinerAll", m_minerAll);
	base->putInt(L"HSMinerCursor", m_minerCursor);
	base->putInt(L"HSUpgFilter", m_upgrades[UPG_FILTER]);
	saveAdvanced(base);
	ListTag<CompoundTag> *buf = new ListTag<CompoundTag>();
	for (auto &b : m_buffer)
	{
		CompoundTag *t = new CompoundTag();
		b->save(t);
		buf->add(t);
	}
	base->put(L"HSBuffer", buf);
}

void ModMachineTileEntity::hsUpdateData()
{
	ModMachineDef *d = def();
	for (int i = 0; i < HS_DATA_MAX; i++) hsData[i] = 0;
	if (d == nullptr) return;
	hsData[HSD_KIND] = d->kind + 1;
	hsPutBig(hsData, HSD_ENERGY, m_energy);
	hsPutBig(hsData, HSD_ENERGY_CAP, (d->kind == MachineKind_Tank || d->kind == MachineKind_Pipe || d->kind == MachineKind_Transporter) ? 0 : capacity());
	hsPutBig(hsData, HSD_TANK_CAP, tankCapacity());
	for (int t = 0; t < TANK_COUNT; t++)
	{
		const int base = HSD_TANK0 + t * 4;
		if (m_tanks[t].amount <= 0) continue;
		hsData[base] = ModMachines::chemicalIndex(m_tanks[t].name) + 1;
		hsPutBig(hsData, base + 1, m_tanks[t].amount);
	}
	hsData[HSD_UPGRADES] = (m_upgrades[UPG_SPEED] & 0xF) | ((m_upgrades[UPG_ENERGY] & 0xF) << 4) | ((m_upgrades[UPG_MUFFLING] & 0xF) << 8);
	hsData[HSD_FORMED] = d->multiblock.enabled ? (m_formed == 1 ? 2 : 1) : 0;
	hsData[HSD_FREQ] = m_frequency;
	hsData[HSD_EJECT] = m_eject ? 1 : 0;
	hsData[HSD_MB_VOLUME] = (std::min)(32767, m_mbVolume);
	if (d->kind == MachineKind_Processor && d->processTicks > 0)
	{
		const long long need = static_cast<long long>(d->processTicks) * 1000;
		hsData[HSD_PROGRESS] = static_cast<int>((std::min)(100LL, m_progress * 100 / need));
	}
	hsUpdateDataExtra(d);
}

wstring ModMachineTileEntity::getName()
{
	ModMachineDef *d = def();
	return d ? d->name : FurnaceTileEntity::getName();
}

wstring ModMachineTileEntity::getCustomName()
{
	return getName();
}

bool ModMachineTileEntity::hasCustomName()
{
	return def() != nullptr;
}

bool ModMachineTileEntity::canPlaceItem(int slot, shared_ptr<ItemInstance> item)
{
	if (slot == SLOT_RESULT) return false;
	if (item == nullptr) return true;
	ModMachineDef *d = def();
	if (d == nullptr) return true;
	if (slot == SLOT_FUEL)
	{
		if (d->kind == MachineKind_ModStation) return ModMachines::moduleFor(item->id) != nullptr;
		if (d->kind == MachineKind_QioDrive) return ModMachines::qioDriveCapacity(item->id) > 0;
		if (powerTicks(item) > 0) return true;
		if (ModMachines::refillFor(item->id) != nullptr) return true;
		for (auto &r : d->recipes) if (r.extraId != 0 && r.extraId == item->id) return true;
		return false;
	}
	if (d->kind == MachineKind_Tank) return item->id == Item::bucket_water_Id || item->id == Item::bucket_lava_Id;
	if (d->kind == MachineKind_ModStation) return ModMachines::suitPieceSlot(item->id) != 0;
	if (d->kind == MachineKind_QioDrive) return ModMachines::qioDriveCapacity(item->id) > 0;
	if (d->kind == MachineKind_QioExporter || d->kind == MachineKind_QioImporter || d->kind == MachineKind_Entangloporter) return true;
	if (d->kind != MachineKind_Processor) return false;
	for (auto &r : d->recipes) if (r.inId != 0 && r.inId == item->id) return true;
	if (d->furnaceRecipes && FurnaceRecipes::getInstance()->getResult(item->id, item->getAuxValue()) != nullptr) return true;
	return false;
}

shared_ptr<TileEntity> ModMachineTileEntity::clone()
{
	shared_ptr<ModMachineTileEntity> c = std::make_shared<ModMachineTileEntity>(m_machineId);
	CompoundTag tag;
	save(&tag);
	c->load(&tag);
	return c;
}

// ======================================================================================
// Tile
// ======================================================================================

static ModMachineDef *defAt(int index)
{
	auto &v = ModMachines::defs();
	return (index >= 0 && index < static_cast<int>(v.size())) ? &v[index] : nullptr;
}

static bool defIsConnector(int index)
{
	ModMachineDef *d = defAt(index);
	return d != nullptr && isConnectorKind(d->kind);
}

ModMachineTile::ModMachineTile(int id, int defIndex, float hardness) : BaseEntityTile(id, Material::metal, !defIsConnector(defIndex))
{
	m_defIndex = defIndex;
	if (defIsConnector(defIndex))
	{
		const float a = 5.0f / 16.0f, b = 11.0f / 16.0f;
		setShape(a, a, a, b, b, b);
		setLightBlock(0);
	}
	m_random = new Random();
	setDestroyTime(hardness);
	setExplodeable(hardness * 3.0f);
	setSoundType(Tile::SOUND_METAL);
}

Icon *ModMachineTile::getTexture(int face, int data)
{
	if (face == Facing::UP) return m_top ? m_top : icon;
	if (face == Facing::DOWN) return m_bottom ? m_bottom : (m_top ? m_top : icon);
	int facing = data & 7;
	if (facing < 2) facing = Facing::SOUTH;	// item in hand / inventory: show the front
	if (face != facing) return icon;
	if ((data & 8) && m_frontActive) return m_frontActive;
	return m_front ? m_front : icon;
}

void ModMachineTile::registerIcons(IconRegister *iconRegister)
{
	if (HSModels::hasModel(id)) ModModels::registerIcons(HSModels::tileIndex()[id], iconRegister);	// Hellish Sight
	ModMachineDef *d = defAt(m_defIndex);
	if (d == nullptr) { Tile::registerIcons(iconRegister); return; }
	icon = iconRegister->registerIcon(d->iconSide);
	m_front = d->iconFront.empty() ? icon : iconRegister->registerIcon(d->iconFront);
	m_frontActive = d->iconFrontActive.empty() ? m_front : iconRegister->registerIcon(d->iconFrontActive);
	m_top = d->iconTop.empty() ? icon : iconRegister->registerIcon(d->iconTop);
	m_bottom = d->iconBottom.empty() ? m_top : iconRegister->registerIcon(d->iconBottom);
}

shared_ptr<TileEntity> ModMachineTile::newTileEntity(Level *level)
{
	ModMachineDef *d = defAt(m_defIndex);
	return std::make_shared<ModMachineTileEntity>(d ? d->fullId : std::string());
}

bool ModMachineTile::TestUse()
{
	return true;
}

bool ModMachineTile::isCable() const
{
	return defIsConnector(m_defIndex);
}

bool ModMachineTile::isSolidRender(bool isServerLevel)
{
	return !isCable() && !HSModels::hasModel(id) && !HSMeshRender::machineHidesBlock(id);	// models are not full cubes: neighbours keep their faces
}

bool ModMachineTile::isCubeShaped()
{
	return !isCable() && !HSModels::hasModel(id);
}

int ModMachineTile::getRenderShape()
{
	if (HSMeshRender::machineHidesBlock(id)) return Tile::SHAPE_INVISIBLE;	// drawn by the mesh tile entity renderer
	return HSModels::hasModel(id) ? Tile::SHAPE_HS_MODEL : BaseEntityTile::getRenderShape();
}

bool ModMachines::connectorConnects(LevelSource *level, int x, int y, int z, int face)
{
	static const int off[6][3] = { {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0} };
	if (level == nullptr || face < 0 || face > 5) return false;
	const int self = level->getTile(x, y, z);
	ModMachineDef *selfDef = ModMachines::byTile(self);
	const int id = level->getTile(x + off[face][0], y + off[face][1], z + off[face][2]);
	if (id <= 0 || id >= Tile::TILE_NUM_COUNT || Tile::tiles[id] == nullptr) return false;
	ModMachineDef *other = ModMachines::byTile(id);
	if (other != nullptr)
	{
		// connectors of different kinds (cable / pipe / tube...) do not join, machines always do
		if (selfDef != nullptr && isConnectorKind(other->kind) && isConnectorKind(selfDef->kind) && other->kind != selfDef->kind) return false;
		return true;
	}
	// transporters also connect to chests, furnaces, hoppers...
	return selfDef != nullptr && selfDef->kind == MachineKind_Transporter && Tile::tiles[id]->isEntityTile();
}

void ModMachineTile::updateShape(LevelSource *level, int x, int y, int z, int forceData, shared_ptr<TileEntity> forceEntity)
{
	if (!isCable()) { BaseEntityTile::updateShape(level, x, y, z, forceData, forceEntity); return; }
	ModMachineDef *self = defAt(m_defIndex);
	auto connects = [&](int dx, int dy, int dz) -> bool {
		const int id = level->getTile(x + dx, y + dy, z + dz);
		if (id <= 0 || id >= Tile::TILE_NUM_COUNT || Tile::tiles[id] == nullptr) return false;
		if (dynamic_cast<ModMachineTile *>(Tile::tiles[id]) != nullptr) return true;
		// transporters also connect to chests, furnaces, hoppers...
		return self != nullptr && self->kind == MachineKind_Transporter && Tile::tiles[id]->isEntityTile();
	};
	const float a = 5.0f / 16.0f, b = 11.0f / 16.0f;
	setShape(connects(-1, 0, 0) ? 0.0f : a, connects(0, -1, 0) ? 0.0f : a, connects(0, 0, -1) ? 0.0f : a,
		connects(1, 0, 0) ? 1.0f : b, connects(0, 1, 0) ? 1.0f : b, connects(0, 0, 1) ? 1.0f : b);
}

bool ModMachineTile::use(Level *level, int x, int y, int z, shared_ptr<Player> player, int clickedFace, float clickX, float clickY, float clickZ, bool soundOnly)
{
	if (soundOnly || player == nullptr) return false;
	ModMachineDef *d = defAt(m_defIndex);
	const bool connector = isCable();
	shared_ptr<ItemInstance> held = player->inventory->getSelected();
	if (level->isClientSide)
	{
		// predict what the server does so blocks can still be placed against connectors
		if (held == nullptr) return true;
		if (held->id == Item::bucket_water_Id || held->id == Item::bucket_lava_Id || held->id == Item::bucket_empty_Id) return true;
		if (ModMachines::toolRole(held->id) != MachineTool_None) return true;
		return !connector;
	}
	shared_ptr<ModMachineTileEntity> te = dynamic_pointer_cast<ModMachineTileEntity>(level->getTileEntity(x, y, z));
	if (te == nullptr) return false;
	if (te->interact(player, clickedFace)) return true;
	if (connector || d == nullptr) return false;
	player->openFurnace(te);
	return true;
}

void ModMachineTile::setPlacedBy(Level *level, int x, int y, int z, shared_ptr<LivingEntity> by, shared_ptr<ItemInstance> itemInstance)
{
	if (isCable()) return;
	const int dir = (Mth::floor(by->yRot * 4 / (360) + 0.5)) & 3;
	int facing = Facing::NORTH;
	if (dir == 1) facing = Facing::EAST;
	if (dir == 2) facing = Facing::SOUTH;
	if (dir == 3) facing = Facing::WEST;
	level->setData(x, y, z, facing, Tile::UPDATE_CLIENTS);
}

void ModMachineTile::setActive(Level *level, int x, int y, int z, bool active)
{
	const int data = level->getData(x, y, z);
	const int next = (data & 7) | (active ? 8 : 0);
	if (next != data) level->setData(x, y, z, next, Tile::UPDATE_CLIENTS);
}

void ModMachineTile::onRemove(Level *level, int x, int y, int z, int id, int data)
{
	shared_ptr<ModMachineTileEntity> machine = dynamic_pointer_cast<ModMachineTileEntity>(level->getTileEntity(x, y, z));
	shared_ptr<Container> container = dynamic_pointer_cast<FurnaceTileEntity>(level->getTileEntity(x, y, z));
	if (container != nullptr && !level->isClientSide)
	{
		for (unsigned int i = 0; i < container->getContainerSize(); i++)
		{
			shared_ptr<ItemInstance> item = container->getItem(i);
			if (item == nullptr) continue;
			const float xo = m_random->nextFloat() * 0.8f + 0.1f;
			const float yo = m_random->nextFloat() * 0.8f + 0.1f;
			const float zo = m_random->nextFloat() * 0.8f + 0.1f;
			while (item->count > 0)
			{
				int count = m_random->nextInt(21) + 10;
				if (count > item->count) count = item->count;
				item->count -= count;
				shared_ptr<ItemInstance> newItem = std::make_shared<ItemInstance>(item->id, count, item->getAuxValue());
				shared_ptr<ItemEntity> itemEntity = std::make_shared<ItemEntity>(level, x + xo, y + yo, z + zo, newItem);
				itemEntity->xd = static_cast<float>(m_random->nextGaussian()) * 0.05f;
				itemEntity->yd = static_cast<float>(m_random->nextGaussian()) * 0.05f + 0.2f;
				itemEntity->zd = static_cast<float>(m_random->nextGaussian()) * 0.05f;
				level->addEntity(itemEntity);
			}
			container->setItem(i, nullptr);
		}
		if (machine != nullptr) machine->dropExtras(level);
	}
	BaseEntityTile::onRemove(level, x, y, z, id, data);
}

void ModMachineTile::animateTick(Level *level, int xt, int yt, int zt, Random *random)
{
	const int data = level->getData(xt, yt, zt);
	if (!(data & 8) || isCable()) return;
	ModMachineDef *td = defAt(m_defIndex);
	if (td != nullptr && td->kind == MachineKind_Teleporter)
	{
		int ax = -1, ox = 0;
		if (ModMachines::teleporterFrame(level, xt, yt, zt, td, ax, ox))
		{
			for (int i = 0; i < 4; i++)
			{
				const float along = ox + random->nextFloat() * 2.0f;
				const float px = ax == 0 ? xt + along : xt + 0.5f;
				const float pz = ax == 0 ? zt + 0.5f : zt + along;
				level->addParticle(eParticleType_endportal, px, yt + 1 + random->nextFloat() * 3.0f, pz, (random->nextFloat() - 0.5f) * 0.5f, 0, (random->nextFloat() - 0.5f) * 0.5f);
			}
		}
		return;
	}
	// a few sparks at the front while working
	const int dir = data & 7;
	float x = xt + 0.5f, y = yt + 0.3f + random->nextFloat() * 0.5f, z = zt + 0.5f;
	const float r = 0.52f, ss = random->nextFloat() * 0.6f - 0.3f;
	if (dir == 4) x -= r, z += ss;
	else if (dir == 5) x += r, z += ss;
	else if (dir == 2) z -= r, x += ss;
	else z += r, x += ss;
	level->addParticle(eParticleType_reddust, x, y, z, 0, 0, 0);
}
