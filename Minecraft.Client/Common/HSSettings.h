#pragma once

// Hellish Sight: small key=value settings file (hellish-settings.cfg, next to the exe)
// for options that have no slot in the 4J profile data.

#include <string>

namespace HSSettings
{
	int getInt(const char *key, int defaultValue);
	void setInt(const char *key, int value);

	// Applies the saved values (simulation distance) at start-up.
	void applyAtStartup();
}
