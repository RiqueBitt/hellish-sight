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
#include "Registry/IDs.h"

#include <deque>

namespace ModMachines
{
	std::vector<ModMachineDef> &defs()
	{
		// deque-like stability is not needed: defs are only added during mod loading
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
			int aux = -1;
			lookup(r.in, r.inId, aux);
			r.inAux = aux > 0 ? aux : -1;	// names of aux variants (lapis, charcoal...) only match that variant
			int outAux = 0;
			lookup(r.out, r.outId, outAux);
			r.outAux = outAux < 0 ? 0 : outAux;
			if (!r.extra.empty())
			{
				int ea = -1;
				lookup(r.extra, r.extraId, ea);
				r.extraAux = ea > 0 ? ea : -1;
			}
		}
		for (auto &f : def.fuels)
		{
			int a = -1;
			lookup(f.item, f.id, a);
			f.aux = a > 0 ? a : -1;
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
		return false;
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
	}
}

// ======================================================================================
// Tile entity
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

const ModMachineRecipe *ModMachineTileEntity::findRecipe(shared_ptr<ItemInstance> input, const ModMachineRecipe &, ModMachineRecipe &furnaceOut)
{
	ModMachineDef *d = def();
	if (d == nullptr || input == nullptr) return nullptr;
	for (auto &r : d->recipes)
	{
		if (r.inId == 0 || r.outId == 0) continue;
		if (input->id != r.inId) continue;
		if (input->count < r.inCount) continue;
		if (r.inAux >= 0 && input->getAuxValue() != r.inAux) continue;
		return &r;
	}
	if (d->furnaceRecipes)
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
	if (d->kind != MachineKind_Processor)
	{
		tickEnergyBlock(d);
		return;
	}

	bool changed = false;
	ModMachineRecipe furnaceTmp;
	const ModMachineRecipe *r = findRecipe(getItem(SLOT_INPUT), furnaceTmp, furnaceTmp);

	// does the output fit?
	bool fits = false;
	if (r != nullptr)
	{
		shared_ptr<ItemInstance> out = getItem(SLOT_RESULT);
		if (out == nullptr) fits = true;
		else if (out->id == r->outId && out->getAuxValue() == r->outAux)
		{
			const int max = (std::min)(getMaxStackSize(), out->getMaxStackSize());
			fits = out->count + r->count <= max;
		}
	}

	bool hasExtra = true;
	if (r != nullptr && r->extraId != 0)
	{
		shared_ptr<ItemInstance> ex = getItem(SLOT_FUEL);
		hasExtra = ex != nullptr && ex->id == r->extraId && (r->extraAux < 0 || ex->getAuxValue() == r->extraAux) && ex->count > 0;
	}

	const bool usesPower = d->needsPower && (r == nullptr || r->extraId == 0);
	const bool electric = d->energyPerTick > 0;

	// electric machines: power items in the bottom slot charge the FE buffer
	if (electric)
	{
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

	// refuel when empty and there is work to do
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

	const bool running = r != nullptr && fits && hasExtra &&
		(electric ? m_energy >= d->energyPerTick : (!usesPower || litTime > 0));
	if (running)
	{
		if (electric) m_energy -= d->energyPerTick;
		else if (usesPower) litTime--;
		else { litTime = litDuration = 1; }
		// the furnace screen expects progress out of 200
		tickCount += (std::max)(1, 200 / (std::max)(1, d->processTicks));
		if (tickCount >= 200)
		{
			tickCount = 0;
			shared_ptr<ItemInstance> in = getItem(SLOT_INPUT);
			in->count -= (std::max)(1, r->inCount);
			if (in->count <= 0) setItem(SLOT_INPUT, nullptr);
			if (r->extraId != 0)
			{
				shared_ptr<ItemInstance> ex = getItem(SLOT_FUEL);
				ex->count--;
				if (ex->count <= 0) setItem(SLOT_FUEL, nullptr);
			}
			shared_ptr<ItemInstance> out = getItem(SLOT_RESULT);
			if (out == nullptr) setItem(SLOT_RESULT, std::make_shared<ItemInstance>(r->outId, r->count, r->outAux));
			else out->count += r->count;
			changed = true;
		}
	}
	else
	{
		if (tickCount != 0) changed = true;
		tickCount = 0;
		if (!usesPower && !electric) litTime = 0;
	}
	if (electric) updateGauge(d);

	if (running != m_active)
	{
		m_active = running;
		ModMachineTile::setActive(level, x, y, z, running);
		changed = true;
	}
	if (changed) setChanged();
}

void ModMachineTileEntity::load(CompoundTag *base)
{
	FurnaceTileEntity::load(base);
	const wstring id = base->getString(L"HSMachineId");
	m_machineId.assign(id.begin(), id.end());
	m_def = nullptr;
	m_energy = base->getInt(L"HSEnergy");
	m_burn = base->getInt(L"HSBurn");
	m_burnTotal = base->getInt(L"HSBurnTotal");
}

void ModMachineTileEntity::save(CompoundTag *base)
{
	FurnaceTileEntity::save(base);
	base->putString(L"id", L"HSMachine");
	base->putString(L"HSMachineId", wstring(m_machineId.begin(), m_machineId.end()));
	base->putInt(L"HSEnergy", m_energy);
	base->putInt(L"HSBurn", m_burn);
	base->putInt(L"HSBurnTotal", m_burnTotal);
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
		if (powerTicks(item) > 0) return true;
		for (auto &r : d->recipes) if (r.extraId != 0 && r.extraId == item->id) return true;
		return false;
	}
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

// --------------------------------------------------------------------------------------
// Energy
// --------------------------------------------------------------------------------------
static const int kFaceDx[6] = { 0, 0, 0, 0, -1, 1 };
static const int kFaceDy[6] = { -1, 1, 0, 0, 0, 0 };
static const int kFaceDz[6] = { 0, 0, -1, 1, 0, 0 };
static int oppositeFace(int f) { return f ^ 1; }

int ModMachineTileEntity::capacity()
{
	ModMachineDef *d = def();
	if (d == nullptr) return 0;
	if (d->energyCapacity > 0) return d->energyCapacity;
	if (d->kind == MachineKind_Cable) return (std::max)(1, d->transfer * 2);
	return 0;
}

int ModMachineTileEntity::receiveEnergy(int amount, int intoFace, bool fromCable)
{
	ModMachineDef *d = def();
	if (d == nullptr || amount <= 0) return 0;
	switch (d->kind)
	{
	case MachineKind_Processor:
		if (d->energyPerTick <= 0) return 0;
		break;
	case MachineKind_Generator:
		return 0;
	case MachineKind_Storage:
		if (level && intoFace == (level->getData(x, y, z) & 7)) return 0;	// the front only outputs
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

void ModMachineTileEntity::updateGauge(ModMachineDef *d)
{
	// the furnace "flame" shows the stored energy
	const int cap = capacity();
	litDuration = 1000;
	litTime = cap > 0 ? static_cast<int>(static_cast<long long>(m_energy) * 1000 / cap) : 0;
}

void ModMachineTileEntity::tickEnergyBlock(ModMachineDef *d)
{
	const int before = m_energy;
	bool active = false;

	if (d->kind == MachineKind_Generator)
	{
		if (d->generatorType == 1)
		{
			// solar: daylight, open sky, no rain
			if (level->isDay() && level->canSeeSky(x, y + 1, z) && !level->isRainingAt(x, y + 1, z))
			{
				m_energy = (std::min)(capacity(), m_energy + d->generation);
				active = true;
			}
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
				m_energy = (std::min)(capacity(), m_energy + d->generation);
				active = true;
			}
		}
		// burn progress on the arrow
		tickCount = m_burnTotal > 0 ? 200 - m_burn * 200 / m_burnTotal : 0;
		const int each = (std::max)(1, d->transfer);
		for (int f = 0; f < 6; f++) pushEnergy(f, each);
	}
	else if (d->kind == MachineKind_Storage)
	{
		// charge from power items (energy tablets, redstone...) placed in the bottom slot
		shared_ptr<ItemInstance> fuel = getItem(SLOT_FUEL);
		const int fe = powerTicks(fuel) * 20;
		if (fe > 0 && m_energy + fe <= capacity())
		{
			m_energy += fe;
			fuel->count--;
			if (fuel->count <= 0) setItem(SLOT_FUEL, nullptr);
		}
		const int front = level->getData(x, y, z) & 7;
		pushEnergy(front < 6 ? front : 3, d->transfer);
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
		if (active != m_active)
		{
			m_active = active;
			ModMachineTile::setActive(level, x, y, z, active);
		}
	}
	if (m_energy != before) setChanged();
}

// ======================================================================================
// Tile
// ======================================================================================

static bool defIsCable(int index)
{
	auto &v = ModMachines::defs();
	return index >= 0 && index < static_cast<int>(v.size()) && v[index].kind == MachineKind_Cable;
}

ModMachineTile::ModMachineTile(int id, int defIndex, float hardness) : BaseEntityTile(id, Material::metal, !defIsCable(defIndex))
{
	m_defIndex = defIndex;
	if (defIsCable(defIndex))
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

static ModMachineDef *defAt(int index)
{
	auto &v = ModMachines::defs();
	return (index >= 0 && index < static_cast<int>(v.size())) ? &v[index] : nullptr;
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
	return !isCable();
}

bool ModMachineTile::isCable() const
{
	return defIsCable(m_defIndex);
}

bool ModMachineTile::isSolidRender(bool isServerLevel)
{
	return !isCable();
}

bool ModMachineTile::isCubeShaped()
{
	return !isCable();
}

void ModMachineTile::updateShape(LevelSource *level, int x, int y, int z, int forceData, shared_ptr<TileEntity> forceEntity)
{
	if (!isCable()) { BaseEntityTile::updateShape(level, x, y, z, forceData, forceEntity); return; }
	auto connects = [&](int dx, int dy, int dz) -> bool {
		const int id = level->getTile(x + dx, y + dy, z + dz);
		return id > 0 && id < Tile::TILE_NUM_COUNT && dynamic_cast<ModMachineTile *>(Tile::tiles[id]) != nullptr;
	};
	const float a = 5.0f / 16.0f, b = 11.0f / 16.0f;
	setShape(connects(-1, 0, 0) ? 0.0f : a, connects(0, -1, 0) ? 0.0f : a, connects(0, 0, -1) ? 0.0f : a,
		connects(1, 0, 0) ? 1.0f : b, connects(0, 1, 0) ? 1.0f : b, connects(0, 0, 1) ? 1.0f : b);
}

bool ModMachineTile::use(Level *level, int x, int y, int z, shared_ptr<Player> player, int clickedFace, float clickX, float clickY, float clickZ, bool soundOnly)
{
	if (soundOnly) return false;
	if (isCable()) return false;
	if (level->isClientSide) return true;
	shared_ptr<FurnaceTileEntity> te = dynamic_pointer_cast<FurnaceTileEntity>(level->getTileEntity(x, y, z));
	if (te != nullptr) player->openFurnace(te);
	return true;
}

void ModMachineTile::setPlacedBy(Level *level, int x, int y, int z, shared_ptr<LivingEntity> by, shared_ptr<ItemInstance> itemInstance)
{
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
	}
	BaseEntityTile::onRemove(level, x, y, z, id, data);
}

void ModMachineTile::animateTick(Level *level, int xt, int yt, int zt, Random *random)
{
	const int data = level->getData(xt, yt, zt);
	if (!(data & 8)) return;
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
