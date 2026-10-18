#pragma once

// Ruby Launcher / Hellish Sight: loads box models (HSModel) for mod blocks.
//
// Model file (JSON, written by the mod's build tools from the Java block models):
// {
//   "textures": ["res/models/chemical_infuser.png", ...],
//   "rotate": true,
//   "elements": [
//     { "from": [x, y, z], "to": [x, y, z],
//       "rotation": { "axis": "y", "angle": 22.5, "origin": [8, 8, 8], "rescale": false },
//       "shade": true, "emission": 15,
//       "faces": { "north": { "t": 0, "uv": [u0, v0, u1, v1], "rotation": 90, "cull": "north", "tint": 0 }, ... } }
//   ]
// }
// Textures are registered as mod block textures (16, 32 or 64 px squares).

#include <string>

struct HSModel;
class IconRegister;
class LevelSource;

namespace ModModels
{
	// returns the model index (HSModels::all()), -1 if the file could not be read
	int load(const std::string &modFolder, const std::string &modId, const std::string &file);

	// a connector (cable / pipe) model using one texture
	int makeConnector(const std::wstring &iconName, float thickness, bool (*connects)(LevelSource *, int, int, int, int));

	// resolves the icons of a model (call from Tile::registerIcons)
	void registerIcons(int modelIndex, IconRegister *iconRegister);
}
