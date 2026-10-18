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

	// refuel when empty and there is work to do
	if (usesPower && r != nullptr && fits && litTime <= 0)
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

	const bool running = r != nullptr && fits && hasExtra && (!usesPower || litTime > 0);
	if (running)
	{
		if (usesPower) litTime--;
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
		if (!usesPower) litTime = 0;
	}

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
}

void ModMachineTileEntity::save(CompoundTag *base)
{
	FurnaceTileEntity::save(base);
	base->putString(L"id", L"HSMachine");
	base->putString(L"HSMachineId", wstring(m_machineId.begin(), m_machineId.end()));
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

// ======================================================================================
// Tile
// ======================================================================================

ModMachineTile::ModMachineTile(int id, int defIndex, float hardness) : BaseEntityTile(id, Material::metal)
{
	m_defIndex = defIndex;
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
	return true;
}

bool ModMachineTile::use(Level *level, int x, int y, int z, shared_ptr<Player> player, int clickedFace, float clickX, float clickY, float clickZ, bool soundOnly)
{
	if (soundOnly) return false;
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
