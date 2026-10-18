// Hellish Sight - Java Edition 1.8 resource packs. See HSResourcePacks.h.

#include "stdafx.h"
#include "HSResourcePacks.h"
#include "..\Common\HSSettings.h"
#include "..\Minecraft.h"
#include "..\Textures.h"
#include "..\LevelRenderer.h"
#include "..\StringTable.h"
#include "Registry/Item/ItemRegistry.h"
#include "..\Common\zlib\zlib.h"

#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

namespace HSResourcePacks
{
namespace
{
	std::mutex g_lock;
	std::wstring g_selected;
	bool g_selectedLoaded = false;

	std::wstring exeDir()
	{
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::wstring p = path;
		const size_t slash = p.find_last_of(L"\\/");
		return slash == std::wstring::npos ? L"." : p.substr(0, slash);
	}

	bool fileExists(const std::wstring &p)
	{
		const DWORD a = GetFileAttributesW(p.c_str());
		return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
	}

	bool dirExists(const std::wstring &p)
	{
		const DWORD a = GetFileAttributesW(p.c_str());
		return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
	}

	void makeDirs(const std::wstring &path)
	{
		std::wstring cur;
		for (size_t i = 0; i < path.size(); i++)
		{
			cur.push_back(path[i]);
			if ((path[i] == L'\\' || path[i] == L'/') && cur.size() > 3) CreateDirectoryW(cur.c_str(), nullptr);
		}
		CreateDirectoryW(path.c_str(), nullptr);
	}

	std::wstring toWide(const std::string &s)
	{
		if (s.empty()) return L"";
		const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
		std::wstring w(n, 0);
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
		return w;
	}

	std::string toNarrow(const std::wstring &w)
	{
		if (w.empty()) return "";
		const int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
		std::string s(n, 0);
		WideCharToMultiByte(CP_ACP, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
		return s;
	}

	// ---------------------------------------------------------------------------------
	// Minimal .zip reader (stored + deflate), enough for resource packs
	// ---------------------------------------------------------------------------------
	bool extractZipTextures(const std::wstring &zipPath, const std::wstring &outDir)
	{
		FILE *f = nullptr;
		if (_wfopen_s(&f, zipPath.c_str(), L"rb") != 0 || !f) return false;
		fseek(f, 0, SEEK_END);
		const long size = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (size < 22 || size > 512 * 1024 * 1024) { fclose(f); return false; }
		std::vector<unsigned char> zip(static_cast<size_t>(size));
		const bool readOk = fread(zip.data(), 1, zip.size(), f) == zip.size();
		fclose(f);
		if (!readOk) return false;

		auto u16 = [&](size_t o) { return static_cast<uint32_t>(zip[o] | (zip[o + 1] << 8)); };
		auto u32 = [&](size_t o) { return static_cast<uint32_t>(zip[o] | (zip[o + 1] << 8) | (zip[o + 2] << 16) | (static_cast<uint32_t>(zip[o + 3]) << 24)); };

		// end of central directory
		long eocd = -1;
		for (long i = size - 22; i >= 0 && i >= size - 65557; i--)
		{
			if (u32(i) == 0x06054b50) { eocd = i; break; }
		}
		if (eocd < 0) return false;
		const uint32_t entries = u16(eocd + 10);
		size_t cd = u32(eocd + 16);

		const std::string prefix = "assets/minecraft/textures/";
		int written = 0;
		for (uint32_t e = 0; e < entries && cd + 46 <= zip.size(); e++)
		{
			if (u32(cd) != 0x02014b50) break;
			const uint32_t method = u16(cd + 10);
			const uint32_t compSize = u32(cd + 20);
			const uint32_t uncompSize = u32(cd + 24);
			const uint32_t nameLen = u16(cd + 28), extraLen = u16(cd + 30), commentLen = u16(cd + 32);
			const uint32_t localOff = u32(cd + 42);
			std::string name(reinterpret_cast<const char *>(&zip[cd + 46]), nameLen);
			cd += 46 + nameLen + extraLen + commentLen;

			std::replace(name.begin(), name.end(), '\\', '/');
			// some packs wrap everything in one top-level folder
			size_t at = name.find(prefix);
			if (at == std::string::npos) continue;
			std::string rel = name.substr(at + prefix.size());
			if (rel.empty() || rel.back() == '/') continue;
			if (rel.compare(0, 7, "blocks/") != 0 && rel.compare(0, 6, "items/") != 0) continue;
			if (rel.size() < 4 || rel.compare(rel.size() - 4, 4, ".png") != 0) continue;
			if (rel.find("..") != std::string::npos) continue;
			if (localOff + 30 > zip.size() || u32(localOff) != 0x04034b50) continue;
			const size_t dataOff = localOff + 30 + u16(localOff + 26) + u16(localOff + 28);
			if (dataOff + compSize > zip.size() || uncompSize > 64 * 1024 * 1024) continue;

			std::vector<unsigned char> out(uncompSize);
			if (method == 0)
			{
				memcpy(out.data(), &zip[dataOff], uncompSize);
			}
			else if (method == 8)
			{
				z_stream zs = {};
				zs.next_in = &zip[dataOff];
				zs.avail_in = compSize;
				zs.next_out = out.data();
				zs.avail_out = uncompSize;
				if (inflateInit2(&zs, -15) != Z_OK) continue;
				const int r = inflate(&zs, Z_FINISH);
				inflateEnd(&zs);
				if (r != Z_STREAM_END) continue;
			}
			else continue;

			std::wstring dst = outDir + L"\\" + toWide(rel);
			std::replace(dst.begin(), dst.end(), L'/', L'\\');
			makeDirs(dst.substr(0, dst.find_last_of(L'\\')));
			FILE *o = nullptr;
			if (_wfopen_s(&o, dst.c_str(), L"wb") == 0 && o)
			{
				fwrite(out.data(), 1, out.size(), o);
				fclose(o);
				written++;
			}
		}
		return written > 0;
	}

	// Folder with "blocks" and "items" of the selected pack, "" when none
	std::wstring texturesRoot()
	{
		const std::wstring sel = GetSelected();
		if (sel.empty()) return L"";
		const std::wstring folder = Folder() + L"\\" + sel;
		if (dirExists(folder))
		{
			std::wstring t = folder + L"\\assets\\minecraft\\textures";
			if (dirExists(t)) return t;
			// pack wrapped in an inner folder
			WIN32_FIND_DATAW fd;
			HANDLE h = FindFirstFileW((folder + L"\\*").c_str(), &fd);
			if (h != INVALID_HANDLE_VALUE)
			{
				do
				{
					if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.')
					{
						t = folder + L"\\" + fd.cFileName + L"\\assets\\minecraft\\textures";
						if (dirExists(t)) { FindClose(h); return t; }
					}
				} while (FindNextFileW(h, &fd));
				FindClose(h);
			}
			return L"";
		}
		const std::wstring cache = Folder() + L"\\.cache\\" + sel;
		return dirExists(cache) ? cache : L"";
	}

	// 4J icon names that differ from the Java 1.8 file names
	const std::unordered_map<std::wstring, std::wstring> &blockNames()
	{
		static const std::unordered_map<std::wstring, std::wstring> m = {
			{ L"stoneslab_side", L"stone_slab_side" }, { L"stoneslab_top", L"stone_slab_top" }, { L"sapling", L"sapling_oak" },
			{ L"fire_0", L"fire_layer_0" }, { L"fire_1", L"fire_layer_1" }, { L"workbench_top", L"crafting_table_top" },
			{ L"workbench_side", L"crafting_table_side" }, { L"workbench_front", L"crafting_table_front" },
			{ L"furnace_front", L"furnace_front_off" }, { L"furnace_front_lit", L"furnace_front_on" },
			{ L"dispenser_front", L"dispenser_front_horizontal" }, { L"leaves", L"leaves_oak" }, { L"snow_side", L"grass_side_snowed" },
			{ L"mycel_side", L"mycelium_side" }, { L"mycel_top", L"mycelium_top" }, { L"torch_on", L"torch_on" },
			{ L"piston_top", L"piston_top_normal" }, { L"piston_inner_top", L"piston_inner" },
			{ L"stem_straight", L"pumpkin_stem_disconnected" }, { L"stem_bent", L"pumpkin_stem_connected" },
			{ L"enchantment_top", L"enchanting_table_top" }, { L"enchantment_side", L"enchanting_table_side" },
			{ L"enchantment_bottom", L"enchanting_table_bottom" }, { L"cocoa_0", L"cocoa_stage_0" }, { L"cocoa_1", L"cocoa_stage_1" },
			{ L"cocoa_2", L"cocoa_stage_2" }, { L"itemframe_back", L"itemframe_background" }, { L"sandstone_side", L"sandstone_normal" },
			{ L"detectorRail", L"rail_detector" }, { L"detectorRail_on", L"rail_detector_powered" },
			{ L"redstoneLight", L"redstone_lamp_off" }, { L"redstoneLight_lit", L"redstone_lamp_on" }, { L"anvil_top", L"anvil_top_damaged_0" },
			{ L"log_dark", L"log_big_oak" }, { L"log_dark_top", L"log_big_oak_top" }, { L"planks_dark", L"planks_big_oak" },
			{ L"leaves_dark_oak", L"leaves_big_oak" }, { L"sapling_dark_oak", L"sapling_roofed_oak" },
			{ L"door_dark_upper", L"door_dark_oak_upper" }, { L"door_dark_lower", L"door_dark_oak_lower" },
			{ L"prismarine", L"prismarine_rough" }, { L"packed_ice", L"ice_packed" },
			{ L"inverted_daylight_detector", L"daylight_detector_inverted_top" },
			{ L"tallgrass2_tall_grass_lower", L"double_plant_grass_bottom" }, { L"tallgrass2_tall_grass_upper", L"double_plant_grass_top" },
			{ L"tallgrass2_large_fern_lower", L"double_plant_fern_bottom" }, { L"tallgrass2_large_fern_upper", L"double_plant_fern_top" },
			{ L"tallgrass2_peony_lower", L"double_plant_paeonia_bottom" }, { L"tallgrass2_peony_upper", L"double_plant_paeonia_top" },
			{ L"tallgrass2_rose_bush_lower", L"double_plant_rose_bottom" }, { L"tallgrass2_rose_bush_upper", L"double_plant_rose_top" },
			{ L"tallgrass2_lilac_lower", L"double_plant_syringa_bottom" }, { L"tallgrass2_lilac_upper", L"double_plant_syringa_top" },
		};
		return m;
	}

	std::wstring itemName(const std::wstring &n)
	{
		static const std::unordered_map<std::wstring, std::wstring> m = {
			{ L"flintAndSteel", L"flint_and_steel" }, { L"seeds", L"seeds_wheat" }, { L"appleGold", L"apple_golden" },
			{ L"slot_empty_helmet", L"empty_armor_slot_helmet" }, { L"slot_empty_chestplate", L"empty_armor_slot_chestplate" },
			{ L"slot_empty_leggings", L"empty_armor_slot_leggings" }, { L"slot_empty_boots", L"empty_armor_slot_boots" },
			{ L"bow", L"bow_standby" }, { L"bow_pull_0", L"bow_pulling_0" }, { L"bow_pull_1", L"bow_pulling_1" }, { L"bow_pull_2", L"bow_pulling_2" },
			{ L"ingotIron", L"iron_ingot" }, { L"ingotGold", L"gold_ingot" }, { L"sulphur", L"gunpowder" },
			{ L"doorWood", L"door_wood" }, { L"doorIron", L"door_iron" }, { L"redstone", L"redstone_dust" }, { L"clay", L"clay_ball" },
			{ L"book", L"book_normal" }, { L"map", L"map_filled" }, { L"fishingRod_uncast", L"fishing_rod_uncast" },
			{ L"fishingRod_cast", L"fishing_rod_cast" }, { L"mushroomStew", L"mushroom_stew" }, { L"yellowDust", L"glowstone_dust" },
			{ L"bucket", L"bucket_empty" }, { L"bucketWater", L"bucket_water" }, { L"bucketLava", L"bucket_lava" }, { L"milk", L"bucket_milk" },
			{ L"diode", L"repeater" }, { L"porkchopRaw", L"porkchop_raw" }, { L"porkchopCooked", L"porkchop_cooked" },
			{ L"fishRaw", L"fish_cod_raw" }, { L"fishCooked", L"fish_cod_cooked" }, { L"rottenFlesh", L"rotten_flesh" },
			{ L"carrotOnAStick", L"carrot_on_a_stick" }, { L"beefRaw", L"beef_raw" }, { L"beefCooked", L"beef_cooked" },
			{ L"enderPearl", L"ender_pearl" }, { L"blazeRod", L"blaze_rod" }, { L"potatoBaked", L"potato_baked" }, { L"carrots", L"carrot" },
			{ L"chickenRaw", L"chicken_raw" }, { L"chickenCooked", L"chicken_cooked" }, { L"ghastTear", L"ghast_tear" },
			{ L"goldNugget", L"gold_nugget" }, { L"netherStalkSeeds", L"nether_wart" }, { L"potatoPoisonous", L"potato_poisonous" },
			{ L"minecart", L"minecart_normal" }, { L"speckledMelon", L"melon_speckled" }, { L"fermentedSpiderEye", L"spider_eye_fermented" },
			{ L"spiderEye", L"spider_eye" }, { L"potion", L"potion_bottle_drinkable" }, { L"glassBottle", L"potion_bottle_empty" },
			{ L"potion_contents", L"potion_overlay" }, { L"carrotGolden", L"carrot_golden" }, { L"pumpkinPie", L"pumpkin_pie" },
			{ L"monsterPlacer", L"spawn_egg" }, { L"monsterPlacer_overlay", L"spawn_egg_overlay" }, { L"potion_splash", L"potion_bottle_splash" },
			{ L"eyeOfEnder", L"ender_eye" }, { L"blazePowder", L"blaze_powder" }, { L"clownfish", L"fish_clownfish_raw" },
			{ L"expBottle", L"experience_bottle" }, { L"brewingStand", L"brewing_stand" }, { L"magmaCream", L"magma_cream" },
			{ L"muttonRaw", L"mutton_raw" }, { L"rabbitRaw", L"rabbit_raw" }, { L"pufferfish", L"fish_pufferfish_raw" },
			{ L"writingBook", L"book_writable" }, { L"writtenBook", L"book_written" }, { L"flowerPot", L"flower_pot" },
			{ L"muttonCooked", L"mutton_cooked" }, { L"rabbitCooked", L"rabbit_cooked" }, { L"salmonRaw", L"fish_salmon_raw" },
			{ L"armorStand", L"wooden_armorstand" }, { L"netherquartz", L"quartz" }, { L"frame", L"item_frame" },
			{ L"enchantedBook", L"book_enchanted" }, { L"doorAcacia", L"door_acacia" }, { L"doorBirch", L"door_birch" },
			{ L"doorDark", L"door_dark_oak" }, { L"doorJungle", L"door_jungle" }, { L"doorSpruce", L"door_spruce" },
			{ L"rabbitStew", L"rabbit_stew" }, { L"salmonCooked", L"fish_salmon_cooked" }, { L"rabbitsFoot", L"rabbit_foot" },
			{ L"rabbitHide", L"rabbit_hide" }, { L"prismarineCrystal", L"prismarine_crystals" }, { L"prismarineShard", L"prismarine_shard" },
		};
		auto it = m.find(n);
		if (it != m.end()) return it->second;

		// armour and tools: helmetIron -> iron_helmet, swordWood -> wood_sword, hatchetGold -> gold_axe, ...
		static const wchar_t *kinds[][2] = {
			{ L"helmet", L"helmet" }, { L"chestplate", L"chestplate" }, { L"leggings", L"leggings" }, { L"boots", L"boots" },
			{ L"sword", L"sword" }, { L"shovel", L"shovel" }, { L"pickaxe", L"pickaxe" }, { L"hatchet", L"axe" }, { L"hoe", L"hoe" } };
		static const wchar_t *mats[][2] = {
			{ L"Cloth", L"leather" }, { L"Chain", L"chainmail" }, { L"Iron", L"iron" }, { L"Diamond", L"diamond" }, { L"Gold", L"gold" },
			{ L"Wood", L"wood" }, { L"Stone", L"stone" } };
		for (auto &k : kinds)
		{
			const size_t kl = wcslen(k[0]);
			if (n.compare(0, kl, k[0]) != 0) continue;
			for (auto &mt : mats)
			{
				const size_t ml = wcslen(mt[0]);
				if (n.compare(kl, ml, mt[0]) != 0) continue;
				std::wstring rest = n.substr(kl + ml);	// "" or "_overlay"
				return std::wstring(mt[1]) + L"_" + k[1] + rest;
			}
		}
		// dyePowder_red -> dye_powder_red
		if (n.compare(0, 10, L"dyePowder_") == 0) return L"dye_powder_" + n.substr(10);
		return n;
	}

	bool skipIcon(const std::wstring &n)
	{
		// animated or engine-generated textures keep the built-in version
		return n == L"water" || n == L"lava" || n == L"water_flow" || n == L"lava_flow" || n == L"portal" ||
			n.compare(0, 8, L"compassP") == 0 || n.compare(0, 6, L"clockP") == 0 || n == L"leaves_opaque" ||
			n.find(L"_opaque") != std::wstring::npos;
	}

	// ---------------------------------------------------------------------------------
	// Dialog
	// ---------------------------------------------------------------------------------
	unsigned int dynString(int slot, const std::wstring &text)
	{
		static int ids[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
		if (ids[slot] < 0) ids[slot] = ItemRegistry::nextItemNameId();
		ItemRegistry::langList[ids[slot]] = text;
		if (app.getStringTable() != nullptr) app.getStringTable()->registerString(ids[slot], text);
		return static_cast<unsigned int>(ids[slot]);
	}

	int g_menuPad = 0;
	std::wstring g_pending;		// pack chosen in the dialog, applied on "Continuar"

	void showDialog();

	int dialogReturned(void *param, int iPad, const C4JStorage::EMessageResult result)
	{
		switch (result)
		{
		case C4JStorage::EMessage_ResultAccept:		// next pack
			{
				std::vector<std::wstring> packs = List();
				packs.insert(packs.begin(), L"");
				size_t idx = 0;
				for (size_t i = 0; i < packs.size(); i++) if (packs[i] == g_pending) idx = i;
				g_pending = packs[(idx + 1) % packs.size()];
				showDialog();
			}
			break;
		case C4JStorage::EMessage_ResultDecline:	// open folder
			OpenFolder();
			showDialog();
			break;
		case C4JStorage::EMessage_ResultThirdOption:	// continue = apply
			if (g_pending != GetSelected())
			{
				SetSelected(g_pending);
				Minecraft *mc = Minecraft::GetInstance();
				if (mc && mc->textures)
				{
					mc->textures->stitch();
					if (mc->levelRenderer) mc->levelRenderer->allChanged();
				}
			}
			break;
		default:	// exit without changes
			break;
		}
		return 0;
	}

	void showDialog()
	{
		std::vector<std::wstring> packs = List();
		std::wstring body = L"Pack escolhido: " + (g_pending.empty() ? std::wstring(L"Padr\u00E3o (texturas do jogo)") : g_pending) + L"\n\n";
		body += L"Packs encontrados: " + std::to_wstring(packs.size()) + L"\n";
		body += L"Coloque packs do Java 1.8 (.zip ou pasta) na pasta resourcepacks e use \"Pr\u00F3ximo pack\" para escolher. \"Continuar\" aplica as texturas.";
		UINT options[4];
		options[0] = dynString(2, L"Pr\u00F3ximo pack");
		options[1] = dynString(3, L"Abrir pasta de texturas");
		options[2] = dynString(4, L"Continuar");
		options[3] = dynString(5, L"Sair");
		ui.RequestAlertMessage(dynString(0, L"Texturas (Java 1.8)"), dynString(1, body), options, 4, g_menuPad, &dialogReturned, nullptr, nullptr);
	}
}

std::wstring Folder()
{
	const std::wstring f = exeDir() + L"\\resourcepacks";
	if (!dirExists(f)) CreateDirectoryW(f.c_str(), nullptr);
	return f;
}

std::vector<std::wstring> List()
{
	std::vector<std::wstring> out;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((Folder() + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	do
	{
		const std::wstring n = fd.cFileName;
		if (n.empty() || n[0] == L'.') continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) out.push_back(n);
		else if (n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".zip") == 0) out.push_back(n);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	std::sort(out.begin(), out.end());
	return out;
}

std::wstring GetSelected()
{
	std::lock_guard<std::mutex> guard(g_lock);
	if (!g_selectedLoaded)
	{
		g_selectedLoaded = true;
		wchar_t buf[512] = {};
		const std::wstring cfg = exeDir() + L"\\hellish-resourcepack.cfg";
		FILE *f = nullptr;
		if (_wfopen_s(&f, cfg.c_str(), L"r, ccs=UTF-8") == 0 && f)
		{
			if (fgetws(buf, 511, f))
			{
				std::wstring s = buf;
				while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
				g_selected = s;
			}
			fclose(f);
		}
		// the pack was deleted: back to the default textures
		if (!g_selected.empty() && !dirExists(exeDir() + L"\\resourcepacks\\" + g_selected) && !fileExists(exeDir() + L"\\resourcepacks\\" + g_selected))
			g_selected.clear();
	}
	return g_selected;
}

void SetSelected(const std::wstring &name)
{
	GetSelected();
	{
		std::lock_guard<std::mutex> guard(g_lock);
		g_selected = name;
	}
	const std::wstring cfg = exeDir() + L"\\hellish-resourcepack.cfg";
	FILE *f = nullptr;
	if (_wfopen_s(&f, cfg.c_str(), L"w, ccs=UTF-8") == 0 && f)
	{
		fputws(name.c_str(), f);
		fclose(f);
	}
	// zip packs are unpacked once into resourcepacks\.cache\<name>
	if (!name.empty() && fileExists(Folder() + L"\\" + name))
	{
		const std::wstring cache = Folder() + L"\\.cache\\" + name;
		if (!dirExists(cache))
		{
			makeDirs(cache);
			SetFileAttributesW((Folder() + L"\\.cache").c_str(), FILE_ATTRIBUTE_HIDDEN);
			extractZipTextures(Folder() + L"\\" + name, cache);
		}
	}
}

void OpenFolder()
{
	ShellExecuteW(nullptr, L"open", Folder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool LoadIcon(bool terrain, const std::wstring &iconName, int cellPx, std::vector<int> &pixels)
{
	if (cellPx <= 0 || skipIcon(iconName)) return false;
	const std::wstring root = texturesRoot();
	if (root.empty()) return false;

	std::wstring file;
	if (terrain)
	{
		auto it = blockNames().find(iconName);
		file = root + L"\\blocks\\" + (it != blockNames().end() ? it->second : iconName) + L".png";
	}
	else
	{
		file = root + L"\\items\\" + itemName(iconName) + L".png";
	}
	if (!fileExists(file)) return false;

	D3DXIMAGE_INFO info;
	ZeroMemory(&info, sizeof(info));
	int *data = nullptr;
	if (RenderManager.LoadTextureData(toNarrow(file).c_str(), &info, &data) != ERROR_SUCCESS || data == nullptr) return false;
	const int w = static_cast<int>(info.Width);
	const int h = static_cast<int>(info.Height);
	if (w <= 0 || h < w)
	{
		delete[] data;
		return false;
	}

	// frame 0 of animated strips is the top w x w square; scale it to the atlas cell
	pixels.assign(cellPx * cellPx, 0);
	for (int y = 0; y < cellPx; y++)
	{
		for (int x = 0; x < cellPx; x++)
		{
			if (w <= cellPx)
			{
				pixels[y * cellPx + x] = data[(y * w / cellPx) * w + (x * w / cellPx)];
				continue;
			}
			// box filter, alpha-weighted so transparent edges stay clean
			const int x0 = x * w / cellPx, x1 = (std::max)(x0 + 1, (x + 1) * w / cellPx);
			const int y0 = y * w / cellPx, y1 = (std::max)(y0 + 1, (y + 1) * w / cellPx);
			unsigned long long a = 0, r = 0, g = 0, b = 0, n = 0;
			for (int yy = y0; yy < y1; yy++)
			{
				for (int xx = x0; xx < x1; xx++)
				{
					const unsigned int p = static_cast<unsigned int>(data[yy * w + xx]);
					const unsigned int pa = p >> 24;
					a += pa; r += ((p >> 16) & 255) * pa; g += ((p >> 8) & 255) * pa; b += (p & 255) * pa; n++;
				}
			}
			unsigned int out = 0;
			if (a > 0)
			{
				const unsigned int ca = static_cast<unsigned int>(a / n);
				out = (ca << 24) | (static_cast<unsigned int>(r / a) << 16) | (static_cast<unsigned int>(g / a) << 8) | static_cast<unsigned int>(b / a);
			}
			pixels[y * cellPx + x] = static_cast<int>(out);
		}
	}
	delete[] data;
	return true;
}

void ShowMenu(int iPad)
{
	g_menuPad = iPad;
	g_pending = GetSelected();
	showDialog();
}
}
