#include "stdafx.h"
#include "RecipeData.h"
#include "RecipeViewer.h"

#include "net.minecraft.world.item.h"
#include "net.minecraft.world.item.crafting.h"
#include "net.minecraft.world.item.alchemy.h"
#include "net.minecraft.world.effect.h"
#include "net.minecraft.world.level.tile.h"
#include "net.minecraft.world.level.tile.entity.h"
#include "FurnaceRecipes.h"
#include "Common/Potion_Macros.h"

#include "Client/ModMachine.h"
#include "Host/RubyLauncherHost.h"
#include "Registry/IDs.h"
#include "Common/ModPaths.h"

#include <algorithm>
#include <cwctype>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>

namespace HSRecipeViewer
{
namespace Data
{
	namespace
	{
		std::vector<RVCategory> g_categories;
		std::vector<std::shared_ptr<ItemInstance> > g_items;
		std::vector<std::wstring> g_searchNames, g_searchMods;
		std::map<int, std::vector<int> > g_variants;		// id -> aux values present in the item list
		std::unordered_map<std::string, std::wstring> g_modNames;	// modId -> display name
		size_t g_builtRecipeCount = (size_t)-1;
		size_t g_builtFurnaceCount = (size_t)-1;
		size_t g_builtMachineCount = (size_t)-1;
		size_t g_builtItemCount = (size_t)-1;
		const std::wstring g_empty;

		bool validItem(int id)
		{
			return id > 0 && id < Item::ITEM_NUM_COUNT && Item::items[id] != nullptr;
		}

		std::wstring lower(const std::wstring &s)
		{
			std::wstring r(s);
			for (size_t i = 0; i < r.size(); ++i) r[i] = static_cast<wchar_t>(towlower(r[i]));
			return r;
		}

		RVStack stackOf(const ItemInstance *it)
		{
			RVStack s;
			if (it == nullptr) return s;
			s.id = it->id;
			s.aux = it->getAuxValue();
			s.count = (std::max)(1, it->count);
			return s;
		}

		// one ingredient from an id / aux pair (aux -1 or 0xFF... = any): every variant in turn
		RVIngredient ingredientOf(int id, int aux, int count = 1)
		{
			RVIngredient ing;
			if (!validItem(id)) return ing;
			const bool any = aux < 0 || aux == 0xFF || aux == 0x7FFF || aux == 32767;
			if (any)
			{
				auto it = g_variants.find(id);
				if (it != g_variants.end() && !it->second.empty())
				{
					for (int a : it->second)
					{
						RVStack s; s.id = id; s.aux = a; s.count = count;
						ing.options.push_back(s);
					}
					return ing;
				}
				aux = 0;
			}
			RVStack s; s.id = id; s.aux = aux; s.count = count;
			ing.options.push_back(s);
			return ing;
		}

		std::wstring prettyChem(const std::string &name)
		{
			std::wstring w = ModMachines::chemicalName(name);
			if (!w.empty()) return w;
			std::string base = name;
			const size_t colon = base.find(':');
			if (colon != std::string::npos) base = base.substr(colon + 1);
			std::wstring out;
			bool up = true;
			for (char c : base)
			{
				if (c == '_') { out.push_back(L' '); up = true; continue; }
				out.push_back(static_cast<wchar_t>(up ? toupper(static_cast<unsigned char>(c)) : c));
				up = false;
			}
			return out;
		}

		std::wstring itemName(int id, int aux)
		{
			if (!validItem(id)) return L"?";
			std::shared_ptr<ItemInstance> it = std::make_shared<ItemInstance>(id, 1, aux < 0 ? 0 : aux);
			return it->getHoverName();
		}

		RVStack catalyst(int id, int aux = 0)
		{
			RVStack s; s.id = id; s.aux = aux; return s;
		}

		// ---- item list ---------------------------------------------------------------
		void buildItems()
		{
			g_items.clear();
			g_searchNames.clear();
			g_searchMods.clear();
			g_variants.clear();

			std::vector<std::shared_ptr<ItemInstance> > all;
			HS_CreativeCollectAll(all);

			std::set<long long> seen;
			for (const std::shared_ptr<ItemInstance> &it : all)
			{
				if (it == nullptr || !validItem(it->id)) continue;
				const long long key = (static_cast<long long>(it->id) << 32) | static_cast<unsigned int>(it->getAuxValue());
				if (it->hasTag()) { /* enchanted books, fireworks: keep every one */ }
				else if (!seen.insert(key).second) continue;

				std::shared_ptr<ItemInstance> copy = it->copy();
				copy->count = 1;
				g_items.push_back(copy);
				g_searchNames.push_back(lower(copy->getHoverName()));
				g_searchMods.push_back(lower(modName(copy->id, copy->getAuxValue())));

				std::vector<int> &v = g_variants[copy->id];
				const int aux = copy->getAuxValue();
				if (!copy->hasTag() && std::find(v.begin(), v.end(), aux) == v.end()) v.push_back(aux);
			}
		}

		// ---- crafting ------------------------------------------------------------------
		void buildCrafting(RVCategory &cat)
		{
			cat.name = L"Crafting Table";
			cat.icon = catalyst(Tile::workBench_Id);
			cat.catalysts.push_back(catalyst(Tile::workBench_Id));
			cat.layout = Layout_Crafting;

			Recipes *recipes = Recipes::getInstance();
			if (recipes == nullptr) return;
			std::vector<Recipy *> *list = recipes->getRecipies();
			Recipy::INGREDIENTS_REQUIRED *req = recipes->getRecipeIngredientsArray();
			if (list == nullptr || req == nullptr) return;

			for (size_t i = 0; i < list->size(); ++i)
			{
				Recipy *r = (*list)[i];
				if (r == nullptr) continue;
				const ItemInstance *result = r->getResultItem();
				if (result == nullptr || !validItem(result->id)) continue;
				if (req[i].pRecipy != r || req[i].uiGridA == nullptr) continue;

				RVRecipe rv;
				rv.inputs.resize(9);
				bool anyInput = false;
				int maxX = 0, maxY = 0;
				for (int c = 0; c < 9; ++c)
				{
					const unsigned int v = req[i].uiGridA[c];
					if (v == 0) continue;
					const int id = static_cast<int>(v & 0x00FFFFFF);
					int aux = static_cast<int>((v >> 24) & 0xFF);
					if (aux == 0xFF) aux = -1;
					rv.inputs[c] = ingredientOf(id, aux);
					if (!rv.inputs[c].empty())
					{
						anyInput = true;
						maxX = (std::max)(maxX, c % 3 + 1);
						maxY = (std::max)(maxY, c / 3 + 1);
					}
				}
				if (!anyInput) continue;

				rv.shapeless = dynamic_cast<ShapelessRecipy *>(r) != nullptr;
				if (rv.shapeless)
				{
					int n = 0;
					for (int c = 0; c < 9; ++c) if (!rv.inputs[c].empty()) ++n;
					rv.width = n <= 4 ? 2 : 3;
					rv.height = n <= 2 ? 1 : (n <= 4 ? 2 : 3);
				}
				else
				{
					rv.width = maxX;
					rv.height = maxY;
				}
				rv.outputs.push_back(stackOf(result));
				cat.recipes.push_back(rv);
			}
		}

		// ---- smelting / fuel -------------------------------------------------------------
		void buildSmelting(RVCategory &smelt, RVCategory &fuel)
		{
			smelt.name = L"Smelting";
			smelt.icon = catalyst(Tile::furnace_Id);
			smelt.catalysts.push_back(catalyst(Tile::furnace_Id));
			smelt.layout = Layout_Smelting;

			fuel.name = L"Fuel";
			fuel.icon = catalyst(Item::coal_Id);
			fuel.catalysts.push_back(catalyst(Tile::furnace_Id));
			fuel.layout = Layout_Fuel;

			FurnaceRecipes *fr = FurnaceRecipes::getInstance();
			std::set<long long> done;
			for (const std::shared_ptr<ItemInstance> &it : g_items)
			{
				if (it->hasTag()) continue;
				const int id = it->id, aux = it->getAuxValue();

				if (fr != nullptr)
				{
					ItemInstance *res = fr->getResult(id, aux);
					if (res != nullptr && validItem(res->id))
					{
						const long long key = (static_cast<long long>(id) << 32) | static_cast<unsigned int>(aux);
						if (done.insert(key).second)
						{
							RVRecipe rv;
							RVStack in; in.id = id; in.aux = aux;
							RVIngredient ing; ing.options.push_back(in);
							rv.inputs.push_back(ing);
							rv.outputs.push_back(stackOf(res));
							const float xp = fr->getRecipeValue(res->id);
							if (xp > 0.0f)
							{
								wchar_t buf[48];
								swprintf_s(buf, 48, L"%.1f XP", xp);
								rv.lines.push_back(buf);
							}
							smelt.recipes.push_back(rv);
						}
					}
				}

				const int burn = FurnaceTileEntity::getBurnDuration(it);
				if (burn > 0)
				{
					RVRecipe rv;
					RVStack in; in.id = id; in.aux = aux;
					RVIngredient ing; ing.options.push_back(in);
					rv.inputs.push_back(ing);
					wchar_t buf[64];
					swprintf_s(buf, 64, L"Burn time: %d ticks", burn);
					rv.lines.push_back(buf);
					const float items = burn / 200.0f;
					if (items == static_cast<int>(items)) swprintf_s(buf, 64, L"Smelts %d items", static_cast<int>(items));
					else swprintf_s(buf, 64, L"Smelts %.1f items", items);
					rv.lines.push_back(buf);
					fuel.recipes.push_back(rv);
				}
			}
		}

		// ---- brewing -----------------------------------------------------------------------
		bool sameEffects(vector<MobEffectInstance *> *a, vector<MobEffectInstance *> *b)
		{
			if (a == nullptr || b == nullptr) return a == b;
			if (a->size() != b->size()) return false;
			for (size_t i = 0; i < a->size(); ++i)
			{
				MobEffectInstance *x = (*a)[i], *y = (*b)[i];
				if (x == y) continue;
				if (x == nullptr || y == nullptr) return false;
				if (x->getId() != y->getId() || x->getAmplifier() != y->getAmplifier() || x->getDuration() != y->getDuration()) return false;
			}
			return true;
		}

		// the brewing stand's rule for one bottle (see BrewingStandTileEntity::doBrew); -1 = no change
		int brewOnce(int currentBrew, int ingredientId)
		{
			Item *ing = Item::items[ingredientId];
			if (ing == nullptr || !ing->hasPotionBrewingFormula()) return -1;
			const int newBrew = NORMALISE_POTION_AUXVAL(PotionBrewing::applyBrew(currentBrew, ing->getPotionBrewingFormula()));

			vector<MobEffectInstance *> *currentEffects = Item::potion->getMobEffects(currentBrew);
			vector<MobEffectInstance *> *newEffects = Item::potion->getMobEffects(newBrew);
			const bool equals = currentEffects != nullptr && newEffects != nullptr && sameEffects(currentEffects, newEffects);

			if ((currentBrew > 0 && currentEffects == newEffects) || (currentEffects != nullptr && (equals || newEffects == nullptr)))
			{
				if (!PotionItem::isThrowable(currentBrew) && PotionItem::isThrowable(newBrew)) return newBrew;
				return -1;
			}
			if (currentBrew != newBrew) return newBrew;
			return -1;
		}

		void buildBrewing(RVCategory &cat)
		{
			cat.name = L"Brewing";
			cat.icon = catalyst(Item::brewingStand_Id);
			cat.catalysts.push_back(catalyst(Item::brewingStand_Id));
			cat.layout = Layout_Brewing;

			std::vector<int> ingredients;
			for (const std::shared_ptr<ItemInstance> &it : g_items)
			{
				Item *item = Item::items[it->id];
				if (item != nullptr && item->hasPotionBrewingFormula() &&
					std::find(ingredients.begin(), ingredients.end(), it->id) == ingredients.end())
				{
					ingredients.push_back(it->id);
				}
			}

			std::map<int, int> steps;		// brew -> steps from a water bottle
			std::deque<int> queue;
			steps[0] = 0;
			queue.push_back(0);
			std::set<long long> edges;
			while (!queue.empty() && cat.recipes.size() < 2000)
			{
				const int cur = queue.front();
				queue.pop_front();
				const int depth = steps[cur];
				if (depth >= 6) continue;
				for (int ingId : ingredients)
				{
					const int next = brewOnce(cur, ingId);
					if (next < 0 || next == cur) continue;
					const long long key = (static_cast<long long>(cur) << 32) | (static_cast<long long>(ingId) << 16) | (next & 0xFFFF);
					if (!edges.insert(key).second) continue;

					if (steps.find(next) == steps.end())
					{
						steps[next] = depth + 1;
						queue.push_back(next);
					}

					RVRecipe rv;
					RVIngredient ingredient; RVStack is; is.id = ingId; is.aux = 0;
					ingredient.options.push_back(is);
					rv.inputs.push_back(ingredient);
					for (int b = 0; b < 3; ++b)
					{
						RVIngredient bottle; RVStack bs; bs.id = Item::potion_Id; bs.aux = cur;
						bottle.options.push_back(bs);
						rv.inputs.push_back(bottle);
					}
					RVStack out; out.id = Item::potion_Id; out.aux = next;
					rv.outputs.push_back(out);
					wchar_t buf[32];
					swprintf_s(buf, 32, L"Steps: %d", depth + 1);
					rv.lines.push_back(buf);
					cat.recipes.push_back(rv);
				}
			}
		}

		// ---- anvil repairs ---------------------------------------------------------------------
		void buildAnvil(RVCategory &cat)
		{
			cat.name = L"Anvil";
			cat.icon = catalyst(Tile::anvil_Id);
			cat.catalysts.push_back(catalyst(Tile::anvil_Id));
			cat.layout = Layout_Anvil;

			std::vector<std::shared_ptr<ItemInstance> > materials;
			for (const std::shared_ptr<ItemInstance> &it : g_items)
			{
				Item *item = Item::items[it->id];
				if (item != nullptr && !it->hasTag() && !item->canBeDepleted()) materials.push_back(it);
			}

			for (const std::shared_ptr<ItemInstance> &it : g_items)
			{
				Item *item = Item::items[it->id];
				if (item == nullptr || it->hasTag() || !item->canBeDepleted() || item->getMaxDamage() <= 0) continue;
				const int maxDamage = item->getMaxDamage();
				for (const std::shared_ptr<ItemInstance> &mat : materials)
				{
					if (!item->isValidRepairItem(it, mat)) continue;
					RVRecipe rv;
					RVStack damaged; damaged.id = it->id; damaged.aux = (maxDamage * 3) / 4;
					RVIngredient left; left.options.push_back(damaged);
					RVIngredient right = ingredientOf(mat->id, mat->getAuxValue());
					rv.inputs.push_back(left);
					rv.inputs.push_back(right);
					RVStack repaired; repaired.id = it->id; repaired.aux = maxDamage / 2;
					rv.outputs.push_back(repaired);
					rv.lines.push_back(L"Repair");
					cat.recipes.push_back(rv);
				}
			}
		}

		// ---- mod machines -------------------------------------------------------------------------
		void buildMachines(std::vector<RVCategory> &out, RVCategory &smelting)
		{
			std::vector<ModMachineDef> &defs = ModMachines::defs();
			for (ModMachineDef &def : defs)
			{
				if (!def.resolved) ModMachines::resolve(def);
				const int machineItem = def.tileId;
				if (def.furnaceRecipes && validItem(machineItem)) smelting.catalysts.push_back(catalyst(machineItem));
				if (def.recipes.empty()) continue;

				RVCategory cat;
				cat.name = def.name.empty() ? RubyPaths::toWide(def.fullId) : def.name;
				cat.icon = catalyst(validItem(machineItem) ? machineItem : Tile::furnace_Id);
				if (validItem(machineItem)) cat.catalysts.push_back(catalyst(machineItem));
				cat.layout = Layout_Machine;

				for (const ModMachineRecipe &r : def.recipes)
				{
					RVRecipe rv;
					rv.inputs.push_back(ingredientOf(r.inId, r.inAux, (std::max)(1, r.inCount)));
					rv.inputs.push_back(ingredientOf(r.extraId, r.extraAux));

					if (validItem(r.outId))
					{
						RVStack o; o.id = r.outId; o.aux = r.outAux; o.count = (std::max)(1, r.count);
						rv.outputs.push_back(o);
					}
					else rv.outputs.push_back(RVStack());
					if (validItem(r.secondaryId))
					{
						RVStack s; s.id = r.secondaryId; s.aux = r.secondaryAux; s.count = (std::max)(1, r.secondaryCount);
						rv.outputs.push_back(s);
						wchar_t buf[48];
						swprintf_s(buf, 48, L"Bonus: %d%% chance", static_cast<int>(r.secondaryChance * 100.0f + 0.5f));
						rv.lines.push_back(buf);
					}
					for (int k = 0; k < 2; ++k)
					{
						if (!r.chemIn[k].name.empty() && r.chemIn[k].amount > 0)
							rv.lines.push_back(L"In: " + std::to_wstring(r.chemIn[k].amount) + L" mB " + prettyChem(r.chemIn[k].name));
					}
					for (int k = 0; k < 2; ++k)
					{
						if (!r.chemOut[k].name.empty() && r.chemOut[k].amount > 0)
							rv.lines.push_back(L"Out: " + std::to_wstring(r.chemOut[k].amount) + L" mB " + prettyChem(r.chemOut[k].name));
					}
					if (rv.inputs[0].empty() && rv.inputs[1].empty() && rv.lines.empty()) continue;
					if (rv.outputs[0].empty() && rv.outputs.size() < 2)
					{
						bool chemOut = !r.chemOut[0].name.empty() || !r.chemOut[1].name.empty();
						if (!chemOut) continue;
					}
					cat.recipes.push_back(rv);
				}
				if (!cat.recipes.empty()) out.push_back(cat);
			}
		}

		void buildModNames()
		{
			g_modNames.clear();
			g_modNames["minecraft"] = L"Minecraft";
			for (const RubyModInfo &info : RubyLoader::getInstalledMods())
			{
				if (info.modId.empty()) continue;
				g_modNames[info.modId] = info.name.empty() ? RubyPaths::toWide(info.modId) : RubyPaths::toWide(info.name);
			}
		}

		size_t machineRecipeCount()
		{
			size_t n = 0;
			for (ModMachineDef &d : ModMachines::defs()) n += d.recipes.size() + 1;
			return n;
		}
	}

	std::wstring modName(int id, int aux)
	{
		IDMapping *mapping = IDMapping::get();
		if (mapping == nullptr) return L"Minecraft";
		if (mapping->idToString.empty()) mapping->buildReverseMap();
		std::string key = mapping->getByID(id, aux < 0 ? 0 : aux);
		if (key.empty()) key = mapping->getByID(id, 0);
		const size_t colon = key.find(':');
		if (colon == std::string::npos) return L"Minecraft";
		const std::string modId = key.substr(0, colon);
		auto it = g_modNames.find(modId);
		if (it != g_modNames.end()) return it->second;
		return modId == "minecraft" ? std::wstring(L"Minecraft") : RubyPaths::toWide(modId);
	}

	void ensureBuilt()
	{
		Recipes *recipes = Recipes::getInstance();
		const size_t recipeCount = (recipes != nullptr && recipes->getRecipies() != nullptr) ? recipes->getRecipies()->size() : 0;
		FurnaceRecipes *fr = FurnaceRecipes::getInstance();
		const size_t furnaceCount = fr != nullptr ? fr->getRecipies()->size() : 0;
		const size_t machineCount = machineRecipeCount();

		std::vector<std::shared_ptr<ItemInstance> > probe;
		HS_CreativeCollectAll(probe);
		const size_t itemCount = probe.size();

		if (recipeCount == g_builtRecipeCount && furnaceCount == g_builtFurnaceCount &&
			machineCount == g_builtMachineCount && itemCount == g_builtItemCount)
		{
			return;
		}
		g_builtRecipeCount = recipeCount;
		g_builtFurnaceCount = furnaceCount;
		g_builtMachineCount = machineCount;
		g_builtItemCount = itemCount;

		buildModNames();
		buildItems();

		g_categories.clear();
		RVCategory crafting, smelting, fuel, brewing, anvil;
		buildCrafting(crafting);
		buildSmelting(smelting, fuel);
		buildBrewing(brewing);
		buildAnvil(anvil);
		std::vector<RVCategory> machines;
		buildMachines(machines, smelting);

		g_categories.push_back(crafting);
		g_categories.push_back(smelting);
		g_categories.push_back(fuel);
		g_categories.push_back(brewing);
		g_categories.push_back(anvil);
		for (RVCategory &m : machines) g_categories.push_back(m);
	}

	const std::vector<RVCategory> &categories() { return g_categories; }
	const std::vector<std::shared_ptr<ItemInstance> > &items() { return g_items; }
	const std::wstring &searchName(size_t index) { return index < g_searchNames.size() ? g_searchNames[index] : g_empty; }
	const std::wstring &searchMod(size_t index) { return index < g_searchMods.size() ? g_searchMods[index] : g_empty; }

	bool matches(const RVStack &want, int id, int aux)
	{
		if (want.id != id) return false;
		if (want.aux < 0 || aux < 0) return true;
		Item *item = (id > 0 && id < Item::ITEM_NUM_COUNT) ? Item::items[id] : nullptr;
		// damageable items: any damage counts as the same item
		if (item != nullptr && item->canBeDepleted()) return true;
		if (id == Item::potion_Id) return NORMALISE_POTION_AUXVAL(want.aux) == NORMALISE_POTION_AUXVAL(aux);
		return want.aux == aux;
	}

	bool ingredientMatches(const RVIngredient &ing, int id, int aux)
	{
		for (const RVStack &s : ing.options) if (matches(s, id, aux)) return true;
		return false;
	}

	void findOutputs(int id, int aux, std::vector<Hit> &out)
	{
		for (size_t c = 0; c < g_categories.size(); ++c)
		{
			const RVCategory &cat = g_categories[c];
			if (cat.layout == Layout_Fuel) continue;
			for (size_t r = 0; r < cat.recipes.size(); ++r)
			{
				for (const RVStack &o : cat.recipes[r].outputs)
				{
					if (!o.empty() && matches(o, id, aux)) { out.push_back({ static_cast<int>(c), static_cast<int>(r) }); break; }
				}
			}
		}
	}

	void findInputs(int id, int aux, std::vector<Hit> &out)
	{
		for (size_t c = 0; c < g_categories.size(); ++c)
		{
			const RVCategory &cat = g_categories[c];
			bool catalyst = false;
			for (const RVStack &k : cat.catalysts) if (matches(k, id, aux)) catalyst = true;
			for (size_t r = 0; r < cat.recipes.size(); ++r)
			{
				bool hit = catalyst;
				for (const RVIngredient &ing : cat.recipes[r].inputs)
				{
					if (hit) break;
					if (ingredientMatches(ing, id, aux)) hit = true;
				}
				if (hit) out.push_back({ static_cast<int>(c), static_cast<int>(r) });
			}
		}
	}
}
}
