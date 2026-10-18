#pragma once

// Hellish Sight - PC (keyboard and mouse) experience:
//   * button prompts: the controller icons (A/B/X/Y, LT/RT...) of the tooltip bar are replaced by
//     the keys / mouse buttons that do the same thing, depending on the active input device
//   * input mode: Automatic (last device used), Keyboard and mouse, Controller - F9 cycles it,
//     the choice is saved
//   * keyboard and mouse controls page shown over the Controls menu
// Engine-free header (the UI includes it).

#include <string>

namespace HSPcInput
{
	enum Mode { Mode_Auto = 0, Mode_Keyboard, Mode_Controller, Mode_Count };

	Mode mode();
	void cycleMode();						// F9
	bool keyboardPrompts(int pad);			// show keys instead of controller buttons?

	// tooltip bar (EToolTipButton order). inMenu: a menu is open (keys differ from gameplay)
	void setTooltip(int pad, int button, const std::wstring &label, bool show);
	void setTooltipsVisible(int pad, bool visible, bool inMenu);

	// called every frame on the main thread
	void tick();
	void render();
}
