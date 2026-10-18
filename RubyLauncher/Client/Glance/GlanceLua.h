#pragma once

// Lua surface for the Glance HUD (client state only):
//
//   glance.version                      -> "1.0.0"
//   glance.setEnabled(bool) / glance.isEnabled()
//   glance.configure{ ... }             -> see Config in GlanceHud.h
//   glance.addProvider(fn) -> id        -> fn(target) returns string | {string,...} | nil
//   glance.removeProvider(id) -> bool
//   glance.getTarget() -> table | nil   -> what the first player is looking at

#include "sol/sol.hpp"

#include <string>
#include <vector>

namespace Glance
{
	struct Target;

	void bindLua(sol::state &lua);

	// Runs every registered provider for the target and appends their lines.
	void collectProviderLines(const Target &target, std::vector<std::wstring> &out);
}
