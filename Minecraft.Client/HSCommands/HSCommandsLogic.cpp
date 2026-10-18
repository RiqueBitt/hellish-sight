// Hellish Sight - logic commands: /execute, /scoreboard, /trigger, /function, /schedule,
// /data, /team, /help, /random, /version, /reload, plus the tag / score storage.

#include "stdafx.h"
#include "HSCommandsInternal.h"

#include "../MinecraftServer.h"
#include "../ServerLevel.h"
#include "../ServerPlayer.h"
#include "../PlayerList.h"
#include "../../Minecraft.World/net.minecraft.world.level.h"
#include "../../Minecraft.World/net.minecraft.world.level.dimension.h"
#include "../../Minecraft.World/net.minecraft.world.entity.h"
#include "../../Minecraft.World/net.minecraft.world.entity.player.h"
#include "../../Minecraft.World/StringHelpers.h"
#include "../../Minecraft.World/EntityIO.h"
#include "../../Minecraft.World/ChatPacket.h"
#include "../../Minecraft.World/Mth.h"
#include "../../Minecraft.World/LevelData.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>

namespace HSCommands
{
	namespace
	{
		const CommandInfo *self(const wchar_t *name) { return findCommand(name); }
	}

	// =================================================================================
	// Storage
	// =================================================================================
	std::unordered_map<std::wstring, Objective> &objectives()
	{
		static std::unordered_map<std::wstring, Objective> o;
		return o;
	}

	std::wstring &sidebarObjective()
	{
		static std::wstring s;
		return s;
	}

	std::vector<std::wstring> &entityTags(const std::shared_ptr<Entity> &e)
	{
		static std::unordered_map<int, std::vector<std::wstring> > tags;
		static std::vector<std::wstring> none;
		if (!e) { none.clear(); return none; }
		return tags[e->entityId];
	}

	// =================================================================================
	// /help
	// =================================================================================
	int cmdHelp(Source &src, const Args &a)
	{
		if (a.size() >= 1)
		{
			const CommandInfo *info = findCommand(a[0]);
			if (!info) return fail(src, L"Comando desconhecido: " + a[0]);
			feedback(src, std::wstring(L"/") + info->name + L" " + info->usage);
			return 1;
		}
		std::wstring s;
		int n = 0;
		for (const std::wstring &c : commandNames())
		{
			const CommandInfo *info = findCommand(c);
			if (info && src.permission < info->permission) continue;
			if (!s.empty()) s += L", ";
			s += c;
			n++;
		}
		feedback(src, L"Comandos (" + std::to_wstring(n) + L"): " + s);
		feedback(src, L"Use /help <comando> para ver como usar.");
		return n;
	}

	int cmdVersion(Source &src, const Args &a)
	{
		(void)a;
		feedback(src, L"Hellish Sight - Minecraft Legacy Console Edition (base neoLegacy TU31), comandos estilo Java Edition");
		return 1;
	}

	int cmdRandom(Source &src, const Args &a)
	{
		if (a.size() < 2) return usage(src, self(L"random"));
		const std::wstring op = toLower(a[0]);
		if (op != L"value" && op != L"roll") return usage(src, self(L"random"));
		const std::wstring r = a[1];
		const size_t dots = r.find(L"..");
		int lo, hi;
		if (dots == std::wstring::npos || !parseInt(r.substr(0, dots), lo) || !parseInt(r.substr(dots + 2), hi) || hi < lo) return fail(src, L"Intervalo inv\u00E1lido (ex.: 1..6)");
		Random *rnd = src.level ? src.level->random : nullptr;
		const int v = lo + (rnd ? rnd->nextInt(hi - lo + 1) : 0);
		if (op == L"roll" && src.server && src.server->getPlayers())
			src.server->getPlayers()->broadcastAll(std::make_shared<ChatPacket>(src.name + L" rolou " + std::to_wstring(v) + L" (de " + std::to_wstring(lo) + L" a " + std::to_wstring(hi) + L")"));
		else feedback(src, L"Valor aleat\u00F3rio: " + std::to_wstring(v));
		return v;
	}

	// =================================================================================
	// /scoreboard
	// =================================================================================
	namespace
	{
		bool holders(Source &src, const std::wstring &sel, std::vector<std::wstring> &out)
		{
			out.clear();
			if (sel == L"*")
			{
				std::map<std::wstring, bool> all;
				for (auto &o : objectives()) for (auto &s : o.second.scores) all[s.first] = true;
				for (auto &kv : all) out.push_back(kv.first);
				return true;
			}
			if (!sel.empty() && sel[0] == L'@')
			{
				std::vector<std::shared_ptr<Entity> > es;
				std::wstring err;
				if (!selectEntities(src, sel, es, false, err)) { fail(src, err); return false; }
				for (auto &e : es) out.push_back(scoreHolderName(e));
				return true;
			}
			out.push_back(sel);	// fake players / names
			return true;
		}

		Objective *objective(Source &src, const std::wstring &name)
		{
			auto it = objectives().find(name);
			if (it == objectives().end()) { fail(src, L"Objetivo desconhecido: " + name); return nullptr; }
			return &it->second;
		}

		void showSidebar(Source &src)
		{
			// No sidebar UI in this game: show the objective in chat when it changes
			if (sidebarObjective().empty()) return;
			auto it = objectives().find(sidebarObjective());
			if (it == objectives().end()) return;
			std::vector<std::pair<int, std::wstring> > rows;
			for (auto &s : it->second.scores) rows.push_back({ s.second, s.first });
			std::sort(rows.rbegin(), rows.rend());
			std::wstring msg = L"\u00A7e" + it->second.displayName + L":";
			int n = 0;
			for (auto &r : rows) { if (n++ >= 15) break; msg += L"  " + r.second + L" \u00A7c" + std::to_wstring(r.first) + L"\u00A7e"; }
			(void)src;
		}
	}

	int cmdScoreboard(Source &src, const Args &a)
	{
		const CommandInfo *info = self(L"scoreboard");
		if (a.size() < 2) return usage(src, info);
		const std::wstring group = toLower(a[0]), op = toLower(a[1]);
		if (group == L"objectives")
		{
			if (op == L"list")
			{
				std::wstring s;
				for (auto &o : objectives()) { if (!s.empty()) s += L", "; s += o.first + L" (" + o.second.criteria + L")"; }
				feedback(src, L"Objetivos (" + std::to_wstring(objectives().size()) + L"): " + s);
				return static_cast<int>(objectives().size());
			}
			if (op == L"add")
			{
				if (!a.has(3)) return usage(src, info);
				if (objectives().count(a[2])) return fail(src, L"J\u00E1 existe um objetivo com esse nome.");
				Objective o;
				o.criteria = a[3];
				o.displayName = a.has(4) ? a.rest(4) : a[2];
				objectives()[a[2]] = o;
				feedback(src, L"Objetivo criado: " + a[2]);
				return 1;
			}
			if (op == L"remove")
			{
				if (!a.has(2) || objectives().erase(a[2]) == 0) return fail(src, L"Objetivo desconhecido.");
				if (sidebarObjective() == a[2]) sidebarObjective().clear();
				feedback(src, L"Objetivo removido: " + a[2]);
				return 1;
			}
			if (op == L"setdisplay")
			{
				if (!a.has(2)) return usage(src, info);
				if (a.has(3)) { if (!objectives().count(a[3])) return fail(src, L"Objetivo desconhecido."); sidebarObjective() = a[3]; }
				else sidebarObjective().clear();
				showSidebar(src);
				feedback(src, L"Exibi\u00E7\u00E3o definida (este jogo mostra o placar pelo chat com /scoreboard players list)");
				return 1;
			}
			if (op == L"modify" && a.has(4))
			{
				Objective *o = objective(src, a[2]);
				if (!o) return 0;
				if (toLower(a[3]) == L"displayname") o->displayName = a.rest(4);
				feedback(src, L"Objetivo alterado");
				return 1;
			}
			return usage(src, info);
		}
		if (group != L"players") return usage(src, info);

		if (op == L"list")
		{
			if (!a.has(2))
			{
				std::map<std::wstring, bool> all;
				for (auto &o : objectives()) for (auto &s : o.second.scores) all[s.first] = true;
				std::wstring s;
				for (auto &kv : all) { if (!s.empty()) s += L", "; s += kv.first; }
				feedback(src, L"Com pontua\u00E7\u00E3o: " + s);
				return static_cast<int>(all.size());
			}
			std::vector<std::wstring> hs;
			if (!holders(src, a[2], hs)) return 0;
			int n = 0;
			for (auto &h : hs)
			{
				std::wstring s;
				for (auto &o : objectives())
				{
					auto it = o.second.scores.find(h);
					if (it != o.second.scores.end()) { if (!s.empty()) s += L", "; s += o.first + L"=" + std::to_wstring(it->second); n++; }
				}
				feedback(src, h + L": " + (s.empty() ? L"(nada)" : s));
			}
			return n;
		}

		if (!a.has(2)) return usage(src, info);
		std::vector<std::wstring> hs;
		if (!holders(src, a[2], hs)) return 0;

		if (op == L"reset")
		{
			int n = 0;
			for (auto &o : objectives())
			{
				if (a.has(3) && o.first != a[3]) continue;
				for (auto &h : hs) n += static_cast<int>(o.second.scores.erase(h));
			}
			feedback(src, L"Pontua\u00E7\u00F5es apagadas");
			return (std::max)(1, n);
		}
		if (op == L"enable") { feedback(src, L"Trigger liberado"); return 1; }
		if (!a.has(3)) return usage(src, info);
		Objective *o = objective(src, a[3]);
		if (!o) return 0;

		if (op == L"get")
		{
			if (hs.size() != 1) return fail(src, L"Somente um alvo.");
			auto it = o->scores.find(hs[0]);
			if (it == o->scores.end()) return fail(src, hs[0] + L" n\u00E3o tem pontua\u00E7\u00E3o em " + a[3]);
			feedback(src, hs[0] + L" tem " + std::to_wstring(it->second) + L" [" + o->displayName + L"]");
			return it->second;
		}
		if (op == L"set" || op == L"add" || op == L"remove")
		{
			int v;
			if (!a.has(4) || !parseInt(a[4], v)) return usage(src, info);
			for (auto &h : hs)
			{
				int &s = o->scores[h];
				if (op == L"set") s = v; else if (op == L"add") s += v; else s -= v;
			}
			showSidebar(src);
			feedback(src, L"Pontua\u00E7\u00E3o de " + std::to_wstring(hs.size()) + L" alvo(s) alterada em " + a[3]);
			return static_cast<int>(hs.size());
		}
		if (op == L"operation")
		{
			// players operation <targets> <obj> <op> <source> <sourceObj>
			if (!a.has(6)) return usage(src, info);
			std::vector<std::wstring> srcHs;
			if (!holders(src, a[5], srcHs)) return 0;
			Objective *so = objective(src, a[6]);
			if (!so) return 0;
			const std::wstring oper = a[4];
			for (auto &h : hs)
			{
				for (auto &sh : srcHs)
				{
					int &t = o->scores[h];
					int &sv = so->scores[sh];
					if (oper == L"=") t = sv;
					else if (oper == L"+=") t += sv;
					else if (oper == L"-=") t -= sv;
					else if (oper == L"*=") t *= sv;
					else if (oper == L"/=") { if (sv != 0) t = static_cast<int>(floor(static_cast<double>(t) / sv)); }
					else if (oper == L"%=") { if (sv != 0) { t %= sv; if (t < 0) t += std::abs(sv); } }
					else if (oper == L"<") t = (std::min)(t, sv);
					else if (oper == L">") t = (std::max)(t, sv);
					else if (oper == L"><") std::swap(t, sv);
					else return fail(src, L"Opera\u00E7\u00E3o inv\u00E1lida: " + oper);
				}
			}
			showSidebar(src);
			return static_cast<int>(hs.size());
		}
		return usage(src, info);
	}

	int cmdTrigger(Source &src, const Args &a)
	{
		if (a.size() < 1 || !src.entity) return usage(src, self(L"trigger"));
		auto it = objectives().find(a[0]);
		if (it == objectives().end() || toLower(it->second.criteria) != L"trigger") return fail(src, L"Objetivo de trigger inv\u00E1lido: " + a[0]);
		int v = 1;
		std::wstring mode = L"add";
		if (a.has(2)) { mode = toLower(a[1]); parseInt(a[2], v); }
		int &s = it->second.scores[scoreHolderName(src.entity)];
		if (mode == L"set") s = v; else s += v;
		feedback(src, L"Trigger " + a[0] + L" acionado");
		return 1;
	}

	int cmdTeam(Source &src, const Args &a)
	{
		static std::map<std::wstring, std::vector<std::wstring> > teams;
		if (a.size() < 1) return usage(src, self(L"team"));
		const std::wstring op = toLower(a[0]);
		if (op == L"list")
		{
			std::wstring s;
			for (auto &t : teams) { if (!s.empty()) s += L", "; s += t.first + L" (" + std::to_wstring(t.second.size()) + L")"; }
			feedback(src, L"Times: " + s);
			return static_cast<int>(teams.size());
		}
		if (!a.has(1)) return usage(src, self(L"team"));
		if (op == L"add") { teams[a[1]]; feedback(src, L"Time criado: " + a[1]); return 1; }
		if (op == L"remove") { teams.erase(a[1]); feedback(src, L"Time removido: " + a[1]); return 1; }
		if (op == L"empty") { teams[a[1]].clear(); return 1; }
		if (op == L"join" || op == L"leave")
		{
			std::vector<std::wstring> hs;
			const std::wstring sel = op == L"join" ? (a.has(2) ? a[2] : L"@s") : a[1];
			if (!holders(src, sel, hs)) return 0;
			for (auto &t : teams) for (auto &h : hs) t.second.erase(std::remove(t.second.begin(), t.second.end(), h), t.second.end());
			if (op == L"join") for (auto &h : hs) teams[a[1]].push_back(h);
			feedback(src, op == L"join" ? L"Entrou no time " + a[1] : L"Saiu do time");
			return static_cast<int>(hs.size());
		}
		if (op == L"modify") { feedback(src, L"Op\u00E7\u00F5es de time n\u00E3o t\u00EAm efeito neste jogo."); return 1; }
		return usage(src, self(L"team"));
	}

	// =================================================================================
	// /data get entity <target> [path]
	// =================================================================================
	int cmdData(Source &src, const Args &a)
	{
		if (a.size() < 3 || toLower(a[0]) != L"get") return fail(src, L"Somente /data get entity <alvo> [Pos|Health|Rotation|...] \u00E9 suportado.");
		if (toLower(a[1]) != L"entity") return fail(src, L"Somente entidades: /data get entity <alvo>");
		std::vector<std::shared_ptr<Entity> > es;
		std::wstring err;
		if (!selectEntities(src, a[2], es, false, err)) return fail(src, err);
		if (es.size() != 1) return fail(src, L"Somente uma entidade.");
		std::shared_ptr<Entity> e = es[0];
		const std::wstring path = a.has(3) ? a[3] : L"";
		std::wstring s = entityName(e) + L": {";
		bool any = false;
		auto field = [&](const wchar_t *k, const std::wstring &v)
		{
			if (!path.empty() && path != k) return;
			if (any) s += L", ";
			s += std::wstring(k) + L": " + v;
			any = true;
		};
		field(L"id", L"\"" + EntityIO::getEncodeId(e) + L"\"");
		field(L"Pos", L"[" + std::to_wstring(e->x) + L"d, " + std::to_wstring(e->y) + L"d, " + std::to_wstring(e->z) + L"d]");
		field(L"Rotation", L"[" + std::to_wstring(e->yRot) + L"f, " + std::to_wstring(e->xRot) + L"f]");
		field(L"Dimension", std::to_wstring(e->dimension));
		std::shared_ptr<LivingEntity> le = std::dynamic_pointer_cast<LivingEntity>(e);
		if (le) field(L"Health", std::to_wstring(le->getHealth()) + L"f");
		std::shared_ptr<Player> p = std::dynamic_pointer_cast<Player>(e);
		if (p) { field(L"XpLevel", std::to_wstring(p->experienceLevel)); field(L"SelectedItemSlot", std::to_wstring(p->inventory->selected)); }
		s += L"}";
		if (!any) return fail(src, L"Caminho desconhecido: " + path);
		feedback(src, s);
		return 1;
	}

	// =================================================================================
	// Functions (/function, /schedule, /reload)
	// =================================================================================
	std::wstring functionsFolder()
	{
		return L"functions";	// next to Minecraft.Client.exe: functions/<namespace>/<path>.mcfunction
	}

	namespace
	{
		std::string narrowPath(const std::wstring &w)
		{
			std::string s;
			for (wchar_t c : w) s += (c < 128) ? static_cast<char>(c) : '_';
			return s;
		}

		bool readFunction(const std::wstring &name, std::vector<std::wstring> &lines)
		{
			std::wstring ns = L"minecraft", path = name;
			const size_t c = name.find(L':');
			if (c != std::wstring::npos) { ns = name.substr(0, c); path = name.substr(c + 1); }
			if (path.find(L"..") != std::wstring::npos) return false;
			const std::string file = narrowPath(functionsFolder() + L"/" + ns + L"/" + path + L".mcfunction");
			FILE *f = nullptr;
			if (fopen_s(&f, file.c_str(), "rb") != 0 || !f) return false;
			std::string data;
			char buf[4096];
			size_t n;
			while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
			fclose(f);
			std::wstring w = convStringToWstring(data);
			size_t start = 0;
			while (start <= w.size())
			{
				size_t end = w.find(L'\n', start);
				if (end == std::wstring::npos) end = w.size();
				std::wstring line = trimString(w.substr(start, end - start));
				if (!line.empty() && line[0] != L'#') lines.push_back(line);
				start = end + 1;
			}
			return true;
		}

		int &functionDepth() { static int d = 0; return d; }

		struct Scheduled { std::wstring name; long long due; };
		std::deque<Scheduled> &scheduled() { static std::deque<Scheduled> q; return q; }
		long long &tickCounter() { static long long t = 0; return t; }
	}

	int runFunction(Source &src, const std::wstring &name)
	{
		std::vector<std::wstring> lines;
		if (!readFunction(name, lines)) return fail(src, L"Fun\u00E7\u00E3o n\u00E3o encontrada: " + name + L" (pasta functions/<namespace>/<nome>.mcfunction)");
		if (functionDepth() > 32) return fail(src, L"Fun\u00E7\u00F5es aninhadas demais.");
		functionDepth()++;
		int total = 0;
		Source child = src;
		child.silent = true;
		for (auto &l : lines) total += execute(child, l) > 0 ? 1 : 0;
		functionDepth()--;
		feedback(src, L"Executados " + std::to_wstring(lines.size()) + L" comandos da fun\u00E7\u00E3o " + name);
		return total;
	}

	void scheduleFunction(const std::wstring &name, long long delayTicks, bool replace)
	{
		if (replace)
		{
			auto &q = scheduled();
			q.erase(std::remove_if(q.begin(), q.end(), [&](const Scheduled &s) { return s.name == name; }), q.end());
		}
		scheduled().push_back({ name, tickCounter() + (std::max)(1LL, delayTicks) });
	}

	void tick(MinecraftServer *server)
	{
		tickCounter()++;
		auto &q = scheduled();
		if (q.empty() || !server) return;
		std::vector<std::wstring> due;
		for (auto it = q.begin(); it != q.end();)
		{
			if (it->due <= tickCounter()) { due.push_back(it->name); it = q.erase(it); }
			else ++it;
		}
		for (auto &name : due)
		{
			Source s;
			s.server = server;
			s.level = server->getLevel(0);
			if (s.level)
			{
				s.x = s.level->getLevelData()->getXSpawn();
				s.y = s.level->getLevelData()->getYSpawn();
				s.z = s.level->getLevelData()->getZSpawn();
			}
			s.name = L"Servidor";
			s.silent = true;
			runFunction(s, name);
		}
	}

	int cmdFunction(Source &src, const Args &a)
	{
		if (a.size() < 1) return usage(src, self(L"function"));
		return runFunction(src, a[0]);
	}

	int cmdSchedule(Source &src, const Args &a)
	{
		if (a.size() < 2) return usage(src, self(L"schedule"));
		const std::wstring op = toLower(a[0]);
		if (op == L"clear")
		{
			auto &q = scheduled();
			const size_t before = q.size();
			q.erase(std::remove_if(q.begin(), q.end(), [&](const Scheduled &s) { return s.name == a[1]; }), q.end());
			feedback(src, L"Agendamentos removidos: " + std::to_wstring(before - q.size()));
			return static_cast<int>(before - q.size());
		}
		if (op != L"function" || !a.has(2)) return usage(src, self(L"schedule"));
		std::wstring t = toLower(a[2]);
		double mul = 1;
		if (!t.empty() && (t.back() == L'd' || t.back() == L's' || t.back() == L't')) { mul = t.back() == L'd' ? 24000 : (t.back() == L's' ? 20 : 1); t.pop_back(); }
		double n;
		if (!parseDouble(t, n) || n <= 0) return fail(src, L"Tempo inv\u00E1lido: " + a[2]);
		const bool replace = !a.has(3) || toLower(a[3]) != L"append";
		scheduleFunction(a[1], static_cast<long long>(n * mul), replace);
		feedback(src, L"Fun\u00E7\u00E3o " + a[1] + L" agendada para daqui a " + std::to_wstring(static_cast<long long>(n * mul)) + L" ticks");
		return 1;
	}

	int cmdReload(Source &src, const Args &a)
	{
		(void)a;
		feedback(src, L"Fun\u00E7\u00F5es s\u00E3o lidas da pasta functions a cada uso; nada para recarregar.");
		return 1;
	}

	// =================================================================================
	// /execute
	// =================================================================================
	namespace
	{
		struct ExecState
		{
			std::vector<Source> ctx;
		};

		bool blockMatches(Source &s, const Args &a, size_t i, bool &match)
		{
			int x, y, z, id, data;
			if (!parseBlockPos(s, a, i, x, y, z)) return false;
			if (!resolveBlock(a[i + 3], id, data)) return false;
			const bool anyData = a[i + 3].find(L'[') == std::wstring::npos && a[i + 3].find(L':') == std::wstring::npos;
			ServerLevel *l = s.level;
			match = l && y >= 0 && y < Level::maxBuildHeight && l->getTile(x, y, z) == id && (anyData || l->getData(x, y, z) == data);
			return true;
		}

		bool rangeMatch(const std::wstring &r, int v)
		{
			const size_t dots = r.find(L"..");
			if (dots == std::wstring::npos) { int n; return parseInt(r, n) && n == v; }
			int lo = INT_MIN, hi = INT_MAX;
			if (dots > 0 && !parseInt(r.substr(0, dots), lo)) return false;
			if (dots + 2 < r.size() && !parseInt(r.substr(dots + 2), hi)) return false;
			return v >= lo && v <= hi;
		}

		std::vector<std::wstring> holderNames(Source &s, const std::wstring &sel)
		{
			std::vector<std::wstring> out;
			if (!sel.empty() && sel[0] == L'@')
			{
				std::vector<std::shared_ptr<Entity> > es;
				std::wstring err;
				if (selectEntities(s, sel, es, false, err)) for (auto &e : es) out.push_back(scoreHolderName(e));
			}
			else out.push_back(sel);
			return out;
		}
	}

	int cmdExecute(Source &src, const Args &a)
	{
		const CommandInfo *info = self(L"execute");
		if (a.size() < 1) return usage(src, info);

		// 1.12 syntax: execute <entity> <x> <y> <z> [detect <x y z> <block> <data>] <command>
		const std::wstring first = toLower(a[0]);
		static const wchar_t *keywords[] = { L"as", L"at", L"positioned", L"rotated", L"facing", L"align", L"anchored", L"in", L"if", L"unless", L"run", L"store", L"on", L"summon" };
		bool modern = false;
		for (const wchar_t *k : keywords) if (first == k) modern = true;
		if (!modern)
		{
			if (!a.has(4)) return usage(src, info);
			std::vector<std::shared_ptr<Entity> > es;
			std::wstring err;
			if (!selectEntities(src, a[0], es, false, err)) return fail(src, err);
			size_t cmdAt = 4;
			int total = 0;
			for (auto &e : es)
			{
				Source s = src;
				s.entity = e;
				s.level = levelOf(e);
				s.x = e->x; s.y = e->y; s.z = e->z; s.yRot = e->yRot; s.xRot = e->xRot;
				s.name = entityName(e);
				double x, y, z;
				if (!parseVec3(s, a, 1, x, y, z, false)) return fail(src, L"Coordenadas inv\u00E1lidas.");
				s.x = x; s.y = y; s.z = z;
				if (toLower(a[4]) == L"detect")
				{
					if (!a.has(9)) return usage(src, info);
					int bx, by, bz, id, data;
					if (!parseBlockPos(s, a, 5, bx, by, bz) || !resolveBlock(a[8], id, data)) return fail(src, L"detect inv\u00E1lido.");
					int d = -1;
					parseInt(a[9], d);
					if (!s.level || s.level->getTile(bx, by, bz) != id || (d >= 0 && s.level->getData(bx, by, bz) != d)) continue;
					cmdAt = 10;
				}
				total += execute(s, a.rest(cmdAt)) > 0 ? 1 : 0;
			}
			return total;
		}

		std::vector<Source> ctx(1, src);
		size_t i = 0;
		while (i < a.size())
		{
			const std::wstring k = toLower(a[i]);
			if (k == L"run")
			{
				const std::wstring cmd = a.rest(i + 1);
				if (cmd.empty()) return usage(src, info);
				int total = 0;
				for (auto &s : ctx) total += execute(s, cmd) > 0 ? 1 : 0;
				return total;
			}
			std::vector<Source> next;
			if (k == L"as" || k == L"at")
			{
				if (!a.has(i + 1)) return usage(src, info);
				for (auto &s : ctx)
				{
					std::vector<std::shared_ptr<Entity> > es;
					std::wstring err;
					if (!selectEntities(s, a[i + 1], es, false, err)) continue;
					for (auto &e : es)
					{
						Source n = s;
						if (k == L"as") { n.entity = e; n.name = entityName(e); }
						else { n.level = levelOf(e); n.x = e->x; n.y = e->y; n.z = e->z; n.yRot = e->yRot; n.xRot = e->xRot; }
						next.push_back(n);
					}
				}
				i += 2;
			}
			else if (k == L"positioned")
			{
				if (a.has(i + 1) && toLower(a[i + 1]) == L"as")
				{
					for (auto &s : ctx)
					{
						std::vector<std::shared_ptr<Entity> > es;
						std::wstring err;
						if (!selectEntities(s, a[i + 2], es, false, err)) continue;
						for (auto &e : es) { Source n = s; n.x = e->x; n.y = e->y; n.z = e->z; next.push_back(n); }
					}
					i += 3;
				}
				else
				{
					for (auto &s : ctx)
					{
						Source n = s;
						if (!parseVec3(n, a, i + 1, n.x, n.y, n.z, false)) return fail(src, L"Coordenadas inv\u00E1lidas em positioned.");
						next.push_back(n);
					}
					i += 4;
				}
			}
			else if (k == L"rotated")
			{
				if (a.has(i + 1) && toLower(a[i + 1]) == L"as")
				{
					for (auto &s : ctx)
					{
						std::vector<std::shared_ptr<Entity> > es;
						std::wstring err;
						if (!selectEntities(s, a[i + 2], es, false, err)) continue;
						for (auto &e : es) { Source n = s; n.yRot = e->yRot; n.xRot = e->xRot; next.push_back(n); }
					}
					i += 3;
				}
				else
				{
					for (auto &s : ctx)
					{
						Source n = s;
						if (!parseRotation(n, a[i + 1], s.yRot, n.yRot) || !parseRotation(n, a[i + 2], s.xRot, n.xRot)) return fail(src, L"Rota\u00E7\u00E3o inv\u00E1lida.");
						next.push_back(n);
					}
					i += 3;
				}
			}
			else if (k == L"facing")
			{
				const bool ent = a.has(i + 1) && toLower(a[i + 1]) == L"entity";
				for (auto &s : ctx)
				{
					double fx, fy, fz;
					if (ent)
					{
						std::vector<std::shared_ptr<Entity> > es;
						std::wstring err;
						if (!selectEntities(s, a[i + 2], es, false, err)) continue;
						fx = es[0]->x; fy = es[0]->y + 1.62; fz = es[0]->z;
					}
					else if (!parseVec3(s, a, i + 1, fx, fy, fz, false)) return fail(src, L"Coordenadas inv\u00E1lidas em facing.");
					Source n = s;
					const double dx = fx - s.x, dy = fy - s.y, dz = fz - s.z;
					n.yRot = static_cast<float>(atan2(dz, dx) * 180.0 / 3.14159265358979) - 90.0f;
					n.xRot = static_cast<float>(-atan2(dy, sqrt(dx * dx + dz * dz)) * 180.0 / 3.14159265358979);
					next.push_back(n);
				}
				i += ent ? 4 : 4;
			}
			else if (k == L"align")
			{
				const std::wstring axes = toLower(a[i + 1]);
				for (auto &s : ctx)
				{
					Source n = s;
					if (axes.find(L'x') != std::wstring::npos) n.x = floor(n.x);
					if (axes.find(L'y') != std::wstring::npos) n.y = floor(n.y);
					if (axes.find(L'z') != std::wstring::npos) n.z = floor(n.z);
					next.push_back(n);
				}
				i += 2;
			}
			else if (k == L"anchored")
			{
				const bool eyes = toLower(a[i + 1]) == L"eyes";
				for (auto &s : ctx) { Source n = s; if (eyes && s.entity) n.y = s.entity->y + 1.62; next.push_back(n); }
				i += 2;
			}
			else if (k == L"in")
			{
				const std::wstring d = toLower(stripNamespace(a[i + 1]));
				const int dim = d == L"the_nether" || d == L"nether" ? -1 : (d == L"the_end" || d == L"end" ? 1 : 0);
				for (auto &s : ctx)
				{
					Source n = s;
					n.level = levelForDimension(src.server, dim);
					if (!n.level) return fail(src, L"Dimens\u00E3o indispon\u00EDvel.");
					next.push_back(n);
				}
				i += 2;
			}
			else if (k == L"if" || k == L"unless")
			{
				const bool want = (k == L"if");
				const std::wstring type = toLower(a[i + 1]);
				size_t consumed = 0;
				for (auto &s : ctx)
				{
					bool match = false;
					if (type == L"entity")
					{
						std::vector<std::shared_ptr<Entity> > es;
						std::wstring err;
						match = selectEntities(s, a[i + 2], es, false, err);
						consumed = 3;
					}
					else if (type == L"block")
					{
						if (!blockMatches(s, a, i + 2, match)) return fail(src, L"Bloco/coordenadas inv\u00E1lidos em " + k + L" block.");
						consumed = 6;
					}
					else if (type == L"blocks")
					{
						// if blocks <start> <end> <destination> all|masked
						int ax, ay, az, bx, by, bz, dx, dy, dz;
						if (!parseBlockPos(s, a, i + 2, ax, ay, az) || !parseBlockPos(s, a, i + 5, bx, by, bz) || !parseBlockPos(s, a, i + 8, dx, dy, dz)) return fail(src, L"Coordenadas inv\u00E1lidas.");
						const bool masked = toLower(a[i + 11]) == L"masked";
						match = s.level != nullptr;
						for (int y = (std::min)(ay, by); match && y <= (std::max)(ay, by); y++)
						for (int x = (std::min)(ax, bx); match && x <= (std::max)(ax, bx); x++)
						for (int z = (std::min)(az, bz); match && z <= (std::max)(az, bz); z++)
						{
							const int t1 = s.level->getTile(x, y, z);
							if (masked && t1 == 0) continue;
							const int ox = dx + (x - (std::min)(ax, bx)), oy = dy + (y - (std::min)(ay, by)), oz = dz + (z - (std::min)(az, bz));
							if (t1 != s.level->getTile(ox, oy, oz) || s.level->getData(x, y, z) != s.level->getData(ox, oy, oz)) match = false;
						}
						consumed = 12;
					}
					else if (type == L"score")
					{
						// if score <target> <obj> matches <range> | <op> <source> <sourceObj>
						std::vector<std::wstring> hs = holderNames(s, a[i + 2]);
						auto obj = objectives().find(a[i + 3]);
						if (toLower(a[i + 4]) == L"matches")
						{
							consumed = 6;
							match = obj != objectives().end() && !hs.empty();
							for (auto &h : hs)
							{
								if (obj == objectives().end()) { match = false; break; }
								auto sc = obj->second.scores.find(h);
								if (sc == obj->second.scores.end() || !rangeMatch(a[i + 5], sc->second)) { match = false; break; }
							}
						}
						else
						{
							consumed = 7;
							std::vector<std::wstring> shs = holderNames(s, a[i + 5]);
							auto sobj = objectives().find(a[i + 6]);
							match = obj != objectives().end() && sobj != objectives().end() && !hs.empty() && !shs.empty();
							if (match)
							{
								auto t = obj->second.scores.find(hs[0]);
								auto o = sobj->second.scores.find(shs[0]);
								if (t == obj->second.scores.end() || o == sobj->second.scores.end()) match = false;
								else
								{
									const std::wstring op = a[i + 4];
									const int lv = t->second, rv = o->second;
									match = (op == L"<" && lv < rv) || (op == L"<=" && lv <= rv) || (op == L"=" && lv == rv) || (op == L">" && lv > rv) || (op == L">=" && lv >= rv);
								}
							}
						}
					}
					else return fail(src, L"Condi\u00E7\u00E3o desconhecida: " + type);
					if (match == want) next.push_back(s);
				}
				if (ctx.empty()) consumed = (type == L"entity") ? 3 : (type == L"block" ? 6 : 6);
				i += consumed;
				if (i >= a.size())
				{
					// condition at the end: report the result like Java
					if (next.empty()) return fail(src, L"Teste falhou");
					feedback(src, L"Teste passou" + (next.size() > 1 ? L", contagem: " + std::to_wstring(next.size()) : L""));
					return static_cast<int>(next.size());
				}
			}
			else if (k == L"store")
			{
				return fail(src, L"/execute store n\u00E3o \u00E9 suportado neste jogo.");
			}
			else
			{
				return fail(src, L"Subcomando desconhecido de /execute: " + a[i]);
			}
			ctx.swap(next);
			if (ctx.empty()) return 0;
		}
		return usage(src, info);
	}
}
