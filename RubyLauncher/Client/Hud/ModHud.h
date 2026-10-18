#pragma once

// Ruby Launcher - HUD layer API for client mods.
//
// Client scripts register "layers" (Lua functions) with hud.addLayer(fn).
// Every frame, for every local viewport, the engine calls each layer with a
// context table describing that viewport; inside the callback the layer can
// draw with the hud.* primitives (rectangles, gradients, text, item icons and
// sprites from the GUI icon sheet). Drawing calls made outside a layer
// callback are ignored, so a buggy mod can never draw at a random moment.
//
// The core game ships no HUD elements of its own here: mods such as Glance
// build their panels entirely on top of this API.

#include <sol/sol.hpp>

class Minecraft;

namespace RubyHud
{
	struct Viewport
	{
		int pad = 0;        // local player index
		int left = 0;       // viewport origin in GUI pixels
		int top = 0;        // top of the safe zone in GUI pixels
		int width = 0;      // viewport size in GUI pixels
		int height = 0;
	};

	// Called from Gui::render once per viewport while the HUD is visible.
	void renderLayers(Minecraft *mc, const Viewport &viewport);

	// Registers the `hud` table and client.getLookTarget() in the client state.
	void bindLua(sol::state &lua);
}
