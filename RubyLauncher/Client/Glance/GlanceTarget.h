#pragma once

// Glance - "what am I looking at" HUD for Ruby Launcher.
// Original implementation written from scratch for this codebase.
//
// GlanceTarget turns the engine's crosshair hit result into a plain,
// renderer-agnostic description of the thing under the crosshair.

#include <memory>
#include <string>
#include <vector>

class Minecraft;
class ItemInstance;

namespace Glance
{
	enum class TargetKind
	{
		None,
		Block,
		Entity
	};

	enum class ToolKind
	{
		None,
		Pickaxe,
		Shovel,
		Axe,
		Shears,
		Sword
	};

	struct Target
	{
		TargetKind kind = TargetKind::None;

		// Identity
		std::string id;      // "minecraft:stone", "steelMod:steelOre", "minecraft:Zombie"
		std::string modId;   // "minecraft" or the owning mod's id
		std::wstring name;   // localized display name
		std::wstring modName;
		std::shared_ptr<ItemInstance> icon; // may be null (fluids, mobs, ...)

		// Blocks
		int x = 0, y = 0, z = 0;
		int tileId = 0;
		int data = 0;
		ToolKind tool = ToolKind::None;
		bool canHarvest = true;
		float growth = -1.0f;        // 0..1 for crops, -1 when not applicable
		float breakProgress = 0.0f;  // 0..1 while the player is mining this block

		// Entities
		int entityId = -1;
		float health = -1.0f;
		float maxHealth = -1.0f;
		int armor = 0;

		// Lines contributed by Lua providers (may contain section-sign color codes)
		std::vector<std::wstring> extraLines;

		// Stable key used to detect target changes.
		std::string key() const;
	};

	// Resolves the current crosshair target of the given client.
	// Never throws; returns a Target with kind == None when nothing applies.
	Target resolve(Minecraft *mc, bool includeEntities);

	// Item used to draw the "effective tool" icon, or null.
	std::shared_ptr<ItemInstance> toolIcon(ToolKind tool);
}
