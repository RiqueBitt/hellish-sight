#include "stdafx.h"

#include "CliCommandRevokeToken.h"

#include "../../ServerCliEngine.h"
#include "../../ServerCliParser.h"
#include "../../../Access/Access.h"
#include "../../../Security/IdentityTokenManager.h"
#include "../../../ServerLogManager.h"

namespace ServerRuntime
{
	const char *CliCommandRevokeToken::Name() const
	{
		return "revoketoken";
	}

	const char *CliCommandRevokeToken::Usage() const
	{
		return "revoketoken <name|xuid>";
	}

	const char *CliCommandRevokeToken::Description() const
	{
		return "Revoke a player's identity token. They will be issued a new one on next login.";
	}

	bool CliCommandRevokeToken::Execute(const ServerCliParsedLine &line, ServerCliEngine *engine)
	{
		if (line.tokens.size() < 2)
		{
			engine->LogWarn(std::string("Usage: ") + Usage());
			return false;
		}

		PlayerUID xuid = INVALID_XUID;

		// Try parsing as XUID first
		if (ServerRuntime::Access::TryParseXuid(line.tokens[1], &xuid))
		{
			// Direct XUID
		}
		else
		{
			// Try name lookup from cache
			std::vector<PlayerUID> cachedXuids;
			int count = ServerRuntime::ServerLogManager::GetCachedXuids(line.tokens[1], &cachedXuids);
			if (count == 0)
			{
				engine->LogWarn("Unknown player: " + line.tokens[1]);
				engine->LogWarn("The player must have attempted to connect, or use: revoketoken <xuid>");
				return false;
			}
			if (count > 1)
			{
				engine->LogWarn("Ambiguous: " + std::to_string(count) + " XUIDs seen for '" + line.tokens[1] + "':");
				for (size_t i = 0; i < cachedXuids.size(); ++i)
				{
					std::string label = (i == cachedXuids.size() - 1) ? " (most recent)" : "";
					engine->LogWarn("  " + ServerRuntime::Access::FormatXuid(cachedXuids[i]) + label);
				}
				engine->LogWarn("Re-run with the explicit XUID: revoketoken <xuid>");
				return false;
			}
			xuid = cachedXuids.back();
			engine->LogInfo("Resolved '" + line.tokens[1] + "' to XUID " + ServerRuntime::Access::FormatXuid(xuid));
		}

		if (!ServerRuntime::Security::GetIdentityTokenManager().HasToken(xuid))
		{
			engine->LogWarn("No identity token found for XUID " + ServerRuntime::Access::FormatXuid(xuid));
			return false;
		}

		if (!ServerRuntime::Security::GetIdentityTokenManager().RevokeToken(xuid))
		{
			engine->LogError("Failed to revoke token.");
			return false;
		}

		engine->LogInfo("Revoked identity token for XUID " + ServerRuntime::Access::FormatXuid(xuid) +
			". Player will receive a new token on next login.");
		return true;
	}
}
