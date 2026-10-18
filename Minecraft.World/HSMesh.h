#pragma once
// Hellish Sight: free-form 3D meshes for mods (OBJ / Blockbench models converted to JSON by the
// mod's tools). Unlike HSModel (box models baked into the terrain atlas), meshes use their own
// textures and are drawn every frame, so they can animate:
//   - item models   (inventory, hand, dropped)      registerItemModel(item, file)
//   - armour models (worn on the player / mobs)      registerArmorModel(item, file, slot)
//   - machine models (tile entity renderer, parts can spin: wind generator blades)
//
// Mesh file (JSON):
// {
//   "textures": ["res/mesh/x.png", ...],
//   "parts": { "name": [ [tex, emissive, x,y,z,u,v, x,y,z,u,v, x,y,z,u,v, x,y,z,u,v], ... ], ... },
//   "pivots": { "name": [x, y, z] },                 (optional, spinning parts)
//   "display": { "gui": {"rotation":[x,y,z], "translation":[x,y,z], "scale":[x,y,z]}, "firstperson": ..., "thirdperson": ..., "ground": ... }
// }
// Positions are in blocks (Java model pixels / 16), uv in 0..1.

#include <string>
#include <vector>

struct HSMeshQuad
{
	int tex = 0;
	bool emissive = false;
	float v[4][5];		// x, y, z, u, v
	float n[3] = { 0, 1, 0 };	// face normal (computed at load)
};

struct HSMeshRun { int tex; bool emissive; int first, count; };	// quads sorted by texture at load

struct HSMeshPart
{
	std::string name;
	std::vector<HSMeshQuad> quads;
	std::vector<HSMeshRun> runs;
	float pivot[3] = { 0.5f, 0.5f, 0.5f };
	bool hasPivot = false;				// quads are relative to the pivot (spinning parts)
};

struct HSMeshDisplay
{
	bool present = false;
	float rotation[3] = { 0, 0, 0 };
	float translation[3] = { 0, 0, 0 };	// pixels
	float scale[3] = { 1, 1, 1 };
};

enum HSMeshDisplayKind { HSDisplay_Gui = 0, HSDisplay_FirstPerson, HSDisplay_ThirdPerson, HSDisplay_Ground, HSDisplay_Count };

struct HSMesh
{
	std::vector<std::string> texturePaths;	// absolute paths
	std::vector<int> glTextures;				// loaded lazily (-1 = not loaded)
	std::vector<HSMeshPart> parts;
	HSMeshDisplay display[HSDisplay_Count];

	int findPart(const std::string &name) const
	{
		for (size_t i = 0; i < parts.size(); ++i) if (parts[i].name == name) return static_cast<int>(i);
		return -1;
	}
};

// armour slots for registerArmorModel
enum HSArmorSlot { HSArmor_Head = 0, HSArmor_Chest, HSArmor_Legs, HSArmor_Feet };

namespace HSMeshes
{
	inline std::vector<HSMesh *> &all() { static std::vector<HSMesh *> v; return v; }

	struct ItemModel { int mesh = -1; };
	struct ArmorModel { int mesh = -1; int slot = 0; };
	struct MachineModel { int mesh = -1; std::string spinPart; int spinAxis = 2; float spinSpeed = 0.0f; bool spinWhenActiveOnly = true; };

	inline std::vector<ItemModel> &items() { static std::vector<ItemModel> v(32000); return v; }
	inline std::vector<ArmorModel> &armor() { static std::vector<ArmorModel> v(32000); return v; }
	inline std::vector<MachineModel> &machines() { static std::vector<MachineModel> v(4096); return v; }

	inline HSMesh *mesh(int index) { return (index >= 0 && index < static_cast<int>(all().size())) ? all()[index] : nullptr; }
	inline HSMesh *itemMesh(int itemId) { return (itemId > 0 && itemId < static_cast<int>(items().size())) ? mesh(items()[itemId].mesh) : nullptr; }
	inline const ArmorModel *armorModel(int itemId)
	{
		if (itemId <= 0 || itemId >= static_cast<int>(armor().size()) || armor()[itemId].mesh < 0) return nullptr;
		return &armor()[itemId];
	}
	inline const MachineModel *machineModel(int tileId)
	{
		if (tileId <= 0 || tileId >= static_cast<int>(machines().size()) || machines()[tileId].mesh < 0) return nullptr;
		return &machines()[tileId];
	}
}
