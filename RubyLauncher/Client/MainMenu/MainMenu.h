#pragma once

// Hellish Sight - redesigned title screen. The original Flash main menu keeps running (it owns the
// sign-in / navigation logic), but it is not drawn: this module draws the new screen and turns
// mouse / keyboard / controller input into the original menu actions.
// Engine-free header.

#include <string>

namespace HSMainMenu
{
	// actions understood by UIScene_MainMenu::HS_Press (same values as its EControls)
	enum Action
	{
		Act_None = -1,
		Act_Play = 0, Act_Leaderboards = 1, Act_Achievements = 2, Act_Options = 3, Act_Store = 4,
		Act_MiniGames = 5, Act_JavaServers = 6, Act_TexturePacks = 7, Act_Exit = 8,
	};

	bool enabled();
	// renders the screen (called from the main menu scene's render, so dialogs stay on top)
	void render(int pad, int renderW, int renderH, const std::wstring &splash);
	// menu input; returns the action to run (Act_None when consumed or ignored).
	// handled = the key was used by the new screen
	int handleInput(int pad, int key, bool pressed, bool repeat, bool &handled);
	void reset();	// the scene was (re)created
}
