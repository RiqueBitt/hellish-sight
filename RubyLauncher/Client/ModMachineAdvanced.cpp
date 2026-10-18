// Hellish Sight - advanced Ruby mod machines: heat (heaters, conductors, boilers),
// fission (with meltdowns) and fusion reactors, seismic vibrator, QIO storage network,
// quantum entangloporter, teleporter portals, Modification Station + MekaSuit modules.
// See ModMachine.h.

#include "stdafx.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.entity.item.h"
#include "net.minecraft.world.entity.player.h"
#include "net.minecraft.world.item.h"
#include "net.minecraft.world.level.tile.entity.h"
#include "net.minecraft.world.phys.h"
#include "net.minecraft.world.h"
#include "com.mojang.nbt.h"
#include "Facing.h"
#include "Mth.h"
#include "Material.h"
#include "FoodData.h"
#include "MobEffect.h"
#include "MobEffectInstance.h"
#include "ContainerOpenPacket.h"
#include "ModMachine.h"
#include "Registry/IDs.h"
#include "ServerPlayer.h"
#include "PlayerConnection.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

std::wstring hsFmtNum(long long v);

namespace
{
	const int kDx[6] = { 0, 0, 0, 0, -1, 1 };
	const int kDy[6] = { -1, 1, 0, 0, 0, 0 };
	const int kDz[6] = { 0, 0, -1, 1, 0, 0 };
	int opposite(int f) { return f ^ 1; }

	int lookupId(const std::string &name)
	{
		if (name.empty()) return 0;
		return IDMapping::get()->getID(name).id;
	}

	std::wstring wide(const std::string &s) { return std::wstring(s.begin(), s.end()); }

	shared_ptr<ModMachineTileEntity> machineAt(Level *level, int x, int y, int z)
	{
		return dynamic_pointer_cast<ModMachineTileEntity>(level->getTileEntity(x, y, z));
	}

	// ---------------------------------------------------------------------------------
	// name -> item id registries (resolved lazily: items may be registered later)
	// ---------------------------------------------------------------------------------
	template <class T>
	struct NamedRegistry
	{
		std::vector<std::pair<std::string, T> > named;
		std::map<int, T> byId;
		bool dirty = true;
		void add(const std::string &n, const T &v) { named.push_back(std::make_pair(n, v)); dirty = true; }
		const T *find(int id)
		{
			if (dirty)
			{
				dirty = false;
				byId.clear();
				for (auto &p : named)
				{
					const int i = lookupId(p.first);
					if (i > 0) byId[i] = p.second; else dirty = true;
				}
			}
			auto it = byId.find(id);
			return it != byId.end() ? &it->second : nullptr;
		}
	};
	NamedRegistry<int> &qioDrives() { static NamedRegistry<int> r; return r; }
	NamedRegistry<ModModuleDef> &modules() { static NamedRegistry<ModModuleDef> r; return r; }
	NamedRegistry<int> &suitPieces() { static NamedRegistry<int> r; return r; }

	// ---------------------------------------------------------------------------------
	// position registries (QIO drive arrays, teleporters)
	// ---------------------------------------------------------------------------------
	struct PosEntry { Level *level; int x, y, z; };
	std::vector<PosEntry> &qioArrays() { static std::vector<PosEntry> s; return s; }
	std::vector<PosEntry> &teleporterList() { static std::vector<PosEntry> s; return s; }

	void registerPos(std::vector<PosEntry> &list, Level *level, int x, int y, int z)
	{
		for (auto &e : list) if (e.level == level && e.x == x && e.y == y && e.z == z) return;
		PosEntry e;
		e.level = level; e.x = x; e.y = y; e.z = z;
		list.push_back(e);
	}

	// ---------------------------------------------------------------------------------
	// QIO storage (items live inside the drive items' tags)
	// ---------------------------------------------------------------------------------
	struct QStack { int id; int aux; long long count; };

	std::vector<QStack> readDrive(shared_ptr<ItemInstance> drive)
	{
		std::vector<QStack> out;
		if (drive == nullptr || drive->tag == nullptr || !drive->tag->contains(L"HSQio")) return out;
		ListTag<CompoundTag> *list = (ListTag<CompoundTag> *)drive->tag->getList(L"HSQio");
		for (int i = 0; list != nullptr && i < list->size(); i++)
		{
			CompoundTag *t = list->get(i);
			QStack q;
			q.id = t->getInt(L"id");
			q.aux = t->getInt(L"aux");
			q.count = t->getInt(L"n");
			if (q.id > 0 && q.count > 0) out.push_back(q);
		}
		return out;
	}

	void writeDrive(shared_ptr<ItemInstance> drive, const std::vector<QStack> &items)
	{
		if (drive == nullptr) return;
		if (drive->tag == nullptr) drive->setTag(new CompoundTag());
		ListTag<CompoundTag> *list = new ListTag<CompoundTag>();
		for (auto &q : items)
		{
			if (q.count <= 0) continue;
			CompoundTag *t = new CompoundTag();
			t->putInt(L"id", q.id);
			t->putInt(L"aux", q.aux);
			t->putInt(L"n", static_cast<int>(q.count));
			list->add(t);
		}
		drive->tag->put(L"HSQio", list);
	}

	long long driveUsed(const std::vector<QStack> &items)
	{
		long long n = 0;
		for (auto &q : items) n += q.count;
		return n;
	}

	// every drive of the network (array TE, slot)
	std::vector<std::pair<shared_ptr<ModMachineTileEntity>, int> > networkDrives(Level *level, int freq)
	{
		std::vector<std::pair<shared_ptr<ModMachineTileEntity>, int> > out;
		auto &list = qioArrays();
		for (size_t i = 0; i < list.size(); )
		{
			PosEntry e = list[i];
			shared_ptr<ModMachineTileEntity> te = e.level == level ? machineAt(level, e.x, e.y, e.z) : nullptr;
			if (e.level == level && (te == nullptr || te->def() == nullptr || te->def()->kind != MachineKind_QioDrive))
			{
				list.erase(list.begin() + i);
				continue;
			}
			i++;
			if (te == nullptr || te->frequency() != freq) continue;
			for (int slot = FurnaceTileEntity::SLOT_INPUT; slot <= FurnaceTileEntity::SLOT_FUEL; slot++)
			{
				shared_ptr<ItemInstance> d = te->getItem(slot);
				if (d != nullptr && ModMachines::qioDriveCapacity(d->id) > 0) out.push_back(std::make_pair(te, slot));
			}
		}
		return out;
	}

	std::vector<QStack> networkList(Level *level, int freq, long long *usedOut = nullptr, long long *capOut = nullptr)
	{
		std::vector<QStack> all;
		long long used = 0, cap = 0;
		for (auto &dr : networkDrives(level, freq))
		{
			shared_ptr<ItemInstance> d = dr.first->getItem(dr.second);
			cap += ModMachines::qioDriveCapacity(d->id);
			for (auto &q : readDrive(d))
			{
				used += q.count;
				bool merged = false;
				for (auto &a : all) if (a.id == q.id && a.aux == q.aux) { a.count += q.count; merged = true; break; }
				if (!merged) all.push_back(q);
			}
		}
		if (usedOut) *usedOut = used;
		if (capOut) *capOut = cap;
		return all;
	}

	long long networkInsert(Level *level, int freq, int id, int aux, long long count)
	{
		long long left = count;
		for (auto &dr : networkDrives(level, freq))
		{
			if (left <= 0) break;
			shared_ptr<ItemInstance> d = dr.first->getItem(dr.second);
			std::vector<QStack> items = readDrive(d);
			const long long room = ModMachines::qioDriveCapacity(d->id) - driveUsed(items);
			if (room <= 0) continue;
			const long long n = (std::min)(room, left);
			bool merged = false;
			for (auto &q : items) if (q.id == id && q.aux == aux) { q.count += n; merged = true; break; }
			if (!merged)
			{
				if (items.size() >= 128) continue;	// type limit per drive
				QStack q; q.id = id; q.aux = aux; q.count = n;
				items.push_back(q);
			}
			writeDrive(d, items);
			dr.first->setChanged();
			left -= n;
		}
		return count - left;
	}

	long long networkExtract(Level *level, int freq, int id, int aux, long long count)
	{
		long long got = 0;
		for (auto &dr : networkDrives(level, freq))
		{
			if (got >= count) break;
			shared_ptr<ItemInstance> d = dr.first->getItem(dr.second);
			std::vector<QStack> items = readDrive(d);
			bool changed = false;
			for (auto &q : items)
			{
				if (q.id != id || q.aux != aux) continue;
				const long long n = (std::min)(q.count, count - got);
				q.count -= n;
				got += n;
				changed = true;
				break;
			}
			if (changed)
			{
				writeDrive(d, items);
				dr.first->setChanged();
			}
		}
		return got;
	}

	// the dashboard's screen: a large chest whose slots show the network's contents
	class QioContainer : public Container
	{
	public:
		QioContainer(Level *level, int freq, int x, int y, int z) : m_level(level), m_freq(freq), m_x(x), m_y(y), m_z(z)
		{
			m_view.resize(SIZE);
			refresh();
		}
		static const int SIZE = 54;

		virtual int getContainerType() { return ContainerOpenPacket::LARGE_CHEST; }
		virtual unsigned int getContainerSize() { return SIZE; }
		virtual shared_ptr<ItemInstance> getItem(unsigned int slot)
		{
			if (slot == 0) refresh();	// once per menu sync
			return slot < m_view.size() ? m_view[slot] : nullptr;
		}
		virtual shared_ptr<ItemInstance> removeItem(unsigned int slot, int count)
		{
			if (slot >= m_view.size() || m_view[slot] == nullptr || count <= 0) return nullptr;
			shared_ptr<ItemInstance> it = m_view[slot];
			const long long got = networkExtract(m_level, m_freq, it->id, it->getAuxValue(), (std::min)(count, it->count));
			const int aux = it->getAuxValue(), id = it->id;
			refresh();
			if (got <= 0) return nullptr;
			return std::make_shared<ItemInstance>(id, static_cast<int>(got), aux);
		}
		virtual shared_ptr<ItemInstance> removeItemNoUpdate(int slot)
		{
			if (slot < 0 || slot >= SIZE || m_view[slot] == nullptr) return nullptr;
			return removeItem(slot, m_view[slot]->count);
		}
		virtual void setItem(unsigned int slot, shared_ptr<ItemInstance> item)
		{
			if (slot >= m_view.size()) return;
			shared_ptr<ItemInstance> old = m_view[slot];
			const bool same = old != nullptr && item != nullptr && old->id == item->id && old->getAuxValue() == item->getAuxValue();
			if (same)
			{
				const int diff = item->count - old->count;
				if (diff > 0) spill(item->id, item->getAuxValue(), diff - networkInsert(m_level, m_freq, item->id, item->getAuxValue(), diff));
				else if (diff < 0) networkExtract(m_level, m_freq, old->id, old->getAuxValue(), -diff);
			}
			else
			{
				if (old != nullptr) networkExtract(m_level, m_freq, old->id, old->getAuxValue(), old->count);
				if (item != nullptr && item->count > 0)
				{
					if (item->hasTag()) spill(item);
					else spill(item->id, item->getAuxValue(), item->count - networkInsert(m_level, m_freq, item->id, item->getAuxValue(), item->count));
				}
			}
			refresh();
		}
		virtual wstring getName() { return L"QIO Dashboard (frequ\u00EAncia " + std::to_wstring(m_freq) + L")"; }
		virtual wstring getCustomName() { return getName(); }
		virtual bool hasCustomName() { return true; }
		virtual int getMaxStackSize() const { return 64; }
		virtual void setChanged() {}
		virtual bool stillValid(shared_ptr<Player> player) { return player->distanceToSqr(m_x + 0.5, m_y + 0.5, m_z + 0.5) <= 64.0; }
		virtual void startOpen() {}
		virtual void stopOpen() {}
		virtual bool canPlaceItem(int slot, shared_ptr<ItemInstance> item) { return item == nullptr || !item->hasTag(); }

	private:
		void refresh()
		{
			std::vector<QStack> all = networkList(m_level, m_freq);
			std::sort(all.begin(), all.end(), [](const QStack &a, const QStack &b) { return a.count > b.count; });
			size_t slot = 0;
			for (auto &q : all)
			{
				long long left = q.count;
				const int maxStack = Item::items[q.id] ? Item::items[q.id]->getMaxStackSize() : 64;
				while (left > 0 && slot < m_view.size())
				{
					const int n = static_cast<int>((std::min)(left, static_cast<long long>(maxStack)));
					m_view[slot++] = std::make_shared<ItemInstance>(q.id, n, q.aux);
					left -= n;
				}
			}
			for (; slot < m_view.size(); slot++) m_view[slot] = nullptr;
		}
		void spill(int id, int aux, long long count)
		{
			while (count > 0)
			{
				const int n = static_cast<int>((std::min)(count, 64LL));
				spill(std::make_shared<ItemInstance>(id, n, aux));
				count -= n;
			}
		}
		void spill(shared_ptr<ItemInstance> item)
		{
			// the network is full: drop what did not fit on top of the dashboard
			shared_ptr<ItemEntity> e = std::make_shared<ItemEntity>(m_level, m_x + 0.5, m_y + 1.2, m_z + 0.5, item);
			m_level->addEntity(e);
		}

		Level *m_level;
		int m_freq, m_x, m_y, m_z;
		std::vector<shared_ptr<ItemInstance> > m_view;
	};

	// ---------------------------------------------------------------------------------
	// Quantum entangloporter buffers (shared per frequency)
	// ---------------------------------------------------------------------------------
	struct EntBuffer
	{
		bool loaded = false;
		long long energy = 0;
		std::string chem;
		int amount = 0;
		shared_ptr<ItemInstance> item;
	};
	std::map<int, EntBuffer> &entBuffers() { static std::map<int, EntBuffer> s; return s; }

	// ---------------------------------------------------------------------------------
	// teleporter cooldowns / module flight bookkeeping
	// ---------------------------------------------------------------------------------
	std::map<int, int> &teleportCooldown() { static std::map<int, int> s; return s; }
	int &globalTick() { static int s = 0; return s; }
	std::set<int> &moduleFlight() { static std::set<int> s; return s; }

}

// ======================================================================================
// Registries (ModMachines namespace)
// ======================================================================================

namespace ModMachines
{
	void registerQioDrive(const std::string &itemName, int capacity) { qioDrives().add(itemName, capacity); }
	int qioDriveCapacity(int itemId) { const int *c = qioDrives().find(itemId); return c ? *c : 0; }
	void registerModule(const std::string &itemName, const ModModuleDef &def) { modules().add(itemName, def); }
	const ModModuleDef *moduleFor(int itemId) { return modules().find(itemId); }
	void registerSuitPiece(const std::string &itemName, int slot) { suitPieces().add(itemName, slot); }
	int suitPieceSlot(int itemId) { const int *s = suitPieces().find(itemId); return s ? *s : 0; }

	int moduleLevel(shared_ptr<ItemInstance> item, const std::string &module)
	{
		if (item == nullptr || item->tag == nullptr || !item->tag->contains(L"HSModules")) return 0;
		return item->tag->getCompound(L"HSModules")->getInt(wide(module));
	}

	bool teleporterFrame(Level *level, int x, int y, int z, ModMachineDef *d, int &axis, int &offset)
	{
		// Mekanism layout: a 4 x 5 frame ring with a 2 x 3 hole; the teleporter block sits
		// in the middle of the bottom row.
		if (d == nullptr) return false;
		if (d->portalFrameId == 0 && !d->portalFrame.empty()) d->portalFrameId = lookupId(d->portalFrame);
		const int frame = d->portalFrameId;
		if (frame <= 0) return false;
		const int self = level->getTile(x, y, z);
		for (int ax = 0; ax < 2; ax++)
		{
			for (int o = -1; o <= 0; o++)
			{
				bool ok = true;
				for (int a = o - 1; ok && a <= o + 2; a++)
				{
					for (int dy = 0; ok && dy <= 4; dy++)
					{
						const int px = ax == 0 ? x + a : x, pz = ax == 0 ? z : z + a, py = y + dy;
						const bool ring = a == o - 1 || a == o + 2 || dy == 0 || dy == 4;
						const int id = level->getTile(px, py, pz);
						if (ring)
						{
							if (id != frame && !(dy == 0 && a == 0 && id == self)) ok = false;
						}
						else if (id != 0)
						{
							ok = false;
						}
					}
				}
				if (ok) { axis = ax; offset = o; return true; }
			}
		}
		return false;
	}

	bool onItemUse(ItemInstance *item, Level *level, shared_ptr<Player> player)
	{
		if (item == nullptr || level == nullptr || player == nullptr || level->isClientSide) return false;
		if (toolRole(item->id) != MachineTool_PortableTeleporter) return false;
		const int freq = (item->tag != nullptr && item->tag->contains(L"HSFreq")) ? item->tag->getInt(L"HSFreq") : 0;
		const int cost = 40;	// durability (charge) per jump
		if (item->isDamageableItem() && item->getDamageValue() + cost > item->getMaxDamage())
		{
			player->sendMessage(L"Portable Teleporter sem energia (recarregue num Energy Cube)");
			return true;
		}
		shared_ptr<ServerPlayer> sp = dynamic_pointer_cast<ServerPlayer>(player);
		if (sp == nullptr || sp->connection == nullptr) return false;
		for (auto &e : teleporterList())
		{
			if (e.level != level) continue;
			shared_ptr<ModMachineTileEntity> t = machineAt(level, e.x, e.y, e.z);
			if (t == nullptr || t->frequency() != freq || !t->teleporterReady()) continue;
			int x0, y0, z0, ax;
			t->teleporterInterior(x0, y0, z0, ax);
			const double tx = ax == 0 ? x0 + 1.0 : x0 + 0.5, tz = ax == 0 ? z0 + 0.5 : z0 + 1.0;
			sp->connection->teleport(tx, y0, tz, sp->yRot, sp->xRot);
			teleportCooldown()[player->entityId] = globalTick() + 60;
			if (item->isDamageableItem()) item->setAuxValue(item->getDamageValue() + cost);
			return true;
		}
		player->sendMessage(L"Nenhum teleporter pronto na frequ\u00EAncia " + std::to_wstring(freq));
		return true;
	}

	static void applyEffect(Player *p, MobEffect *effect, int amplifier, int duration)
	{
		if (effect == nullptr) return;
		p->addEffect(new MobEffectInstance(effect->id, duration, amplifier));
	}

	void armorTick(Player *player)
	{
		if (player == nullptr || player->level == nullptr || player->level->isClientSide) return;
		const int tick = player->tickCount;
		shared_ptr<ItemInstance> helmet = player->inventory->getArmor(3);
		shared_ptr<ItemInstance> chest = player->inventory->getArmor(2);
		shared_ptr<ItemInstance> legs = player->inventory->getArmor(1);
		shared_ptr<ItemInstance> boots = player->inventory->getArmor(0);

		auto usable = [](shared_ptr<ItemInstance> it) -> bool {
			return it != nullptr && (!it->isDamageableItem() || it->getDamageValue() < it->getMaxDamage() - 1);
		};
		auto drain = [&](shared_ptr<ItemInstance> it, int points) {
			if (it != nullptr && it->isDamageableItem() && (tick % 40) == 0)
				it->setAuxValue((std::min)(it->getMaxDamage() - 1, it->getDamageValue() + points));
		};

		// helmet
		if (usable(helmet))
		{
			if (moduleLevel(helmet, "vision_enhancement") > 0 && (tick % 100) == 0) { applyEffect(player, MobEffect::nightVision, 0, 300); drain(helmet, 1); }
			if (moduleLevel(helmet, "inhalation_purification") > 0 && (tick % 10) == 0)
			{
				MobEffect *bad[] = { MobEffect::poison, MobEffect::wither, MobEffect::hunger, MobEffect::confusion, MobEffect::blindness, MobEffect::weakness, MobEffect::movementSlowdown, MobEffect::digSlowdown };
				for (MobEffect *e : bad) if (e != nullptr && player->hasEffect(e->id)) player->removeEffect(e->id);
			}
			if (moduleLevel(helmet, "electrolytic_breathing") > 0 && player->isUnderLiquid(Material::water)) drain(helmet, 1);
		}
		if (usable(helmet) && moduleLevel(helmet, "nutritional_injection") > 0 && (tick % 100) == 0 && player->getFoodData()->needsFood())
		{
			applyEffect(player, MobEffect::saturation, 0, 2);
			drain(helmet, 2);
		}
		// held tool (atomic disassembler)
		shared_ptr<ItemInstance> held = player->inventory->getSelected();
		if (usable(held) && suitPieceSlot(held->id) == ArmorSlot_Tool && (tick % 20) == 0)
		{
			const int dig = moduleLevel(held, "excavation_escalation");
			if (dig > 0) applyEffect(player, MobEffect::digSpeed, (std::min)(3, dig - 1), 40);
			const int atk = moduleLevel(held, "attack_amplification");
			if (atk > 0) applyEffect(player, MobEffect::damageBoost, (std::min)(3, atk - 1), 40);
		}
		// chest: gravitational modulating (creative-style flight)
		const bool gravity = usable(chest) && moduleLevel(chest, "gravitational_modulating") > 0;
		std::set<int> &flying = moduleFlight();
		if (gravity)
		{
			if (!player->abilities.mayfly)
			{
				player->abilities.mayfly = true;
				player->onUpdateAbilities();
				flying.insert(player->entityId);
			}
			if (player->abilities.flying) drain(chest, 2);
		}
		else if (flying.count(player->entityId))
		{
			flying.erase(player->entityId);
			if (!player->abilities.instabuild && !player->isSpectator())
			{
				player->abilities.mayfly = false;
				player->abilities.flying = false;
				player->onUpdateAbilities();
			}
		}
		// legs: locomotive boosting
		if (usable(legs))
		{
			const int lv = moduleLevel(legs, "locomotive_boosting");
			if (lv > 0 && (tick % 20) == 0 && player->isSprinting()) { applyEffect(player, MobEffect::movementSpeed, (std::min)(3, lv - 1), 40); drain(legs, 1); }
		}
		// boots: hydraulic propulsion, magnetic attraction
		if (usable(boots))
		{
			const int jump = moduleLevel(boots, "hydraulic_propulsion");
			if (jump > 0 && (tick % 20) == 0) { applyEffect(player, MobEffect::jump, (std::min)(3, jump - 1), 40); drain(boots, 1); }
			const int magnet = moduleLevel(boots, "magnetic_attraction");
			if (magnet > 0 && (tick % 2) == 0)
			{
				const double r = 4.0 + 2.0 * magnet;
				AABB *box = AABB::newTemp(player->x - r, player->y - r, player->z - r, player->x + r, player->y + r, player->z + r);
				vector<shared_ptr<Entity> > *ents = player->level->getEntitiesOfClass(typeid(ItemEntity), box);
				if (ents != nullptr)
				{
					for (auto &e : *ents)
					{
						const double dx = player->x - e->x, dy = player->y + 0.5 - e->y, dz = player->z - e->z;
						const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
						if (dist < 0.5) continue;
						e->xd = dx / dist * 0.35; e->yd = dy / dist * 0.35; e->zd = dz / dist * 0.35;
					}
					delete ents;
				}
			}
		}
	}
}

// ======================================================================================
// Heat
// ======================================================================================

bool ModMachineTileEntity::hasHeat()
{
	ModMachineDef *d = def();
	if (d == nullptr) return false;
	return d->kind == MachineKind_Heater || d->kind == MachineKind_Conductor || d->kind == MachineKind_Boiler || d->kind == MachineKind_Fission;
}

double ModMachineTileEntity::heatCapacityNow()
{
	ModMachineDef *d = def();
	double c = d ? (std::max)(1, d->heatCapacity) : 1000.0;
	if (d && d->multiblock.enabled) c *= (std::max)(1, m_mbVolume);
	return c;
}

void ModMachineTileEntity::conductHeat(ModMachineTileEntity *other)
{
	if (other == nullptr || other == this || !other->hasHeat()) return;
	const double dT = m_temp - other->m_temp;
	if (dT <= 0.01) return;	// heat only flows from hot to cold
	const double ca = heatCapacityNow(), cb = other->heatCapacityNow();
	const double k = (std::min)(def()->conductance, other->def()->conductance);
	const double q = dT * k * (ca * cb / (ca + cb));
	m_temp -= q / ca;
	other->m_temp += q / cb;
	other->setChanged();
}

void ModMachineTileEntity::boilCoolant(int maxMb, int heatPerMb, int inTank, const char *inChem, int outTank, const char *outChem)
{
	ModChemAmount &in = m_tanks[inTank];
	ModChemAmount &out = m_tanks[outTank];
	if (in.name != inChem || in.amount <= 0) return;
	if (out.amount > 0 && out.name != outChem) return;
	const double boilPoint = 373.0;
	if (m_temp <= boilPoint) return;
	const double c = heatCapacityNow();
	const long long byHeat = static_cast<long long>((m_temp - boilPoint) * c / (std::max)(1, heatPerMb));
	const int room = tankCapacity() - out.amount;
	const int n = static_cast<int>((std::min)((long long)(std::min)((std::min)(maxMb, in.amount), room), byHeat));
	if (n <= 0) return;
	in.amount -= n;
	if (in.amount <= 0) { in.amount = 0; in.name.clear(); }
	out.name = outChem;
	out.amount += n;
	m_temp -= static_cast<double>(n) * heatPerMb / c;
	setChanged();
}

void ModMachineTileEntity::tickHeater(ModMachineDef *d)
{
	bool active = false;
	if (d->energyPerTick > 0)
	{
		// resistive heater: FE -> heat
		drainItemInSlot(d);
		const int use = energyUse(d);
		if (m_energy >= use && !level->hasNeighborSignal(x, y, z))
		{
			m_energy -= use;
			addHeat(use * 0.6);	// Mekanism resistive heaters are 60% efficient
			active = true;
		}
	}
	else
	{
		// fuelwood heater: burns furnace fuels
		if (m_burn <= 0)
		{
			shared_ptr<ItemInstance> fuel = getItem(SLOT_FUEL);
			const int ticks = powerTicks(fuel);
			if (ticks > 0)
			{
				m_burn = m_burnTotal = ticks;
				fuel->count--;
				if (fuel->count <= 0) setItem(SLOT_FUEL, nullptr);
			}
		}
		if (m_burn > 0)
		{
			m_burn--;
			addHeat((std::max)(1, d->heatPerTick));
			active = true;
		}
		tickCount = m_burnTotal > 0 ? 200 - m_burn * 200 / m_burnTotal : 0;
	}
	m_temp -= (m_temp - 300.0) * 0.002;	// loss to the air
	for (int f = 0; f < 6; f++)
	{
		shared_ptr<ModMachineTileEntity> o = machineAt(level, x + kDx[f], y + kDy[f], z + kDz[f]);
		if (o != nullptr) conductHeat(o.get());
	}
	updateGauge(d);
	setActiveState(active || m_temp > 330.0);
	setChanged();
}

void ModMachineTileEntity::tickConductor(ModMachineDef *d)
{
	m_temp -= (m_temp - 300.0) * 0.0005;
	for (int f = 0; f < 6; f++)
	{
		shared_ptr<ModMachineTileEntity> o = machineAt(level, x + kDx[f], y + kDy[f], z + kDz[f]);
		if (o != nullptr) conductHeat(o.get());
	}
}

void ModMachineTileEntity::tickBoiler(ModMachineDef *d)
{
	// heaters / conductors touching the outside of the structure feed it
	if ((m_tickCounter % 20) == 2)
	{
		m_heatSources.clear();
		for (int px = m_mbMin[0]; px <= m_mbMax[0]; px++)
			for (int py = m_mbMin[1]; py <= m_mbMax[1]; py++)
				for (int pz = m_mbMin[2]; pz <= m_mbMax[2]; pz++)
				{
					const bool shell = px == m_mbMin[0] || px == m_mbMax[0] || py == m_mbMin[1] || py == m_mbMax[1] || pz == m_mbMin[2] || pz == m_mbMax[2];
					if (!shell) continue;
					for (int f = 0; f < 6; f++)
					{
						const int nx = px + kDx[f], ny = py + kDy[f], nz = pz + kDz[f];
						if (nx >= m_mbMin[0] && nx <= m_mbMax[0] && ny >= m_mbMin[1] && ny <= m_mbMax[1] && nz >= m_mbMin[2] && nz <= m_mbMax[2]) continue;
						if (!level->isEntityTile(nx, ny, nz)) continue;
						shared_ptr<ModMachineTileEntity> o = machineAt(level, nx, ny, nz);
						if (o != nullptr && o->def() != nullptr && (o->def()->kind == MachineKind_Heater || o->def()->kind == MachineKind_Conductor))
						{
							bool dup = false;
							for (size_t i = 0; i + 2 < m_heatSources.size(); i += 3)
								if (m_heatSources[i] == nx && m_heatSources[i + 1] == ny && m_heatSources[i + 2] == nz) { dup = true; break; }
							if (!dup) { m_heatSources.push_back(nx); m_heatSources.push_back(ny); m_heatSources.push_back(nz); }
						}
					}
				}
	}
	for (size_t i = 0; i + 2 < m_heatSources.size(); i += 3)
	{
		shared_ptr<ModMachineTileEntity> o = machineAt(level, m_heatSources[i], m_heatSources[i + 1], m_heatSources[i + 2]);
		if (o != nullptr) o->conductHeat(this);
	}

	// superheated sodium from fission reactors carries heat in
	ModChemAmount &hot = m_tanks[TANK_IN1];
	if (hot.name == "mekanism:superheated_sodium" && hot.amount > 0)
	{
		ModChemAmount &cold = m_tanks[TANK_OUT1];
		const int room = tankCapacity() - cold.amount;
		const int n = (std::min)((std::min)(hot.amount, room), static_cast<int>(1000 * m_mbSpeed));
		if (n > 0 && (cold.amount == 0 || cold.name == "mekanism:sodium"))
		{
			hot.amount -= n;
			if (hot.amount <= 0) { hot.amount = 0; hot.name.clear(); }
			cold.name = "mekanism:sodium";
			cold.amount += n;
			addHeat(n * 50.0);
		}
	}
	const int before = m_tanks[TANK_OUT0].amount;
	boilCoolant(static_cast<int>(200 * m_mbSpeed), 20, TANK_IN0, "minecraft:water", TANK_OUT0, "mekanism:steam");
	m_temp -= (m_temp - 300.0) * 0.0005;
	setActiveState(m_tanks[TANK_OUT0].amount != before);
	setChanged();
}

// ======================================================================================
// Fission reactor
// ======================================================================================

void ModMachineTileEntity::tickFission(ModMachineDef *d)
{
	if (level->hasNeighborSignal(x, y, z) && m_enabled)
	{
		m_enabled = false;	// redstone = SCRAM
	}
	const int assemblies = (std::max)(1, static_cast<int>(m_mbSpeed - 1.0f + 0.5f));
	const int maxBurn = assemblies * (std::max)(1, d->burnRateMax);
	if (m_burnRate > maxBurn) m_burnRate = maxBurn;
	if (m_burnRate < 1) m_burnRate = 1;

	bool burning = false;
	ModChemAmount &fuel = m_tanks[TANK_IN0];
	ModChemAmount &waste = m_tanks[TANK_OUT1];
	if (m_enabled && fuel.name == d->fuelChem.name && fuel.amount > 0 && (waste.amount == 0 || waste.name == "mekanism:nuclear_waste"))
	{
		const int burn = (std::min)((std::min)(m_burnRate, fuel.amount), tankCapacity() - waste.amount);
		if (burn > 0)
		{
			fuel.amount -= burn;
			if (fuel.amount <= 0) { fuel.amount = 0; fuel.name.clear(); }
			waste.name = "mekanism:nuclear_waste";
			waste.amount += burn;
			addHeat(burn * 20000.0);
			burning = true;
		}
	}
	// coolant: water boils into steam, sodium becomes superheated sodium
	const ModChemAmount &cool = m_tanks[TANK_IN1];
	if (cool.name == "minecraft:water") boilCoolant(1000000, 20, TANK_IN1, "minecraft:water", TANK_OUT0, "mekanism:steam");
	else if (cool.name == "mekanism:sodium") boilCoolant(1000000, 50, TANK_IN1, "mekanism:sodium", TANK_OUT0, "mekanism:superheated_sodium");
	m_temp -= (m_temp - 300.0) * 0.0002;

	// damage and meltdown
	const double limit = (std::max)(400, d->maxTemperature);
	if (m_temp > limit) m_damage += static_cast<int>((m_temp - limit) / 10.0) + 1;
	else if (m_damage > 0) m_damage--;
	if (m_damage > 10000) m_damage = 10000;

	auto warn = [&](const std::wstring &msg) {
		for (auto &p : level->players)
		{
			if (p != nullptr && p->distanceToSqr(x + 0.5, y + 0.5, z + 0.5) < 64.0 * 64.0) p->sendMessage(msg);
		}
	};
	if (m_damage >= 5000 && m_meltdownWarned < 1) { m_meltdownWarned = 1; warn(L"\u26A0 Reator de fiss\u00E3o com 50% de dano! Resfrie ou desligue (redstone)."); }
	if (m_damage >= 9000 && m_meltdownWarned < 2) { m_meltdownWarned = 2; warn(L"\u26A0 REATOR DE FISS\u00C3O PRESTES A DERRETER!"); }
	if (m_damage < 3000) m_meltdownWarned = 0;

	if (m_damage >= 10000)
	{
		// meltdown: everything is lost and the structure blows up
		const double cx = (m_mbMin[0] + m_mbMax[0]) / 2.0 + 0.5, cy = (m_mbMin[1] + m_mbMax[1]) / 2.0 + 0.5, cz = (m_mbMin[2] + m_mbMax[2]) / 2.0 + 0.5;
		const float power = static_cast<float>((std::min)(30.0, 6.0 + m_mbVolume / 6.0));
		for (auto &t : m_tanks) { t.amount = 0; t.name.clear(); }
		m_damage = 0;
		m_temp = 300.0;
		m_enabled = false;
		m_formed = -1;
		setChanged();
		Level *lvl = level;
		warn(L"\u2622 O reator de fiss\u00E3o derreteu!");
		lvl->explode(nullptr, cx, cy, cz, power, true, true);
		return;
	}
	setActiveState(burning);
	setChanged();
}

// ======================================================================================
// Fusion reactor
// ======================================================================================

void ModMachineTileEntity::tickFusion(ModMachineDef *d)
{
	ModChemAmount &fuel = m_tanks[TANK_IN0];
	const int per = (std::max)(1, d->fuelChem.amount);
	if (!m_ignited)
	{
		if (fuel.name == d->fuelChem.name && fuel.amount >= per * 20 && d->ignitionEnergy > 0 && m_energy >= d->ignitionEnergy)
		{
			m_energy -= static_cast<int>(d->ignitionEnergy);
			m_ignited = true;
			for (auto &p : level->players)
				if (p != nullptr && p->distanceToSqr(x + 0.5, y + 0.5, z + 0.5) < 32.0 * 32.0) p->sendMessage(L"Reator de fus\u00E3o: plasma aceso!");
		}
	}
	bool active = false;
	if (m_ignited)
	{
		if (fuel.name == d->fuelChem.name && fuel.amount >= per)
		{
			fuel.amount -= per;
			if (fuel.amount <= 0) { fuel.amount = 0; fuel.name.clear(); }
			const long long gen = static_cast<long long>(d->generation * m_mbSpeed);
			m_energy = static_cast<int>((std::min)(static_cast<long long>(capacity()), m_energy + gen));
			active = true;
		}
		else
		{
			m_ignited = false;	// out of fuel: the plasma goes out
		}
	}
	if (m_ignited)
	{
		const int each = (std::max)(1, d->transfer);
		for (int f = 0; f < 6; f++) pushEnergy(f, each);
	}
	updateGauge(d);
	setActiveState(active);
	setChanged();
}

// ======================================================================================
// Seismic vibrator
// ======================================================================================

void ModMachineTileEntity::tickSeismic(ModMachineDef *d)
{
	drainItemInSlot(d);
	shared_ptr<ItemInstance> fuel = getItem(SLOT_FUEL);
	const int fe = powerTicks(fuel) * (std::max)(1, d->energyPerTick);
	if (fe > 0 && m_energy + fe <= capacity())
	{
		m_energy += fe;
		fuel->count--;
		if (fuel->count <= 0) setItem(SLOT_FUEL, nullptr);
	}
	const int use = (std::max)(1, energyUse(d));
	const bool active = m_energy >= use;
	if (active) m_energy -= use;
	updateGauge(d);
	setActiveState(active);
}

void ModMachineTileEntity::seismicReport(shared_ptr<Player> player)
{
	if (!m_active)
	{
		player->sendMessage(L"O Seismic Vibrator precisa de energia para funcionar.");
		return;
	}
	player->sendMessage(L"== Leitura s\u00EDsmica (X " + std::to_wstring(x) + L", Z " + std::to_wstring(z) + L") ==");
	int lines = 0;
	int runId = -1, runData = -1, runTop = 0, runCount = 0;
	auto flush = [&](int bottomY) {
		if (runId <= 0 || runCount <= 0) return;
		if (runId == Tile::stone_Id || runId == Tile::dirt_Id || runId == Tile::gravel_Id || runId == Tile::unbreakable_Id) return;
		if (lines >= 14) return;
		shared_ptr<ItemInstance> it = std::make_shared<ItemInstance>(runId, 1, runData);
		std::wstring name = it->getHoverName();
		player->sendMessage(L"Y " + std::to_wstring(bottomY) + (runCount > 1 ? L"-" + std::to_wstring(runTop) : L"") + L": " + name);
		lines++;
	};
	for (int py = y - 1; py >= 0; py--)
	{
		const int id = level->getTile(x, py, z);
		const int data = level->getData(x, py, z);
		if (id == runId && data == runData) { runCount++; continue; }
		flush(py + 1);
		runId = id; runData = data; runTop = py; runCount = 1;
	}
	flush(0);
	if (lines == 0) player->sendMessage(L"S\u00F3 pedra, terra e cascalho abaixo.");
}

// ======================================================================================
// QIO
// ======================================================================================

void ModMachineTileEntity::tickQio(ModMachineDef *d)
{
	if (d->kind == MachineKind_QioDrive)
	{
		registerPos(qioArrays(), level, x, y, z);
		long long used = 0, cap = 0;
		networkList(level, m_frequency, &used, &cap);
		litDuration = 1000;
		litTime = cap > 0 ? static_cast<int>(used * 1000 / cap) : 0;
		setActiveState(cap > 0);
		return;
	}
	if ((m_tickCounter % 10) != 0) return;
	if (d->kind == MachineKind_QioImporter)
	{
		// items given to the importer (transporters, hoppers) and items pulled from neighbours
		shared_ptr<ItemInstance> in = getItem(SLOT_INPUT);
		if (in != nullptr && !in->hasTag())
		{
			const long long n = networkInsert(level, m_frequency, in->id, in->getAuxValue(), in->count);
			in->count -= static_cast<int>(n);
			if (in->count <= 0) setItem(SLOT_INPUT, nullptr);
		}
		for (int f = 0; f < 6; f++)
		{
			shared_ptr<TileEntity> te = level->getTileEntity(x + kDx[f], y + kDy[f], z + kDz[f]);
			shared_ptr<Container> c = dynamic_pointer_cast<Container>(te);
			if (c == nullptr || dynamic_pointer_cast<ModMachineTileEntity>(te) != nullptr && dynamic_pointer_cast<ModMachineTileEntity>(te)->def() != nullptr
				&& dynamic_pointer_cast<ModMachineTileEntity>(te)->def()->kind >= MachineKind_QioDrive) continue;
			const bool furnaceLike = dynamic_pointer_cast<FurnaceTileEntity>(te) != nullptr;
			for (unsigned int s = 0; s < c->getContainerSize(); s++)
			{
				if (furnaceLike && s != static_cast<unsigned int>(SLOT_RESULT)) continue;
				shared_ptr<ItemInstance> it = c->getItem(s);
				if (it == nullptr || it->count <= 0 || it->hasTag()) continue;
				const int want = (std::min)(it->count, 16);
				const long long n = networkInsert(level, m_frequency, it->id, it->getAuxValue(), want);
				if (n > 0) { c->removeItem(s, static_cast<int>(n)); c->setChanged(); }
				break;
			}
		}
		setActiveState(true);
	}
	else if (d->kind == MachineKind_QioExporter)
	{
		// exports the item shown in its input slot (the filter) to the neighbours
		shared_ptr<ItemInstance> filter = getItem(SLOT_INPUT);
		if (filter == nullptr) { setActiveState(false); return; }
		for (int f = 0; f < 6; f++)
		{
			shared_ptr<TileEntity> te = level->getTileEntity(x + kDx[f], y + kDy[f], z + kDz[f]);
			if (te == nullptr) continue;
			shared_ptr<ModMachineTileEntity> mm = dynamic_pointer_cast<ModMachineTileEntity>(te);
			if (mm != nullptr && mm->def() != nullptr && mm->def()->kind >= MachineKind_QioDrive && mm->def()->kind <= MachineKind_QioExporter) continue;
			const long long got = networkExtract(level, m_frequency, filter->id, filter->getAuxValue(), 16);
			if (got <= 0) break;
			shared_ptr<ItemInstance> stack = std::make_shared<ItemInstance>(filter->id, static_cast<int>(got), filter->getAuxValue());
			int moved = 0;
			if (mm != nullptr) moved = mm->acceptItem(stack, opposite(f));
			else
			{
				shared_ptr<Container> c = dynamic_pointer_cast<Container>(te);
				if (c != nullptr)
				{
					for (unsigned int s = 0; s < c->getContainerSize() && moved < stack->count; s++)
					{
						if (!c->canPlaceItem(s, stack)) continue;
						shared_ptr<ItemInstance> cur = c->getItem(s);
						if (cur == nullptr)
						{
							shared_ptr<ItemInstance> copy = stack->copy();
							copy->count = stack->count - moved;
							c->setItem(s, copy);
							moved = stack->count;
						}
						else if (cur->id == stack->id && cur->getAuxValue() == stack->getAuxValue() && !cur->hasTag())
						{
							const int n = (std::min)(cur->getMaxStackSize() - cur->count, stack->count - moved);
							if (n > 0) { cur->count += n; moved += n; }
						}
					}
					c->setChanged();
				}
			}
			if (moved < got) networkInsert(level, m_frequency, filter->id, filter->getAuxValue(), got - moved);
		}
		setActiveState(true);
	}
	else
	{
		setActiveState(!networkDrives(level, m_frequency).empty());
	}
}

void ModMachineTileEntity::openQioDashboard(shared_ptr<Player> player)
{
	if (level == nullptr || level->isClientSide) return;
	if (networkDrives(level, m_frequency).empty())
	{
		player->sendMessage(L"Nenhum QIO Drive Array com drives na frequ\u00EAncia " + std::to_wstring(m_frequency) + L" (use o Configurator para mudar)");
		return;
	}
	player->openContainer(std::make_shared<QioContainer>(level, m_frequency, x, y, z));
}

// ======================================================================================
// Quantum entangloporter
// ======================================================================================

int ModMachineTileEntity::entangleEnergyIn(int amount)
{
	ModMachineDef *d = def();
	if (d == nullptr) return 0;
	EntBuffer &b = entBuffers()[m_frequency];
	b.loaded = true;
	const long long room = (std::max)(0LL, static_cast<long long>(d->energyCapacity) - b.energy);
	const int n = static_cast<int>((std::min)(room, static_cast<long long>(amount)));
	b.energy += n;
	return n;
}

void ModMachineTileEntity::tickEntangloporter(ModMachineDef *d)
{
	EntBuffer &b = entBuffers()[m_frequency];
	b.loaded = true;
	// chemicals received by this block go to the shared tank
	ModChemAmount &in = m_tanks[TANK_IN0];
	if (in.amount > 0 && (b.amount == 0 || b.chem == in.name))
	{
		const int n = (std::min)(in.amount, tankCapacity() - b.amount);
		b.chem = in.name;
		b.amount += n;
		in.amount -= n;
		if (in.amount <= 0) { in.amount = 0; in.name.clear(); }
	}
	// items placed in (or sent into) the input slot go to the shared slot
	shared_ptr<ItemInstance> it = getItem(SLOT_INPUT);
	if (it != nullptr)
	{
		if (b.item == nullptr) { b.item = it; setItem(SLOT_INPUT, nullptr); }
		else if (b.item->id == it->id && b.item->getAuxValue() == it->getAuxValue() && !b.item->hasTag() && !it->hasTag())
		{
			const int n = (std::min)(it->count, b.item->getMaxStackSize() - b.item->count);
			b.item->count += n;
			it->count -= n;
			if (it->count <= 0) setItem(SLOT_INPUT, nullptr);
		}
	}
	// output faces (configurator) receive everything
	for (int f = 0; f < 6; f++)
	{
		if (faceMode(f) != FACE_OUTPUT) continue;
		shared_ptr<TileEntity> te = level->getTileEntity(x + kDx[f], y + kDy[f], z + kDz[f]);
		shared_ptr<ModMachineTileEntity> mm = dynamic_pointer_cast<ModMachineTileEntity>(te);
		if (mm != nullptr && mm->def() != nullptr && mm->def()->kind == MachineKind_Entangloporter) continue;
		if (mm != nullptr)
		{
			if (b.energy > 0)
			{
				const int moved = mm->receiveEnergy(static_cast<int>((std::min)(b.energy, static_cast<long long>((std::max)(1, d->transfer)))), opposite(f), false);
				b.energy -= moved;
			}
			if (b.amount > 0)
			{
				const int moved = mm->receiveChem(b.chem, (std::min)(b.amount, (std::max)(1000, d->transfer)), opposite(f));
				b.amount -= moved;
				if (b.amount <= 0) { b.amount = 0; b.chem.clear(); }
			}
			if (b.item != nullptr)
			{
				const int moved = mm->acceptItem(b.item, opposite(f));
				b.item->count -= moved;
				if (b.item->count <= 0) b.item = nullptr;
			}
		}
		else if (b.item != nullptr)
		{
			shared_ptr<Container> c = dynamic_pointer_cast<Container>(te);
			if (c == nullptr) continue;
			for (unsigned int s = 0; s < c->getContainerSize() && b.item != nullptr; s++)
			{
				if (!c->canPlaceItem(s, b.item)) continue;
				shared_ptr<ItemInstance> cur = c->getItem(s);
				if (cur == nullptr) { c->setItem(s, b.item); b.item = nullptr; }
				else if (cur->id == b.item->id && cur->getAuxValue() == b.item->getAuxValue() && !cur->hasTag() && !b.item->hasTag())
				{
					const int n = (std::min)(cur->getMaxStackSize() - cur->count, b.item->count);
					cur->count += n;
					b.item->count -= n;
					if (b.item->count <= 0) b.item = nullptr;
				}
			}
			c->setChanged();
		}
	}
	// mirror the shared state for the screen
	m_energy = static_cast<int>((std::min)(b.energy, 2000000000LL));
	m_tanks[TANK_OUT0].name = b.chem;
	m_tanks[TANK_OUT0].amount = b.amount;
	updateGauge(d);
	setActiveState(b.energy > 0 || b.amount > 0 || b.item != nullptr);
	setChanged();
}

// ======================================================================================
// Modification station
// ======================================================================================

void ModMachineTileEntity::tickModStation(ModMachineDef *d)
{
	drainItemInSlot(d);
	shared_ptr<ItemInstance> piece = getItem(SLOT_INPUT);
	shared_ptr<ItemInstance> module = getItem(SLOT_FUEL);
	const ModModuleDef *md = module ? ModMachines::moduleFor(module->id) : nullptr;
	const int slot = piece ? ModMachines::suitPieceSlot(piece->id) : 0;
	bool ok = piece != nullptr && md != nullptr && slot != 0 && (md->slots & slot) && getItem(SLOT_RESULT) == nullptr;
	if (ok && ModMachines::moduleLevel(piece, md->name) >= md->maxLevel) ok = false;
	const int use = (std::max)(1, energyUse(d));
	const bool running = ok && m_energy >= use;
	if (running)
	{
		m_energy -= use;
		const long long need = static_cast<long long>((std::max)(1, d->processTicks)) * 1000;
		m_progress += static_cast<long long>(1000.0f * speedMultiplier(d));
		if (m_progress >= need)
		{
			m_progress = 0;
			if (piece->tag == nullptr) piece->setTag(new CompoundTag());
			if (!piece->tag->contains(L"HSModules")) piece->tag->putCompound(L"HSModules", new CompoundTag());
			CompoundTag *mods = piece->tag->getCompound(L"HSModules");
			mods->putInt(wide(md->name), mods->getInt(wide(md->name)) + 1);
			module->count--;
			if (module->count <= 0) setItem(SLOT_FUEL, nullptr);
			setItem(SLOT_RESULT, piece);
			setItem(SLOT_INPUT, nullptr);
		}
		tickCount = static_cast<int>((std::min)(199LL, m_progress * 200 / need));
	}
	else
	{
		m_progress = 0;
		tickCount = 0;
	}
	updateGauge(d);
	setActiveState(running);
	setChanged();
}

// ======================================================================================
// Teleporter (portal frame)
// ======================================================================================

bool ModMachineTileEntity::scanTeleporterFrame(int &ax, int &ox)
{
	return ModMachines::teleporterFrame(level, x, y, z, def(), ax, ox);
}

bool ModMachineTileEntity::teleporterReady()
{
	ModMachineDef *d = def();
	return d != nullptr && m_portalAxis >= 0 && m_energy >= (std::max)(0, d->energyPerTick);
}

void ModMachineTileEntity::teleporterInterior(int &x0, int &y0, int &z0, int &ax)
{
	ax = m_portalAxis < 0 ? 0 : m_portalAxis;
	x0 = ax == 0 ? x + m_portalOffset : x;
	z0 = ax == 0 ? z : z + m_portalOffset;
	y0 = y + 1;
}

void ModMachineTileEntity::tickTeleporter(ModMachineDef *d)
{
	drainItemInSlot(d);
	globalTick()++;
	registerPos(teleporterList(), level, x, y, z);
	if (m_portalAxis < 0 || (m_tickCounter % 20) == 3)
	{
		int ax = -1, ox = 0;
		if (scanTeleporterFrame(ax, ox)) { m_portalAxis = ax; m_portalOffset = ox; }
		else m_portalAxis = -1;
	}
	updateGauge(d);
	const bool ready = teleporterReady();
	setActiveState(ready);
	if (!ready || (m_tickCounter % 2) != 0) return;

	int x0, y0, z0, ax;
	teleporterInterior(x0, y0, z0, ax);
	for (auto &p : level->players)
	{
		if (p == nullptr) continue;
		const int px = Mth::floor(p->x), py = Mth::floor(p->y + 0.1), pz = Mth::floor(p->z);
		const bool inside = py >= y0 && py <= y0 + 2 &&
			(ax == 0 ? (pz == z0 && px >= x0 && px <= x0 + 1) : (px == x0 && pz >= z0 && pz <= z0 + 1));
		if (!inside) continue;
		auto cd = teleportCooldown().find(p->entityId);
		if (cd != teleportCooldown().end() && cd->second > globalTick()) continue;
		shared_ptr<ServerPlayer> sp = dynamic_pointer_cast<ServerPlayer>(p);
		if (sp == nullptr || sp->connection == nullptr) continue;

		// next ready teleporter with the same frequency
		auto &list = teleporterList();
		size_t self = 0;
		for (size_t i = 0; i < list.size(); i++) if (list[i].level == level && list[i].x == x && list[i].y == y && list[i].z == z) self = i;
		for (size_t step = 1; step < list.size(); step++)
		{
			PosEntry e = list[(self + step) % list.size()];
			if (e.level != level) continue;
			shared_ptr<ModMachineTileEntity> target = machineAt(level, e.x, e.y, e.z);
			if (target == nullptr || target->def() == nullptr || target->def()->kind != MachineKind_Teleporter || target->frequency() != m_frequency || target->m_portalAxis < 0) continue;
			const double dist = std::sqrt(double(e.x - x) * (e.x - x) + double(e.y - y) * (e.y - y) + double(e.z - z) * (e.z - z));
			const int cost = (std::max)(d->energyPerTick, 0) + static_cast<int>(dist * 10.0);
			if (m_energy < cost)
			{
				sp->sendMessage(L"Teleporter sem energia suficiente (" + hsFmtNum(cost) + L" FE)");
				teleportCooldown()[p->entityId] = globalTick() + 40;
				return;
			}
			int tx0, ty0, tz0, tax;
			target->teleporterInterior(tx0, ty0, tz0, tax);
			m_energy -= cost;
			teleportCooldown()[p->entityId] = globalTick() + 60;
			const double tx = tax == 0 ? tx0 + 1.0 : tx0 + 0.5, tz = tax == 0 ? tz0 + 0.5 : tz0 + 1.0;
			sp->connection->teleport(tx, ty0, tz, sp->yRot, sp->xRot);
			setChanged();
			return;
		}
		sp->sendMessage(L"Nenhum outro teleporter pronto na frequ\u00EAncia " + std::to_wstring(m_frequency));
		teleportCooldown()[p->entityId] = globalTick() + 60;
		return;
	}
}

// ======================================================================================
// Interaction / screen data / save
// ======================================================================================

bool ModMachineTileEntity::interactAdvanced(shared_ptr<Player> player, int face, shared_ptr<ItemInstance> held, int role)
{
	ModMachineDef *d = def();
	if (d == nullptr) return false;
	if (d->kind == MachineKind_Fission && role == MachineTool_Configurator)
	{
		if (face == frontFace())
		{
			if (level->hasNeighborSignal(x, y, z))
			{
				player->sendMessage(L"Reator travado por redstone (SCRAM). Remova o sinal para ligar.");
				return true;
			}
			m_enabled = !m_enabled;
			player->sendMessage(std::wstring(L"Reator de fiss\u00E3o ") + (m_enabled ? L"LIGADO" : L"desligado"));
		}
		else
		{
			const int assemblies = (std::max)(1, static_cast<int>(m_mbSpeed - 1.0f + 0.5f));
			const int maxBurn = assemblies * (std::max)(1, d->burnRateMax);
			m_burnRate = m_burnRate >= maxBurn ? 1 : (std::min)(maxBurn, m_burnRate * 2);
			player->sendMessage(L"Taxa de queima: " + std::to_wstring(m_burnRate) + L" mB/t (m\u00E1x. " + std::to_wstring(maxBurn) + L")");
		}
		setChanged();
		return true;
	}
	if (role == MachineTool_SeismicReader)
	{
		if (d->kind == MachineKind_Seismic) { seismicReport(player); return true; }
		return false;
	}
	if (role == MachineTool_PortableTeleporter && d->kind == MachineKind_Teleporter)
	{
		if (held->tag == nullptr) held->setTag(new CompoundTag());
		held->tag->putInt(L"HSFreq", m_frequency);
		player->sendMessage(L"Portable Teleporter ligado \u00E0 frequ\u00EAncia " + std::to_wstring(m_frequency));
		return true;
	}
	return false;
}

void ModMachineTileEntity::hsUpdateDataExtra(ModMachineDef *d)
{
	if (hasHeat())
	{
		hsPutBig(hsData, HSD_HEAT, static_cast<long long>(m_temp));
		hsData[HSD_HEAT_MAX] = (d->kind == MachineKind_Fission ? (std::max)(400, d->maxTemperature) * 2 : 2000) / 10;
	}
	if (d->kind == MachineKind_Fission)
	{
		hsData[HSD_DAMAGE] = m_damage / 100;
		hsData[HSD_EXTRA] = (m_enabled ? 0x4000 : 0) | (std::min)(0x3FFF, m_burnRate);
	}
	else if (d->kind == MachineKind_Fusion)
	{
		hsData[HSD_EXTRA] = m_ignited ? 1 : 0;
	}
	else if (d->kind == MachineKind_Miner)
	{
		const int r = (std::max)(1, d->radius);
		const long long total = static_cast<long long>(r * 2 + 1) * (r * 2 + 1) * (std::max)(1, y - 1);
		hsData[HSD_EXTRA] = static_cast<int>((std::min)(100LL, static_cast<long long>(m_minerCursor) * 100 / total));
	}
	else if (d->kind == MachineKind_Teleporter)
	{
		hsData[HSD_FORMED] = m_portalAxis >= 0 ? 2 : 1;
	}
	hsData[HSD_UPGRADES] |= (m_upgrades[UPG_FILTER] & 0x1) << 12;
}

void ModMachineTileEntity::saveAdvanced(CompoundTag *base)
{
	base->putDouble(L"HSTemp", m_temp);
	base->putInt(L"HSDamage", m_damage);
	base->putBoolean(L"HSEnabled", m_enabled);
	base->putBoolean(L"HSIgnited", m_ignited);
	base->putInt(L"HSBurnRate", m_burnRate);
	ModMachineDef *d = def();
	if (d != nullptr && d->kind == MachineKind_Entangloporter)
	{
		EntBuffer &b = entBuffers()[m_frequency];
		base->putLong(L"HSEntEnergy", b.energy);
		base->putString(L"HSEntChem", wide(b.chem));
		base->putInt(L"HSEntAmount", b.amount);
		if (b.item != nullptr)
		{
			CompoundTag *t = new CompoundTag();
			b.item->save(t);
			base->putCompound(L"HSEntItem", t);
		}
	}
}

void ModMachineTileEntity::loadAdvanced(CompoundTag *base)
{
	m_temp = base->contains(L"HSTemp") ? base->getDouble(L"HSTemp") : 300.0;
	if (m_temp < 1.0) m_temp = 300.0;
	m_damage = base->getInt(L"HSDamage");
	m_enabled = base->getBoolean(L"HSEnabled");
	m_ignited = base->getBoolean(L"HSIgnited");
	m_burnRate = (std::max)(1, base->getInt(L"HSBurnRate"));
	if (base->contains(L"HSEntEnergy"))
	{
		EntBuffer &b = entBuffers()[m_frequency];
		if (!b.loaded)
		{
			b.loaded = true;
			b.energy = base->getLong(L"HSEntEnergy");
			const wstring c = base->getString(L"HSEntChem");
			b.chem.assign(c.begin(), c.end());
			b.amount = base->getInt(L"HSEntAmount");
			if (base->contains(L"HSEntItem")) b.item = ItemInstance::fromTag(base->getCompound(L"HSEntItem"));
		}
	}
}
