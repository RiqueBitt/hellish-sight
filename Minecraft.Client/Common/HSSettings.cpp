#include "stdafx.h"
#include "HSSettings.h"
#include "../../Minecraft.World/Level.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <atomic>

namespace HSSettings
{
	namespace
	{
		const char *kFile = "hellish-settings.cfg";
		std::mutex g_lock;
		std::map<std::string, std::string> g_values;
		bool g_loaded = false;
		std::atomic<int> g_fpsLimit(-1);	// -1 = not read yet

		void load()
		{
			if (g_loaded) return;
			g_loaded = true;
			FILE *f = nullptr;
			if (fopen_s(&f, kFile, "r") != 0 || f == nullptr) return;
			char line[256];
			while (fgets(line, sizeof(line), f))
			{
				char *eq = strchr(line, '=');
				if (!eq || line[0] == '#') continue;
				*eq = 0;
				std::string v = eq + 1;
				while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
				g_values[line] = v;
			}
			fclose(f);
		}

		void save()
		{
			FILE *f = nullptr;
			if (fopen_s(&f, kFile, "w") != 0 || f == nullptr) return;
			fprintf(f, "# Hellish Sight settings\n");
			for (auto &kv : g_values) fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
			fclose(f);
		}
	}

	int getInt(const char *key, int defaultValue)
	{
		std::lock_guard<std::mutex> guard(g_lock);
		load();
		auto it = g_values.find(key);
		return it == g_values.end() ? defaultValue : atoi(it->second.c_str());
	}

	void setInt(const char *key, int value)
	{
		std::lock_guard<std::mutex> guard(g_lock);
		load();
		g_values[key] = std::to_string(value);
		save();
	}

	int getFpsLimit()
	{
		int v = g_fpsLimit.load(std::memory_order_relaxed);
		if (v < 0)
		{
			v = getInt("fpsLimit", 0);
			if (v < 0) v = 0;
			if (v > kFpsMax) v = kFpsMax;
			g_fpsLimit.store(v, std::memory_order_relaxed);
		}
		return v;
	}

	void setFpsLimit(int fps)
	{
		if (fps < 0) fps = 0;
		if (fps > kFpsMax) fps = kFpsMax;
		g_fpsLimit.store(fps, std::memory_order_relaxed);
		setInt("fpsLimit", fps);
	}

	void applyAtStartup()
	{
		getFpsLimit();
		int sim = getInt("simulationDistance", 10);
		if (sim < 2) sim = 2;
		if (sim > 32) sim = 32;
		Level::s_simulationChunks = sim;
	}
}
