// Hellish Sight - list of every command and its usage text.

#include "stdafx.h"
#include "HSCommandsInternal.h"

namespace HSCommands
{
	// player / entity
	int cmdTeleport(Source &, const Args &);
	int cmdGive(Source &, const Args &);
	int cmdClear(Source &, const Args &);
	int cmdKill(Source &, const Args &);
	int cmdGameMode(Source &, const Args &);
	int cmdDefaultGameMode(Source &, const Args &);
	int cmdEffect(Source &, const Args &);
	int cmdEnchant(Source &, const Args &);
	int cmdExperience(Source &, const Args &);
	int cmdSpawnpoint(Source &, const Args &);
	int cmdSpreadPlayers(Source &, const Args &);
	int cmdRide(Source &, const Args &);
	int cmdDamage(Source &, const Args &);
	int cmdAttribute(Source &, const Args &);
	int cmdItem(Source &, const Args &);
	int cmdTag(Source &, const Args &);
	int cmdSummon(Source &, const Args &);
	int cmdSay(Source &, const Args &);
	int cmdMe(Source &, const Args &);
	int cmdMsg(Source &, const Args &);
	int cmdTellraw(Source &, const Args &);
	int cmdTitle(Source &, const Args &);
	int cmdList(Source &, const Args &);
	int cmdKick(Source &, const Args &);
	int cmdOp(Source &, const Args &);
	int cmdDeop(Source &, const Args &);
	int cmdBan(Source &, const Args &);
	int cmdPardon(Source &, const Args &);
	int cmdBanlist(Source &, const Args &);
	int cmdSwing(Source &, const Args &);
	int cmdRotate(Source &, const Args &);
	int cmdSpectate(Source &, const Args &);
	// world
	int cmdSetblock(Source &, const Args &);
	int cmdFill(Source &, const Args &);
	int cmdClone(Source &, const Args &);
	int cmdTime(Source &, const Args &);
	int cmdWeather(Source &, const Args &);
	int cmdToggleDownfall(Source &, const Args &);
	int cmdDifficulty(Source &, const Args &);
	int cmdGamerule(Source &, const Args &);
	int cmdSeed(Source &, const Args &);
	int cmdSetWorldSpawn(Source &, const Args &);
	int cmdLocate(Source &, const Args &);
	int cmdParticle(Source &, const Args &);
	int cmdPlaySound(Source &, const Args &);
	int cmdStopSound(Source &, const Args &);
	int cmdFillBiome(Source &, const Args &);
	int cmdPlace(Source &, const Args &);
	int cmdWorldBorder(Source &, const Args &);
	int cmdForceload(Source &, const Args &);
	int cmdSaveAll(Source &, const Args &);
	int cmdSaveOff(Source &, const Args &);
	int cmdSaveOn(Source &, const Args &);
	int cmdStop(Source &, const Args &);
	// logic
	int cmdHelp(Source &, const Args &);
	int cmdVersion(Source &, const Args &);
	int cmdRandom(Source &, const Args &);
	int cmdScoreboard(Source &, const Args &);
	int cmdTrigger(Source &, const Args &);
	int cmdTeam(Source &, const Args &);
	int cmdData(Source &, const Args &);
	int cmdFunction(Source &, const Args &);
	int cmdSchedule(Source &, const Args &);
	int cmdReload(Source &, const Args &);
	int cmdExecute(Source &, const Args &);

	void registerAll()
	{
		const CommandInfo list[] = {
			{ L"help", 0, L"[comando]", cmdHelp },
			{ L"?", 0, L"[comando]", cmdHelp },
			{ L"version", 0, L"", cmdVersion },
			{ L"list", 0, L"", cmdList },
			{ L"me", 0, L"<ação>", cmdMe },
			{ L"msg", 0, L"<alvos> <mensagem>", cmdMsg },
			{ L"tell", 0, L"<alvos> <mensagem>", cmdMsg },
			{ L"w", 0, L"<alvos> <mensagem>", cmdMsg },
			{ L"trigger", 0, L"<objetivo> [add|set <valor>]", cmdTrigger },
			{ L"seed", 0, L"", cmdSeed },
			{ L"random", 0, L"value|roll <min..max>", cmdRandom },

			{ L"teleport", 2, L"[alvos] <x y z>|<destino> [yRot xRot | facing <x y z>|facing entity <alvo>]", cmdTeleport },
			{ L"tp", 2, L"[alvos] <x y z>|<destino> [yRot xRot]", cmdTeleport },
			{ L"give", 2, L"<alvos> <item> [quantidade]", cmdGive },
			{ L"clear", 2, L"[alvos] [item] [máximo]", cmdClear },
			{ L"kill", 2, L"[alvos]", cmdKill },
			{ L"gamemode", 2, L"<survival|creative|adventure|spectator> [alvos]", cmdGameMode },
			{ L"defaultgamemode", 2, L"<survival|creative|adventure|spectator>", cmdDefaultGameMode },
			{ L"effect", 2, L"give <alvos> <efeito> [segundos|infinite] [amplificador] [ocultarPartículas] | clear [alvos] [efeito]", cmdEffect },
			{ L"enchant", 2, L"<alvos> <encantamento> [nível]", cmdEnchant },
			{ L"experience", 2, L"add|set <alvos> <quantidade> [points|levels] | query <alvo> points|levels", cmdExperience },
			{ L"xp", 2, L"add|set <alvos> <quantidade> [points|levels] | <quantidade>[L] [jogador]", cmdExperience },
			{ L"spawnpoint", 2, L"[alvos] [x y z]", cmdSpawnpoint },
			{ L"spreadplayers", 2, L"<x> <z> <distância> <alcance> [under <altura>] <respeitarTimes> <alvos>", cmdSpreadPlayers },
			{ L"ride", 2, L"<alvo> mount <veículo> | dismount", cmdRide },
			{ L"damage", 2, L"<alvo> <quantidade> [tipo]", cmdDamage },
			{ L"attribute", 2, L"<alvo> <atributo> get | base get | base set <valor>", cmdAttribute },
			{ L"item", 2, L"replace entity <alvos> <slot> with <item> [quantidade]", cmdItem },
			{ L"replaceitem", 2, L"entity <alvos> <slot> <item> [quantidade] [dados]", cmdItem },
			{ L"tag", 2, L"<alvos> add|remove <nome> | list", cmdTag },
			{ L"summon", 2, L"<entidade> [x y z] [{CustomName:\"nome\"}]", cmdSummon },
			{ L"say", 2, L"<mensagem>", cmdSay },
			{ L"tellraw", 2, L"<alvos> <texto JSON>", cmdTellraw },
			{ L"title", 2, L"<alvos> title|subtitle|actionbar <texto JSON> | clear | reset", cmdTitle },
			{ L"swing", 2, L"[alvos]", cmdSwing },
			{ L"rotate", 2, L"<alvo> <yRot> [xRot]", cmdRotate },
			{ L"spectate", 2, L"<alvo> [jogador]", cmdSpectate },

			{ L"setblock", 2, L"<x y z> <bloco> [destroy|keep|replace]", cmdSetblock },
			{ L"fill", 2, L"<de x y z> <até x y z> <bloco> [replace [filtro]|destroy|keep|hollow|outline]", cmdFill },
			{ L"clone", 2, L"<início> <fim> <destino> [replace|masked] [normal|force|move]", cmdClone },
			{ L"time", 2, L"set <day|noon|night|midnight|tempo> | add <tempo> | query <daytime|gametime|day>", cmdTime },
			{ L"weather", 2, L"<clear|rain|thunder> [duração]", cmdWeather },
			{ L"toggledownfall", 2, L"", cmdToggleDownfall },
			{ L"difficulty", 2, L"[peaceful|easy|normal|hard]", cmdDifficulty },
			{ L"gamerule", 2, L"<regra> [valor]", cmdGamerule },
			{ L"setworldspawn", 2, L"[x y z]", cmdSetWorldSpawn },
			{ L"locate", 2, L"structure <stronghold|village|mineshaft|monument|temple> | biome <bioma>", cmdLocate },
			{ L"particle", 2, L"<nome> [x y z] [dx dy dz] [velocidade] [quantidade]", cmdParticle },
			{ L"playsound", 2, L"<som> [fonte] [alvos] [x y z] [volume] [tom]", cmdPlaySound },
			{ L"stopsound", 2, L"[alvos]", cmdStopSound },
			{ L"fillbiome", 2, L"<de x y z> <até x y z> <bioma>", cmdFillBiome },
			{ L"place", 2, L"feature <oak|birch|spruce|jungle_tree|acacia|dark_oak|...> [x y z]", cmdPlace },
			{ L"worldborder", 2, L"get", cmdWorldBorder },
			{ L"forceload", 2, L"add|remove|query", cmdForceload },

			{ L"execute", 2, L"as|at|positioned|rotated|facing|align|anchored|in|if|unless ... run <comando>", cmdExecute },
			{ L"scoreboard", 2, L"objectives add|remove|list|setdisplay ... | players set|add|remove|get|reset|list|operation ...", cmdScoreboard },
			{ L"team", 2, L"add|remove|empty|join|leave|list|modify ...", cmdTeam },
			{ L"data", 2, L"get entity <alvo> [caminho]", cmdData },
			{ L"function", 2, L"<namespace:nome>", cmdFunction },
			{ L"schedule", 2, L"function <nome> <tempo> [append|replace] | clear <nome>", cmdSchedule },
			{ L"reload", 2, L"", cmdReload },

			{ L"kick", 3, L"<alvos> [motivo]", cmdKick },
			{ L"op", 3, L"<alvos>", cmdOp },
			{ L"deop", 3, L"<alvos>", cmdDeop },
			{ L"ban", 3, L"<jogador> [motivo]", cmdBan },
			{ L"pardon", 3, L"<jogador>", cmdPardon },
			{ L"banlist", 3, L"", cmdBanlist },
			{ L"save-all", 4, L"", cmdSaveAll },
			{ L"save-off", 4, L"", cmdSaveOff },
			{ L"save-on", 4, L"", cmdSaveOn },
			{ L"stop", 4, L"", cmdStop },
		};
		for (const CommandInfo &c : list) registerCommand(c);
	}
}
