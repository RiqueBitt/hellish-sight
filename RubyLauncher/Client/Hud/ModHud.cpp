#include "ModHud.h"
#include "LookTarget.h"
#include "HudText.h"

#include "Loader.h"

#include "Minecraft.h"
#include "GuiComponent.h"
#include "Font.h"
#include "Textures.h"
#include "ResourceLocation.h"
#include "ItemRenderer.h"
#include "Lighting.h"
#include "Item.h"
#include "ItemInstance.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace RubyHud
{
	namespace
	{
		const int kMaxErrorsBeforeDisable = 5;
		const size_t kMaxTextLength = 512;

		struct Layer
		{
			int id = 0;
			std::string owner;  // modId of the registering script, for logs
			sol::protected_function fn;
			int errors = 0;
			bool active = true;
		};

		// Heap-allocated and intentionally never freed: the functions hold
		// references into the client lua_State, whose lifetime is not ordered
		// with static destruction.
		std::vector<Layer> &layers()
		{
			static std::vector<Layer> *list = new std::vector<Layer>();
			return *list;
		}

		int g_nextLayerId = 1;
		bool g_iterating = false;

		// Valid only while a layer callback runs.
		struct DrawContext
		{
			Minecraft *mc = nullptr;
			Viewport viewport;
		};
		DrawContext *g_draw = nullptr;

		// GuiComponent's helpers are protected; expose them to this file only.
		class Painter : public GuiComponent
		{
		public:
			void rect(int x0, int y0, int x1, int y1, int col) { fill(x0, y0, x1, y1, col); }
			void gradient(int x0, int y0, int x1, int y1, int top, int bottom) { fillGradient(x0, y0, x1, y1, top, bottom); }
			void sprite(int x, int y, int u, int v, int w, int h) { blit(x, y, u, v, w, h); }
		};

		Painter &painter()
		{
			static Painter *instance = new Painter();
			return *instance;
		}

		ItemRenderer *itemRenderer()
		{
			static ItemRenderer *renderer = nullptr;
			if (renderer == nullptr) renderer = new ItemRenderer();
			return renderer;
		}

		// The engine binds textures through ResourceLocation; build each once.
		ResourceLocation *spriteSheet(const std::string &name)
		{
			static ResourceLocation icons(TN_GUI_ICONS);
			static ResourceLocation gui(TN_GUI_GUI);
			if (name == "icons") return &icons;
			if (name == "gui") return &gui;
			return nullptr;
		}

		int asColor(long long value)
		{
			return static_cast<int>(static_cast<unsigned int>(value & 0xFFFFFFFFLL));
		}

		std::wstring luaText(const std::string &utf8)
		{
			std::wstring text = utf8ToWide(utf8);
			if (text.size() > kMaxTextLength) text.resize(kMaxTextLength);
			return text;
		}

		bool isValidItemId(int id)
		{
			return id > 0 && static_cast<unsigned int>(id) < Item::items.length && Item::items[id] != nullptr;
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

			if (target.icon != nullptr)
			{
				sol::table icon = lua.create_table();
				icon["id"] = target.icon->id;
				icon["aux"] = target.icon->getAuxValue();
				t["icon"] = icon;
			}

			if (target.kind == TargetKind::Block)
			{
				t["x"] = target.x;
				t["y"] = target.y;
				t["z"] = target.z;
				t["tileId"] = target.tileId;
				t["data"] = target.data;
				t["tool"] = toolName(target.tool);
				t["canHarvest"] = target.canHarvest;
				t["breakProgress"] = target.breakProgress;
				if (target.growth >= 0.0f) t["growth"] = target.growth;

				std::shared_ptr<ItemInstance> tool = toolIcon(target.tool);
				if (tool != nullptr) t["toolItem"] = tool->id;
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

		sol::table viewportToLua(sol::state_view lua, const Viewport &vp)
		{
			sol::table t = lua.create_table();
			t["pad"] = vp.pad;
			t["left"] = vp.left;
			t["top"] = vp.top;
			t["width"] = vp.width;
			t["height"] = vp.height;
			t["centerX"] = vp.left + vp.width / 2;
			return t;
		}

		void compactLayers()
		{
			if (g_iterating) return;
			std::vector<Layer> &list = layers();
			list.erase(std::remove_if(list.begin(), list.end(), [](const Layer &l) { return !l.active; }), list.end());
		}

		// Leaves GL the way Gui::render expects it (blending on for HUD fading).
		void restoreGuiState()
		{
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			glDepthMask(true);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
	}

	void renderLayers(Minecraft *mc, const Viewport &viewport)
	{
		std::vector<Layer> &list = layers();
		if (list.empty() || mc == nullptr || mc->player == nullptr) return;
		if (mc->font == nullptr || mc->textures == nullptr) return;

		Loader *loader = Loader::getInstance();
		if (loader == nullptr) return;

		DrawContext context;
		context.mc = mc;
		context.viewport = viewport;
		g_draw = &context;

		sol::table luaViewport = viewportToLua(sol::state_view(loader->luaClient), viewport);

		g_iterating = true;
		// Index-based: a layer may register more layers while we iterate.
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (!list[i].active) continue;

			sol::protected_function fn = list[i].fn;
			const int id = list[i].id;
			const std::string owner = list[i].owner;

			restoreGuiState();
			sol::protected_function_result result = fn(luaViewport);

			if (!result.valid())
			{
				sol::error err = result;
				Loader::log("HUD layer #" + std::to_string(id) + " (" + owner + ") failed: " + err.what());
				if (i < list.size() && ++list[i].errors >= kMaxErrorsBeforeDisable)
				{
					list[i].active = false;
					Loader::log("HUD layer #" + std::to_string(id) + " (" + owner + ") disabled after repeated errors");
				}
			}
		}
		g_iterating = false;
		g_draw = nullptr;
		compactLayers();
		restoreGuiState();
	}

	void bindLua(sol::state &lua)
	{
		sol::table hud = lua.create_table();
		hud["version"] = "1.0.0";

		// --- layers -------------------------------------------------------------
		hud.set_function("addLayer", [](sol::object fn, sol::this_environment env) -> int {
			if (fn.get_type() != sol::type::function) return 0;
			Layer layer;
			layer.id = g_nextLayerId++;
			layer.fn = fn.as<sol::protected_function>();
			if (env)
			{
				sol::environment &e = env;
				sol::optional<std::string> modId = e["modId"];
				if (modId) layer.owner = *modId;
			}
			if (layer.owner.empty()) layer.owner = "unknown";
			layers().push_back(std::move(layer));
			return layers().back().id;
		});

		hud.set_function("removeLayer", [](int id) -> bool {
			for (Layer &l : layers())
			{
				if (l.id == id && l.active)
				{
					l.active = false;
					compactLayers();
					return true;
				}
			}
			return false;
		});

		// --- drawing primitives (only valid inside a layer callback) ------------
		hud.set_function("fill", [](int x0, int y0, int x1, int y1, long long color) {
			if (g_draw == nullptr) return;
			glDepthMask(false); // never let 2D panels clip the 3D item icons drawn on top
			painter().rect(x0, y0, x1, y1, asColor(color));
			glDepthMask(true);
		});

		hud.set_function("gradient", [](int x0, int y0, int x1, int y1, long long top, long long bottom) {
			if (g_draw == nullptr) return;
			glDepthMask(false);
			painter().gradient(x0, y0, x1, y1, asColor(top), asColor(bottom));
			glDepthMask(true);
		});

		hud.set_function("text", [](const std::string &text, int x, int y, sol::optional<long long> color, sol::optional<bool> shadow) {
			if (g_draw == nullptr) return;
			Font *font = g_draw->mc->font;
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			const int col = asColor(color.value_or(0xFFFFFFFFLL));
			if (shadow.value_or(true)) font->drawShadow(luaText(text), x, y, col);
			else font->draw(luaText(text), x, y, col);
		});

		hud.set_function("textWidth", [](const std::string &text) -> int {
			Minecraft *mc = Minecraft::GetInstance();
			if (mc == nullptr || mc->font == nullptr) return 0;
			return mc->font->width(luaText(text));
		});

		hud.set_function("item", [](int id, int aux, int x, int y, sol::optional<double> scale) {
			if (g_draw == nullptr || !isValidItemId(id)) return;
			Minecraft *mc = g_draw->mc;
			std::shared_ptr<ItemInstance> stack = std::make_shared<ItemInstance>(id, 1, aux);
			const float s = static_cast<float>((std::max)(0.25, (std::min)(scale.value_or(1.0), 4.0)));

			glEnable(GL_RESCALE_NORMAL);
			Lighting::turnOnGui();
			glPushMatrix();
			glTranslatef(static_cast<float>(x), static_cast<float>(y), 0.0f);
			glScalef(s, s, 1.0f);
			itemRenderer()->renderGuiItem(mc->font, mc->textures, stack, 0, 0);
			glPopMatrix();
			Lighting::turnOff();
			glDisable(GL_RESCALE_NORMAL);
			restoreGuiState();
		});

		// sheet: "icons" (gui/icons.png: hearts, armor, hunger...) or "gui" (gui/gui.png)
		hud.set_function("sprite", [](const std::string &sheet, int u, int v, int w, int h, int x, int y) {
			if (g_draw == nullptr) return;
			ResourceLocation *location = spriteSheet(sheet);
			if (location == nullptr) return;
			g_draw->mc->textures->bindTexture(location);
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			painter().sprite(x, y, u, v, w, h);
		});

		// --- queries -------------------------------------------------------------
		auto lookTarget = [](sol::optional<bool> includeEntities, sol::this_state state) -> sol::object {
			Minecraft *mc = Minecraft::GetInstance();
			Target target = resolve(mc, includeEntities.value_or(true));
			if (target.kind == TargetKind::None) return sol::make_object(state, sol::lua_nil);
			return sol::make_object(state, targetToLua(sol::state_view(state), target));
		};
		hud.set_function("getLookTarget", lookTarget);
		lua.set_function("getLookTarget", lookTarget);

		hud.set_function("getTickCount", []() -> int {
			Loader *loader = Loader::getInstance();
			return loader != nullptr ? loader->m_clientTickCount : 0;
		});

		lua["hud"] = hud;
	}
}
