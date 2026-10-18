#pragma once

// Hellish Sight - Minecraft (Java Edition style) text commands.
//
// Used by the chat ("/command ...") and by command blocks. Supports target
// selectors (@p @a @r @e @s with arguments), relative (~) and local (^)
// coordinates, minecraft:names for blocks / items / entities / effects /
// enchantments, and the commands listed by /help.

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Entity;
class ServerPlayer;
class ServerLevel;
class MinecraftServer;

namespace HSCommands
{
	struct Source
	{
		MinecraftServer *server = nullptr;
		ServerLevel *level = nullptr;
		std::shared_ptr<Entity> entity;			// executing entity (@s); null for command blocks / console
		std::shared_ptr<ServerPlayer> player;	// the player that typed the command (gets the feedback)
		double x = 0, y = 0, z = 0;				// execution position
		float yRot = 0, xRot = 0;				// execution rotation (for ^ coordinates)
		int permission = 4;						// 0 = normal player, 2 = command block, 4 = op / host
		bool commandBlock = false;
		bool silent = false;					// no feedback (execute ... run inside command blocks)
		std::wstring name = L"@";				// shown by /say, /me and in command block output
		std::wstring lastOutput;				// last feedback line (command block "previous output")
		std::function<void(const std::wstring &, bool)> output;	// (message, isError)

		void send(const std::wstring &message, bool error = false);
	};

	// Runs one command line (with or without the leading '/'). Returns the success
	// count (0 = failure), like Java command blocks.
	int execute(Source &source, const std::wstring &commandLine);

	// Builds a source for a player typing in chat.
	Source fromPlayer(const std::shared_ptr<ServerPlayer> &player);

	// Called every server tick (scheduled functions).
	void tick(MinecraftServer *server);

	// Names of all registered commands (for /help and completion).
	std::vector<std::wstring> commandNames();
}
