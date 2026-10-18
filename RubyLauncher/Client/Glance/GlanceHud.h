#pragma once

// Glance - "what am I looking at" HUD for Ruby Launcher.
// Original implementation written from scratch for this codebase.

#include "GuiComponent.h"
#include "GlanceTarget.h"

#include <string>
#include <vector>

class Minecraft;
class ItemRenderer;

namespace Glance
{
	struct Config
	{
		// The engine ships the HUD disabled; the "Glance" Workshop mod (or any
		// other mod) turns it on through the Lua `glance` table.
		bool enabled = false;

		bool showEntities = true;
		bool showModName = true;
		bool showHarvestTool = true;
		bool showHealth = true;
		bool showArmor = true;
		bool showGrowth = true;
		bool showBreakProgress = true;

		float opacity = 1.0f;   // 0..1, applied to the panel background/border
		int offsetY = 0;        // extra pixels below the top safe zone
		int maxHearts = 20;     // above this, health is drawn as numbers

		// Panel colours (ARGB). Defaults match the classic tooltip look.
		unsigned int backgroundColor = 0xF0100010u;
		unsigned int borderTopColor = 0x505000FFu;
		unsigned int borderBottomColor = 0x5028007Fu;
		unsigned int nameColor = 0xFFFFFFu;
		unsigned int modNameColor = 0x5555FFu;

		// Labels (overridable for translations).
		std::wstring growthLabel = L"Growth";
		std::wstring matureLabel = L"Mature";
	};

	class Hud : public GuiComponent
	{
	public:
		static Hud &get();

		Config &config() { return m_config; }
		const Config &config() const { return m_config; }

		// Called once per viewport from Gui::render, inside the HUD-visible branch.
		// centerX/topY are in GUI coordinates for the current viewport.
		void render(Minecraft *mc, int centerX, int topY, int pad);

		// Last target seen by the given viewport (pad); kind == None if nothing.
		const Target &lastTarget(int pad) const;

	private:
		Hud() = default;

		static const int kMaxPads = 4;

		struct PadState
		{
			std::string key;
			int refreshTick = -1000000;
			std::vector<std::wstring> extraLines;
			Target target;
		};

		void draw(Minecraft *mc, const Target &target, int centerX, int topY);
		void drawPanel(int x, int y, int w, int h);
		void drawHarvestMark(int x, int y, bool ok);
		unsigned int withOpacity(unsigned int argb) const;

		Config m_config;
		PadState m_pads[kMaxPads];
		ItemRenderer *m_itemRenderer = nullptr;
	};
}
