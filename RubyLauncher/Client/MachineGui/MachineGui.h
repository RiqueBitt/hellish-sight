#pragma once

// Hellish Sight - mod machine screen decorations drawn over the furnace screen (which every mod
// machine uses): a side panel with the mod's own GUI sprites (Mekanism: nine-slice panel,
// energy bar, chemical / fluid gauges, heat bar) and the progress arrow.
//
// Sprites are looked up in the mods' res/gui folders:
//   gui_base.png (nine-slice, 16 px, border 4), gui_bar_base.png (5 px, border 1),
//   gui_bar_power.png (tiled), gui_gauge.png (5 px, border 2), gui_gauge_overlay.png,
//   gui_progress_empty.png / gui_progress_full.png.
// Engine-free header (the UI scenes include it).

#include <string>
#include <vector>

namespace HSMachineGui
{
	enum BarKind { Bar_Energy = 0, Bar_Tank = 1, Bar_Heat = 2 };

	struct Bar
	{
		float frac = 0.0f;		// 0..1
		int color = 0xFFFFFF;	// 0xRRGGBB
		int kind = Bar_Energy;
		std::wstring tooltip;
	};

	struct Screen
	{
		int pad = 0;
		int screenW = 0, screenH = 0;	// UI render target, pixels
		float panelX0 = 0, panelY0 = 0, panelX1 = 0, panelY1 = 0;	// furnace panel, pixels
		float pointerX = -1, pointerY = -1;
		std::vector<Bar> bars;
		int progress = -1;				// 0..100, -1 = none
	};

	// true when the sprites were found (the scene then hides its own fallback panel)
	bool available();
	void update(const Screen &screen);
	void clear(int pad);
	void render();
}
