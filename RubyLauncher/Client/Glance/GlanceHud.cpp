#include "GlanceHud.h"
#include "GlanceLua.h"

#include "Loader.h"

#include "Minecraft.h"
#include "Font.h"
#include "Textures.h"
#include "ItemRenderer.h"
#include "Lighting.h"
#include "ItemInstance.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace Glance
{
	namespace
	{
		// Layout (GUI pixels)
		const int kIconSize = 16;
		const int kIconGap = 4;
		const int kLineHeight = 10;
		const int kHeartSize = 9;
		const int kHeartStep = 8;
		const int kHeartsPerRow = 10;
		const int kToolSize = 10;               // tool icon drawn at 10/16 scale
		const float kToolScale = 10.0f / 16.0f;
		const int kMarkWidth = 7;
		const int kPanelMargin = 4;             // space the frame takes around the content

		// Provider lines are refreshed at most this often (client ticks).
		const int kRefreshTicks = 4;

		// Sprite positions on the vanilla GUI icons sheet.
		const int kHeartEmptyU = 16, kHeartFullU = 52, kHeartHalfU = 61, kHeartV = 0;
		const int kArmorEmptyU = 16, kArmorHalfU = 25, kArmorFullU = 34, kArmorV = 9;

		const unsigned int kGreen = 0xFF55FF55u;
		const unsigned int kRed = 0xFFFF5555u;

		enum class RowType
		{
			Name,
			Text,
			Hearts,
			HealthNumbers,
			Armor,
		};

		struct Row
		{
			RowType type = RowType::Text;
			std::wstring text;
			unsigned int color = 0xFFFFFFu;
			int width = 0;
			int height = kLineHeight;
		};

		int currentTick()
		{
			Loader *loader = Loader::getInstance();
			return loader != nullptr ? loader->m_clientTickCount : 0;
		}

		std::wstring formatNumber(float value)
		{
			wchar_t buffer[32];
			if (std::fabs(value - std::round(value)) < 0.05f)
			{
				std::swprintf(buffer, 32, L"%d", static_cast<int>(std::round(value)));
			}
			else
			{
				std::swprintf(buffer, 32, L"%.1f", value);
			}
			return buffer;
		}

		int heartCount(float maxHealth)
		{
			return std::max(1, static_cast<int>(std::ceil(maxHealth / 2.0f)));
		}
	}

	Hud &Hud::get()
	{
		static Hud instance;
		return instance;
	}

	const Target &Hud::lastTarget(int pad) const
	{
		if (pad < 0 || pad >= kMaxPads) pad = 0;
		return m_pads[pad].target;
	}

	unsigned int Hud::withOpacity(unsigned int argb) const
	{
		const float opacity = std::max(0.0f, std::min(m_config.opacity, 1.0f));
		const unsigned int alpha = static_cast<unsigned int>(((argb >> 24) & 0xFFu) * opacity + 0.5f);
		return (alpha << 24) | (argb & 0x00FFFFFFu);
	}

	void Hud::render(Minecraft *mc, int centerX, int topY, int pad)
	{
		if (!m_config.enabled || mc == nullptr || mc->player == nullptr) return;
		if (pad < 0 || pad >= kMaxPads) pad = 0;

		PadState &state = m_pads[pad];
		Target target = resolve(mc, m_config.showEntities);
		if (target.kind == TargetKind::None)
		{
			state.key.clear();
			state.extraLines.clear();
			state.target = Target();
			return;
		}

		const std::string key = target.key();
		const int now = currentTick();
		if (key != state.key || now - state.refreshTick >= kRefreshTicks || now < state.refreshTick)
		{
			state.extraLines.clear();
			collectProviderLines(target, state.extraLines);
			state.key = key;
			state.refreshTick = now;
		}

		target.extraLines = state.extraLines;
		state.target = target;

		draw(mc, target, centerX, topY + m_config.offsetY);
	}

	void Hud::drawPanel(int x, int y, int w, int h)
	{
		// Classic tooltip frame: a dark body with notched corners and a thin
		// two-tone gradient border just inside it.
		const int bg = static_cast<int>(withOpacity(m_config.backgroundColor));
		const int borderTop = static_cast<int>(withOpacity(m_config.borderTopColor));
		const int borderBottom = static_cast<int>(withOpacity(m_config.borderBottomColor));

		fillGradient(x - 3, y - 4, x + w + 3, y - 3, bg, bg);
		fillGradient(x - 3, y + h + 3, x + w + 3, y + h + 4, bg, bg);
		fillGradient(x - 3, y - 3, x + w + 3, y + h + 3, bg, bg);
		fillGradient(x - 4, y - 3, x - 3, y + h + 3, bg, bg);
		fillGradient(x + w + 3, y - 3, x + w + 4, y + h + 3, bg, bg);

		fillGradient(x - 3, y - 2, x - 2, y + h + 2, borderTop, borderBottom);
		fillGradient(x + w + 2, y - 2, x + w + 3, y + h + 2, borderTop, borderBottom);
		fillGradient(x - 3, y - 3, x + w + 3, y - 2, borderTop, borderTop);
		fillGradient(x - 3, y + h + 2, x + w + 3, y + h + 3, borderBottom, borderBottom);
	}

	void Hud::drawHarvestMark(int x, int y, bool ok)
	{
		if (ok)
		{
			// 7x5 check mark
			static const int px[][2] = { {0,2}, {1,3}, {2,4}, {3,3}, {4,2}, {5,1}, {6,0} };
			for (const auto &p : px)
			{
				fill(x + p[0], y + p[1], x + p[0] + 1, y + p[1] + 2, static_cast<int>(kGreen));
			}
		}
		else
		{
			// 5x5 cross
			for (int i = 0; i < 5; ++i)
			{
				fill(x + 1 + i, y + i, x + 2 + i, y + i + 1, static_cast<int>(kRed));
				fill(x + 5 - i, y + i, x + 6 - i, y + i + 1, static_cast<int>(kRed));
			}
		}
	}

	void Hud::draw(Minecraft *mc, const Target &target, int centerX, int topY)
	{
		Font *font = mc->font;
		Textures *textures = mc->textures;
		if (font == nullptr || textures == nullptr) return;

		if (m_itemRenderer == nullptr)
		{
			m_itemRenderer = new ItemRenderer();
		}

		// ---- Build rows ------------------------------------------------------
		std::vector<Row> rows;

		std::shared_ptr<ItemInstance> tool;
		if (m_config.showHarvestTool && target.kind == TargetKind::Block)
		{
			tool = toolIcon(target.tool);
		}

		{
			Row name;
			name.type = RowType::Name;
			name.text = target.name;
			name.color = m_config.nameColor;
			name.width = font->width(name.text) + (tool != nullptr ? 3 + kToolSize + 1 + kMarkWidth : 0);
			rows.push_back(name);
		}

		const bool hasHealth = target.kind == TargetKind::Entity && target.maxHealth > 0.0f;
		if (m_config.showHealth && hasHealth)
		{
			const int hearts = heartCount(target.maxHealth);
			Row row;
			if (hearts <= std::max(1, m_config.maxHearts))
			{
				const int perRow = std::min(hearts, kHeartsPerRow);
				const int lines = (hearts + kHeartsPerRow - 1) / kHeartsPerRow;
				row.type = RowType::Hearts;
				row.width = perRow * kHeartStep + 1;
				row.height = lines * kLineHeight;
			}
			else
			{
				row.type = RowType::HealthNumbers;
				row.text = formatNumber(std::max(0.0f, target.health)) + L" / " + formatNumber(target.maxHealth);
				row.width = kHeartSize + 2 + font->width(row.text);
			}
			rows.push_back(row);
		}

		if (m_config.showArmor && target.kind == TargetKind::Entity && target.armor > 0)
		{
			const int icons = std::min((target.armor + 1) / 2, kHeartsPerRow);
			Row row;
			row.type = RowType::Armor;
			row.width = icons * kHeartStep + 1;
			rows.push_back(row);
		}

		if (m_config.showGrowth && target.kind == TargetKind::Block && target.growth >= 0.0f)
		{
			Row row;
			if (target.growth >= 0.999f)
			{
				row.text = L"§7" + m_config.growthLabel + L": §a" + m_config.matureLabel;
			}
			else
			{
				const int percent = static_cast<int>(target.growth * 100.0f + 0.5f);
				row.text = L"§7" + m_config.growthLabel + L": §f" + std::to_wstring(percent) + L"%";
			}
			row.width = font->width(row.text);
			rows.push_back(row);
		}

		for (const std::wstring &line : target.extraLines)
		{
			Row row;
			row.text = line;
			row.width = font->width(row.text);
			rows.push_back(row);
		}

		if (m_config.showModName && !target.modName.empty())
		{
			Row row;
			row.text = L"§o" + target.modName;
			row.color = m_config.modNameColor;
			row.width = font->width(row.text);
			rows.push_back(row);
		}

		// ---- Measure ---------------------------------------------------------
		const bool hasIcon = target.icon != nullptr;
		const int iconColumn = hasIcon ? kIconSize + kIconGap : 0;

		int textWidth = 0;
		int textHeight = 0;
		for (const Row &row : rows)
		{
			textWidth = std::max(textWidth, row.width);
			textHeight += row.height;
		}
		textHeight = std::max(0, textHeight - 2); // last line needs no leading

		const int width = iconColumn + textWidth;
		const int height = std::max(hasIcon ? kIconSize : 0, textHeight);

		const int x = centerX - width / 2;
		const int y = topY + kPanelMargin;
		const int textX = x + iconColumn;
		const int textY = y + std::max(0, (height - textHeight) / 2);

		// ---- Panel -----------------------------------------------------------
		drawPanel(x, y, width, height);

		// ---- Item icons (lit) ------------------------------------------------
		int toolX = 0, toolY = 0;
		if (tool != nullptr)
		{
			toolX = textX + font->width(target.name) + 3;
			toolY = textY - 1;
		}

		if (hasIcon || tool != nullptr)
		{
			glEnable(GL_RESCALE_NORMAL);
			Lighting::turnOnGui();

			if (hasIcon)
			{
				m_itemRenderer->renderGuiItem(font, textures, target.icon, x, y + (height - kIconSize) / 2);
			}

			if (tool != nullptr)
			{
				glPushMatrix();
				glTranslatef(static_cast<float>(toolX), static_cast<float>(toolY), 0.0f);
				glScalef(kToolScale, kToolScale, 1.0f);
				m_itemRenderer->renderGuiItem(font, textures, tool, 0, 0);
				glPopMatrix();
			}

			Lighting::turnOff();
			glDisable(GL_RESCALE_NORMAL);
		}

		// ---- Rows ------------------------------------------------------------
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		int rowY = textY;
		for (const Row &row : rows)
		{
			switch (row.type)
			{
			case RowType::Name:
			case RowType::Text:
				font->drawShadow(row.text, textX, rowY, static_cast<int>(row.color));
				break;

			case RowType::Hearts:
			{
				const int hearts = heartCount(target.maxHealth);
				const int halfHearts = static_cast<int>(std::ceil(std::max(0.0f, target.health)));
				textures->bindTexture(TN_GUI_ICONS);
				glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
				for (int i = 0; i < hearts; ++i)
				{
					const int hx = textX + (i % kHeartsPerRow) * kHeartStep;
					const int hy = rowY + (i / kHeartsPerRow) * kLineHeight;
					blit(hx, hy, kHeartEmptyU, kHeartV, kHeartSize, kHeartSize);
					if (i * 2 + 1 < halfHearts) blit(hx, hy, kHeartFullU, kHeartV, kHeartSize, kHeartSize);
					else if (i * 2 + 1 == halfHearts) blit(hx, hy, kHeartHalfU, kHeartV, kHeartSize, kHeartSize);
				}
				break;
			}

			case RowType::HealthNumbers:
				textures->bindTexture(TN_GUI_ICONS);
				glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
				blit(textX, rowY, kHeartEmptyU, kHeartV, kHeartSize, kHeartSize);
				blit(textX, rowY, kHeartFullU, kHeartV, kHeartSize, kHeartSize);
				font->drawShadow(row.text, textX + kHeartSize + 2, rowY + 1, 0xFFFFFF);
				break;

			case RowType::Armor:
			{
				const int icons = std::min((target.armor + 1) / 2, kHeartsPerRow);
				textures->bindTexture(TN_GUI_ICONS);
				glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
				for (int i = 0; i < icons; ++i)
				{
					const int ax = textX + i * kHeartStep;
					if (i * 2 + 1 < target.armor) blit(ax, rowY, kArmorFullU, kArmorV, kHeartSize, kHeartSize);
					else if (i * 2 + 1 == target.armor) blit(ax, rowY, kArmorHalfU, kArmorV, kHeartSize, kHeartSize);
					else blit(ax, rowY, kArmorEmptyU, kArmorV, kHeartSize, kHeartSize);
				}
				break;
			}
			}

			rowY += row.height;
		}

		if (tool != nullptr)
		{
			drawHarvestMark(toolX + kToolSize + 1, toolY + 3, target.canHarvest);
		}

		// ---- Mining progress along the bottom edge ---------------------------
		if (m_config.showBreakProgress && target.breakProgress > 0.0f)
		{
			const int barLeft = x - 3;
			const int barWidth = width + 6;
			const int filled = static_cast<int>(barWidth * target.breakProgress + 0.5f);
			fill(barLeft, y + height + 2, barLeft + filled, y + height + 3, static_cast<int>(withOpacity(0xFFFFFFFFu)));
		}

		// Leave GL the way Gui::render expects it (blending on for HUD fading).
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}
