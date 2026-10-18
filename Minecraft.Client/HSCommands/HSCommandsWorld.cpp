// Hellish Sight - world commands (blocks, time, weather, rules, structures, effects).

#include "stdafx.h"
#include "HSCommandsInternal.h"

#include "../MinecraftServer.h"
#include "../ServerLevel.h"
#include "../ServerPlayer.h"
#include "../PlayerList.h"
#include "../PlayerConnection.h"
#include "../Common/Audio/Consoles_SoundEngine.h"
#include "../../Minecraft.World/net.minecraft.network.packet.h"
#include "../../Minecraft.World/net.minecraft.world.level.h"
#include "../../Minecraft.World/net.minecraft.world.level.chunk.h"
#include "../../Minecraft.World/net.minecraft.world.level.dimension.h"
#include "../../Minecraft.World/net.minecraft.world.level.storage.h"
#include "../../Minecraft.World/net.minecraft.world.level.tile.h"
#include "../../Minecraft.World/net.minecraft.world.level.tile.entity.h"
#include "../../Minecraft.World/net.minecraft.world.level.biome.h"
#include "../../Minecraft.World/net.minecraft.world.entity.h"
#include "../../Minecraft.World/LevelParticlesPacket.h"
#include "../../Minecraft.World/StringHelpers.h"
#include "../../Minecraft.World/LargeFeature.h"
#include "../../Minecraft.World/TilePos.h"
#include "../../Minecraft.World/Mth.h"
#include "../../Minecraft.World/LevelData.h"
#include "../../Minecraft.World/TreeFeature.h"
#include "../../Minecraft.World/BirchFeature.h"
#include "../../Minecraft.World/SpruceFeature.h"
#include "../../Minecraft.World/SwampTreeFeature.h"
#include "../../Minecraft.World/MegaTreeFeature.h"
#include "../../Minecraft.World/SavannaTreeFeature.h"
#include "../../Minecraft.World/RoofTreeFeature.h"
#include "../../Minecraft.World/HugeMushroomFeature.h"
#include "../../Minecraft.World/MegaPineTreeFeature.h"
#include "../../Minecraft.World/BasicTree.h"
#include "../../Minecraft.World/IceSpikeFeature.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace HSCommands
{
	namespace
	{
		const CommandInfo *self(const wchar_t *name) { return findCommand(name); }

		void syncHostOptions(Source &src)
		{
			if (src.server && src.server->getPlayers())
				src.server->getPlayers()->broadcastAll(std::make_shared<ServerSettingsChangedPacket>(ServerSettingsChangedPacket::HOST_IN_GAME_SETTINGS, app.GetGameHostOption(eGameHostOption_All)));
		}

		std::wstring posText(int x, int y, int z)
		{
			return std::to_wstring(x) + L", " + std::to_wstring(y) + L", " + std::to_wstring(z);
		}

		bool inWorld(int y) { return y >= 0 && y < Level::maxBuildHeight; }

		const long long MAX_FILL = 32768LL * 4;	// Java limit is 32768; bigger here for infinite worlds

		struct Box { int x0, y0, z0, x1, y1, z1; long long volume() const { return (long long)(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1); } };

		Box makeBox(int ax, int ay, int az, int bx, int by, int bz)
		{
			Box b;
			b.x0 = (std::min)(ax, bx); b.x1 = (std::max)(ax, bx);
			b.y0 = (std::max)(0, (std::min)(ay, by)); b.y1 = (std::min)(Level::maxBuildHeight - 1, (std::max)(ay, by));
			b.z0 = (std::min)(az, bz); b.z1 = (std::max)(az, bz);
			return b;
		}

		bool setBlock(ServerLevel *level, int x, int y, int z, int id, int data, bool destroy)
		{
			if (!inWorld(y)) return false;
			if (destroy && level->getTile(x, y, z) != 0) level->destroyTile(x, y, z, true);
			if (level->getTileEntity(x, y, z) != nullptr) level->removeTileEntity(x, y, z);
			return level->setTileAndData(x, y, z, id, data, Tile::UPDATE_ALL);
		}
	}

	// ---------------------------------------------------------------------------------
	// /setblock <pos> <block> [destroy|keep|replace]
	// ---------------------------------------------------------------------------------
	int cmdSetblock(Source &src, const Args &a)
	{
		if (a.size() < 4 || !src.level) return usage(src, self(L"setblock"));
		int x, y, z, id, data;
		if (!parseBlockPos(src, a, 0, x, y, z)) return fail(src, L"Coordenadas inválidas.");
		if (!resolveBlock(a[3], id, data)) return fail(src, L"Bloco desconhecido: " + a[3]);
		size_t mi = 4;
		int d; if (a.has(4) && parseInt(a[4], d)) { data = d; mi = 5; }	// 1.12 data value
		const std::wstring mode = a.has(mi) ? toLower(a[mi]) : L"replace";
		if (!inWorld(y)) return fail(src, L"Fora do mundo.");
		if (mode == L"keep" && src.level->getTile(x, y, z) != 0) return fail(src, L"Não foi possível colocar o bloco.");
		if (src.level->getTile(x, y, z) == id && src.level->getData(x, y, z) == data) return fail(src, L"Nada mudou.");
		if (!setBlock(src.level, x, y, z, id, data, mode == L"destroy")) return fail(src, L"Não foi possível colocar o bloco.");
		feedback(src, L"Bloco alterado em " + posText(x, y, z));
		return 1;
	}

	// ---------------------------------------------------------------------------------
	// /fill <from> <to> <block> [replace [filter]|destroy|keep|hollow|outline]
	// ---------------------------------------------------------------------------------
	int cmdFill(Source &src, const Args &a)
	{
		if (a.size() < 7 || !src.level) return usage(src, self(L"fill"));
		int ax, ay, az, bx, by, bz, id, data;
		if (!parseBlockPos(src, a, 0, ax, ay, az) || !parseBlockPos(src, a, 3, bx, by, bz)) return fail(src, L"Coordenadas inválidas.");
		if (!resolveBlock(a[6], id, data)) return fail(src, L"Bloco desconhecido: " + a[6]);
		size_t mi = 7;
		int d; if (a.has(7) && parseInt(a[7], d)) { data = d; mi = 8; }
		const std::wstring mode = a.has(mi) ? toLower(a[mi]) : L"replace";
		int fid = -1, fdata = -1;
		if (mode == L"replace" && a.has(mi + 1))
		{
			if (!resolveBlock(a[mi + 1], fid, fdata)) return fail(src, L"Bloco desconhecido: " + a[mi + 1]);
			if (a[mi + 1].find(L'[') == std::wstring::npos && a[mi + 1].find(L':') == std::wstring::npos) fdata = -1;
			int fd; if (a.has(mi + 2) && parseInt(a[mi + 2], fd)) fdata = fd;
		}
		Box b = makeBox(ax, ay, az, bx, by, bz);
		if (b.volume() > MAX_FILL) return fail(src, L"Área grande demais: " + std::to_wstring(b.volume()) + L" blocos (máximo " + std::to_wstring(MAX_FILL) + L")");
		int changed = 0;
		for (int y = b.y0; y <= b.y1; y++)
		for (int x = b.x0; x <= b.x1; x++)
		for (int z = b.z0; z <= b.z1; z++)
		{
			const bool edge = x == b.x0 || x == b.x1 || y == b.y0 || y == b.y1 || z == b.z0 || z == b.z1;
			int tid = id, tdata = data;
			if (mode == L"hollow" && !edge) { tid = 0; tdata = 0; }
			if (mode == L"outline" && !edge) continue;
			const int cur = src.level->getTile(x, y, z);
			if (mode == L"keep" && cur != 0) continue;
			if (fid >= 0 && (cur != fid || (fdata >= 0 && src.level->getData(x, y, z) != fdata))) continue;
			if (cur == tid && src.level->getData(x, y, z) == tdata) continue;
			if (setBlock(src.level, x, y, z, tid, tdata, mode == L"destroy")) changed++;
		}
		if (changed == 0) return fail(src, L"Nenhum bloco foi preenchido.");
		feedback(src, L"Preenchidos " + std::to_wstring(changed) + L" blocos");
		return changed;
	}

	// ---------------------------------------------------------------------------------
	// /clone <begin> <end> <destination> [replace|masked] [normal|force|move]
	// ---------------------------------------------------------------------------------
	int cmdClone(Source &src, const Args &a)
	{
		if (a.size() < 9 || !src.level) return usage(src, self(L"clone"));
		int ax, ay, az, bx, by, bz, dx, dy, dz;
		if (!parseBlockPos(src, a, 0, ax, ay, az) || !parseBlockPos(src, a, 3, bx, by, bz) || !parseBlockPos(src, a, 6, dx, dy, dz)) return fail(src, L"Coordenadas inválidas.");
		const std::wstring mask = a.has(9) ? toLower(a[9]) : L"replace";
		const std::wstring mode = a.has(10) ? toLower(a[10]) : L"normal";
		Box b = makeBox(ax, ay, az, bx, by, bz);
		if (b.volume() > MAX_FILL) return fail(src, L"Área grande demais.");
		struct Cell { int id, data; std::shared_ptr<TileEntity> te; };
		std::vector<Cell> cells;
		cells.reserve(static_cast<size_t>(b.volume()));
		for (int y = b.y0; y <= b.y1; y++)
		for (int x = b.x0; x <= b.x1; x++)
		for (int z = b.z0; z <= b.z1; z++)
		{
			Cell c;
			c.id = src.level->getTile(x, y, z);
			c.data = src.level->getData(x, y, z);
			std::shared_ptr<TileEntity> te = src.level->getTileEntity(x, y, z);
			c.te = te ? te->clone() : nullptr;
			cells.push_back(c);
		}
		if (mode == L"move")
		{
			for (int y = b.y0; y <= b.y1; y++)
			for (int x = b.x0; x <= b.x1; x++)
			for (int z = b.z0; z <= b.z1; z++)
				setBlock(src.level, x, y, z, 0, 0, false);
		}
		int changed = 0;
		size_t i = 0;
		for (int y = b.y0; y <= b.y1; y++)
		for (int x = b.x0; x <= b.x1; x++)
		for (int z = b.z0; z <= b.z1; z++, i++)
		{
			const Cell &c = cells[i];
			if (mask == L"masked" && c.id == 0) continue;
			const int tx = dx + (x - b.x0), ty = dy + (y - b.y0), tz = dz + (z - b.z0);
			if (!inWorld(ty)) continue;
			if (setBlock(src.level, tx, ty, tz, c.id, c.data, false)) changed++;
			if (c.te)
			{
				c.te->x = tx; c.te->y = ty; c.te->z = tz;
				src.level->setTileEntity(tx, ty, tz, c.te);
			}
		}
		if (changed == 0) return fail(src, L"Nenhum bloco foi clonado.");
		feedback(src, L"Clonados " + std::to_wstring(changed) + L" blocos");
		return changed;
	}

	// ---------------------------------------------------------------------------------
	// /time set|add|query
	// ---------------------------------------------------------------------------------
	int cmdTime(Source &src, const Args &a)
	{
		if (a.size() < 2 || !src.server) return usage(src, self(L"time"));
		const std::wstring op = toLower(a[0]);
		ServerLevel *ov = src.server->getLevel(0);
		if (!ov) return 0;
		if (op == L"query")
		{
			const std::wstring q = toLower(a[1]);
			long long v;
			if (q == L"daytime") v = ov->getDayTime() % 24000;
			else if (q == L"gametime") v = ov->getGameTime();
			else if (q == L"day") v = ov->getDayTime() / 24000;
			else return usage(src, self(L"time"));
			feedback(src, L"O tempo é " + std::to_wstring(v));
			return static_cast<int>(v % 2147483647LL);
		}
		long long t = 0;
		std::wstring v = toLower(a[1]);
		if (v == L"day") t = 1000; else if (v == L"noon") t = 6000; else if (v == L"sunset") t = 12000;
		else if (v == L"night") t = 13000; else if (v == L"midnight") t = 18000; else if (v == L"sunrise") t = 23000;
		else
		{
			double mul = 1;
			if (!v.empty() && (v.back() == L'd' || v.back() == L's' || v.back() == L't'))
			{
				mul = v.back() == L'd' ? 24000 : (v.back() == L's' ? 20 : 1);
				v.pop_back();
			}
			double n;
			if (!parseDouble(v, n)) return fail(src, L"Tempo inválido: " + a[1]);
			t = static_cast<long long>(n * mul);
		}
		for (unsigned int i = 0; i < src.server->levels.length; i++)
		{
			ServerLevel *l = src.server->levels[i];
			if (!l) continue;
			if (op == L"set") l->setDayTime(t);
			else if (op == L"add") l->setDayTime(l->getDayTime() + t);
		}
		if (op != L"set" && op != L"add") return usage(src, self(L"time"));
		feedback(src, L"Tempo definido para " + std::to_wstring(ov->getDayTime() % 24000));
		return static_cast<int>(ov->getDayTime() % 24000);
	}

	// ---------------------------------------------------------------------------------
	// /weather clear|rain|thunder [duration]   /toggledownfall
	// ---------------------------------------------------------------------------------
	int cmdWeather(Source &src, const Args &a)
	{
		if (a.size() < 1 || !src.server) return usage(src, self(L"weather"));
		const std::wstring w = toLower(a[0]);
		int seconds = 300 + (src.level ? src.level->random->nextInt(600) : 0);
		if (a.has(1))
		{
			std::wstring d = a[1];
			int mul = 1;
			if (!d.empty() && d.back() == L't') { mul = 0; d.pop_back(); }
			else if (!d.empty() && d.back() == L's') d.pop_back();
			else if (!d.empty() && d.back() == L'd') { mul = 1200; d.pop_back(); }
			int n;
			if (!parseInt(d, n) || n < 0) return fail(src, L"Duração inválida.");
			seconds = mul == 0 ? n / 20 : n * mul;
		}
		const int ticks = (std::max)(1, seconds * 20);
		ServerLevel *ov = src.server->getLevel(0);
		if (!ov) return 0;
		LevelData *ld = ov->getLevelData();
		if (w == L"clear") { ld->setRaining(false); ld->setThundering(false); ld->setRainTime(ticks); ld->setThunderTime(ticks); }
		else if (w == L"rain") { ld->setRaining(true); ld->setThundering(false); ld->setRainTime(ticks); }
		else if (w == L"thunder") { ld->setRaining(true); ld->setThundering(true); ld->setRainTime(ticks); ld->setThunderTime(ticks); }
		else return usage(src, self(L"weather"));
		feedback(src, L"Clima alterado para " + w);
		return 1;
	}

	int cmdToggleDownfall(Source &src, const Args &a)
	{
		(void)a;
		ServerLevel *ov = src.server ? src.server->getLevel(0) : nullptr;
		if (!ov) return 0;
		LevelData *ld = ov->getLevelData();
		ld->setRaining(!ld->isRaining());
		feedback(src, L"Chuva alternada");
		return 1;
	}

	// ---------------------------------------------------------------------------------
	// /difficulty [peaceful|easy|normal|hard]
	// ---------------------------------------------------------------------------------
	int cmdDifficulty(Source &src, const Args &a)
	{
		static const wchar_t *names[4] = { L"Pacífico", L"Fácil", L"Normal", L"Difícil" };
		const int cur = app.GetGameHostOption(eGameHostOption_Difficulty);
		if (a.size() < 1) { feedback(src, std::wstring(L"Dificuldade: ") + names[(std::min)(3, (std::max)(0, cur))]); return cur; }
		const std::wstring v = toLower(a[0]);
		int d = -1;
		if (v == L"peaceful" || v == L"p" || v == L"0" || v == L"pacifico" || v == L"pacífico") d = 0;
		else if (v == L"easy" || v == L"e" || v == L"1" || v == L"facil" || v == L"fácil") d = 1;
		else if (v == L"normal" || v == L"n" || v == L"2") d = 2;
		else if (v == L"hard" || v == L"h" || v == L"3" || v == L"dificil" || v == L"difícil") d = 3;
		if (d < 0) return fail(src, L"Dificuldade desconhecida: " + a[0]);
		app.SetGameHostOption(eGameHostOption_Difficulty, d);
		if (src.server)
			for (unsigned int i = 0; i < src.server->levels.length; i++)
				if (src.server->levels[i]) src.server->levels[i]->difficulty = d;
		syncHostOptions(src);
		feedback(src, std::wstring(L"Dificuldade definida para ") + names[d]);
		return 1;
	}

	// ---------------------------------------------------------------------------------
	// /gamerule <rule> [value]
	// ---------------------------------------------------------------------------------
	int cmdGamerule(Source &src, const Args &a)
	{
		struct Rule { const wchar_t *name; eGameHostOption opt; };
		static const Rule rules[] = {
			{ L"doDaylightCycle", eGameHostOption_DoDaylightCycle }, { L"keepInventory", eGameHostOption_KeepInventory },
			{ L"mobGriefing", eGameHostOption_MobGriefing }, { L"doMobSpawning", eGameHostOption_DoMobSpawning },
			{ L"doMobLoot", eGameHostOption_DoMobLoot }, { L"doTileDrops", eGameHostOption_DoTileDrops },
			{ L"naturalRegeneration", eGameHostOption_NaturalRegeneration }, { L"doFireTick", eGameHostOption_FireSpreads },
			{ L"tntExplodes", eGameHostOption_TNT }, { L"pvp", eGameHostOption_PvP },
		};
		if (a.size() < 1)
		{
			std::wstring s;
			for (const Rule &r : rules) { if (!s.empty()) s += L", "; s += r.name; }
			feedback(src, L"Regras: " + s + L", commandBlockOutput, sendCommandFeedback");
			return 1;
		}
		const std::wstring n = toLower(a[0]);
		static bool commandBlockOutput = true;
		if (n == L"commandblockoutput" || n == L"sendcommandfeedback")
		{
			bool v;
			if (!a.has(1)) { feedback(src, a[0] + L" = " + (commandBlockOutput ? L"true" : L"false")); return 1; }
			if (!parseBool(a[1], v)) return fail(src, L"Valor inválido: " + a[1]);
			commandBlockOutput = v;
			feedback(src, a[0] + L" = " + a[1]);
			return 1;
		}
		for (const Rule &r : rules)
		{
			if (toLower(r.name) != n) continue;
			if (!a.has(1))
			{
				const bool v = app.GetGameHostOption(r.opt) != 0;
				feedback(src, std::wstring(r.name) + L" = " + (v ? L"true" : L"false"));
				return v ? 1 : 0;
			}
			bool v;
			if (!parseBool(a[1], v)) return fail(src, L"Valor inválido: " + a[1]);
			app.SetGameHostOption(r.opt, v ? 1 : 0);
			syncHostOptions(src);
			feedback(src, std::wstring(L"Regra ") + r.name + L" = " + (v ? L"true" : L"false"));
			return 1;
		}
		return fail(src, L"Regra desconhecida (ou não existe neste jogo): " + a[0]);
	}

	// ---------------------------------------------------------------------------------
	// /seed /setworldspawn /locate
	// ---------------------------------------------------------------------------------
	int cmdSeed(Source &src, const Args &a)
	{
		(void)a;
		ServerLevel *l = src.level ? src.level : (src.server ? src.server->getLevel(0) : nullptr);
		if (!l) return 0;
		feedback(src, L"Semente: [" + std::to_wstring(l->getSeed()) + L"]");
		return 1;
	}

	int cmdSetWorldSpawn(Source &src, const Args &a)
	{
		if (!src.server) return 0;
		int x = Mth::floor(src.x), y = Mth::floor(src.y), z = Mth::floor(src.z);
		if (a.size() >= 3 && !parseBlockPos(src, a, 0, x, y, z)) return fail(src, L"Coordenadas inválidas.");
		ServerLevel *ov = src.server->getLevel(0);
		if (!ov) return 0;
		ov->getLevelData()->setSpawn(x, y, z);
		feedback(src, L"Ponto de nascimento do mundo: " + posText(x, y, z));
		return 1;
	}

	int cmdLocate(Source &src, const Args &a)
	{
		if (a.size() < 1 || !src.level) return usage(src, self(L"locate"));
		size_t i = 0;
		std::wstring kind = toLower(a[0]);
		if (kind == L"structure" || kind == L"biome" || kind == L"poi") { if (!a.has(1)) return usage(src, self(L"locate")); i = 1; }
		std::wstring what = toLower(stripNamespace(a[i]));
		if (kind == L"biome")
		{
			// spiral search over the biome source
			BiomeSource *bs = src.level->getBiomeSource();
			const int cx = Mth::floor(src.x), cz = Mth::floor(src.z);
			for (int r = 0; r <= 6400; r += 32)
			{
				for (int step = 0; step < (std::max)(1, r / 4); step++)
				{
					const double ang = 6.2831853 * step / (std::max)(1, r / 4);
					const int x = cx + static_cast<int>(cos(ang) * r), z = cz + static_cast<int>(sin(ang) * r);
					Biome *b = bs->getBiome(x, z);
					if (!b) continue;
					std::wstring bn = toLower(b->m_name);
					for (auto &ch : bn) if (ch == L' ') ch = L'_';
					if (bn == what || bn.find(what) != std::wstring::npos)
					{
						feedback(src, L"O bioma " + b->m_name + L" mais próximo está em " + std::to_wstring(x) + L", ~, " + std::to_wstring(z) + L" (" + std::to_wstring(r) + L" blocos)");
						return (std::max)(1, r);
					}
				}
			}
			return fail(src, L"Bioma não encontrado num raio de 6400 blocos: " + a[i]);
		}
		std::wstring feature;
		if (what == L"stronghold") feature = LargeFeature::STRONGHOLD;
		else if (what == L"village" || what.find(L"village") != std::wstring::npos) feature = L"Village";
		else if (what == L"mineshaft") feature = L"Mineshaft";
		else if (what == L"monument" || what == L"ocean_monument") feature = L"Monument";
		else if (what == L"temple" || what == L"desert_pyramid" || what == L"jungle_pyramid" || what == L"swamp_hut" || what == L"witch_hut") feature = L"Temple";
		else return fail(src, L"Estrutura desconhecida: " + a[i] + L" (stronghold, village, mineshaft, monument, temple)");
		TilePos *p = src.level->getChunkSource()->findNearestMapFeature(src.level, feature, Mth::floor(src.x), Mth::floor(src.y), Mth::floor(src.z));
		if (!p) return fail(src, L"Nenhuma estrutura desse tipo foi encontrada por perto (explore mais o mundo).");
		const double dist = sqrt((p->x - src.x) * (p->x - src.x) + (p->z - src.z) * (p->z - src.z));
		feedback(src, L"A estrutura " + what + L" mais próxima está em " + std::to_wstring(p->x) + L", ~, " + std::to_wstring(p->z) + L" (" + std::to_wstring(static_cast<int>(dist)) + L" blocos)");
		delete p;
		return (std::max)(1, static_cast<int>(dist));
	}

	// ---------------------------------------------------------------------------------
	// /particle <name> [pos] [delta] [speed] [count] [force|normal] [viewers]
	// ---------------------------------------------------------------------------------
	int cmdParticle(Source &src, const Args &a)
	{
		if (a.size() < 1 || !src.server) return usage(src, self(L"particle"));
		std::wstring name = toLower(stripNamespace(a[0]));
		struct M { const wchar_t *modern; const wchar_t *legacy; };
		static const M map[] = {
			{ L"explosion", L"largeexplode" }, { L"explosion_emitter", L"hugeexplosion" }, { L"large_smoke", L"largesmoke" },
			{ L"dust", L"reddust" }, { L"item_snowball", L"snowballpoof" }, { L"angry_villager", L"angryVillager" },
			{ L"happy_villager", L"happyVillager" }, { L"enchanted_hit", L"magicCrit" }, { L"dripping_water", L"dripWater" },
			{ L"dripping_lava", L"dripLava" }, { L"enchant", L"enchantmenttable" }, { L"portal", L"portal" }, { L"witch", L"witchMagic" },
			{ L"effect", L"spell" }, { L"entity_effect", L"mobSpell" }, { L"instant_effect", L"instantSpell" }, { L"firework", L"fireworksSpark" },
			{ L"bubble", L"bubble" }, { L"splash", L"splash" }, { L"underwater", L"suspended" }, { L"cloud", L"explode" }, { L"item_slime", L"slime" },
		};
		for (const M &m : map) if (name == m.modern) { name = m.legacy; break; }
		double x = src.x, y = src.y, z = src.z, dx = 0, dy = 0, dz = 0, speed = 0;
		int count = 1;
		if (a.has(3) && !parseVec3(src, a, 1, x, y, z, false)) return fail(src, L"Coordenadas inválidas.");
		if (a.has(6)) { parseDouble(a[4], dx); parseDouble(a[5], dy); parseDouble(a[6], dz); }
		if (a.has(7)) parseDouble(a[7], speed);
		if (a.has(8)) parseInt(a[8], count);
		count = (std::max)(0, (std::min)(count, 10000));
		std::shared_ptr<Packet> pkt = std::make_shared<LevelParticlesPacket>(name, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
			static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz), static_cast<float>(speed), count);
		const int dim = src.level && src.level->dimension ? src.level->dimension->id : 0;
		src.server->getPlayers()->broadcastAll(pkt, dim);
		feedback(src, L"Partícula " + name + L" exibida");
		return 1;
	}

	// ---------------------------------------------------------------------------------
	// /playsound <sound> [source] [targets] [pos] [volume] [pitch]   /stopsound
	// ---------------------------------------------------------------------------------
	int cmdPlaySound(Source &src, const Args &a)
	{
		if (a.size() < 1 || !src.level) return usage(src, self(L"playsound"));
		const std::wstring name = toLower(stripNamespace(a[0]));
		int sound = -1;
		for (int i = 0; i < eSoundType_MAX; i++)
		{
			if (ConsoleSoundEngine::wchSoundNames[i] && name == ConsoleSoundEngine::wchSoundNames[i]) { sound = i; break; }
		}
		if (sound < 0)
		{
			// modern names: entity.cow.ambient -> mob.cow.say etc. try a loose match on the last parts
			std::wstring tail = name;
			const size_t dot = tail.find(L'.');
			if (dot != std::wstring::npos) tail = tail.substr(dot + 1);
			for (int i = 0; i < eSoundType_MAX && sound < 0; i++)
			{
				const wchar_t *n = ConsoleSoundEngine::wchSoundNames[i];
				if (n && std::wstring(n).find(tail) != std::wstring::npos) sound = i;
			}
		}
		if (sound < 0) return fail(src, L"Som desconhecido: " + a[0] + L" (use nomes como mob.cow.say, random.explode)");
		double x = src.x, y = src.y, z = src.z;
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		std::wstring err;
		if (a.has(2) && selectPlayers(src, a[2], ps, err) && !ps.empty()) { x = ps[0]->x; y = ps[0]->y; z = ps[0]->z; }
		if (a.has(5)) parseVec3(src, a, 3, x, y, z, false);
		double vol = 1, pitch = 1;
		if (a.has(6)) parseDouble(a[6], vol);
		if (a.has(7)) parseDouble(a[7], pitch);
		src.level->playSound(x, y, z, sound, static_cast<float>(vol), static_cast<float>(pitch), static_cast<float>(16.0 * (std::max)(1.0, vol)));
		feedback(src, L"Som tocado: " + std::wstring(ConsoleSoundEngine::wchSoundNames[sound]));
		return 1;
	}

	int cmdStopSound(Source &src, const Args &a)
	{
		(void)a;
		feedback(src, L"Este jogo não permite parar sons já tocando.");
		return 1;
	}

	// ---------------------------------------------------------------------------------
	// /fillbiome <from> <to> <biome>
	// ---------------------------------------------------------------------------------
	int cmdFillBiome(Source &src, const Args &a)
	{
		if (a.size() < 7 || !src.level) return usage(src, self(L"fillbiome"));
		int ax, ay, az, bx, by, bz;
		if (!parseBlockPos(src, a, 0, ax, ay, az) || !parseBlockPos(src, a, 3, bx, by, bz)) return fail(src, L"Coordenadas inválidas.");
		std::wstring want = toLower(stripNamespace(a[6]));
		Biome *target = nullptr;
		int bid;
		if (parseInt(want, bid) && bid >= 0 && bid < 256) target = Biome::biomes[bid];
		for (int i = 0; i < 256 && !target; i++)
		{
			Biome *b = Biome::biomes[i];
			if (!b) continue;
			std::wstring bn = toLower(b->m_name);
			for (auto &ch : bn) if (ch == L' ') ch = L'_';
			if (bn == want) target = b;
		}
		if (!target) return fail(src, L"Bioma desconhecido: " + a[6]);
		Box b = makeBox(ax, 0, az, bx, 0, bz);
		int changed = 0;
		for (int cx = b.x0 >> 4; cx <= b.x1 >> 4; cx++)
		for (int cz = b.z0 >> 4; cz <= b.z1 >> 4; cz++)
		{
			LevelChunk *chunk = src.level->getChunk(cx, cz);
			if (!chunk) continue;
			byteArray biomes = chunk->getBiomes();
			if (biomes.data == nullptr || biomes.length < 256) continue;
			byteArray copy(256);
			memcpy(copy.data, biomes.data, 256);
			for (int lz = 0; lz < 16; lz++)
			for (int lx = 0; lx < 16; lx++)
			{
				const int wx = (cx << 4) + lx, wz = (cz << 4) + lz;
				if (wx < b.x0 || wx > b.x1 || wz < b.z0 || wz > b.z1) continue;
				copy[(lz << 4) | lx] = static_cast<byte>(target->id);
				changed++;
			}
			chunk->setBiomes(copy);
			chunk->markUnsaved();
		}
		feedback(src, L"Bioma alterado em " + std::to_wstring(changed) + L" colunas (as cores atualizam ao recarregar os chunks)");
		return changed;
	}

	// ---------------------------------------------------------------------------------
	// /place feature <tree> [pos]
	// ---------------------------------------------------------------------------------
	int cmdPlace(Source &src, const Args &a)
	{
		if (a.size() < 2 || !src.level) return usage(src, self(L"place"));
		if (toLower(a[0]) != L"feature") return fail(src, L"Somente /place feature <árvore> é suportado.");
		int x = Mth::floor(src.x), y = Mth::floor(src.y), z = Mth::floor(src.z);
		if (a.has(4) && !parseBlockPos(src, a, 2, x, y, z)) return fail(src, L"Coordenadas inválidas.");
		const std::wstring f = toLower(stripNamespace(a[1]));
		Feature *feature = nullptr;
		if (f == L"oak" || f == L"tree") feature = new TreeFeature(true);
		else if (f == L"fancy_oak" || f == L"big_tree") feature = new BasicTree(true);
		else if (f == L"birch") feature = new BirchFeature(true);
		else if (f == L"super_birch_bees" || f == L"tall_birch") feature = new BirchFeature(true, true);
		else if (f == L"spruce" || f == L"pine") feature = new SpruceFeature(true);
		else if (f == L"mega_spruce" || f == L"mega_pine") feature = new MegaPineTreeFeature(true, true);
		else if (f == L"jungle_tree" || f == L"jungle") feature = new TreeFeature(true, 4, TreeTile::JUNGLE_TRUNK, LeafTile::JUNGLE_LEAF, true);
		else if (f == L"mega_jungle_tree") feature = new MegaTreeFeature(true, 10, TreeTile::JUNGLE_TRUNK, LeafTile::JUNGLE_LEAF);
		else if (f == L"swamp_oak" || f == L"swamp") feature = new SwampTreeFeature();
		else if (f == L"acacia" || f == L"savanna") feature = new SavannaTreeFeature(true);
		else if (f == L"dark_oak" || f == L"roofed") feature = new RoofTreeFeature(true);
		else if (f == L"huge_brown_mushroom") feature = new HugeMushroomFeature(0);
		else if (f == L"huge_red_mushroom") feature = new HugeMushroomFeature(1);
		else if (f == L"ice_spike") feature = new IceSpikeFeature();
		else return fail(src, L"Feature desconhecida: " + a[1]);
		const bool ok = feature->place(src.level, src.level->random, x, y, z);
		delete feature;
		if (!ok) return fail(src, L"Não foi possível colocar aqui.");
		feedback(src, L"Colocado " + f + L" em " + posText(x, y, z));
		return 1;
	}

	// ---------------------------------------------------------------------------------
	// /worldborder /forceload /tick /publish (accepted, limited)
	// ---------------------------------------------------------------------------------
	int cmdWorldBorder(Source &src, const Args &a)
	{
		(void)a;
		ServerLevel *ov = src.server ? src.server->getLevel(0) : nullptr;
		const int size = ov ? ov->getLevelData()->getXZSize() * 16 : 0;
		feedback(src, L"A borda do mundo é definida pelo tamanho do mundo: " + std::to_wstring(size) + L" blocos de largura.");
		return size;
	}

	int cmdForceload(Source &src, const Args &a)
	{
		(void)a;
		feedback(src, L"Chunks perto dos jogadores já ficam carregados; /forceload não é suportado por este motor.");
		return 0;
	}

	// ---------------------------------------------------------------------------------
	// /save-all /save-off /save-on /stop
	// ---------------------------------------------------------------------------------
	int cmdSaveAll(Source &src, const Args &a)
	{
		(void)a;
		if (!src.server || !src.server->getPlayers()) return 0;
		src.server->getPlayers()->saveAll(nullptr, false);
		feedback(src, L"Mundo salvo.");
		return 1;
	}

	int cmdSaveOff(Source &src, const Args &a)
	{
		(void)a;
		if (!src.server) return 0;
		for (unsigned int i = 0; i < src.server->levels.length; i++) if (src.server->levels[i]) src.server->levels[i]->noSave = true;
		feedback(src, L"Salvamento automático desligado.");
		return 1;
	}

	int cmdSaveOn(Source &src, const Args &a)
	{
		(void)a;
		if (!src.server) return 0;
		for (unsigned int i = 0; i < src.server->levels.length; i++) if (src.server->levels[i]) src.server->levels[i]->noSave = false;
		feedback(src, L"Salvamento automático ligado.");
		return 1;
	}

	int cmdStop(Source &src, const Args &a)
	{
		(void)a;
		if (src.player && src.player->connection && src.player->connection->isLocal())
			return fail(src, L"No jogo local use o menu de pausa para sair.");
		feedback(src, L"Parando o servidor...");
		MinecraftServer::HaltServer();
		return 1;
	}
}
