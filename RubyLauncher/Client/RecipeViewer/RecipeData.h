#pragma once

// Hellish Sight recipe viewer - recipe database built from the game's registries:
// crafting (shaped / shapeless, vanilla and mods), smelting, fuels, brewing, anvil repairs
// and every mod machine (Mekanism processors, enrichment, crushing, chemical machines...).

#include <memory>
#include <string>
#include <vector>

class ItemInstance;

namespace HSRecipeViewer
{
	struct RVStack
	{
		int id = 0;
		int aux = 0;		// -1 = any
		int count = 1;
		bool empty() const { return id <= 0; }
	};

	// one input position; several options are shown in turn (wildcard aux, ore variants...)
	struct RVIngredient
	{
		std::vector<RVStack> options;
		bool empty() const { return options.empty(); }
	};

	enum RVLayout
	{
		Layout_Crafting = 0,
		Layout_Smelting,
		Layout_Fuel,
		Layout_Brewing,
		Layout_Anvil,
		Layout_Machine,
	};

	struct RVRecipe
	{
		// Crafting: 9 cells (x + y * 3). Smelting / Fuel: [0]. Brewing: [0] ingredient,
		// [1..3] bottles. Anvil: [0] left, [1] right. Machine: [0] input, [1] extra.
		std::vector<RVIngredient> inputs;
		std::vector<RVStack> outputs;		// [0] main output, [1] secondary (machines)
		std::vector<std::wstring> lines;	// extra text (chemicals, time, xp, steps)
		int width = 3, height = 3;			// crafting size
		bool shapeless = false;
	};

	struct RVCategory
	{
		std::wstring name;
		RVStack icon;
		std::vector<RVStack> catalysts;
		int layout = Layout_Crafting;
		std::vector<RVRecipe> recipes;
	};

	namespace Data
	{
		// (re)builds everything when the registries changed; cheap when nothing changed
		void ensureBuilt();
		const std::vector<RVCategory> &categories();
		// all items of the item list (creative order, unique)
		const std::vector<std::shared_ptr<ItemInstance> > &items();
		// lower-case search text of items()[i] (name + mod name)
		const std::wstring &searchName(size_t index);
		const std::wstring &searchMod(size_t index);

		bool matches(const RVStack &want, int id, int aux);
		bool ingredientMatches(const RVIngredient &ing, int id, int aux);

		// recipes that make / use the item, per category: pairs (category, recipe index)
		struct Hit { int category; int recipe; };
		void findOutputs(int id, int aux, std::vector<Hit> &out);
		void findInputs(int id, int aux, std::vector<Hit> &out);

		// display name of the mod that owns the item ("Minecraft" for vanilla)
		std::wstring modName(int id, int aux);
	}
}
