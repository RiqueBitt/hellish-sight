#include "BlockRegistry.h"

#include "Item.h"
#include "Loader.h"
#include "Material.h"
#include "Registry/IDs.h"
#include "Tile.h"
#include "TileItem.h"
#include "Common/ModPaths.h"

#include <algorithm>

#include "Client/ModTile.h"
#include "Client/ModMachine.h"
#include "Common/ModPaths.h"
#include "Host/RubyLauncherHost.h"
#include "Registry/Item/ItemRegistry.h"

#if defined(_WINDOWS64)
#include "Windows64Media/strings.h"
#else
#include "DurangoMedia/loc/strings.h"
#endif

std::vector<PendingBlockDrop> BlockRegistry::pendingDrops;

namespace
{
	const Tile::SoundType *soundTypeForName(const std::string &name)
	{
		if (name == "normal")	return Tile::SOUND_NORMAL;
		if (name == "stone")	return Tile::SOUND_STONE;
		if (name == "wood")		return Tile::SOUND_WOOD;
		if (name == "gravel")	return Tile::SOUND_GRAVEL;
		if (name == "grass")	return Tile::SOUND_GRASS;
		if (name == "metal")	return Tile::SOUND_METAL;
		if (name == "glass")	return Tile::SOUND_GLASS;
		if (name == "cloth")	return Tile::SOUND_CLOTH;
		if (name == "sand")		return Tile::SOUND_SAND;
		if (name == "snow")		return Tile::SOUND_SNOW;
		if (name == "ladder")	return Tile::SOUND_LADDER;
		if (name == "anvil")	return Tile::SOUND_ANVIL;
		return nullptr;
	}

	const int TILE_ID_MIN = 269;	// Dark Nustone Stairs; this has to be changed whenever i add new blocks to the base
	const int TILE_ID_MAX = 511;

	int tileIdMax = TILE_ID_MIN - 1;

	ModTileTraits traitsFor(const BlockDefinition &def)
	{
		ModTileTraits traits;

		switch (def.tool)
		{
		case BlockTool_None:
			traits.material = Material::dirt;
			traits.sound = Tile::SOUND_GRAVEL;
			traits.placeholderIconName = L"dirt";
			traits.craftingCategory = Item::eBaseItemType_structblock;
			traits.itemMaterial = Item::eMaterial_undefined;
			break;
		case BlockTool_Hatchet:
			traits.material = Material::wood;
			traits.sound = Tile::SOUND_WOOD;
			traits.placeholderIconName = L"planks_oak";
			traits.craftingCategory = Item::eBaseItemType_structwoodstuff;
			traits.itemMaterial = Item::eMaterial_wood;
			break;
		case BlockTool_Shovel:
			traits.material = Material::dirt;
			traits.sound = Tile::SOUND_SAND;
			traits.placeholderIconName = L"sand";
			traits.craftingCategory = Item::eBaseItemType_structblock;
			traits.itemMaterial = Item::eMaterial_sand;
			break;
		case BlockTool_Hoe:
			traits.material = Material::dirt;
			traits.sound = Tile::SOUND_GRASS;
			traits.placeholderIconName = L"dirt";
			traits.craftingCategory = Item::eBaseItemType_structblock;
			traits.itemMaterial = Item::eMaterial_undefined;
			break;
		case BlockTool_Pickaxe:
		default:
			traits.material = Material::stone;
			traits.sound = Tile::SOUND_STONE;
			traits.placeholderIconName = L"stone";
			traits.craftingCategory = Item::eBaseItemType_structblock;
			traits.itemMaterial = Item::eMaterial_stone;
			break;
		}

	traits.hardness = def.hardness;
		traits.resistance = def.resistance;

		if (!def.sound.empty())
		{
			const Tile::SoundType *soundType = soundTypeForName(def.sound);
			if (soundType != nullptr)
			{
				traits.sound = soundType;
			}
			else
			{
				Loader::_debugPrint("unknown block sound '" + def.sound + "' (expected normal, stone, wood, gravel, grass, metal, glass, cloth, sand, snow, ladder or anvil); using the tool default");
			}
		}

		traits.dropId = def.dropId;
		traits.dropAux = def.dropAux;
		traits.dropMin = def.dropMin;
		traits.dropMax = def.dropMax;
		traits.dropChance = def.dropChance;
		traits.dropNothing = def.dropNothing;

		return traits;
	}
}

void BlockDefinition::finalizeDrops(const std::string &modId, const std::string &context)
{
	if (dropNothing || drop.empty())
	{
		return;
	}

	IDMapping::MappedItem mapped = IDMapping::get()->getID(drop);
	if (mapped.id != 0)
	{
		dropId = mapped.id;
		dropAux = mapped.aux;
		return;
	}

	dropPending = true;
}

BlockDefinition::BlockDefinition(sol::table table)
{
	sol::optional<float> hardnessValue = table["hardness"];
	if (hardnessValue) hardness = hardnessValue.value();

	sol::optional<float> resistanceValue = table["resistance"];
	if (resistanceValue) resistance = resistanceValue.value();

	sol::optional<EBlockTool> toolValue = table["tool"];
	if (toolValue) tool = toolValue.value();

	sol::optional<std::string> topTextureValue = table["topTexture"];
	if (topTextureValue) topTexture = topTextureValue.value();

	sol::optional<std::string> bottomTextureValue = table["bottomTexture"];
	if (bottomTextureValue) bottomTexture = bottomTextureValue.value();

	sol::optional<bool> transparentValue = table["transparent"];
	if (transparentValue) transparent = transparentValue.value();

	sol::optional<std::string> soundValue = table["sound"];
	if (soundValue) sound = soundValue.value();

	sol::optional<std::string> dropValue = table["drop"];
	if (dropValue) drop = dropValue.value();

	sol::optional<int> dropMinValue = table["dropMin"];
	if (dropMinValue) dropMin = dropMinValue.value();

	sol::optional<int> dropMaxValue = table["dropMax"];
	if (dropMaxValue) dropMax = dropMaxValue.value();

	sol::optional<float> dropChanceValue = table["dropChance"];
	if (dropChanceValue) dropChance = dropChanceValue.value();

	sol::optional<bool> dropNothingValue = table["dropNothing"];
	if (dropNothingValue) dropNothing = dropNothingValue.value();
	if (drop == "nothing") dropNothing = true;
}

int BlockRegistry::maxBlockCount() {
	return TILE_ID_MAX - TILE_ID_MIN + 1;
}

int BlockRegistry::nextItemId() {
	do {
		tileIdMax += 1;
	} while (tileIdMax <= TILE_ID_MAX && (Tile::tiles[tileIdMax] != nullptr || Item::items[tileIdMax] != nullptr));

	return (tileIdMax > TILE_ID_MAX) ? -1 : tileIdMax;
}

int BlockRegistry::registerBlock(const std::wstring& path, const std::string& id, const std::string& name, std::string modId, const std::string& texturePath, const BlockDefinition& def) {
	int nameId = ItemRegistry::nextItemNameId();
	int tileId = BlockRegistry::nextItemId();

	if (tileId == -1) {
		Loader::_debugPrint("out of block ids (" + std::to_string(TILE_ID_MIN) + "-" + std::to_string(TILE_ID_MAX) + "), '" + id + "' was not registered");
		return -1;
	}

	std::wstring wname(name.begin(), name.end());

	BlockDefinition defCopy = def;
	defCopy.finalizeDrops(modId, modId + ":" + id);
	if (defCopy.dropPending)
	{
		defCopy.dropId = -1;
		PendingBlockDrop pending;
		pending.tile = nullptr;
		pending.drop = defCopy.drop;
		pending.context = modId + ":" + id;
		pendingDrops.push_back(pending);
	}
	ModTileTraits traits = traitsFor(defCopy);

	if (!texturePath.empty())
	{
		RubyModTexture request;
		request.iconName = rubyModTextureIconName(modId, id);
		request.filePath = RubyPaths::resolveModTexturePath(RubyPaths::toNarrow(path), texturePath);
		request.fallback = RubyPaths::toNarrow(traits.placeholderIconName);
		request.block = true;

		RubyLoader::addModTexture(request);
		traits.textureIconName = RubyPaths::toWide(request.iconName);

		if (!def.topTexture.empty())
		{
			RubyModTexture topRequest;
			topRequest.iconName = request.iconName + "_top";
			topRequest.filePath = RubyPaths::resolveModTexturePath(RubyPaths::toNarrow(path), def.topTexture);
			topRequest.fallback = request.iconName;
			topRequest.block = true;

			RubyLoader::addModTexture(topRequest);
			traits.topTextureIconName = RubyPaths::toWide(topRequest.iconName);
		}

		if (!def.bottomTexture.empty())
		{
			RubyModTexture bottomRequest;
			bottomRequest.iconName = request.iconName + "_bottom";
			bottomRequest.filePath = RubyPaths::resolveModTexturePath(RubyPaths::toNarrow(path), def.bottomTexture);
			bottomRequest.fallback = request.iconName + (def.topTexture.empty() ? "" : "_top");
			bottomRequest.block = true;

			RubyLoader::addModTexture(bottomRequest);
			traits.bottomTextureIconName = RubyPaths::toWide(bottomRequest.iconName);
		}
	}

	traits.transparent = def.transparent;

	Tile *tile = new ModTile(tileId, traits);
	tile->setDescriptionId(nameId);

	if (defCopy.dropPending && !pendingDrops.empty())
	{
		pendingDrops.back().tile = static_cast<ModTile *>(tile);
	}

	Item::items[tileId] = (new TileItem(tileId - 256))
		->setIconName(traits.textureIconName.empty() ? traits.placeholderIconName : traits.textureIconName)
		->setDescriptionId(nameId)
		->setBaseItemTypeAndMaterial(traits.craftingCategory, traits.itemMaterial)
		->setUseDescriptionId(IDS_DESC_STONE);

	ItemRegistry::langList[nameId] = wname;
	IDMapping::get()->add(modId, id, true, tileId);

	RubyCreative::addEntry(tileId, 0, RubyCreativeGroup_BuildingBlocks);
	Loader::_debugPrint("registered block " + modId + ":" + id + " as tile " + std::to_string(tileId) + " (hardness " + std::to_string(def.hardness) + ")" + (texturePath.empty() ? "" : ", texture '" + texturePath + "'"));

	return tileId;
}

int BlockRegistry::registerMachine(const std::wstring& path, const std::string& id, const std::string& name, const std::string& modId, sol::table textures, sol::table table)
{
	ModMachines::registerTileEntityType();

	const int nameId = ItemRegistry::nextItemNameId();
	const int tileId = BlockRegistry::nextItemId();
	if (tileId == -1)
	{
		Loader::_debugPrint("out of block ids, machine '" + id + "' was not registered");
		return -1;
	}

	ModMachineDef def;
	def.fullId = modId + ":" + id;
	def.name = std::wstring(name.begin(), name.end());
	def.tileId = tileId;

	auto texture = [&](const char *key, const char *suffix) -> std::wstring {
		sol::optional<std::string> value = textures[key];
		if (!value || value.value().empty()) return L"";
		RubyModTexture request;
		request.iconName = rubyModTextureIconName(modId, id) + suffix;
		request.filePath = RubyPaths::resolveModTexturePath(RubyPaths::toNarrow(path), value.value());
		request.fallback = "furnace_side";
		request.block = true;
		RubyLoader::addModTexture(request);
		return RubyPaths::toWide(request.iconName);
	};
	def.iconSide = texture("side", "");
	def.iconFront = texture("front", "_front");
	def.iconFrontActive = texture("frontActive", "_front_on");
	def.iconTop = texture("top", "_top");
	def.iconBottom = texture("bottom", "_bottom");
	if (def.iconSide.empty()) def.iconSide = L"furnace_side";

	float hardness = table.get_or("hardness", 3.5f);
	def.processTicks = table.get_or("processTicks", 200);
	def.furnaceRecipes = table.get_or("furnaceRecipes", false);
	def.furnaceFuels = table.get_or("furnaceFuels", false);
	def.needsPower = table.get_or("needsPower", true);
	const std::string kind = table.get_or<std::string>("kind", "processor");
	def.kind = kind == "generator" ? MachineKind_Generator : kind == "storage" ? MachineKind_Storage : kind == "cable" ? MachineKind_Cable
		: kind == "tank" ? MachineKind_Tank : kind == "pipe" ? MachineKind_Pipe : kind == "transporter" ? MachineKind_Transporter
		: kind == "pump" ? MachineKind_Pump : kind == "miner" ? MachineKind_Miner : kind == "teleporter" ? MachineKind_Teleporter
		: kind == "heater" ? MachineKind_Heater : kind == "conductor" ? MachineKind_Conductor : kind == "boiler" ? MachineKind_Boiler
		: kind == "fission" ? MachineKind_Fission : kind == "fusion" ? MachineKind_Fusion : kind == "seismic" ? MachineKind_Seismic
		: kind == "qio_drive" ? MachineKind_QioDrive : kind == "qio_dashboard" ? MachineKind_QioDashboard
		: kind == "qio_importer" ? MachineKind_QioImporter : kind == "qio_exporter" ? MachineKind_QioExporter
		: kind == "entangloporter" ? MachineKind_Entangloporter : kind == "modification_station" ? MachineKind_ModStation
		: MachineKind_Processor;
	def.energyCapacity = table.get_or("energyCapacity", 0);
	def.energyPerTick = table.get_or("energyPerTick", 0);
	def.generation = table.get_or("generation", 0);
	def.generatorType = table.get_or<std::string>("generatorType", "burn") == "solar" ? 1 : 0;
	def.transfer = table.get_or("transfer", 0);
	def.tankCapacity = table.get_or("tankCapacity", 10000);
	def.parallel = (std::max)(1, table.get_or("parallel", 1));
	def.radius = table.get_or("radius", 8);
	def.upgrades = table.get_or("upgrades", true);
	def.heatCapacity = table.get_or("heatCapacity", 1000);
	def.heatPerTick = table.get_or("heatPerTick", 0);
	def.conductance = table.get_or("conductance", 0.2f);
	def.maxTemperature = table.get_or("maxTemperature", 1200);
	def.ignitionEnergy = table.get_or("ignitionEnergy", 0LL);
	def.burnRateMax = table.get_or("burnRateMax", 0);

	// names without a namespace belong to this mod
	auto qualify = [&](const std::string &n) -> std::string {
		if (n.empty() || n.find(':') != std::string::npos) return n;
		return modId + ":" + n;
	};
	auto chemAmount = [&](sol::object o) -> ModChemAmount {
		ModChemAmount c;
		if (o.valid() && o.is<sol::table>())
		{
			sol::table t = o.as<sol::table>();
			c.name = qualify(t.get_or<std::string>(1, ""));
			c.amount = t.get_or(2, 1);
		}
		return c;
	};
	def.fuelChem = chemAmount(table["fuelChem"]);
	def.portalFrame = qualify(table.get_or<std::string>("portalFrame", ""));
	def.exhaustChem = chemAmount(table["exhaustChem"]);

	sol::optional<sol::table> mbt = table["multiblock"];
	if (mbt)
	{
		sol::table m = mbt.value();
		ModMultiblockDef &mb = def.multiblock;
		mb.enabled = true;
		mb.interiorAir = m.get_or("interiorAir", true);
		mb.minSize = m.get_or("min", 3);
		mb.maxSize = m.get_or("max", 18);
		mb.capacityPerVolume = m.get_or("capacityPerVolume", 0);
		mb.tankPerVolume = m.get_or("tankPerVolume", 0);
		mb.speedPerVolume = m.get_or("speedPerVolume", 0.0f);
		mb.speedPerHeight = m.get_or("speedPerHeight", 0.0f);
		sol::optional<sol::table> frame = m["frame"];
		if (frame) for (auto &kv : frame.value()) if (kv.second.is<std::string>()) mb.frame.push_back(qualify(kv.second.as<std::string>()));
		sol::optional<sol::table> inner = m["inner"];
		if (inner)
		{
			for (auto &kv : inner.value())
			{
				if (!kv.first.is<std::string>() || !kv.second.is<sol::table>()) continue;
				sol::table it = kv.second.as<sol::table>();
				ModMultiblockInner in;
				in.capacity = it.get_or("capacity", 0);
				in.transfer = it.get_or("transfer", 0);
				in.speed = it.get_or("speed", 0.0f);
				mb.inner[qualify(kv.first.as<std::string>())] = in;
			}
		}
	}

	sol::optional<sol::table> recipes = table["recipes"];
	if (recipes)
	{
		for (auto &kv : recipes.value())
		{
			if (!kv.second.is<sol::table>()) continue;
			sol::table r = kv.second.as<sol::table>();
			ModMachineRecipe recipe;
			recipe.in = r.get_or<std::string>(1, "");
			recipe.out = r.get_or<std::string>(2, "");
			recipe.count = r.get_or(3, 1);
			recipe.extra = r.get_or<std::string>("extra", "");
			recipe.inCount = r.get_or("inCount", 1);
			recipe.chemIn[0] = chemAmount(r["chemIn"]);
			recipe.chemIn[1] = chemAmount(r["chemIn2"]);
			recipe.chemOut[0] = chemAmount(r["chemOut"]);
			recipe.chemOut[1] = chemAmount(r["chemOut2"]);
			sol::optional<sol::table> sec = r["secondary"];
			if (sec)
			{
				recipe.secondary = sec.value().get_or<std::string>(1, "");
				recipe.secondaryChance = sec.value().get_or("chance", sec.value().get_or(2, 1.0f));
				recipe.secondaryCount = sec.value().get_or("count", 1);
			}
			const bool hasIn = !recipe.in.empty() || !recipe.chemIn[0].name.empty() || !recipe.chemIn[1].name.empty();
			const bool hasOut = !recipe.out.empty() || !recipe.chemOut[0].name.empty() || !recipe.chemOut[1].name.empty();
			if (hasIn && hasOut) def.recipes.push_back(recipe);
		}
	}
	sol::optional<sol::table> fuels = table["power"];
	if (fuels)
	{
		for (auto &kv : fuels.value())
		{
			if (!kv.second.is<sol::table>()) continue;
			sol::table f = kv.second.as<sol::table>();
			ModMachineFuel fuel;
			fuel.item = f.get_or<std::string>(1, "");
			fuel.ticks = f.get_or(2, 200);
			if (!fuel.item.empty()) def.fuels.push_back(fuel);
		}
	}

	const int defIndex = static_cast<int>(ModMachines::defs().size());
	ModMachines::defs().push_back(def);

	Tile *tile = new ModMachineTile(tileId, defIndex, hardness);
	tile->setDescriptionId(nameId);

	Item::items[tileId] = (new TileItem(tileId - 256))
		->setIconName(def.iconFront.empty() ? def.iconSide : def.iconFront)
		->setDescriptionId(nameId)
		->setUseDescriptionId(IDS_DESC_STONE);

	ItemRegistry::langList[nameId] = def.name;
	IDMapping::get()->add(modId, id, true, tileId);
	RubyCreative::addEntry(tileId, 0, RubyCreativeGroup_Decoration);
	Loader::_debugPrint("registered machine " + def.fullId + " as tile " + std::to_string(tileId) + " with " + std::to_string(def.recipes.size()) + " recipes");
	return tileId;
}

void BlockRegistry::finalizeDrops()
{
	for (const PendingBlockDrop &pending : pendingDrops)
	{
		if (pending.tile == nullptr) continue;

		IDMapping::MappedItem mapped = IDMapping::get()->getID(pending.drop);
		if (mapped.id != 0)
		{
			pending.tile->setDropOverride(mapped.id, mapped.aux);
		}
		else
		{
			Loader::_debugPrint("block '" + pending.context + "': unknown drop '" + pending.drop + "'; dropping itself instead");
		}
	}
	pendingDrops.clear();
}
