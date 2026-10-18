// Hellish Sight - player / entity commands.

#include "stdafx.h"
#include "HSCommandsInternal.h"

#include "../MinecraftServer.h"
#include "../ServerLevel.h"
#include "../ServerPlayer.h"
#include "../ServerPlayerGameMode.h"
#include "../PlayerList.h"
#include "../PlayerConnection.h"
#include "../../Minecraft.World/net.minecraft.network.packet.h"
#include "../../Minecraft.World/net.minecraft.world.level.h"
#include "../../Minecraft.World/net.minecraft.world.level.dimension.h"
#include "../../Minecraft.World/net.minecraft.world.entity.h"
#include "../../Minecraft.World/net.minecraft.world.entity.item.h"
#include "../../Minecraft.World/net.minecraft.world.entity.player.h"
#include "../../Minecraft.World/net.minecraft.world.entity.ai.attributes.h"
#include "../../Minecraft.World/net.minecraft.world.entity.monster.h"
#include "../../Minecraft.World/net.minecraft.world.entity.global.h"
#include "../../Minecraft.World/net.minecraft.world.item.h"
#include "../../Minecraft.World/net.minecraft.world.item.enchantment.h"
#include "../../Minecraft.World/net.minecraft.world.effect.h"
#include "../../Minecraft.World/net.minecraft.world.damagesource.h"
#include "../../Minecraft.World/net.minecraft.world.level.tile.h"
#include "../../Minecraft.World/EntityIO.h"
#include "../../Minecraft.World/ChatPacket.h"
#include "../../Minecraft.World/StringHelpers.h"
#include "../../Minecraft.World/LevelSettings.h"
#include "../../Minecraft.World/Pos.h"
#include "../../Minecraft.World/Mth.h"
#include "../../Minecraft.World/LevelData.h"
#include "../../Minecraft.World/AbstractContainerMenu.h"
#include "../../Minecraft.World/SharedConstants.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace HSCommands
{
	namespace
	{
		const CommandInfo *self(const wchar_t *name) { return findCommand(name); }

		bool targets(Source &src, const Args &a, size_t i, std::vector<std::shared_ptr<Entity> > &out, bool playersOnly, bool defaultSelf)
		{
			std::wstring err;
			if (!a.has(i))
			{
				if (defaultSelf && src.entity && (!playersOnly || src.entity->instanceof(eTYPE_PLAYER)))
				{
					out.assign(1, src.entity);
					return true;
				}
				fail(src, L"Informe um alvo.");
				return false;
			}
			if (!selectEntities(src, a[i], out, playersOnly, err)) { fail(src, err); return false; }
			return true;
		}

		bool playerTargets(Source &src, const Args &a, size_t i, std::vector<std::shared_ptr<ServerPlayer> > &out, bool defaultSelf)
		{
			std::wstring err;
			if (!a.has(i))
			{
				if (defaultSelf && src.player) { out.assign(1, src.player); return true; }
				std::shared_ptr<ServerPlayer> sp = std::dynamic_pointer_cast<ServerPlayer>(src.entity);
				if (defaultSelf && sp) { out.assign(1, sp); return true; }
				fail(src, L"Informe um jogador.");
				return false;
			}
			if (!selectPlayers(src, a[i], out, err)) { fail(src, err); return false; }
			return true;
		}

		std::wstring names(const std::vector<std::shared_ptr<Entity> > &v)
		{
			if (v.size() == 1) return entityName(v[0]);
			return std::to_wstring(v.size()) + L" entidades";
		}

		std::wstring pnames(const std::vector<std::shared_ptr<ServerPlayer> > &v)
		{
			if (v.size() == 1) return v[0]->getName();
			return std::to_wstring(v.size()) + L" jogadores";
		}

		void syncPrivileges(Source &src, const std::shared_ptr<ServerPlayer> &p)
		{
			if (src.server && src.server->getPlayers()) src.server->getPlayers()->broadcastAll(std::make_shared<PlayerInfoPacket>(p));
		}

		void syncHostOptions(Source &src)
		{
			if (src.server && src.server->getPlayers())
				src.server->getPlayers()->broadcastAll(std::make_shared<ServerSettingsChangedPacket>(ServerSettingsChangedPacket::HOST_IN_GAME_SETTINGS, app.GetGameHostOption(eGameHostOption_All)));
		}

		int gameModeFromName(const std::wstring &s)
		{
			const std::wstring l = toLower(s);
			if (l == L"survival" || l == L"s" || l == L"0" || l == L"sobrevivencia" || l == L"sobrevivência") return 0;
			if (l == L"creative" || l == L"c" || l == L"1" || l == L"criativo") return 1;
			if (l == L"adventure" || l == L"a" || l == L"2" || l == L"aventura") return 2;
			if (l == L"spectator" || l == L"sp" || l == L"3" || l == L"espectador") return 3;
			return -1;
		}

		const wchar_t *gameModeName(int id)
		{
			switch (id)
			{
			case 0: return L"Sobrevivência";
			case 1: return L"Criativo";
			case 2: return L"Aventura";
			case 3: return L"Espectador";
			default: return L"?";
			}
		}
	}

	// ---------------------------------------------------------------------------------
	// /teleport, /tp
	// ---------------------------------------------------------------------------------
	int cmdTeleport(Source &src, const Args &a)
	{
		const CommandInfo *info = self(L"teleport");
		if (a.size() == 0) return usage(src, info);

		std::vector<std::shared_ptr<Entity> > who;
		size_t i = 0;
		// /tp <x y z> or /tp <destination>
		bool firstIsCoord = !a[0].empty() && (a[0][0] == L'~' || a[0][0] == L'^' || iswdigit(a[0][0]) || a[0][0] == L'-' || a[0][0] == L'.');
		if (a.size() == 1 || (firstIsCoord && (a.size() == 3 || a.size() == 5)))
		{
			if (!src.entity) return fail(src, L"Somente uma entidade pode ser teleportada sem alvo.");
			who.assign(1, src.entity);
		}
		else
		{
			if (!targets(src, a, 0, who, false, false)) return 0;
			i = 1;
		}

		if (a.size() == i + 1)
		{
			std::vector<std::shared_ptr<Entity> > dest;
			std::wstring err;
			if (!selectEntities(src, a[i], dest, false, err)) return fail(src, err);
			if (dest.size() != 1) return fail(src, L"O destino precisa ser uma única entidade.");
			for (auto &e : who) teleportEntity(e, levelOf(dest[0]), dest[0]->x, dest[0]->y, dest[0]->z, dest[0]->yRot, dest[0]->xRot);
			feedback(src, L"Teleportado " + names(who) + L" até " + entityName(dest[0]));
			return static_cast<int>(who.size());
		}

		if (!a.has(i + 2)) return usage(src, info);
		int count = 0;
		for (auto &e : who)
		{
			Source rel = src;
			double x, y, z;
			if (!parseVec3(rel, a, i, x, y, z, true)) return fail(src, L"Coordenadas inválidas.");
			float yRot = e->yRot, xRot = e->xRot;
			if (a.has(i + 3))
			{
				if (toLower(a[i + 3]) == L"facing")
				{
					double fx, fy, fz;
					std::vector<std::shared_ptr<Entity> > look;
					std::wstring err;
					if (a.has(i + 6) && parseVec3(rel, a, i + 4, fx, fy, fz, true)) {}
					else if (a.has(i + 4) && toLower(a[i + 4]) == L"entity" && selectEntities(src, a[i + 5], look, false, err) && !look.empty()) { fx = look[0]->x; fy = look[0]->y + 1.62; fz = look[0]->z; }
					else return fail(src, L"Uso: ... facing <x y z> | facing entity <alvo>");
					const double ddx = fx - x, ddy = fy - (y + 1.62), ddz = fz - z;
					yRot = static_cast<float>(atan2(ddz, ddx) * 180.0 / 3.14159265358979) - 90.0f;
					xRot = static_cast<float>(-atan2(ddy, sqrt(ddx * ddx + ddz * ddz)) * 180.0 / 3.14159265358979);
				}
				else
				{
					if (!parseRotation(src, a[i + 3], e->yRot, yRot)) return fail(src, L"Rotação inválida.");
					if (a.has(i + 4) && !parseRotation(src, a[i + 4], e->xRot, xRot)) return fail(src, L"Rotação inválida.");
				}
			}
			teleportEntity(e, levelOf(e), x, y, z, yRot, xRot);
			count++;
		}
		feedback(src, L"Teleportado " + names(who));
		return count;
	}

	// ---------------------------------------------------------------------------------
	// /give <targets> <item> [count]
	// ---------------------------------------------------------------------------------
	int cmdGive(Source &src, const Args &a)
	{
		if (a.size() < 2) return usage(src, self(L"give"));
		std::vector<std::shared_ptr<ServerPlayer> > players;
		if (!playerTargets(src, a, 0, players, false)) return 0;
		int id, aux;
		if (!resolveItem(a[1], id, aux)) return fail(src, L"Item desconhecido: " + a[1]);
		int count = 1;
		if (a.has(2) && (!parseInt(a[2], count) || count < 1 || count > 6400)) return fail(src, L"Quantidade inválida: " + a[2]);
		if (a.has(3)) { int d; if (parseInt(a[3], d)) aux = d; }	// 1.12 style data value
		for (auto &p : players)
		{
			int left = count;
			const int maxStack = (std::max)(1, Item::items[id]->getMaxStackSize());
			while (left > 0)
			{
				const int n = (std::min)(left, maxStack);
				left -= n;
				std::shared_ptr<ItemInstance> stack = std::make_shared<ItemInstance>(id, n, aux);
				if (!p->inventory->add(stack) || stack->count > 0)
				{
					if (stack->count > 0)
					{
						std::shared_ptr<ItemEntity> drop = p->drop(stack);
						if (drop) drop->throwTime = 0;
					}
				}
			}
		}
		feedback(src, L"Dado(s) " + std::to_wstring(count) + L" [" + itemName(id, aux) + L"] para " + pnames(players));
		return static_cast<int>(players.size());
	}

	// ---------------------------------------------------------------------------------
	// /clear [targets] [item] [maxCount]
	// ---------------------------------------------------------------------------------
	int cmdClear(Source &src, const Args &a)
	{
		std::vector<std::shared_ptr<ServerPlayer> > players;
		if (!playerTargets(src, a, 0, players, true)) return 0;
		int id = -1, aux = -1;
		if (a.has(1) && !resolveItem(a[1], id, aux)) return fail(src, L"Item desconhecido: " + a[1]);
		if (a.has(1) && a[1].find(L':') == std::wstring::npos && a[1].find(L'[') == std::wstring::npos) aux = -1;
		int maxCount = -1;
		if (a.has(2)) parseInt(a[2], maxCount);
		int total = 0;
		for (auto &p : players)
		{
			if (maxCount == 0)
			{
				// count only
				for (unsigned int s = 0; s < p->inventory->getContainerSize(); s++)
				{
					std::shared_ptr<ItemInstance> it = p->inventory->getItem(s);
					if (it && (id < 0 || it->id == id) && (aux < 0 || it->getAuxValue() == aux)) total += it->count;
				}
				continue;
			}
			total += p->inventory->clearInventory(id, aux);
			p->inventoryMenu->broadcastChanges();
		}
		if (maxCount == 0) { feedback(src, L"Encontrados " + std::to_wstring(total) + L" itens"); return total; }
		if (total == 0) return fail(src, L"Nenhum item encontrado em " + pnames(players));
		feedback(src, L"Removidos " + std::to_wstring(total) + L" itens de " + pnames(players));
		return total;
	}

	// ---------------------------------------------------------------------------------
	// /kill [targets]
	// ---------------------------------------------------------------------------------
	int cmdKill(Source &src, const Args &a)
	{
		std::vector<std::shared_ptr<Entity> > who;
		if (!targets(src, a, 0, who, false, true)) return 0;
		for (auto &e : who)
		{
			if (e->instanceof(eTYPE_PLAYER) || e->instanceof(eTYPE_LIVINGENTITY))
			{
				// out_of_world damage also kills players in creative
				e->hurt(DamageSource::outOfWorld, 3.4e38f);
			}
			else
			{
				e->remove();
			}
		}
		feedback(src, L"Matou " + names(who));
		return static_cast<int>(who.size());
	}

	// ---------------------------------------------------------------------------------
	// /gamemode <mode> [targets]   /defaultgamemode <mode>
	// ---------------------------------------------------------------------------------
	int cmdGameMode(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"gamemode"));
		const int mode = gameModeFromName(a[0]);
		if (mode < 0) return fail(src, L"Modo de jogo desconhecido: " + a[0]);
		std::vector<std::shared_ptr<ServerPlayer> > players;
		if (!playerTargets(src, a, 1, players, true)) return 0;
		GameType *gt = GameType::byId(mode);
		if (!gt) return fail(src, L"Modo de jogo indisponível.");
		for (auto &p : players)
		{
			p->setGameMode(gt);
			p->resetLastActionTime();
			p->sendMessage(std::wstring(L"§7Seu modo de jogo agora é ") + gameModeName(mode));
		}
		feedback(src, std::wstring(L"Modo de jogo de ") + pnames(players) + L" definido como " + gameModeName(mode));
		return static_cast<int>(players.size());
	}

	int cmdDefaultGameMode(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"defaultgamemode"));
		const int mode = gameModeFromName(a[0]);
		GameType *gt = mode >= 0 ? GameType::byId(mode) : nullptr;
		if (!gt) return fail(src, L"Modo de jogo desconhecido: " + a[0]);
		if (src.server)
		{
			for (unsigned int i = 0; i < src.server->levels.length; i++)
				if (src.server->levels[i]) src.server->levels[i]->getLevelData()->setGameType(gt);
		}
		app.SetGameHostOption(eGameHostOption_GameType, mode);
		feedback(src, std::wstring(L"Modo de jogo padrão: ") + gameModeName(mode));
		return 1;
	}

	// ---------------------------------------------------------------------------------
	// /effect give <targets> <effect> [seconds|infinite] [amplifier] [hideParticles]
	// /effect clear [targets] [effect]   (1.12: /effect <targets> <effect>|clear [s] [amp])
	// ---------------------------------------------------------------------------------
	int cmdEffect(Source &src, const Args &a)
	{
		const CommandInfo *info = self(L"effect");
		if (a.size() < 1) return usage(src, info);
		const std::wstring sub = toLower(a[0]);
		std::vector<std::shared_ptr<Entity> > who;
		if (sub == L"clear")
		{
			if (!targets(src, a, 1, who, false, true)) return 0;
			int effect = -1;
			if (a.has(2) && (effect = resolveEffect(a[2])) < 0) return fail(src, L"Efeito desconhecido: " + a[2]);
			int n = 0;
			for (auto &e : who)
			{
				std::shared_ptr<LivingEntity> le = std::dynamic_pointer_cast<LivingEntity>(e);
				if (!le) continue;
				if (effect < 0) le->removeAllEffects();
				else le->removeEffect(effect);
				n++;
			}
			if (n == 0) return fail(src, L"Nenhuma entidade viva encontrada.");
			feedback(src, L"Efeitos removidos de " + names(who));
			return n;
		}

		size_t i = 1;
		if (sub != L"give")
		{
			// 1.12 syntax: /effect <targets> <effect|clear> ...
			i = 0;
			if (a.has(1) && toLower(a[1]) == L"clear")
			{
				Args b;
				b.t = { L"clear", a[0] };
				return cmdEffect(src, b);
			}
		}
		if (!a.has(i + 1)) return usage(src, info);
		if (!targets(src, a, i, who, false, false)) return 0;
		const int effect = resolveEffect(a[i + 1]);
		if (effect < 0) return fail(src, L"Efeito desconhecido: " + a[i + 1]);
		int seconds = 30;
		bool infinite = false;
		if (a.has(i + 2))
		{
			if (toLower(a[i + 2]) == L"infinite") infinite = true;
			else if (!parseInt(a[i + 2], seconds) || seconds < 0 || seconds > 1000000) return fail(src, L"Duração inválida: " + a[i + 2]);
		}
		int amp = 0;
		if (a.has(i + 3) && (!parseInt(a[i + 3], amp) || amp < 0 || amp > 255)) return fail(src, L"Amplificador inválido: " + a[i + 3]);
		bool hide = false;
		if (a.has(i + 4)) parseBool(a[i + 4], hide);

		MobEffect *me = MobEffect::effects[effect];
		int duration = infinite ? 0x7FFFFFFF : seconds * SharedConstants::TICKS_PER_SECOND;
		if (me->isInstantenous()) duration = infinite ? 1 : (std::max)(1, seconds);
		int n = 0;
		for (auto &e : who)
		{
			std::shared_ptr<LivingEntity> le = std::dynamic_pointer_cast<LivingEntity>(e);
			if (!le) continue;
			if (seconds == 0 && !infinite) { le->removeEffect(effect); n++; continue; }
			le->addEffect(new MobEffectInstance(effect, duration, amp, hide));
			n++;
		}
		if (n == 0) return fail(src, L"Nenhuma entidade viva encontrada.");
		feedback(src, L"Efeito aplicado em " + names(who));
		return n;
	}

	// ---------------------------------------------------------------------------------
	// /enchant <targets> <enchantment> [level]
	// ---------------------------------------------------------------------------------
	int cmdEnchant(Source &src, const Args &a)
	{
		if (a.size() < 2) return usage(src, self(L"enchant"));
		std::vector<std::shared_ptr<Entity> > who;
		if (!targets(src, a, 0, who, false, false)) return 0;
		const int id = resolveEnchantment(a[1]);
		if (id < 0) return fail(src, L"Encantamento desconhecido: " + a[1]);
		Enchantment *ench = Enchantment::enchantments[id];
		int level = 1;
		if (a.has(2) && !parseInt(a[2], level)) return fail(src, L"Nível inválido: " + a[2]);
		if (level < 1) level = 1;
		if (level > 255) level = 255;
		int n = 0;
		for (auto &e : who)
		{
			std::shared_ptr<LivingEntity> le = std::dynamic_pointer_cast<LivingEntity>(e);
			if (!le) continue;
			std::shared_ptr<ItemInstance> item = le->getCarriedItem();
			if (!item) continue;
			if (!ench->canEnchant(item))
			{
				fail(src, entityName(e) + L": este item não aceita esse encantamento.");
				continue;
			}
			item->enchant(ench, level);
			n++;
		}
		if (n == 0) return fail(src, L"Nenhum item foi encantado.");
		feedback(src, L"Encantamento aplicado em " + names(who));
		return n;
	}

	// ---------------------------------------------------------------------------------
	// /experience|/xp add|set|query <targets> <amount> [points|levels]  (+1.12: /xp <n>[L] [player])
	// ---------------------------------------------------------------------------------
	int cmdExperience(Source &src, const Args &a)
	{
		const CommandInfo *info = self(L"experience");
		if (a.size() < 1) return usage(src, info);
		std::wstring sub = toLower(a[0]);
		if (sub != L"add" && sub != L"set" && sub != L"query")
		{
			// legacy /xp <amount>[L] [player]
			std::wstring n = a[0];
			bool levels = !n.empty() && (n.back() == L'l' || n.back() == L'L');
			if (levels) n.pop_back();
			int amount;
			if (!parseInt(n, amount)) return usage(src, info);
			std::vector<std::shared_ptr<ServerPlayer> > ps;
			if (!playerTargets(src, a, 1, ps, true)) return 0;
			for (auto &p : ps)
			{
				if (levels) p->giveExperienceLevels(amount);
				else if (amount > 0) p->increaseXp(amount);
			}
			feedback(src, L"Experiência dada a " + pnames(ps));
			return static_cast<int>(ps.size());
		}
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		if (!playerTargets(src, a, 1, ps, false)) return 0;
		const bool levels = a.has(3) ? toLower(a[3]) == L"levels" : (sub == L"query" ? (a.has(2) && toLower(a[2]) == L"levels") : false);
		if (sub == L"query")
		{
			if (ps.size() != 1) return fail(src, L"Somente um jogador.");
			std::shared_ptr<ServerPlayer> p = ps[0];
			const int v = levels ? p->experienceLevel : static_cast<int>(p->experienceProgress * p->getXpNeededForNextLevel());
			feedback(src, p->getName() + L" tem " + std::to_wstring(v) + (levels ? L" níveis" : L" pontos de experiência"));
			return v;
		}
		int amount;
		if (!a.has(2) || !parseInt(a[2], amount)) return usage(src, info);
		for (auto &p : ps)
		{
			if (sub == L"add")
			{
				if (levels) p->giveExperienceLevels(amount);
				else if (amount > 0) p->increaseXp(amount);
			}
			else
			{
				if (levels)
				{
					p->giveExperienceLevels(amount - p->experienceLevel);
				}
				else
				{
					const int need = (std::max)(1, p->getXpNeededForNextLevel());
					p->experienceProgress = (std::min)(0.999f, (std::max)(0.0f, static_cast<float>(amount) / need));
				}
			}
			// force the XP bar to resync
			p->resetSentInfo();
		}
		feedback(src, L"Experiência de " + pnames(ps) + L" alterada");
		return static_cast<int>(ps.size());
	}

	// ---------------------------------------------------------------------------------
	// /spawnpoint [targets] [pos]
	// ---------------------------------------------------------------------------------
	int cmdSpawnpoint(Source &src, const Args &a)
	{
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		if (!playerTargets(src, a, 0, ps, true)) return 0;
		for (auto &p : ps)
		{
			int x = Mth::floor(p->x), y = Mth::floor(p->y), z = Mth::floor(p->z);
			if (a.has(3))
			{
				Source rel = src;
				if (!parseBlockPos(rel, a, 1, x, y, z)) return fail(src, L"Coordenadas inválidas.");
			}
			Pos pos(x, y, z);
			p->setRespawnPosition(&pos, true);
		}
		feedback(src, L"Ponto de renascimento definido para " + pnames(ps));
		return static_cast<int>(ps.size());
	}

	// ---------------------------------------------------------------------------------
	// /spreadplayers <x> <z> <spreadDistance> <maxRange> [under maxHeight] <respectTeams> <targets>
	// ---------------------------------------------------------------------------------
	int cmdSpreadPlayers(Source &src, const Args &a)
	{
		if (a.size() < 5) return usage(src, self(L"spreadplayers"));
		double cx, cz, spread, range;
		auto coord = [&](const std::wstring &t, double base, double &out) -> bool
		{
			if (!t.empty() && t[0] == L'~') { double off = 0; if (t.size() > 1 && !parseDouble(t.substr(1), off)) return false; out = base + off; return true; }
			return parseDouble(t, out);
		};
		if (!coord(a[0], src.x, cx) || !coord(a[1], src.z, cz) || !parseDouble(a[2], spread) || !parseDouble(a[3], range)) return usage(src, self(L"spreadplayers"));
		size_t i = 4;
		int maxHeight = 255;
		if (toLower(a[i]) == L"under") { if (!a.has(i + 1) || !parseInt(a[i + 1], maxHeight)) return usage(src, self(L"spreadplayers")); i += 2; }
		i++;	// respectTeams
		std::vector<std::shared_ptr<Entity> > who;
		if (!targets(src, a, i, who, false, false)) return 0;
		ServerLevel *lvl = src.level;
		if (!lvl) return fail(src, L"Sem mundo.");
		Random *r = lvl->random;
		std::vector<std::pair<double, double> > placed;
		int n = 0;
		for (auto &e : who)
		{
			double px = cx, pz = cz;
			for (int attempt = 0; attempt < 200; attempt++)
			{
				px = cx + (r->nextDouble() * 2 - 1) * range;
				pz = cz + (r->nextDouble() * 2 - 1) * range;
				bool ok = true;
				for (auto &q : placed)
				{
					if ((q.first - px) * (q.first - px) + (q.second - pz) * (q.second - pz) < spread * spread) { ok = false; break; }
				}
				if (ok) break;
			}
			const int ix = Mth::floor(px), iz = Mth::floor(pz);
			int y = lvl->getTopSolidBlock(ix, iz);
			if (y > maxHeight) y = maxHeight;
			placed.push_back({ px, pz });
			teleportEntity(e, lvl, ix + 0.5, y + 1, iz + 0.5, e->yRot, e->xRot);
			n++;
		}
		feedback(src, L"Espalhados " + names(who));
		return n;
	}

	// ---------------------------------------------------------------------------------
	// /ride <target> mount <vehicle> | dismount
	// ---------------------------------------------------------------------------------
	int cmdRide(Source &src, const Args &a)
	{
		if (a.size() < 2) return usage(src, self(L"ride"));
		std::vector<std::shared_ptr<Entity> > who;
		if (!targets(src, a, 0, who, false, false)) return 0;
		if (who.size() != 1) return fail(src, L"Somente uma entidade.");
		const std::wstring sub = toLower(a[1]);
		if (sub == L"dismount")
		{
			who[0]->ride(nullptr);
			feedback(src, entityName(who[0]) + L" desmontou");
			return 1;
		}
		if (sub != L"mount" || !a.has(2)) return usage(src, self(L"ride"));
		std::vector<std::shared_ptr<Entity> > veh;
		std::wstring err;
		if (!selectEntities(src, a[2], veh, false, err)) return fail(src, err);
		if (veh.size() != 1 || veh[0] == who[0]) return fail(src, L"Veículo inválido.");
		who[0]->ride(veh[0]);
		feedback(src, entityName(who[0]) + L" montou em " + entityName(veh[0]));
		return 1;
	}

	// ---------------------------------------------------------------------------------
	// /damage <target> <amount> [type]
	// ---------------------------------------------------------------------------------
	int cmdDamage(Source &src, const Args &a)
	{
		if (a.size() < 2) return usage(src, self(L"damage"));
		std::vector<std::shared_ptr<Entity> > who;
		if (!targets(src, a, 0, who, false, false)) return 0;
		double amount;
		if (!parseDouble(a[1], amount) || amount < 0) return fail(src, L"Dano inválido: " + a[1]);
		DamageSource *type = DamageSource::genericSource;
		if (a.has(2))
		{
			const std::wstring t = toLower(stripNamespace(a[2]));
			if (t == L"magic") type = DamageSource::magic;
			else if (t == L"fall") type = DamageSource::fall;
			else if (t == L"in_fire") type = DamageSource::inFire;
			else if (t == L"on_fire") type = DamageSource::onFire;
			else if (t == L"lava") type = DamageSource::lava;
			else if (t == L"drown") type = DamageSource::drown;
			else if (t == L"starve") type = DamageSource::starve;
			else if (t == L"cactus") type = DamageSource::cactus;
			else if (t == L"out_of_world") type = DamageSource::outOfWorld;
			else if (t == L"in_wall") type = DamageSource::inWall;
		}
		int n = 0;
		for (auto &e : who) if (e->hurt(type, static_cast<float>(amount))) n++;
		feedback(src, L"Dano aplicado em " + names(who));
		return n;
	}

	// ---------------------------------------------------------------------------------
	// /attribute <target> <attribute> get|base get|base set <value>
	// ---------------------------------------------------------------------------------
	int cmdAttribute(Source &src, const Args &a)
	{
		if (a.size() < 3) return usage(src, self(L"attribute"));
		std::vector<std::shared_ptr<Entity> > who;
		if (!targets(src, a, 0, who, false, false)) return 0;
		if (who.size() != 1) return fail(src, L"Somente uma entidade.");
		std::shared_ptr<LivingEntity> le = std::dynamic_pointer_cast<LivingEntity>(who[0]);
		if (!le) return fail(src, L"A entidade não tem atributos.");
		const std::wstring n = toLower(stripNamespace(a[1]));
		Attribute *attr = nullptr;
		if (n == L"generic.max_health" || n == L"max_health") attr = SharedMonsterAttributes::MAX_HEALTH;
		else if (n == L"generic.movement_speed" || n == L"movement_speed") attr = SharedMonsterAttributes::MOVEMENT_SPEED;
		else if (n == L"generic.attack_damage" || n == L"attack_damage") attr = SharedMonsterAttributes::ATTACK_DAMAGE;
		else if (n == L"generic.knockback_resistance" || n == L"knockback_resistance") attr = SharedMonsterAttributes::KNOCKBACK_RESISTANCE;
		else if (n == L"generic.follow_range" || n == L"follow_range") attr = SharedMonsterAttributes::FOLLOW_RANGE;
		if (!attr) return fail(src, L"Atributo desconhecido: " + a[1]);
		AttributeInstance *inst = le->getAttribute(attr);
		if (!inst) return fail(src, L"A entidade não tem esse atributo.");
		const std::wstring op = toLower(a[2]);
		if (op == L"get")
		{
			const double v = inst->getValue();
			feedback(src, entityName(who[0]) + L": " + n + L" = " + std::to_wstring(v));
			return static_cast<int>(v);
		}
		if (op == L"base" && a.has(3))
		{
			const std::wstring op2 = toLower(a[3]);
			if (op2 == L"get")
			{
				const double v = inst->getBaseValue();
				feedback(src, entityName(who[0]) + L": " + n + L" (base) = " + std::to_wstring(v));
				return static_cast<int>(v);
			}
			double v;
			if (op2 == L"set" && a.has(4) && parseDouble(a[4], v))
			{
				inst->setBaseValue(v);
				if (attr == SharedMonsterAttributes::MAX_HEALTH && le->getHealth() > le->getMaxHealth()) le->setHealth(le->getMaxHealth());
				feedback(src, n + L" de " + entityName(who[0]) + L" = " + std::to_wstring(v));
				return 1;
			}
		}
		return usage(src, self(L"attribute"));
	}

	// ---------------------------------------------------------------------------------
	// /item replace entity <targets> <slot> with <item> [count]
	// ---------------------------------------------------------------------------------
	int cmdItem(Source &src, const Args &a)
	{
		// also accepts 1.12 /replaceitem entity <target> <slot> <item> [count] [data]
		size_t i = 0;
		if (toLower(a[0]) == L"replace") i = 1;
		if (!a.has(i + 3) || toLower(a[i]) != L"entity") return usage(src, self(L"item"));
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		if (!playerTargets(src, a, i + 1, ps, false)) return 0;
		const std::wstring slot = toLower(stripNamespace(a[i + 2]));
		size_t itemTok = i + 3;
		if (toLower(a[itemTok]) == L"with") itemTok++;
		int id, aux;
		if (!a.has(itemTok) || !resolveItem(a[itemTok], id, aux)) return fail(src, L"Item desconhecido: " + a[itemTok]);
		int count = 1;
		if (a.has(itemTok + 1)) parseInt(a[itemTok + 1], count);
		int slotIndex = -1;
		bool armorSlot = false;
		if (slot.compare(0, 7, L"hotbar.") == 0) parseInt(slot.substr(7), slotIndex);
		else if (slot.compare(0, 12, L"slot.hotbar.") == 0) parseInt(slot.substr(12), slotIndex);
		else if (slot.compare(0, 10, L"inventory.") == 0) { parseInt(slot.substr(10), slotIndex); slotIndex += 9; }
		else if (slot == L"armor.head" || slot == L"slot.armor.head") { armorSlot = true; slotIndex = 3; }
		else if (slot == L"armor.chest" || slot == L"slot.armor.chest") { armorSlot = true; slotIndex = 2; }
		else if (slot == L"armor.legs" || slot == L"slot.armor.legs") { armorSlot = true; slotIndex = 1; }
		else if (slot == L"armor.feet" || slot == L"slot.armor.feet") { armorSlot = true; slotIndex = 0; }
		else if (slot == L"weapon.mainhand" || slot == L"weapon" || slot == L"slot.weapon.mainhand") slotIndex = -2;
		if (slotIndex == -1) return fail(src, L"Slot desconhecido: " + slot);
		for (auto &p : ps)
		{
			std::shared_ptr<ItemInstance> stack = std::make_shared<ItemInstance>(id, count, aux);
			if (slotIndex == -2) p->inventory->setItem(p->inventory->selected, stack);
			else if (armorSlot) p->inventory->armor[slotIndex] = stack;
			else p->inventory->setItem(slotIndex, stack);
			p->inventoryMenu->broadcastChanges();
		}
		feedback(src, L"Item colocado em " + pnames(ps));
		return static_cast<int>(ps.size());
	}

	// ---------------------------------------------------------------------------------
	// /tag <targets> add|remove|list <name>
	// ---------------------------------------------------------------------------------
	int cmdTag(Source &src, const Args &a)
	{
		if (a.size() < 2) return usage(src, self(L"tag"));
		std::vector<std::shared_ptr<Entity> > who;
		if (!targets(src, a, 0, who, false, false)) return 0;
		const std::wstring op = toLower(a[1]);
		if (op == L"list")
		{
			std::set<std::wstring> all;
			for (auto &e : who) for (auto &t : entityTags(e)) all.insert(t);
			std::wstring s;
			for (auto &t : all) { if (!s.empty()) s += L", "; s += t; }
			feedback(src, names(who) + L" tem " + std::to_wstring(all.size()) + L" tags: " + s);
			return static_cast<int>(all.size());
		}
		if (!a.has(2)) return usage(src, self(L"tag"));
		int n = 0;
		for (auto &e : who)
		{
			std::vector<std::wstring> &tags = entityTags(e);
			auto it = std::find(tags.begin(), tags.end(), a[2]);
			if (op == L"add" && it == tags.end()) { tags.push_back(a[2]); n++; }
			else if (op == L"remove" && it != tags.end()) { tags.erase(it); n++; }
		}
		if (n == 0) return fail(src, L"Nada mudou.");
		feedback(src, (op == L"add" ? L"Tag adicionada em " : L"Tag removida de ") + std::to_wstring(n) + L" entidade(s)");
		return n;
	}

	// ---------------------------------------------------------------------------------
	// /summon <entity> [pos] [nbt]
	// ---------------------------------------------------------------------------------
	int cmdSummon(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"summon"));
		if (!src.level) return fail(src, L"Sem mundo.");
		double x = src.x, y = src.y, z = src.z;
		if (a.has(3) && !parseVec3(src, a, 1, x, y, z, true)) return fail(src, L"Coordenadas inválidas.");
		const std::wstring type = toLower(stripNamespace(a[0]));
		if (type == L"lightning_bolt" || type == L"lightningbolt" || type == L"lightning")
		{
			src.level->addGlobalEntity(std::make_shared<LightningBolt>(src.level, x, y, z));
			feedback(src, L"Raio invocado");
			return 1;
		}
		const std::wstring id = resolveEntityType(a[0]);
		if (id.empty()) return fail(src, L"Entidade desconhecida: " + a[0]);
		std::shared_ptr<Entity> e = EntityIO::newEntity(id, src.level);
		if (!e) return fail(src, L"Não foi possível criar " + a[0]);
		e->moveTo(x, y, z, src.level->random->nextFloat() * 360.0f, 0);
		std::shared_ptr<Mob> mob = std::dynamic_pointer_cast<Mob>(e);
		if (mob) mob->finalizeMobSpawn(nullptr);
		// minimal NBT: {CustomName:"..."} / {NoAI:1} not supported by this engine; CustomName only
		if (a.has(4))
		{
			const std::wstring nbt = a.rest(4);
			const size_t cn = nbt.find(L"CustomName:");
			if (cn != std::wstring::npos)
			{
				size_t q1 = nbt.find(L'"', cn), q2 = q1 == std::wstring::npos ? q1 : nbt.find(L'"', q1 + 1);
				if (q1 != std::wstring::npos && q2 != std::wstring::npos)
				{
					std::wstring name = nbt.substr(q1 + 1, q2 - q1 - 1);
					if (name.size() > 2 && name.front() == L'{')	// JSON {"text":"..."}
					{
						size_t t = name.find(L"text");
						(void)t;
					}
					e->setCustomName(name);
					e->setCustomNameVisible(true);
				}
			}
		}
		src.level->addEntity(e);
		feedback(src, L"Invocado " + entityName(e));
		return 1;
	}

	// ---------------------------------------------------------------------------------
	// /say /me /msg /tell /w /tellraw /title /list
	// ---------------------------------------------------------------------------------
	namespace
	{
		// tiny JSON text extraction for /tellraw and /title ({"text":"..."} / ["", {...}] / "plain")
		std::wstring jsonText(const std::wstring &json)
		{
			std::wstring s = trimString(json);
			if (s.empty()) return s;
			if (s[0] == L'"' && s.back() == L'"' && s.size() >= 2) return s.substr(1, s.size() - 2);
			if (s[0] != L'{' && s[0] != L'[') return s;
			std::wstring out;
			size_t pos = 0;
			while ((pos = s.find(L"\"text\"", pos)) != std::wstring::npos)
			{
				size_t colon = s.find(L':', pos);
				size_t q1 = colon == std::wstring::npos ? colon : s.find(L'"', colon);
				if (q1 == std::wstring::npos) break;
				std::wstring piece;
				size_t i = q1 + 1;
				for (; i < s.size() && s[i] != L'"'; i++)
				{
					if (s[i] == L'\\' && i + 1 < s.size()) { i++; piece += (s[i] == L'n') ? L'\n' : s[i]; }
					else piece += s[i];
				}
				// colour
				size_t objEnd = s.find(L'}', i);
				size_t col = s.find(L"\"color\"", pos);
				if (col != std::wstring::npos && (objEnd == std::wstring::npos || col < objEnd))
				{
					size_t c1 = s.find(L'"', s.find(L':', col));
					size_t c2 = c1 == std::wstring::npos ? c1 : s.find(L'"', c1 + 1);
					if (c1 != std::wstring::npos && c2 != std::wstring::npos)
					{
						static const wchar_t *colours[16] = { L"black", L"dark_blue", L"dark_green", L"dark_aqua", L"dark_red", L"dark_purple", L"gold", L"gray",
							L"dark_gray", L"blue", L"green", L"aqua", L"red", L"light_purple", L"yellow", L"white" };
						const std::wstring cn = s.substr(c1 + 1, c2 - c1 - 1);
						for (int k = 0; k < 16; k++) if (cn == colours[k]) { out += L'§'; out += L"0123456789abcdef"[k]; }
					}
				}
				out += piece;
				pos = i;
			}
			return out;
		}

		void chatAll(Source &src, const std::wstring &msg)
		{
			if (src.server && src.server->getPlayers()) src.server->getPlayers()->broadcastAll(std::make_shared<ChatPacket>(msg));
		}
	}

	int cmdSay(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"say"));
		chatAll(src, L"[" + src.name + L"] " + a.rest(0));
		return 1;
	}

	int cmdMe(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"me"));
		chatAll(src, L"* " + src.name + L" " + a.rest(0));
		return 1;
	}

	int cmdMsg(Source &src, const Args &a)
	{
		if (a.size() < 2) return usage(src, self(L"msg"));
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		std::wstring err;
		if (!selectPlayers(src, a[0], ps, err)) return fail(src, err);
		const std::wstring msg = a.rest(1);
		for (auto &p : ps) p->sendMessage(L"§7§o" + src.name + L" sussurra para você: " + msg);
		feedback(src, L"§oVocê sussurra para " + pnames(ps) + L": " + msg);
		return static_cast<int>(ps.size());
	}

	int cmdTellraw(Source &src, const Args &a)
	{
		if (a.size() < 2) return usage(src, self(L"tellraw"));
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		std::wstring err;
		if (!selectPlayers(src, a[0], ps, err)) return fail(src, err);
		const std::wstring msg = jsonText(a.rest(1));
		for (auto &p : ps) p->sendMessage(msg);
		return static_cast<int>(ps.size());
	}

	int cmdTitle(Source &src, const Args &a)
	{
		if (a.size() < 2) return usage(src, self(L"title"));
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		std::wstring err;
		if (!selectPlayers(src, a[0], ps, err)) return fail(src, err);
		const std::wstring op = toLower(a[1]);
		if (op == L"clear" || op == L"reset" || op == L"times") return static_cast<int>(ps.size());
		if (!a.has(2)) return usage(src, self(L"title"));
		std::wstring text = jsonText(a.rest(2));
		std::wstring prefix = op == L"title" ? L"§l" : (op == L"subtitle" ? L"§7" : L"§e");
		for (auto &p : ps) p->sendMessage(prefix + text);
		return static_cast<int>(ps.size());
	}

	int cmdList(Source &src, const Args &a)
	{
		(void)a;
		PlayerList *pl = src.server ? src.server->getPlayers() : nullptr;
		if (!pl) return 0;
		std::wstring names;
		for (auto &p : pl->players) { if (!p) continue; if (!names.empty()) names += L", "; names += p->getName(); }
		feedback(src, L"Há " + std::to_wstring(pl->getPlayerCount()) + L" jogador(es) online: " + names);
		return pl->getPlayerCount();
	}

	// ---------------------------------------------------------------------------------
	// /kick /op /deop /ban /pardon /banlist
	// ---------------------------------------------------------------------------------
	namespace
	{
		std::set<std::wstring> &bans() { static std::set<std::wstring> b; return b; }
	}

	bool isNameBanned(const std::wstring &name)
	{
		return bans().count(toLower(name)) != 0;
	}

	int cmdKick(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"kick"));
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		if (!playerTargets(src, a, 0, ps, false)) return 0;
		int n = 0;
		for (auto &p : ps)
		{
			if (p->connection && p->connection->isLocal()) { fail(src, L"Não é possível expulsar o anfitrião."); continue; }
			if (p->connection) p->connection->disconnect(DisconnectPacket::eDisconnect_Kicked);
			n++;
		}
		if (n) feedback(src, L"Expulso(s): " + std::to_wstring(n));
		return n;
	}

	int cmdOp(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"op"));
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		if (!playerTargets(src, a, 0, ps, false)) return 0;
		for (auto &p : ps) { p->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_Op, 1); syncPrivileges(src, p); }
		feedback(src, pnames(ps) + L" agora é operador");
		return static_cast<int>(ps.size());
	}

	int cmdDeop(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"deop"));
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		if (!playerTargets(src, a, 0, ps, false)) return 0;
		for (auto &p : ps) { p->setPlayerGamePrivilege(Player::ePlayerGamePrivilege_Op, 0); syncPrivileges(src, p); }
		feedback(src, pnames(ps) + L" não é mais operador");
		return static_cast<int>(ps.size());
	}

	int cmdBan(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"ban"));
		bans().insert(toLower(a[0]));
		std::wstring err;
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		if (selectPlayers(src, a[0], ps, err))
		{
			for (auto &p : ps)
			{
				bans().insert(toLower(p->getName()));
				if (p->connection && !p->connection->isLocal()) p->connection->disconnect(DisconnectPacket::eDisconnect_Kicked);
			}
		}
		feedback(src, L"Banido: " + a[0] + L" (até o servidor reiniciar)");
		return 1;
	}

	int cmdPardon(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"pardon"));
		if (bans().erase(toLower(a[0])) == 0) return fail(src, a[0] + L" não está banido.");
		feedback(src, L"Desbanido: " + a[0]);
		return 1;
	}

	int cmdBanlist(Source &src, const Args &a)
	{
		(void)a;
		std::wstring s;
		for (auto &b : bans()) { if (!s.empty()) s += L", "; s += b; }
		feedback(src, L"Banidos (" + std::to_wstring(bans().size()) + L"): " + s);
		return static_cast<int>(bans().size());
	}

	// ---------------------------------------------------------------------------------
	// /swing /rotate /spectate
	// ---------------------------------------------------------------------------------
	int cmdSwing(Source &src, const Args &a)
	{
		std::vector<std::shared_ptr<Entity> > who;
		if (!targets(src, a, 0, who, false, true)) return 0;
		int n = 0;
		for (auto &e : who)
		{
			std::shared_ptr<LivingEntity> le = std::dynamic_pointer_cast<LivingEntity>(e);
			if (le) { le->swing(); n++; }
		}
		return n;
	}

	int cmdRotate(Source &src, const Args &a)
	{
		if (a.size() < 2) return usage(src, self(L"rotate"));
		std::vector<std::shared_ptr<Entity> > who;
		if (!targets(src, a, 0, who, false, false)) return 0;
		for (auto &e : who)
		{
			float yRot = e->yRot, xRot = e->xRot;
			if (!parseRotation(src, a[1], e->yRot, yRot)) return fail(src, L"Rotação inválida.");
			if (a.has(2) && !parseRotation(src, a[2], e->xRot, xRot)) return fail(src, L"Rotação inválida.");
			teleportEntity(e, levelOf(e), e->x, e->y, e->z, yRot, xRot);
		}
		return static_cast<int>(who.size());
	}

	int cmdSpectate(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"spectate"));
		std::vector<std::shared_ptr<Entity> > dest;
		std::wstring err;
		if (!selectEntities(src, a[0], dest, false, err)) return fail(src, err);
		std::vector<std::shared_ptr<ServerPlayer> > ps;
		if (!playerTargets(src, a, 1, ps, true)) return 0;
		for (auto &p : ps) teleportEntity(p, levelOf(dest[0]), dest[0]->x, dest[0]->y, dest[0]->z, dest[0]->yRot, dest[0]->xRot);
		feedback(src, L"Espectando " + entityName(dest[0]) + L" (teleporte; este jogo não tem câmera de espectador)");
		return static_cast<int>(ps.size());
	}
}
