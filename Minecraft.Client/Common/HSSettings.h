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

	// FPS limit: 0 = unlimited, otherwise 1..300. Read every frame by the frame pacer.
	static const int kFpsMax = 300;
	static const int kFpsUnlimitedSlider = kFpsMax + 1;
	int getFpsLimit();
	void setFpsLimit(int fps);
}
