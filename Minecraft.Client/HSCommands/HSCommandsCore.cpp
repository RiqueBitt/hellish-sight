// Hellish Sight - command core: tokenizer, coordinates, selectors, registries, dispatch.

#include "stdafx.h"
#include "HSCommandsInternal.h"

#include "../MinecraftServer.h"
#include "../ServerLevel.h"
#include "../ServerPlayer.h"
#include "../PlayerList.h"
#include "../PlayerConnection.h"
#include "../ServerPlayerGameMode.h"
#include "../../Minecraft.World/net.minecraft.world.level.dimension.h"
#include "../../Minecraft.World/net.minecraft.world.level.h"
#include "../../Minecraft.World/net.minecraft.world.entity.h"
#include "../../Minecraft.World/net.minecraft.world.entity.player.h"
#include "../../Minecraft.World/net.minecraft.world.item.h"
#include "../../Minecraft.World/net.minecraft.world.item.enchantment.h"
#include "../../Minecraft.World/net.minecraft.world.effect.h"
#include "../../Minecraft.World/net.minecraft.world.level.tile.h"
#include "../../Minecraft.World/EntityIO.h"
#include "../../Minecraft.World/ChatPacket.h"
#include "../../Minecraft.World/StringHelpers.h"
#include "../../Minecraft.World/AABB.h"
#include "../../Minecraft.World/Mth.h"
#include "Registry/IDs.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>
#include <mutex>

namespace HSCommands
{
	// =================================================================================
	// Source / feedback
	// =================================================================================
	void Source::send(const std::wstring &message, bool error)
	{
		lastOutput = message;
		if (silent) return;
		if (output)
		{
			output(message, error);
			return;
		}
		if (player)
		{
			player->sendMessage((error ? L"§c" : L"§7") + message);
		}
		else if (server)
		{
			if (error) server->warn(message);
			else server->info(message);
		}
	}

	int fail(Source &src, const std::wstring &message)
	{
		src.send(message, true);
		return 0;
	}

	int usage(Source &src, const CommandInfo *info)
	{
		if (info == nullptr) return fail(src, L"Comando desconhecido.");
		return fail(src, std::wstring(L"Uso: /") + info->name + L" " + info->usage);
	}

	void feedback(Source &src, const std::wstring &message)
	{
		src.send(message, false);
	}

	void broadcastOps(Source &src, const std::wstring &message)
	{
		// Java shows "[Name: message]" to the other operators; keep it to the executor only
		// in local play to avoid chat spam.
		(void)src;
		(void)message;
	}

	// =================================================================================
	// Registry
	// =================================================================================
	namespace
	{
		std::map<std::wstring, CommandInfo> &registry()
		{
			static std::map<std::wstring, CommandInfo> r;
			return r;
		}

		void ensureRegistered()
		{
			static std::once_flag once;
			std::call_once(once, []() { registerAll(); });
		}
	}

	void registerCommand(const CommandInfo &info)
	{
		registry()[info.name] = info;
	}

	const CommandInfo *findCommand(const std::wstring &name)
	{
		ensureRegistered();
		auto it = registry().find(toLower(stripNamespace(name)));
		return it == registry().end() ? nullptr : &it->second;
	}

	std::vector<std::wstring> commandNames()
	{
		ensureRegistered();
		std::vector<std::wstring> names;
		for (auto &kv : registry()) names.push_back(kv.first);
		return names;
	}

	// =================================================================================
	// Parsing
	// =================================================================================
	std::wstring Args::rest(size_t from) const
	{
		if (from >= offsets.size()) return L"";
		return trimString(raw.substr(offsets[from]));
	}

	Tokens tokenize(const std::wstring &line, std::vector<size_t> *offsets)
	{
		Tokens out;
		size_t i = 0;
		const size_t n = line.size();
		while (i < n)
		{
			while (i < n && iswspace(line[i])) i++;
			if (i >= n) break;
			const size_t start = i;
			std::wstring tok;
			int depth = 0;
			bool quoted = false;
			while (i < n)
			{
				const wchar_t c = line[i];
				if (quoted)
				{
					if (c == L'\\' && i + 1 < n) { tok += line[i + 1]; i += 2; continue; }
					if (c == L'"') { quoted = false; i++; continue; }
					tok += c;
					i++;
					continue;
				}
				if (c == L'"' && depth == 0) { quoted = true; i++; continue; }
				if (c == L'[' || c == L'{') depth++;
				else if ((c == L']' || c == L'}') && depth > 0) depth--;
				else if (iswspace(c) && depth == 0) break;
				tok += c;
				i++;
			}
			out.push_back(tok);
			if (offsets) offsets->push_back(start);
		}
		return out;
	}

	bool parseInt(const std::wstring &s, int &out)
	{
		if (s.empty()) return false;
		try
		{
			size_t pos = 0;
			long v = std::stol(s, &pos);
			if (pos != s.size()) return false;
			out = static_cast<int>(v);
			return true;
		}
		catch (...) { return false; }
	}

	bool parseInt64(const std::wstring &s, long long &out)
	{
		if (s.empty()) return false;
		try
		{
			size_t pos = 0;
			long long v = std::stoll(s, &pos);
			if (pos != s.size()) return false;
			out = v;
			return true;
		}
		catch (...) { return false; }
	}

	bool parseDouble(const std::wstring &s, double &out)
	{
		if (s.empty()) return false;
		try
		{
			size_t pos = 0;
			double v = std::stod(s, &pos);
			if (pos != s.size()) return false;
			out = v;
			return true;
		}
		catch (...) { return false; }
	}

	bool parseBool(const std::wstring &s, bool &out)
	{
		const std::wstring l = toLower(s);
		if (l == L"true" || l == L"1" || l == L"on" || l == L"sim") { out = true; return true; }
		if (l == L"false" || l == L"0" || l == L"off" || l == L"nao" || l == L"não") { out = false; return true; }
		return false;
	}

	std::wstring stripNamespace(const std::wstring &name)
	{
		const size_t c = name.find(L':');
		if (c != std::wstring::npos && name.compare(0, c, L"minecraft") == 0) return name.substr(c + 1);
		return name;
	}

	namespace
	{
		bool parseRelative(const std::wstring &tok, wchar_t prefix, double base, double &out, bool &isRel)
		{
			isRel = !tok.empty() && tok[0] == prefix;
			if (!isRel) return parseDouble(tok, out);
			double off = 0;
			if (tok.size() > 1 && !parseDouble(tok.substr(1), off)) return false;
			out = base + off;
			return true;
		}

		bool isWholeNumber(const std::wstring &tok)
		{
			return !tok.empty() && tok.find(L'.') == std::wstring::npos && tok[0] != L'~' && tok[0] != L'^';
		}
	}

	bool parseVec3(Source &src, const Args &args, size_t i, double &x, double &y, double &z, bool blockCentre)
	{
		if (!args.has(i + 2)) return false;
		const std::wstring &a = args[i], &b = args[i + 1], &c = args[i + 2];
		const bool local = (!a.empty() && a[0] == L'^');
		if (local)
		{
			if (b.empty() || c.empty() || b[0] != L'^' || c[0] != L'^') return false;
			double left = 0, up = 0, fwd = 0;
			if (a.size() > 1 && !parseDouble(a.substr(1), left)) return false;
			if (b.size() > 1 && !parseDouble(b.substr(1), up)) return false;
			if (c.size() > 1 && !parseDouble(c.substr(1), fwd)) return false;
			// Java ^left ^up ^forward relative to the rotation
			const double yr = (src.yRot + 90.0) * 3.14159265358979 / 180.0;
			const double pr = -src.xRot * 3.14159265358979 / 180.0;
			const double pr2 = (-src.xRot + 90.0) * 3.14159265358979 / 180.0;
			const double fx = cos(yr) * cos(pr), fy = sin(pr), fz = sin(yr) * cos(pr);
			const double ux = cos(yr) * cos(pr2), uy = sin(pr2), uz = sin(yr) * cos(pr2);
			// left = up x forward
			const double lx = uy * fz - uz * fy, ly = uz * fx - ux * fz, lz = ux * fy - uy * fx;
			x = src.x + fx * fwd + ux * up + lx * left;
			y = src.y + fy * fwd + uy * up + ly * left;
			z = src.z + fz * fwd + uz * up + lz * left;
			return true;
		}
		bool rx, ry, rz;
		if (!parseRelative(a, L'~', src.x, x, rx)) return false;
		if (!parseRelative(b, L'~', src.y, y, ry)) return false;
		if (!parseRelative(c, L'~', src.z, z, rz)) return false;
		if (blockCentre)
		{
			if (!rx && isWholeNumber(a)) x += 0.5;
			if (!rz && isWholeNumber(c)) z += 0.5;
		}
		return true;
	}

	bool parseBlockPos(Source &src, const Args &args, size_t i, int &x, int &y, int &z)
	{
		double dx, dy, dz;
		if (!parseVec3(src, args, i, dx, dy, dz, false)) return false;
		x = Mth::floor(dx);
		y = Mth::floor(dy);
		z = Mth::floor(dz);
		return true;
	}

	bool parseRotation(const Source &src, const std::wstring &tok, float current, float &out)
	{
		(void)src;
		double v = 0;
		bool rel;
		if (!parseRelative(tok, L'~', current, v, rel)) return false;
		out = static_cast<float>(v);
		return true;
	}

	// =================================================================================
	// Entities / players
	// =================================================================================
	ServerLevel *levelOf(const std::shared_ptr<Entity> &e)
	{
		return e ? static_cast<ServerLevel *>(e->level) : nullptr;
	}

	ServerLevel *levelForDimension(MinecraftServer *server, int dimension)
	{
		return server ? server->getLevel(dimension) : nullptr;
	}

	bool isOp(const std::shared_ptr<ServerPlayer> &player)
	{
		MinecraftServer *server = MinecraftServer::getInstance();
		if (!player || !server || !server->getPlayers()) return false;
		return server->getPlayers()->isOp(player);
	}

	std::wstring entityName(const std::shared_ptr<Entity> &e)
	{
		if (!e) return L"";
		if (e->instanceof(eTYPE_PLAYER))
		{
			std::shared_ptr<Player> p = std::dynamic_pointer_cast<Player>(e);
			if (p) return p->getName();
		}
		const std::wstring custom = e->getCustomName();
		if (!custom.empty()) return custom;
		const std::wstring id = EntityIO::getEncodeId(e);
		return id.empty() ? L"Entidade" : id;
	}

	std::wstring scoreHolderName(const std::shared_ptr<Entity> &e)
	{
		if (!e) return L"";
		if (e->instanceof(eTYPE_PLAYER)) return entityName(e);
		return L"#" + std::to_wstring(e->entityId);	// Java uses UUIDs for non-player holders
	}

	void teleportEntity(const std::shared_ptr<Entity> &e, ServerLevel *level, double x, double y, double z, float yRot, float xRot)
	{
		if (!e) return;
		(void)level;
		std::shared_ptr<ServerPlayer> sp = std::dynamic_pointer_cast<ServerPlayer>(e);
		if (sp && sp->connection)
		{
			sp->ride(nullptr);
			sp->connection->teleport(x, y, z, yRot, xRot);
		}
		else
		{
			e->moveTo(x, y, z, yRot, xRot);
		}
	}

	namespace
	{
		struct Range
		{
			bool has = false;
			double min = -1e300, max = 1e300;
			bool contains(double v) const { return !has || (v >= min && v <= max); }
		};

		bool parseRange(const std::wstring &s, Range &r)
		{
			r.has = true;
			const size_t dots = s.find(L"..");
			if (dots == std::wstring::npos)
			{
				double v;
				if (!parseDouble(s, v)) return false;
				r.min = r.max = v;
				return true;
			}
			const std::wstring a = s.substr(0, dots), b = s.substr(dots + 2);
			if (!a.empty() && !parseDouble(a, r.min)) return false;
			if (!b.empty() && !parseDouble(b, r.max)) return false;
			return true;
		}

		struct SelectorArg { std::wstring key, value; bool negate = false; };

		std::vector<SelectorArg> splitArgs(const std::wstring &body)
		{
			std::vector<SelectorArg> out;
			std::wstring cur;
			int depth = 0;
			auto flush = [&]()
			{
				const std::wstring t = trimString(cur);
				cur.clear();
				if (t.empty()) return;
				SelectorArg a;
				const size_t eq = t.find(L'=');
				if (eq == std::wstring::npos) { a.key = t; }
				else
				{
					a.key = trimString(t.substr(0, eq));
					a.value = trimString(t.substr(eq + 1));
					if (!a.value.empty() && a.value[0] == L'!') { a.negate = true; a.value = trimString(a.value.substr(1)); }
				}
				a.key = toLower(a.key);
				out.push_back(a);
			};
			for (wchar_t c : body)
			{
				if (c == L'{' || c == L'[') depth++;
				else if (c == L'}' || c == L']') depth--;
				if (c == L',' && depth == 0) { flush(); continue; }
				cur += c;
			}
			flush();
			return out;
		}

		int gameModeIdFromName(const std::wstring &s)
		{
			const std::wstring l = toLower(s);
			if (l == L"survival" || l == L"s" || l == L"0") return 0;
			if (l == L"creative" || l == L"c" || l == L"1") return 1;
			if (l == L"adventure" || l == L"a" || l == L"2") return 2;
			if (l == L"spectator" || l == L"sp" || l == L"3") return 3;
			return -1;
		}

		int playerGameModeId(const std::shared_ptr<Entity> &e)
		{
			std::shared_ptr<ServerPlayer> sp = std::dynamic_pointer_cast<ServerPlayer>(e);
			if (!sp || !sp->gameMode || !sp->gameMode->getGameModeForPlayer()) return -1;
			return sp->gameMode->getGameModeForPlayer()->getId();
		}

		std::vector<std::shared_ptr<Entity> > allPlayers(MinecraftServer *server)
		{
			std::vector<std::shared_ptr<Entity> > out;
			if (!server || !server->getPlayers()) return out;
			for (auto &p : server->getPlayers()->players)
			{
				if (p && !p->removed) out.push_back(p);
			}
			return out;
		}
	}

	bool selectEntities(Source &src, const std::wstring &selectorIn, std::vector<std::shared_ptr<Entity> > &out, bool playersOnly, std::wstring &error)
	{
		out.clear();
		std::wstring selector = trimString(selectorIn);
		if (selector.empty()) { error = L"Alvo vazio."; return false; }

		if (selector[0] != L'@')
		{
			// player name
			if (src.server && src.server->getPlayers())
			{
				for (auto &p : src.server->getPlayers()->players)
				{
					if (p && equalsIgnoreCase(p->getName(), selector)) { out.push_back(p); return true; }
				}
			}
			error = L"Jogador não encontrado: " + selector;
			return false;
		}

		if (selector.size() < 2) { error = L"Seletor inválido: " + selector; return false; }
		const wchar_t kind = towlower(selector[1]);
		std::wstring body;
		if (selector.size() > 2)
		{
			if (selector[2] != L'[' || selector.back() != L']') { error = L"Seletor inválido: " + selector; return false; }
			body = selector.substr(3, selector.size() - 4);
		}
		std::vector<SelectorArg> args = splitArgs(body);

		double bx = src.x, by = src.y, bz = src.z;
		Range distance, level, xRotR, yRotR;
		bool hasBox = false;
		double dx = 0, dy = 0, dz = 0;
		int limit = -1;
		std::wstring sort;
		std::vector<std::pair<std::wstring, bool> > types, names, tags, modes;
		std::vector<std::pair<std::wstring, Range> > scores;

		for (const SelectorArg &a : args)
		{
			if (a.key == L"x") { if (!parseDouble(a.value, bx)) { error = L"x inválido"; return false; } }
			else if (a.key == L"y") { if (!parseDouble(a.value, by)) { error = L"y inválido"; return false; } }
			else if (a.key == L"z") { if (!parseDouble(a.value, bz)) { error = L"z inválido"; return false; } }
			else if (a.key == L"distance") { if (!parseRange(a.value, distance)) { error = L"distance inválido"; return false; } }
			else if (a.key == L"r") { distance.has = true; if (!parseDouble(a.value, distance.max)) { error = L"r inválido"; return false; } }
			else if (a.key == L"rm") { distance.has = true; if (!parseDouble(a.value, distance.min)) { error = L"rm inválido"; return false; } }
			else if (a.key == L"dx") { hasBox = true; parseDouble(a.value, dx); }
			else if (a.key == L"dy") { hasBox = true; parseDouble(a.value, dy); }
			else if (a.key == L"dz") { hasBox = true; parseDouble(a.value, dz); }
			else if (a.key == L"limit" || a.key == L"c") { if (!parseInt(a.value, limit)) { error = L"limit inválido"; return false; } }
			else if (a.key == L"sort") sort = toLower(a.value);
			else if (a.key == L"type") types.push_back({ a.value, a.negate });
			else if (a.key == L"name") names.push_back({ a.value, a.negate });
			else if (a.key == L"tag") tags.push_back({ a.value, a.negate });
			else if (a.key == L"gamemode" || a.key == L"m") modes.push_back({ a.value, a.negate });
			else if (a.key == L"level") { if (!parseRange(a.value, level)) { error = L"level inválido"; return false; } }
			else if (a.key == L"l") { level.has = true; parseDouble(a.value, level.max); }
			else if (a.key == L"lm") { level.has = true; parseDouble(a.value, level.min); }
			else if (a.key == L"x_rotation") parseRange(a.value, xRotR);
			else if (a.key == L"y_rotation") parseRange(a.value, yRotR);
			else if (a.key == L"scores")
			{
				std::wstring inner = a.value;
				if (!inner.empty() && inner.front() == L'{') inner = inner.substr(1);
				if (!inner.empty() && inner.back() == L'}') inner.pop_back();
				for (const SelectorArg &s : splitArgs(inner))
				{
					Range r;
					if (!parseRange(s.value, r)) { error = L"scores inválido"; return false; }
					scores.push_back({ s.key, r });
				}
			}
			else { error = L"Argumento de seletor desconhecido: " + a.key; return false; }
		}

		// candidates
		std::vector<std::shared_ptr<Entity> > cand;
		bool sameDimensionOnly = distance.has || hasBox || kind == L'p';
		switch (kind)
		{
		case L's':
			if (src.entity) cand.push_back(src.entity);
			break;
		case L'p':
		case L'a':
		case L'r':
			cand = allPlayers(src.server);
			break;
		case L'e':
			if (playersOnly) cand = allPlayers(src.server);
			else if (src.level)
			{
				for (auto &e : src.level->getAllEntities())
				{
					if (e && !e->removed) cand.push_back(e);
				}
			}
			break;
		default:
			error = L"Seletor desconhecido: @" + std::wstring(1, kind);
			return false;
		}

		const int srcDim = src.level && src.level->dimension ? src.level->dimension->id : 0;
		for (auto &e : cand)
		{
			if (!e || e->removed) continue;
			if (playersOnly && !e->instanceof(eTYPE_PLAYER)) continue;
			if (sameDimensionOnly && e->level && e->level->dimension && e->level->dimension->id != srcDim) continue;
			if (kind != L's' && !e->isAlive() && e->instanceof(eTYPE_PLAYER)) continue;

			const double ddx = e->x - bx, ddy = e->y - by, ddz = e->z - bz;
			if (distance.has && !distance.contains(sqrt(ddx * ddx + ddy * ddy + ddz * ddz))) continue;
			if (hasBox)
			{
				const double x0 = std::min(bx, bx + dx), x1 = std::max(bx, bx + dx) + 1;
				const double y0 = std::min(by, by + dy), y1 = std::max(by, by + dy) + 1;
				const double z0 = std::min(bz, bz + dz), z1 = std::max(bz, bz + dz) + 1;
				if (e->x < x0 || e->x > x1 || e->y < y0 || e->y > y1 || e->z < z0 || e->z > z1) continue;
			}
			bool ok = true;
			for (auto &t : types)
			{
				const std::wstring want = resolveEntityType(t.first);
				const bool isPlayerType = toLower(stripNamespace(t.first)) == L"player";
				bool match = isPlayerType ? e->instanceof(eTYPE_PLAYER) : (!want.empty() && EntityIO::getEncodeId(e) == want);
				if (match == t.second) { ok = false; break; }
			}
			for (auto &n : names)
			{
				const bool match = entityName(e) == n.first;
				if (match == n.second) { ok = false; break; }
			}
			for (auto &t : tags)
			{
				const std::vector<std::wstring> &et = entityTags(e);
				const bool match = t.first.empty() ? !et.empty() : std::find(et.begin(), et.end(), t.first) != et.end();
				if (match == t.second) { ok = false; break; }
			}
			for (auto &m : modes)
			{
				const int want = gameModeIdFromName(m.first);
				const bool match = playerGameModeId(e) == want;
				if (match == m.second) { ok = false; break; }
			}
			if (ok && level.has)
			{
				std::shared_ptr<Player> p = std::dynamic_pointer_cast<Player>(e);
				if (!p || !level.contains(p->experienceLevel)) ok = false;
			}
			if (ok && xRotR.has && !xRotR.contains(e->xRot)) ok = false;
			if (ok && yRotR.has && !yRotR.contains(Mth::wrapDegrees(e->yRot))) ok = false;
			for (auto &s : scores)
			{
				auto obj = objectives().find(s.first);
				if (obj == objectives().end()) { ok = false; break; }
				auto sc = obj->second.scores.find(scoreHolderName(e));
				if (sc == obj->second.scores.end() || !s.second.contains(sc->second)) { ok = false; break; }
			}
			if (ok) out.push_back(e);
		}

		auto distTo = [&](const std::shared_ptr<Entity> &e)
		{
			const double ddx = e->x - bx, ddy = e->y - by, ddz = e->z - bz;
			return ddx * ddx + ddy * ddy + ddz * ddz;
		};
		Random *rnd = src.level ? src.level->random : nullptr;
		auto shuffle = [&]()
		{
			for (size_t i = out.size(); i > 1; i--)
			{
				const size_t j = rnd ? static_cast<size_t>(rnd->nextInt(static_cast<int>(i))) : 0;
				std::swap(out[i - 1], out[j]);
			}
		};
		if (sort.empty()) sort = (kind == L'p') ? L"nearest" : (kind == L'r') ? L"random" : L"arbitrary";
		if (sort == L"nearest") std::stable_sort(out.begin(), out.end(), [&](const std::shared_ptr<Entity> &a, const std::shared_ptr<Entity> &b) { return distTo(a) < distTo(b); });
		else if (sort == L"furthest") std::stable_sort(out.begin(), out.end(), [&](const std::shared_ptr<Entity> &a, const std::shared_ptr<Entity> &b) { return distTo(a) > distTo(b); });
		else if (sort == L"random") shuffle();

		if (limit < 0 && (kind == L'p' || kind == L'r')) limit = 1;
		if (limit >= 0 && static_cast<int>(out.size()) > limit) out.resize(limit);

		if (out.empty()) { error = L"Nenhuma entidade encontrada."; return false; }
		return true;
	}

	bool selectPlayers(Source &src, const std::wstring &selector, std::vector<std::shared_ptr<ServerPlayer> > &out, std::wstring &error)
	{
		out.clear();
		std::vector<std::shared_ptr<Entity> > ents;
		if (!selectEntities(src, selector, ents, true, error)) return false;
		for (auto &e : ents)
		{
			std::shared_ptr<ServerPlayer> p = std::dynamic_pointer_cast<ServerPlayer>(e);
			if (p) out.push_back(p);
		}
		if (out.empty()) { error = L"Nenhum jogador encontrado."; return false; }
		return true;
	}

	std::shared_ptr<ServerPlayer> singlePlayer(Source &src, const std::wstring &selector, std::wstring &error)
	{
		std::vector<std::shared_ptr<ServerPlayer> > players;
		if (!selectPlayers(src, selector, players, error)) return nullptr;
		if (players.size() > 1) { error = L"Somente um jogador é permitido, mas o seletor encontrou vários."; return nullptr; }
		return players[0];
	}

	// =================================================================================
	// Registries
	// =================================================================================
	namespace
	{
		IDMapping *ids()
		{
			IDMapping *m = IDMapping::get();
			if (m->stringToID.empty()) m->init();
			if (m->idToString.empty()) m->buildReverseMap();
			return m;
		}

		bool parseNumericId(const std::wstring &name, int &id, int &data)
		{
			const size_t colon = name.find(L':');
			if (colon == std::wstring::npos) { data = 0; return parseInt(name, id); }
			return parseInt(name.substr(0, colon), id) && parseInt(name.substr(colon + 1), data);
		}

		std::string narrow(const std::wstring &w)
		{
			std::string s;
			s.reserve(w.size());
			for (wchar_t c : w) s += (c < 128) ? static_cast<char>(c) : '?';
			return s;
		}

		// old 1.12 names / common aliases that are not in the Ruby table
		int blockAlias(const std::wstring &n, int &data)
		{
			struct A { const wchar_t *name; int id; int data; };
			static const A aliases[] = {
				{ L"air", 0, 0 }, { L"water", Tile::water_Id, 0 }, { L"lava", Tile::lava_Id, 0 },
				{ L"flowing_water", Tile::water_Id, 0 }, { L"flowing_lava", Tile::lava_Id, 0 },
				{ L"command_block", Tile::commandBlock_Id, 0 },
				{ L"repeating_command_block", 210, 0 }, { L"chain_command_block", 211, 0 },
				{ L"barrier", Tile::barrier_Id, 0 }, { L"bedrock", Tile::unbreakable_Id, 0 },
				{ L"grass", Tile::grass_Id, 0 }, { L"grass_block", Tile::grass_Id, 0 },
				{ L"red_sand", Tile::sand_Id, 1 }, { L"podzol", Tile::dirt_Id, 2 }, { L"coarse_dirt", Tile::dirt_Id, 1 },
				{ L"packed_ice", Tile::packedIce_Id, 0 }, { L"prismarine", Tile::prismarine_Id, 0 },
				{ L"sea_lantern", Tile::seaLantern_Id, 0 }, { L"hay_block", Tile::hayBlock_Id, 0 },
				{ L"terracotta", Tile::clayHardened_Id, 0 }, { L"hardened_clay", Tile::clayHardened_Id, 0 },
				{ L"coal_block", Tile::coalBlock_Id, 0 }, { L"red_sandstone", Tile::red_sandstone_Id, 0 },
				{ L"fire", Tile::fire_Id, 0 }, { L"spawner", Tile::mobSpawner_Id, 0 }, { L"mob_spawner", Tile::mobSpawner_Id, 0 },
			};
			for (const A &a : aliases) if (n == a.name) { data = a.data; return a.id; }
			return -1;
		}
	}

	bool resolveBlock(const std::wstring &nameIn, int &id, int &data)
	{
		std::wstring name = trimString(nameIn);
		const size_t bracket = name.find(L'[');
		if (bracket != std::wstring::npos) name = name.substr(0, bracket);
		const size_t brace = name.find(L'{');
		if (brace != std::wstring::npos) name = name.substr(0, brace);
		if (parseNumericId(name, id, data)) return id >= 0 && id < 4096 && (id == 0 || Tile::tiles[id] != nullptr);

		const std::wstring shortName = toLower(stripNamespace(name));
		const int alias = blockAlias(shortName, data);
		if (alias >= 0) { id = alias; return id == 0 || Tile::tiles[id] != nullptr; }

		std::string key = narrow(name.find(L':') == std::wstring::npos ? L"minecraft:" + toLower(name) : toLower(name));
		IDMapping::MappedItem m = ids()->getID(key);
		if (m.id <= 0 || !m.isBlock) return false;
		id = m.id;
		data = m.aux;
		return Tile::tiles[id] != nullptr;
	}

	bool resolveItem(const std::wstring &nameIn, int &id, int &aux)
	{
		std::wstring name = trimString(nameIn);
		const size_t brace = name.find(L'{');
		if (brace != std::wstring::npos) name = name.substr(0, brace);
		const size_t bracket = name.find(L'[');
		if (bracket != std::wstring::npos) name = name.substr(0, bracket);
		if (parseNumericId(name, id, aux)) return id > 0 && id < 32000 && Item::items[id] != nullptr;

		const std::wstring shortName = toLower(stripNamespace(name));
		const int alias = blockAlias(shortName, aux);
		if (alias > 0 && Item::items[alias] != nullptr) { id = alias; return true; }

		std::string key = narrow(name.find(L':') == std::wstring::npos ? L"minecraft:" + toLower(name) : toLower(name));
		IDMapping::MappedItem m = ids()->getID(key);
		if (m.id <= 0) return false;
		id = m.id;
		aux = m.aux;
		return Item::items[id] != nullptr;
	}

	std::wstring blockName(int id, int data)
	{
		std::string s = ids()->getByID(id, data);
		if (s.empty()) return std::to_wstring(id) + L":" + std::to_wstring(data);
		return convStringToWstring(s);
	}

	std::wstring itemName(int id, int aux)
	{
		return blockName(id, aux);
	}

	std::wstring resolveEntityType(const std::wstring &nameIn)
	{
		static std::unordered_map<std::wstring, std::wstring> table;
		static std::once_flag once;
		std::call_once(once, []()
		{
			for (int i = 1; i < 256; i++)
			{
				const std::wstring id = EntityIO::getEncodeId(i);
				if (!id.empty()) table[toLower(id)] = id;
			}
			struct A { const wchar_t *modern; const wchar_t *legacy; };
			static const A aliases[] = {
				{ L"zombie_pigman", L"PigZombie" }, { L"zombified_piglin", L"PigZombie" }, { L"magma_cube", L"LavaSlime" },
				{ L"mooshroom", L"MushroomCow" }, { L"ocelot", L"Ozelot" }, { L"cat", L"Ozelot" }, { L"iron_golem", L"VillagerGolem" },
				{ L"snow_golem", L"SnowMan" }, { L"horse", L"EntityHorse" }, { L"donkey", L"EntityHorse" }, { L"mule", L"EntityHorse" },
				{ L"skeleton_horse", L"EntityHorse" }, { L"zombie_horse", L"EntityHorse" }, { L"ender_dragon", L"EnderDragon" },
				{ L"wither", L"WitherBoss" }, { L"cave_spider", L"CaveSpider" }, { L"tnt", L"PrimedTnt" }, { L"falling_block", L"FallingSand" },
				{ L"item", L"Item" }, { L"experience_orb", L"XPOrb" }, { L"xp_orb", L"XPOrb" }, { L"armor_stand", L"ArmorStand" },
				{ L"elder_guardian", L"ElderGuardian" }, { L"end_crystal", L"EnderCrystal" }, { L"ender_crystal", L"EnderCrystal" },
				{ L"leash_knot", L"LeashKnot" }, { L"item_frame", L"ItemFrame" }, { L"wither_skull", L"WitherSkull" },
				{ L"small_fireball", L"SmallFireball" }, { L"ender_pearl", L"ThrownEnderpearl" }, { L"eye_of_ender", L"EyeOfEnderSignal" },
				{ L"potion", L"ThrownPotion" }, { L"experience_bottle", L"ThrownExpBottle" }, { L"firework_rocket", L"FireworksRocketEntity" },
				{ L"minecart", L"MinecartRideable" }, { L"chest_minecart", L"MinecartChest" }, { L"furnace_minecart", L"MinecartFurnace" },
				{ L"tnt_minecart", L"MinecartTNT" }, { L"hopper_minecart", L"MinecartHopper" }, { L"spawner_minecart", L"MinecartSpawner" },
				{ L"dragon_fireball", L"DragonFireball" },
			};
			for (const A &a : aliases) table[a.modern] = a.legacy;
		});
		std::wstring n = toLower(stripNamespace(trimString(nameIn)));
		auto it = table.find(n);
		if (it != table.end()) return it->second;
		// "zombievillager" style without underscores
		std::wstring compact;
		for (wchar_t c : n) if (c != L'_') compact += c;
		it = table.find(compact);
		return it != table.end() ? it->second : L"";
	}

	int resolveEffect(const std::wstring &nameIn)
	{
		struct E { const wchar_t *name; int id; };
		static const E effects[] = {
			{ L"speed", 1 }, { L"slowness", 2 }, { L"haste", 3 }, { L"mining_fatigue", 4 }, { L"strength", 5 },
			{ L"instant_health", 6 }, { L"instant_damage", 7 }, { L"jump_boost", 8 }, { L"nausea", 9 }, { L"regeneration", 10 },
			{ L"resistance", 11 }, { L"fire_resistance", 12 }, { L"water_breathing", 13 }, { L"invisibility", 14 }, { L"blindness", 15 },
			{ L"night_vision", 16 }, { L"hunger", 17 }, { L"weakness", 18 }, { L"poison", 19 }, { L"wither", 20 },
			{ L"health_boost", 21 }, { L"absorption", 22 }, { L"saturation", 23 },
		};
		const std::wstring n = toLower(stripNamespace(trimString(nameIn)));
		int id;
		if (parseInt(n, id)) return (id > 0 && id < MobEffect::NUM_EFFECTS && MobEffect::effects[id] != nullptr) ? id : -1;
		for (const E &e : effects) if (n == e.name) return MobEffect::effects[e.id] != nullptr ? e.id : -1;
		return -1;
	}

	int resolveEnchantment(const std::wstring &nameIn)
	{
		struct E { const wchar_t *name; int id; };
		static const E ench[] = {
			{ L"protection", 0 }, { L"fire_protection", 1 }, { L"feather_falling", 2 }, { L"blast_protection", 3 },
			{ L"projectile_protection", 4 }, { L"respiration", 5 }, { L"aqua_affinity", 6 }, { L"thorns", 7 }, { L"depth_strider", 8 },
			{ L"sharpness", 16 }, { L"smite", 17 }, { L"bane_of_arthropods", 18 }, { L"knockback", 19 }, { L"fire_aspect", 20 },
			{ L"looting", 21 }, { L"efficiency", 32 }, { L"silk_touch", 33 }, { L"unbreaking", 34 }, { L"fortune", 35 },
			{ L"power", 48 }, { L"punch", 49 }, { L"flame", 50 }, { L"infinity", 51 }, { L"luck_of_the_sea", 61 }, { L"lure", 62 },
		};
		const std::wstring n = toLower(stripNamespace(trimString(nameIn)));
		int id = -1;
		if (!parseInt(n, id))
		{
			id = -1;
			for (const E &e : ench) if (n == e.name) { id = e.id; break; }
		}
		if (id < 0 || id >= 256 || Enchantment::enchantments[id] == nullptr) return -1;
		return id;
	}

	// =================================================================================
	// Dispatch
	// =================================================================================
	Source fromPlayer(const std::shared_ptr<ServerPlayer> &player)
	{
		Source s;
		s.server = MinecraftServer::getInstance();
		s.player = player;
		s.entity = player;
		if (player)
		{
			s.level = static_cast<ServerLevel *>(player->level);
			s.x = player->x;
			s.y = player->y;
			s.z = player->z;
			s.yRot = player->yRot;
			s.xRot = player->xRot;
			s.name = player->getName();
			s.permission = isOp(player) ? 4 : 0;
		}
		return s;
	}

	int execute(Source &src, const std::wstring &commandLine)
	{
		std::wstring line = trimString(commandLine);
		while (!line.empty() && line[0] == L'/') line = line.substr(1);
		if (line.empty()) return 0;

		size_t sp = 0;
		while (sp < line.size() && !iswspace(line[sp])) sp++;
		const std::wstring name = line.substr(0, sp);
		const std::wstring rest = sp < line.size() ? trimString(line.substr(sp)) : L"";

		const CommandInfo *info = findCommand(name);
		if (info == nullptr) return fail(src, L"Comando desconhecido: /" + name + L" (use /help)");
		if (src.permission < info->permission)
		{
			if (!app.GetGameHostOption(eGameHostOption_CheatsEnabled))
				return fail(src, L"Os comandos estão desativados neste mundo (ative \"Permitir trapaças\" nas opções do anfitrião).");
			return fail(src, L"Você não tem permissão para usar este comando.");
		}

		Args args;
		args.raw = rest;
		args.t = tokenize(rest, &args.offsets);
		try
		{
			return info->handler(src, args);
		}
		catch (...)
		{
			return fail(src, std::wstring(L"Erro ao executar /") + info->name + L".");
		}
	}
}

// =====================================================================================
// Command blocks
// =====================================================================================
#include "../../Minecraft.World/CommandBlockEntity.h"

namespace
{
	int commandBlockExecutor(CommandBlockEntity *block, Level *level, const std::wstring &command, std::wstring &output)
	{
		HSCommands::Source src;
		src.server = MinecraftServer::getInstance();
		src.level = static_cast<ServerLevel *>(level);
		src.x = block->x + 0.5;
		src.y = block->y + 0.5;
		src.z = block->z + 0.5;
		src.permission = 2;
		src.commandBlock = true;
		std::wstring name = block->getName();
		src.name = name.empty() ? L"@" : name;
		src.output = [&output](const std::wstring &message, bool error)
		{
			output = message;
			(void)error;
		};
		return HSCommands::execute(src, command);
	}

	struct CommandBlockHook
	{
		CommandBlockHook() { CommandBlockEntity::s_executor = &commandBlockExecutor; }
	} g_commandBlockHook;
}
