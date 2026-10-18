#pragma once

// Hellish Sight - built-in recipe viewer (JEI-style).
//
// While a container screen (inventory, crafting table, furnace, chest, machine, creative...)
// is open on the keyboard/mouse player, an item list is shown to the right of the panel with
// pages and a search box. Clicking an item (or pressing R / U over any item) opens the recipe
// window: category tabs, catalysts, pages of recipes and a "+" button that moves the
// ingredients into the crafting grid.
//
// This header is engine-free so the UI scenes can include it.

#include <memory>
#include <string>
#include <vector>

class ItemInstance;

namespace HSRecipeViewer
{
	struct SceneInfo
	{
		int pad = 0;
		int screenW = 0, screenH = 0;		// size of the UI render target in pixels
		float panelX0 = 0, panelY0 = 0, panelX1 = 0, panelY1 = 0;	// container panel (pixels)
		float pointerX = 0, pointerY = 0;	// pointer centre (pixels)
		std::shared_ptr<ItemInstance> hovered;	// container item under the pointer, if any
		int craftGrid = 0;					// 0 none, 2 = 2x2 inventory grid, 3 = 3x3 crafting table
		bool creative = false;
		std::wstring debug;					// temporary layout diagnostics
	};

	// true when the overlay should run for this pad (pad 0 with keyboard and mouse)
	bool enabledFor(int pad);
	// called every tick by the open container scene
	void updateScene(const SceneInfo &info);
	// the container scene closed
	void sceneClosed(int pad);
	// the scene should let the pointer move over the whole screen
	bool wantsFreePointer();
	// menu actions from the container scene (ACTION_MENU_*); true = consumed by the viewer
	bool handleAction(int pad, int action, bool pressed, bool released, bool repeat);
	// the search box is focused: keyboard keys must not reach the game or the UI
	bool capturesKeyboard();
	// keys the viewer uses while a container is open (R / U)
	bool reservesKey(int vk);
	// draws the overlay; called after the UI was rendered
	void render();
}

// Implemented by the creative menu: every item shown in the creative tabs, in order.
void HS_CreativeCollectAll(std::vector<std::shared_ptr<ItemInstance> > &out);
