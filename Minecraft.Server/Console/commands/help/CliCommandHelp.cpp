#include "stdafx.h"

#include "CliCommandHelp.h"

#include "../../ServerCliEngine.h"
#include "../../ServerCliRegistry.h"
#include "../../ServerCliEngine.h"
#include "../../ServerCliRegistry.h"
#include "../../../FourKitBridge.h"

#include <cstring>

namespace ServerRuntime
{
	const char *CliCommandHelp::Name() const
	{
		return "help";
	}

	std::vector<std::string> CliCommandHelp::Aliases() const
	{
		return { "?" };
	}

	const char *CliCommandHelp::Usage() const
	{
		return "help";
	}

	const char *CliCommandHelp::Description() const
	{
		return "Show available server console commands.";
	}

	bool CliCommandHelp::Execute(const ServerCliParsedLine &line, ServerCliEngine *engine)
	{
		(void)line;
		const std::vector<std::unique_ptr<IServerCliCommand>> &commands = engine->Registry().Commands();
		engine->LogInfo("Available commands:");
		for (size_t i = 0; i < commands.size(); ++i)
		{
			std::string row = "  ";
			row += commands[i]->Usage();
			row += " - ";
			row += commands[i]->Description();
			engine->LogInfo(row);
		}

		char buf[4096];
		int len = 0;
		int count = FourKitBridge::GetPluginCommandHelp(buf, sizeof(buf), &len);
		if (count > 0 && len > 0)
		{
			engine->LogInfo("Plugin commands:");
			const char *ptr = buf;
			const char *end = buf + len;
			for (int i = 0; i < count && ptr < end; ++i)
			{
				std::string usage(ptr);
				ptr += usage.size() + 1;
				if (ptr >= end) break;
				std::string description(ptr);
				ptr += description.size() + 1;

				std::string row = "  ";
				row += usage;
				if (!description.empty())
				{
					row += " - ";
					row += description;
				}
				engine->LogInfo(row);
			}
		}

		return true;
	}
}

