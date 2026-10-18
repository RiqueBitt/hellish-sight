#pragma once

// Hellish Sight: 3D mesh models API (see Minecraft.World/HSMesh.h). Engine-free header so the
// renderers in Minecraft.Client can call it.

#include <string>

class TileEntityRenderer;
class LivingEntity;
class HumanoidModel;

namespace HSMeshRender
{
	// loading (mods): returns the mesh index or -1
	int load(const std::string &modFolder, const std::string &file);
	bool registerItemModel(int itemId, int mesh);
	bool registerArmorModel(int itemId, int mesh, int slot);	// HSArmorSlot
	bool registerMachineModel(int tileId, int mesh, const std::string &spinPart, int spinAxis, float spinSpeed, bool onlyWhenActive);

	enum ItemContext { Item_Gui = 0, Item_FirstPerson, Item_ThirdPerson, Item_Ground };

	// item rendering: true when the item has a mesh (and it was drawn)
	bool hasItemModel(int itemId);
	// GUI slot: x, y = top-left of the 16x16 slot, scale = slot scale
	bool renderGuiItem(int itemId, float x, float y, float scaleX, float scaleY, float alpha);
	// hand (first / third person) and dropped items: the caller has set up the transform it uses
	// for blocks (a unit cube centred at the origin)
	bool renderItemAsBlock(int itemId, int context);

	// armour: does this item draw a mesh instead of the vanilla armour layer?
	bool replacesArmorLayer(int itemId);
	// draws every worn mesh armour piece on the humanoid model (call after the body was rendered)
	void renderWornArmor(LivingEntity *mob, HumanoidModel *model, float a);

	// tile entity renderer for mod machines with a mesh (registered for the furnace TE type)
	TileEntityRenderer *createMachineRenderer();
	bool machineHidesBlock(int tileId);
}
