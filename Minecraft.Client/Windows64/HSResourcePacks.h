#pragma once

// Hellish Sight - Java Edition 1.8 resource packs.
//
// Packs go in the "resourcepacks" folder next to the game, as .zip files or folders
// with the usual 1.8 layout (pack.mcmeta + assets/minecraft/textures/...). The block
// and item textures of the selected pack replace the matching cells of the game's
// terrain and item atlases (names are translated from 1.8 to this engine's names;
// higher resolutions are scaled to the atlas cell, animated strips use frame 0).
// Nothing from a pack is ever bundled with the game.

#include <string>
#include <vector>

namespace HSResourcePacks
{
	std::wstring Folder();							// absolute path of "resourcepacks"
	std::vector<std::wstring> List();				// pack names (folder or zip name)
	std::wstring GetSelected();						// "" = default textures
	void SetSelected(const std::wstring &name);		// saves, prepares the cache
	void OpenFolder();								// opens the folder in Explorer

	// Reads the 1.8 texture for an atlas icon ("blocks" or "items"), already scaled
	// to cellPx x cellPx ARGB pixels. Returns false when the pack has no such texture.
	bool LoadIcon(bool terrain, const std::wstring &iconName, int cellPx, std::vector<int> &pixels);

	// Main menu dialog: Next pack / Open folder / Continue / Exit.
	void ShowMenu(int iPad);
}
