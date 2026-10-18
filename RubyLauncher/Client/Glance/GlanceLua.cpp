#include "GlanceLua.h"
#include "GlanceHud.h"
#include "GlanceTarget.h"
#include "GlanceText.h"

#include "Loader.h"

#include <algorithm>

namespace Glance
{
	namespace
	{
		const char *kVersion = "1.0.0";
		const int kMaxErrorsBeforeDisable = 5;
		const size_t kMaxLinesPerProvider = 8;
		const size_t kMaxExtraLines = 16;
		const size_t kMaxLineLength = 200;

		struct Provider
		{
			int id = 0;
			sol::protected_function fn;
			int errors = 0;
			bool active = true;
		};

		// Heap-allocated and intentionally never freed: the functions hold
		// references into the client lua_State, which outlives static
		// destruction order guarantees.
		std::vector<Provider> &providers()
		{
			static std::vector<Provider> *list = new std::vector<Provider>();
			return *list;
		}

		int g_nextProviderId = 1;
		bool g_iterating = false;

		void compactProviders()
		{
			if (g_iterating) return;
			std::vector<Provider> &list = providers();
			list.erase(std::remove_if(list.begin(), list.end(), [](const Provider &p) { return !p.active; }), list.end());
		}

		const char *kindName(TargetKind kind)
		{
			switch (kind)
			{
			case TargetKind::Block: return "block";
			case TargetKind::Entity: return "entity";
			default: return "none";
			}
		}

		const char *toolName(ToolKind tool)
		{
			switch (tool)
			{
			case ToolKind::Pickaxe: return "pickaxe";
			case ToolKind::Shovel: return "shovel";
			case ToolKind::Axe: return "axe";
			case ToolKind::Shears: return "shears";
			case ToolKind::Sword: return "sword";
			default: return "none";
			}
		}

		sol::table targetToLua(sol::state_view lua, const Target &target)
		{
			sol::table t = lua.create_table();
			t["kind"] = kindName(target.kind);
			t["id"] = target.id;
			t["mod"] = target.modId;
			t["modName"] = wideToUtf8(target.modName);
			t["name"] = wideToUtf8(target.name);

			if (target.kind == TargetKind::Block)
			{
				t["x"] = target.x;
				t["y"] = target.y;
				t["z"] = target.z;
				t["data"] = target.data;
				t["tool"] = toolName(target.tool);
				t["canHarvest"] = target.canHarvest;
				t["breakProgress"] = target.breakProgress;
				if (target.growth >= 0.0f) t["growth"] = target.growth;
			}
			else if (target.kind == TargetKind::Entity)
			{
				t["entityId"] = target.entityId;
				if (target.maxHealth > 0.0f)
				{
					t["health"] = target.health;
					t["maxHealth"] = target.maxHealth;
					t["armor"] = target.armor;
				}
			}
			return t;
		}

		void pushLine(const std::string &utf8, std::vector<std::wstring> &out)
		{
			if (out.size() >= kMaxExtraLines) return;
			std::wstring line = utf8ToWide(utf8);
			if (line.size() > kMaxLineLength) line.resize(kMaxLineLength);
			if (!line.empty()) out.push_back(std::move(line));
		}

		void appendResult(const sol::object &result, std::vector<std::wstring> &out)
		{
			if (result.is<std::string>())
			{
				pushLine(result.as<std::string>(), out);
				return;
			}

			if (result.get_type() == sol::type::table)
			{
				sol::table lines = result.as<sol::table>();
				const size_t count = (std::min)(lines.size(), kMaxLinesPerProvider);
				for (size_t i = 1; i <= count; ++i)
				{
					sol::object entry = lines[i];
					if (entry.is<std::string>()) pushLine(entry.as<std::string>(), out);
				}
			}
		}

		unsigned int asColor(long long value)
		{
			return static_cast<unsigned int>(value & 0xFFFFFFFFLL);
		}

		void applyConfig(const sol::table &options)
		{
			Config &cfg = Hud::get().config();

			auto setBool = [&](const char *key, bool &field) {
				sol::optional<bool> value = options[key];
				if (value) field = *value;
			};
			auto setColor = [&](const char *key, unsigned int &field) {
				sol::optional<long long> value = options[key];
				if (value) field = asColor(*value);
			};
			auto setLabel = [&](const char *key, std::wstring &field) {
				sol::optional<std::string> value = options[key];
				if (value && !value->empty()) field = utf8ToWide(*value);
			};

			setBool("enabled", cfg.enabled);
			setBool("showEntities", cfg.showEntities);
			setBool("showModName", cfg.showModName);
			setBool("showHarvestTool", cfg.showHarvestTool);
			setBool("showHealth", cfg.showHealth);
			setBool("showArmor", cfg.showArmor);
			setBool("showGrowth", cfg.showGrowth);
			setBool("showBreakProgress", cfg.showBreakProgress);

			sol::optional<double> opacity = options["opacity"];
			if (opacity) cfg.opacity = static_cast<float>((std::max)(0.0, (std::min)(*opacity, 1.0)));

			sol::optional<int> offsetY = options["offsetY"];
			if (offsetY) cfg.offsetY = (std::max)(-64, (std::min)(*offsetY, 512));

			sol::optional<int> maxHearts = options["maxHearts"];
			if (maxHearts) cfg.maxHearts = (std::max)(1, (std::min)(*maxHearts, 100));

			setColor("backgroundColor", cfg.backgroundColor);
			setColor("borderTopColor", cfg.borderTopColor);
			setColor("borderBottomColor", cfg.borderBottomColor);
			setColor("nameColor", cfg.nameColor);
			setColor("modNameColor", cfg.modNameColor);

			setLabel("growthLabel", cfg.growthLabel);
			setLabel("matureLabel", cfg.matureLabel);
		}
	}

	void bindLua(sol::state &lua)
	{
		sol::table glance = lua.create_table();
		glance["version"] = kVersion;

		glance.set_function("setEnabled", [](bool enabled) {
			Hud::get().config().enabled = enabled;
		});

		glance.set_function("isEnabled", []() {
			return Hud::get().config().enabled;
		});

		glance.set_function("configure", [](sol::object options) -> bool {
			if (options.get_type() != sol::type::table) return false;
			applyConfig(options.as<sol::table>());
			return true;
		});

		glance.set_function("addProvider", [](sol::object fn) -> int {
			if (fn.get_type() != sol::type::function) return 0;
			Provider provider;
			provider.id = g_nextProviderId++;
			provider.fn = fn.as<sol::protected_function>();
			providers().push_back(std::move(provider));
			return providers().back().id;
		});

		glance.set_function("removeProvider", [](int id) -> bool {
			for (Provider &p : providers())
			{
				if (p.id == id && p.active)
				{
					p.active = false;
					compactProviders();
					return true;
				}
			}
			return false;
		});

		glance.set_function("getTarget", [](sol::this_state state) -> sol::object {
			const Target &target = Hud::get().lastTarget(0);
			if (target.kind == TargetKind::None) return sol::make_object(state, sol::lua_nil);
			return sol::make_object(state, targetToLua(sol::state_view(state), target));
		});

		lua["glance"] = glance;
	}

	void collectProviderLines(const Target &target, std::vector<std::wstring> &out)
	{
		std::vector<Provider> &list = providers();
		if (list.empty()) return;

		Loader *loader = Loader::getInstance();
		if (loader == nullptr) return;

		sol::table luaTarget = targetToLua(sol::state_view(loader->luaClient), target);

		g_iterating = true;
		// Index-based: a provider may add new providers while we iterate.
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (!list[i].active) continue;

			sol::protected_function fn = list[i].fn;
			const int id = list[i].id;
			sol::protected_function_result result = fn(luaTarget);

			if (!result.valid())
			{
				sol::error err = result;
				Loader::log("Glance: provider #" + std::to_string(id) + " failed: " + err.what());
				if (i < list.size() && ++list[i].errors >= kMaxErrorsBeforeDisable)
				{
					list[i].active = false;
					Loader::log("Glance: provider #" + std::to_string(id) + " disabled after repeated errors");
				}
				continue;
			}

			if (result.return_count() > 0)
			{
				sol::object first = result.get<sol::object>();
				appendResult(first, out);
			}
		}
		g_iterating = false;
		compactProviders();
	}
}
