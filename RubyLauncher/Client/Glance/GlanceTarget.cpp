#include "GlanceTarget.h"
#include "GlanceText.h"

#include "Loader.h"
#include "RubyMod.h"
#include "Registry/IDs.h"

#include "Minecraft.h"
#include "MultiPlayerLevel.h"
#include "MultiPlayerLocalPlayer.h"
#include "MultiPlayerGameMode.h"
#include "HitResult.h"
#include "Tile.h"
#include "Material.h"
#include "CropTile.h"
#include "Item.h"
#include "ItemInstance.h"
#include "ShearsItem.h"
#include "Entity.h"
#include "EntityIO.h"
#include "LivingEntity.h"
#include "Mob.h"
#include "Player.h"
#include "ItemEntity.h"
#include "MultiEntityMob.h"
#include "MultiEntityMobPart.h"
#include "BossMob.h"
#include "BossMobPart.h"

#include <algorithm>
#include <cstdio>

namespace Glance
{
	namespace
	{
		const char *kVanillaNamespace = "minecraft";

		std::wstring localized(unsigned int stringId)
		{
			if (stringId == 0 || stringId == static_cast<unsigned int>(-1)) return L"";
			LPCWSTR text = app.GetString(static_cast<int>(stringId));
			return text != nullptr ? std::wstring(text) : std::wstring();
		}

		std::string namespaceOf(const std::string &id)
		{
			const size_t colon = id.find(':');
			return colon == std::string::npos ? std::string(kVanillaNamespace) : id.substr(0, colon);
		}

		std::wstring modDisplayName(const std::string &modId)
		{
			if (modId.empty() || modId == kVanillaNamespace) return L"Minecraft";

			Loader *loader = Loader::getInstance();
			if (loader != nullptr)
			{
				RubyMod *mod = loader->findMod(modId);
				if (mod != nullptr && !mod->getName().empty())
				{
					return utf8ToWide(std::string(mod->getName()));
				}
			}
			return utf8ToWide(modId);
		}

		bool isValidItemId(int id)
		{
			return id > 0 && static_cast<unsigned int>(id) < Item::items.length && Item::items[id] != nullptr;
		}

		// Growth stage normalized to 0..1, or -1 if the block does not grow.
		float growthOf(Tile *tile, int tileId, int data)
		{
			switch (tileId)
			{
			case Tile::netherStalk_Id:
				return (std::min)(data, 3) / 3.0f;
			case Tile::cocoa_Id:
				return (std::min)((data >> 2) & 3, 2) / 2.0f;
			case Tile::pumpkinStem_Id:
			case Tile::melonStem_Id:
				return (std::min)(data & 7, 7) / 7.0f;
			default:
				break;
			}

			if (dynamic_cast<CropTile *>(tile) != nullptr)
			{
				return (std::min)(data & 7, 7) / 7.0f;
			}
			return -1.0f;
		}

		// Finds which tool class mines this block efficiently by probing
		// a top-tier tool of each class against the block.
		ToolKind effectiveTool(Tile *tile)
		{
			struct Candidate
			{
				Item *item;
				ToolKind kind;
			};

			const Candidate candidates[] = {
				{ Item::pickAxe_diamond, ToolKind::Pickaxe },
				{ Item::shovel_diamond, ToolKind::Shovel },
				{ Item::hatchet_diamond, ToolKind::Axe },
				{ Item::shears, ToolKind::Shears },
				{ Item::sword_diamond, ToolKind::Sword },
			};

			for (const Candidate &candidate : candidates)
			{
				if (candidate.item == nullptr) continue;
				shared_ptr<ItemInstance> probe = std::make_shared<ItemInstance>(candidate.item);
				if (probe->canDestroySpecial(tile) || probe->getDestroySpeed(tile) > 1.0f)
				{
					return candidate.kind;
				}
			}
			return ToolKind::None;
		}

		bool resolveBlock(Minecraft *mc, HitResult *hit, Target &out)
		{
			MultiPlayerLevel *level = mc->level;
			if (level == nullptr) return false;

			const int tileId = level->getTile(hit->x, hit->y, hit->z);
			if (tileId <= 0 || tileId >= Tile::TILE_NUM_COUNT) return false;

			Tile *tile = Tile::tiles[tileId];
			if (tile == nullptr) return false;

			const int data = level->getData(hit->x, hit->y, hit->z);

			out.kind = TargetKind::Block;
			out.x = hit->x;
			out.y = hit->y;
			out.z = hit->z;
			out.tileId = tileId;
			out.data = data;

			out.id = IDMapping::get()->getByID(tileId, tile->cloneTileData(level, hit->x, hit->y, hit->z));
			if (out.id == "minecraft:air") out.id = "minecraft:tile_" + std::to_string(tileId);
			out.modId = namespaceOf(out.id);
			out.modName = modDisplayName(out.modId);

			// Icon: the "pick block" item, which is what the player expects to see.
			const int cloneId = tile->cloneTileId(level, hit->x, hit->y, hit->z);
			if (isValidItemId(cloneId))
			{
				const int cloneData = tile->cloneTileData(level, hit->x, hit->y, hit->z);
				out.icon = std::make_shared<ItemInstance>(cloneId, 1, cloneData);
			}

			// Name: prefer the item name when the icon *is* this block (keeps
			// variants such as wool colours); otherwise use the block's own name
			// (crops would otherwise read as their seed item).
			if (out.icon != nullptr && cloneId == tileId)
			{
				out.name = out.icon->getHoverName();
			}
			if (out.name.empty())
			{
				out.name = localized(tile->getDescriptionId(data));
			}
			if (out.name.empty() && out.icon != nullptr)
			{
				out.name = out.icon->getHoverName();
			}
			if (out.name.empty())
			{
				out.name = utf8ToWide(out.id);
			}

			// Harvesting
			const float hardness = tile->getDestroySpeed(level, hit->x, hit->y, hit->z);
			if (hardness > 0.0f)
			{
				out.tool = effectiveTool(tile);
			}
			if (mc->player != nullptr)
			{
				out.canHarvest = mc->player->canDestroy(tile);
			}

			out.growth = growthOf(tile, tileId, data);

			// Mining progress, only while the player is breaking *this* block.
			if (mc->gameMode != nullptr)
			{
				int bx = 0, by = 0, bz = 0;
				float progress = 0.0f;
				if (mc->gameMode->getGlanceDestroyState(bx, by, bz, progress) &&
					bx == hit->x && by == hit->y && bz == hit->z)
				{
					out.breakProgress = (std::max)(0.0f, (std::min)(progress, 1.0f));
				}
			}

			return true;
		}

		// Multi-part mobs (e.g. the Ender Dragon) expose pickable part entities
		// that are not registered in EntityIO; resolve them to their owner.
		shared_ptr<Entity> resolveOwner(shared_ptr<Entity> entity)
		{
			if (shared_ptr<MultiEntityMobPart> part = dynamic_pointer_cast<MultiEntityMobPart>(entity))
			{
				if (shared_ptr<MultiEntityMob> parent = part->parentMob.lock())
				{
					if (shared_ptr<Entity> owner = dynamic_pointer_cast<Entity>(parent)) return owner;
				}
			}
			if (shared_ptr<BossMobPart> part = dynamic_pointer_cast<BossMobPart>(entity))
			{
				if (part->bossMob != nullptr)
				{
					if (Entity *owner = dynamic_cast<Entity *>(part->bossMob)) return owner->shared_from_this();
				}
			}
			return entity;
		}

		std::wstring entityName(const shared_ptr<Entity> &entity)
		{
			if (shared_ptr<Player> player = dynamic_pointer_cast<Player>(entity))
			{
				const std::wstring display = player->getDisplayName();
				return display.empty() ? player->getName() : display;
			}

			if (shared_ptr<Mob> mob = dynamic_pointer_cast<Mob>(entity))
			{
				if (mob->hasCustomName()) return mob->getCustomName();
			}

			if (shared_ptr<ItemEntity> itemEntity = dynamic_pointer_cast<ItemEntity>(entity))
			{
				shared_ptr<ItemInstance> stack = itemEntity->getItem();
				if (stack != nullptr) return stack->getHoverName();
			}

			// String-based lookup only: EntityIO::getId(shared_ptr) dereferences an
			// end() iterator for unregistered types.
			const std::wstring encodeId = EntityIO::getEncodeId(entity);
			const int nameId = encodeId.empty() ? -1 : EntityIO::getNameId(EntityIO::getId(encodeId));
			if (nameId > 0)
			{
				const std::wstring name = localized(static_cast<unsigned int>(nameId));
				if (!name.empty()) return name;
			}

			return encodeId.empty() ? std::wstring(L"Entity") : encodeId;
		}

		bool resolveEntity(HitResult *hit, Target &out)
		{
			shared_ptr<Entity> entity = hit->entity;
			if (entity == nullptr) return false;
			entity = resolveOwner(entity);

			out.kind = TargetKind::Entity;
			out.entityId = entity->entityId;
			out.modId = kVanillaNamespace;
			out.modName = modDisplayName(out.modId);

			const std::wstring encodeId = EntityIO::getEncodeId(entity);
			out.id = std::string(kVanillaNamespace) + ":" + wideToUtf8(encodeId.empty() ? L"entity" : encodeId);
			out.name = entityName(entity);

			if (shared_ptr<LivingEntity> living = dynamic_pointer_cast<LivingEntity>(entity))
			{
				out.health = living->getHealth();
				out.maxHealth = living->getMaxHealth();
				out.armor = living->getArmorValue();
			}

			if (shared_ptr<ItemEntity> itemEntity = dynamic_pointer_cast<ItemEntity>(entity))
			{
				out.icon = itemEntity->getItem();
			}

			return true;
		}
	}

	std::string Target::key() const
	{
		char buffer[160];
		switch (kind)
		{
		case TargetKind::Block:
			std::snprintf(buffer, sizeof(buffer), "b|%d|%d|%d|%d|%d", x, y, z, tileId, data);
			return buffer;
		case TargetKind::Entity:
			std::snprintf(buffer, sizeof(buffer), "e|%d", entityId);
			return buffer;
		default:
			return std::string();
		}
	}

	Target resolve(Minecraft *mc, bool includeEntities)
	{
		Target target;
		if (mc == nullptr || mc->player == nullptr || mc->hitResult == nullptr) return target;

		HitResult *hit = mc->hitResult;
		bool ok = false;
		if (hit->type == HitResult::TILE)
		{
			ok = resolveBlock(mc, hit, target);
		}
		else if (hit->type == HitResult::ENTITY && includeEntities)
		{
			ok = resolveEntity(hit, target);
		}

		return ok ? target : Target();
	}

	std::shared_ptr<ItemInstance> toolIcon(ToolKind tool)
	{
		// Iron tier reads as "a pickaxe" without implying a specific tier.
		Item *item = nullptr;
		switch (tool)
		{
		case ToolKind::Pickaxe: item = Item::pickAxe_iron; break;
		case ToolKind::Shovel:  item = Item::shovel_iron; break;
		case ToolKind::Axe:     item = Item::hatchet_iron; break;
		case ToolKind::Shears:  item = Item::shears; break;
		case ToolKind::Sword:   item = Item::sword_iron; break;
		default: break;
		}
		return item != nullptr ? std::make_shared<ItemInstance>(item) : nullptr;
	}
}
