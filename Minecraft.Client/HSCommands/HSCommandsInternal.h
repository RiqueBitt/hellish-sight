#pragma once

// Internal helpers shared by the Hellish Sight command implementations.

#include "HSCommands.h"
#include <unordered_map>

class Level;
class Player;
class LivingEntity;

namespace HSCommands
{
	typedef std::vector<std::wstring> Tokens;

	struct Args
	{
		Tokens t;
		size_t size() const { return t.size(); }
		bool has(size_t i) const { return i < t.size(); }
		const std::wstring &operator[](size_t i) const { static const std::wstring empty; return i < t.size() ? t[i] : empty; }
		// tokens [from, end) joined with single spaces (keeps the original text for messages)
		std::wstring rest(size_t from) const;
		std::wstring raw;		// arguments as typed (after the command name)
		std::vector<size_t> offsets;	// start of each token inside raw
	};

	typedef int (*Handler)(Source &src, const Args &args);

	struct CommandInfo
	{
		const wchar_t *name;
		int permission;			// 0 everyone, 2 command blocks / ops, 3-4 ops / host
		const wchar_t *usage;	// shown on wrong syntax and by /help <command>
		Handler handler;
	};

	void registerCommand(const CommandInfo &info);
	const CommandInfo *findCommand(const std::wstring &name);
	void registerAll();		// defined in HSCommandsRegistry.cpp

	// ---- feedback -------------------------------------------------------------------
	int fail(Source &src, const std::wstring &message);			// returns 0
	int usage(Source &src, const CommandInfo *info);
	void feedback(Source &src, const std::wstring &message);		// success message (to the executor)
	void broadcastOps(Source &src, const std::wstring &message);	// "[name: message]" to other ops like Java

	// ---- parsing --------------------------------------------------------------------
	Tokens tokenize(const std::wstring &line, std::vector<size_t> *offsets = nullptr);
	bool parseInt(const std::wstring &s, int &out);
	bool parseInt64(const std::wstring &s, long long &out);
	bool parseDouble(const std::wstring &s, double &out);
	bool parseBool(const std::wstring &s, bool &out);
	std::wstring stripNamespace(const std::wstring &name);	// "minecraft:stone" -> "stone"

	// Coordinates: three tokens starting at args[i]. Supports numbers, ~ and ^.
	// blockCentre: integer x/z values get +0.5 (entity positions, like Java).
	bool parseVec3(Source &src, const Args &args, size_t i, double &x, double &y, double &z, bool blockCentre);
	bool parseBlockPos(Source &src, const Args &args, size_t i, int &x, int &y, int &z);
	bool parseRotation(const Source &src, const std::wstring &tok, float current, float &out);

	// Selectors / player names. playersOnly restricts @e etc.
	bool selectEntities(Source &src, const std::wstring &selector, std::vector<std::shared_ptr<Entity> > &out, bool playersOnly, std::wstring &error);
	bool selectPlayers(Source &src, const std::wstring &selector, std::vector<std::shared_ptr<ServerPlayer> > &out, std::wstring &error);
	std::shared_ptr<ServerPlayer> singlePlayer(Source &src, const std::wstring &selector, std::wstring &error);
	std::wstring entityName(const std::shared_ptr<Entity> &e);

	// Registries (minecraft:names and numeric ids)
	bool resolveBlock(const std::wstring &name, int &id, int &data);	// also "id" / "id:data" / "name[...]" (states ignored)
	bool resolveItem(const std::wstring &name, int &id, int &aux);
	std::wstring resolveEntityType(const std::wstring &name);			// -> EntityIO id ("Zombie"), empty if unknown
	int resolveEffect(const std::wstring &name);						// -> MobEffect id or -1
	int resolveEnchantment(const std::wstring &name);					// -> Enchantment id or -1
	std::wstring blockName(int id, int data);
	std::wstring itemName(int id, int aux);

	// World helpers
	ServerLevel *levelOf(const std::shared_ptr<Entity> &e);
	ServerLevel *levelForDimension(MinecraftServer *server, int dimension);
	void teleportEntity(const std::shared_ptr<Entity> &e, ServerLevel *level, double x, double y, double z, float yRot, float xRot);
	bool isOp(const std::shared_ptr<ServerPlayer> &player);

	// Scoreboard / tags (kept in memory per server session, saved with the world in hellish-scores.dat)
	struct Objective { std::wstring criteria; std::wstring displayName; std::unordered_map<std::wstring, int> scores; };
	std::unordered_map<std::wstring, Objective> &objectives();
	std::wstring &sidebarObjective();
	std::vector<std::wstring> &entityTags(const std::shared_ptr<Entity> &e);
	std::wstring scoreHolderName(const std::shared_ptr<Entity> &e);

	// Scheduled functions (/schedule) and /function folder
	void scheduleFunction(const std::wstring &name, long long delayTicks, bool replace);
	int runFunction(Source &src, const std::wstring &name);
	std::wstring functionsFolder();
}
