#include "stdafx.h"
#include "RecipeViewer.h"
#include "RecipeData.h"

#include "Minecraft.h"
#include "GuiComponent.h"
#include "Font.h"
#include "Textures.h"
#include "ItemRenderer.h"
#include "Lighting.h"
#include "MultiPlayerGameMode.h"
#include "MultiPlayerLocalPlayer.h"
#include "net.minecraft.world.item.h"
#include "net.minecraft.world.inventory.h"
#include "net.minecraft.world.entity.player.h"
#include "HtmlString.h"
#include "Common/UI/UI.h"

#include <algorithm>
#include <cwctype>

namespace HSRecipeViewer
{
	namespace
	{
		// ---------------------------------------------------------------------------------------
		// state
		// ---------------------------------------------------------------------------------------
		enum HoverType
		{
			H_None = 0,
			H_ListPrev, H_ListNext, H_Search, H_Wrench, H_ListItem,
			H_CatPrev, H_CatNext, H_PagePrev, H_PageNext, H_Tab, H_Catalyst, H_RecipeSlot, H_Transfer,
			H_Window,
		};

		struct Hover
		{
			int type = H_None;
			int a = 0;				// list index / tab index / recipe index
			RVStack stack;			// item under the pointer (slots)
			bool output = false;	// recipe output slot
		};

		struct View
		{
			RVStack focus;
			bool uses = false;
			std::vector<int> cats;					// category indices
			std::vector<std::vector<int> > recipes;	// per category: recipe indices
			int cat = 0;
			int page = 0;
		};

		struct State
		{
			bool sceneOpen = false;
			SceneInfo scene;
			int scale = 2;
			bool hidden = false;
			bool cheat = false;

			int listPage = 0;
			std::wstring search;
			bool searchFocus = false;
			std::vector<int> filtered;
			bool filterDirty = true;

			bool viewOpen = false;
			View view;
			std::vector<View> history;

			Hover hover;
			unsigned int frame = 0;
			unsigned int lastClickFrame = 0xFFFFFFFF;
			unsigned int lastCancelFrame = 0xFFFFFFFF;
		};

		State &S()
		{
			static State *s = new State();
			return *s;
		}

		const int SLOT = 18;
		const int WIN_W = 176;

		// ---------------------------------------------------------------------------------------
		// drawing helpers (GUI units)
		// ---------------------------------------------------------------------------------------
		class Painter : public GuiComponent
		{
		public:
			void rect(int x0, int y0, int x1, int y1, int col) { fill(x0, y0, x1, y1, col); }
			void gradient(int x0, int y0, int x1, int y1, int top, int bottom) { fillGradient(x0, y0, x1, y1, top, bottom); }
		};

		Painter &P()
		{
			static Painter *p = new Painter();
			return *p;
		}

		ItemRenderer *IR()
		{
			static ItemRenderer *r = nullptr;
			if (r == nullptr) r = new ItemRenderer();
			return r;
		}

		bool g_drawing = false;		// render pass (false = hit-test pass)
		float g_px = -1, g_py = -1;	// pointer in GUI units
		Hover g_hit;

		bool inside(int x, int y, int w, int h)
		{
			return g_px >= x && g_px < x + w && g_py >= y && g_py < y + h;
		}

		void fill(int x0, int y0, int x1, int y1, int col)
		{
			if (!g_drawing) return;
			glDisable(GL_DEPTH_TEST);
			glDepthMask(false);
			P().rect(x0, y0, x1, y1, col);
		}

		void fillGradient(int x0, int y0, int x1, int y1, int top, int bottom)
		{
			if (!g_drawing) return;
			glDisable(GL_DEPTH_TEST);
			glDepthMask(false);
			P().gradient(x0, y0, x1, y1, top, bottom);
		}

		Font *font() { return Minecraft::GetInstance()->font; }

		int textWidth(const std::wstring &s) { return font()->width(s); }

		void text(const std::wstring &s, int x, int y, int col, bool shadow = true)
		{
			if (!g_drawing) return;
			glDisable(GL_DEPTH_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			if (shadow) font()->drawShadow(s, x, y, col);
			else font()->draw(s, x, y, col);
		}

		void textCentered(const std::wstring &s, int cx, int y, int col, bool shadow = true)
		{
			text(s, cx - textWidth(s) / 2, y, col, shadow);
		}

		std::shared_ptr<ItemInstance> toInstance(const RVStack &s)
		{
			if (s.id <= 0 || s.id >= Item::ITEM_NUM_COUNT || Item::items[s.id] == nullptr) return nullptr;
			return std::make_shared<ItemInstance>(s.id, (std::max)(1, s.count), s.aux < 0 ? 0 : s.aux);
		}

		void item(const std::shared_ptr<ItemInstance> &it, int x, int y, bool decorate)
		{
			if (!g_drawing || it == nullptr) return;
			Minecraft *mc = Minecraft::GetInstance();
			glEnable(GL_DEPTH_TEST);
			glDepthMask(true);
			glEnable(GL_RESCALE_NORMAL);
			Lighting::turnOnGui();
			IR()->renderAndDecorateItem(mc->font, mc->textures, it, x, y);
			Lighting::turnOff();
			glDisable(GL_RESCALE_NORMAL);
			if (decorate && it->count > 1)
			{
				glDisable(GL_DEPTH_TEST);
				IR()->renderGuiItemDecorations(mc->font, mc->textures, it, x, y);
			}
			glDisable(GL_DEPTH_TEST);
			glDepthMask(false);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		}

		void panel(int x, int y, int w, int h)
		{
			fill(x + 1, y, x + w - 1, y + h, 0xFF000000);
			fill(x, y + 1, x + w, y + h - 1, 0xFF000000);
			fill(x + 1, y + 1, x + w - 1, y + h - 1, 0xFFC6C6C6);
			fill(x + 1, y + 1, x + w - 2, y + 3, 0xFFFFFFFF);
			fill(x + 1, y + 1, x + 3, y + h - 2, 0xFFFFFFFF);
			fill(x + 3, y + h - 3, x + w - 1, y + h - 1, 0xFF555555);
			fill(x + w - 3, y + 3, x + w - 1, y + h - 1, 0xFF555555);
		}

		void slotFrame(int x, int y, int size = SLOT)
		{
			fill(x, y, x + size, y + size, 0xFF8B8B8B);
			fill(x, y, x + size - 1, y + 1, 0xFF373737);
			fill(x, y, x + 1, y + size - 1, 0xFF373737);
			fill(x + 1, y + size - 1, x + size, y + size, 0xFFFFFFFF);
			fill(x + size - 1, y + 1, x + size, y + size, 0xFFFFFFFF);
		}

		bool button(int x, int y, int w, int h, const std::wstring &label, int hoverType, bool enabled = true, int a = 0, int tint = 0)
		{
			const bool over = inside(x, y, w, h);
			if (!g_drawing)
			{
				if (over) { g_hit.type = hoverType; g_hit.a = a; }
				return over;
			}
			int bg = enabled ? (over ? 0xFF7E88BF : 0xFF6F6F6F) : 0xFF3A3A3A;
			if (tint != 0) bg = tint;
			fill(x, y, x + w, y + h, 0xFF000000);
			fill(x + 1, y + 1, x + w - 1, y + h - 1, bg);
			fill(x + 1, y + 1, x + w - 1, y + 2, enabled ? (over ? 0xFFBCC4F0 : 0xFFAAAAAA) : 0xFF555555);
			fill(x + 1, y + 1, x + 2, y + h - 1, enabled ? (over ? 0xFFBCC4F0 : 0xFFAAAAAA) : 0xFF555555);
			fill(x + 1, y + h - 2, x + w - 1, y + h - 1, 0xFF3C3C3C);
			fill(x + w - 2, y + 1, x + w - 1, y + h - 1, 0xFF3C3C3C);
			if (!label.empty())
			{
				const int col = !enabled ? 0xFFA0A0A0 : (over ? 0xFFFFFFA0 : 0xFFE0E0E0);
				textCentered(label, x + w / 2, y + (h - 8) / 2 + 1, col);
			}
			return over;
		}

		void arrowRight(int x, int y, int col = 0xFF8B8B8B)
		{
			fill(x, y + 5, x + 14, y + 10, col);
			for (int i = 0; i < 8; ++i) fill(x + 14 + i, y + i, x + 15 + i, y + 15 - i, col);
		}

		void arrowDown(int x, int y, int len, int col = 0xFFE0E0E0)
		{
			fill(x + 2, y, x + 5, y + len - 4, col);
			for (int i = 0; i < 4; ++i) fill(x + i - 1 + 1, y + len - 4 + i, x + 7 - i - 1 + 1, y + len - 3 + i, col);
		}

		void flame(int x, int y)
		{
			fill(x + 5, y + 1, x + 8, y + 4, 0xFFFFD040);
			fill(x + 3, y + 4, x + 11, y + 9, 0xFFFF9020);
			fill(x + 2, y + 9, x + 12, y + 13, 0xFFE05010);
			fill(x + 5, y + 7, x + 9, y + 12, 0xFFFFE070);
		}

		void wrench(int x, int y, int col)
		{
			for (int i = 0; i < 6; ++i) fill(x + 3 + i, y + 11 - i, x + 5 + i, y + 13 - i, col);
			fill(x + 9, y + 2, x + 13, y + 4, col);
			fill(x + 9, y + 2, x + 11, y + 7, col);
			fill(x + 11, y + 5, x + 14, y + 7, col);
			fill(x + 12, y + 2, x + 14, y + 5, col);
		}

		// an item slot; registers the hover and draws the item
		void itemSlot(int x, int y, const RVStack &stack, int hoverType, int a, bool frame, bool output = false, int size = SLOT)
		{
			const bool over = inside(x, y, size, size);
			if (!g_drawing)
			{
				if (over) { g_hit.type = hoverType; g_hit.a = a; g_hit.stack = stack; g_hit.output = output; }
				return;
			}
			if (frame) slotFrame(x, y, size);
			const int off = (size - 16) / 2;
			if (!stack.empty()) item(toInstance(stack), x + off, y + off, true);
			if (over && !stack.empty()) fill(x + 1, y + 1, x + size - 1, y + size - 1, 0x80FFFFFF);
		}

		RVStack cycled(const RVIngredient &ing)
		{
			if (ing.options.empty()) return RVStack();
			const size_t n = ing.options.size();
			return ing.options[(GetTickCount() / 1000) % n];
		}

		void tooltip(const std::vector<std::pair<std::wstring, int> > &lines, int x, int y)
		{
			if (lines.empty()) return;
			int w = 0;
			for (const auto &l : lines) w = (std::max)(w, textWidth(l.first));
			int h = 8 + (static_cast<int>(lines.size()) - 1) * 10 + (lines.size() > 1 ? 2 : 0);
			const int sw = S().scene.screenW / S().scale, sh = S().scene.screenH / S().scale;
			int tx = x + 12, ty = y - 12;
			if (tx + w + 4 > sw) tx = x - 16 - w;
			if (ty + h + 6 > sh) ty = sh - h - 6;
			if (ty < 4) ty = 4;
			if (tx < 4) tx = 4;
			const int bg = 0xF0100010, b0 = 0x505000FF, b1 = 0x5028007F;
			fill(tx - 3, ty - 4, tx + w + 3, ty - 3, bg);
			fill(tx - 3, ty + h + 3, tx + w + 3, ty + h + 4, bg);
			fill(tx - 3, ty - 3, tx + w + 3, ty + h + 3, bg);
			fill(tx - 4, ty - 3, tx - 3, ty + h + 3, bg);
			fill(tx + w + 3, ty - 3, tx + w + 4, ty + h + 3, bg);
			fillGradient(tx - 3, ty - 3 + 1, tx - 3 + 1, ty + h + 3 - 1, b0, b1);
			fillGradient(tx + w + 2, ty - 3 + 1, tx + w + 3, ty + h + 3 - 1, b0, b1);
			fill(tx - 3, ty - 3, tx + w + 3, ty - 3 + 1, b0);
			fill(tx - 3, ty + h + 2, tx + w + 3, ty + h + 3, b1);
			int yy = ty;
			for (size_t i = 0; i < lines.size(); ++i)
			{
				text(lines[i].first, tx, yy, lines[i].second);
				yy += (i == 0) ? 12 : 10;
			}
		}

		std::vector<std::pair<std::wstring, int> > itemTooltip(const RVStack &stack)
		{
			std::vector<std::pair<std::wstring, int> > lines;
			std::shared_ptr<ItemInstance> it = toInstance(stack);
			if (it == nullptr) return lines;
			Minecraft *mc = Minecraft::GetInstance();
			std::shared_ptr<Player> player = mc->localplayers[S().scene.pad];
			vector<HtmlString> *hover = it->getHoverText(player, false);
			if (hover != nullptr)
			{
				for (size_t i = 0; i < hover->size(); ++i)
				{
					const HtmlString &h = (*hover)[i];
					if (h.text.empty()) continue;
					int col = (i == 0) ? 0xFFFFFFFF : 0xFFAAAAAA;
					if (h.color != eMinecraftColour_NOT_SET) col = 0xFF000000 | (app.GetHTMLColour(h.color) & 0xFFFFFF);
					lines.push_back(std::make_pair(h.text, col));
				}
				delete hover;
			}
			if (lines.empty()) lines.push_back(std::make_pair(it->getHoverName(), 0xFFFFFFFF));
			lines.push_back(std::make_pair(std::wstring(L"\u00A7o") + Data::modName(stack.id, stack.aux), 0xFF5555FF));
			return lines;
		}

		// ---------------------------------------------------------------------------------------
		// item list
		// ---------------------------------------------------------------------------------------
		struct ListLayout
		{
			bool visible = false;
			int x0 = 0, x1 = 0;			// panel bounds
			int gridX = 0, gridY = 0, cols = 0, rows = 0;
			int navY = 0, searchY = 0;
		};

		ListLayout listLayout()
		{
			State &s = S();
			ListLayout l;
			const int sw = s.scene.screenW / s.scale, sh = s.scene.screenH / s.scale;
			l.x0 = static_cast<int>(s.scene.panelX1 / s.scale) + 6;
			l.x1 = sw - 4;
			if (l.x1 - l.x0 < SLOT * 3 + 8) return l;
			l.cols = (std::min)(12, (l.x1 - l.x0) / SLOT);
			l.navY = 4;
			l.searchY = sh - 4 - 16;
			l.gridY = l.navY + 20;
			l.rows = (l.searchY - 4 - l.gridY) / SLOT;
			if (l.rows < 1) return l;
			const int gridW = l.cols * SLOT;
			l.gridX = l.x0 + ((l.x1 - l.x0) - gridW) / 2;
			l.visible = true;
			return l;
		}

		void refilter()
		{
			State &s = S();
			if (!s.filterDirty) return;
			s.filterDirty = false;
			s.filtered.clear();

			std::vector<std::wstring> tokens;
			std::wstring cur;
			for (wchar_t c : s.search)
			{
				if (c == L' ') { if (!cur.empty()) tokens.push_back(cur); cur.clear(); }
				else cur.push_back(static_cast<wchar_t>(towlower(c)));
			}
			if (!cur.empty()) tokens.push_back(cur);

			const auto &items = Data::items();
			for (size_t i = 0; i < items.size(); ++i)
			{
				bool ok = true;
				for (const std::wstring &t : tokens)
				{
					if (t[0] == L'@')
					{
						if (t.size() > 1 && Data::searchMod(i).find(t.substr(1)) == std::wstring::npos) { ok = false; break; }
					}
					else if (Data::searchName(i).find(t) == std::wstring::npos) { ok = false; break; }
				}
				if (ok) s.filtered.push_back(static_cast<int>(i));
			}
		}

		void walkList()
		{
			State &s = S();
			ListLayout l = listLayout();
			if (!l.visible) return;
			refilter();

			const int perPage = l.cols * l.rows;
			const int pages = (std::max)(1, (static_cast<int>(s.filtered.size()) + perPage - 1) / perPage);
			if (s.listPage >= pages) s.listPage = pages - 1;
			if (s.listPage < 0) s.listPage = 0;

			const int gridW = l.cols * SLOT;
			button(l.gridX, l.navY, 20, 16, L"<", H_ListPrev);
			button(l.gridX + gridW - 20, l.navY, 20, 16, L">", H_ListNext);
			textCentered(std::to_wstring(s.listPage + 1) + L"/" + std::to_wstring(pages), l.gridX + gridW / 2, l.navY + 4, 0xFFFFFFFF);

			const auto &items = Data::items();
			const int start = s.listPage * perPage;
			for (int k = 0; k < perPage && start + k < static_cast<int>(s.filtered.size()); ++k)
			{
				const int index = s.filtered[start + k];
				const int x = l.gridX + (k % l.cols) * SLOT, y = l.gridY + (k / l.cols) * SLOT;
				const std::shared_ptr<ItemInstance> &it = items[index];
				RVStack st; st.id = it->id; st.aux = it->getAuxValue();
				const bool over = inside(x, y, SLOT, SLOT);
				if (!g_drawing)
				{
					if (over) { g_hit.type = H_ListItem; g_hit.a = index; g_hit.stack = st; }
					continue;
				}
				if (over) fill(x, y, x + SLOT, y + SLOT, 0x80FFFFFF);
				item(it, x + 1, y + 1, false);
			}

			// search box + wrench
			const int boxX = l.gridX, boxW = gridW - 20;
			const bool overBox = inside(boxX, l.searchY, boxW, 16);
			if (!g_drawing && overBox) g_hit.type = H_Search;
			fill(boxX, l.searchY, boxX + boxW, l.searchY + 16, s.searchFocus ? 0xFFFFFFFF : 0xFFA0A0A0);
			fill(boxX + 1, l.searchY + 1, boxX + boxW - 1, l.searchY + 15, 0xFF000000);
			if (g_drawing)
			{
				std::wstring shown = s.search;
				while (!shown.empty() && textWidth(shown) > boxW - 10) shown.erase(0, 1);
				if (s.searchFocus && (GetTickCount() / 500) % 2 == 0) shown += L"_";
				text(shown, boxX + 4, l.searchY + 4, s.filtered.empty() && !s.search.empty() ? 0xFFFF5555 : 0xFFFFFFFF);
			}
			const bool overWrench = button(boxX + boxW + 4, l.searchY, 16, 16, L"", H_Wrench);
			if (g_drawing) wrench(boxX + boxW + 5, l.searchY + 1, s.cheat ? 0xFFFF6060 : (overWrench ? 0xFFFFFFA0 : 0xFFE0E0E0));
		}

		// ---------------------------------------------------------------------------------------
		// recipe window
		// ---------------------------------------------------------------------------------------
		struct WinLayout
		{
			int x = 0, y = 0, w = WIN_W, h = 0;
			int areaY = 0, areaH = 0;
		};

		WinLayout winLayout()
		{
			State &s = S();
			WinLayout w;
			const int sw = s.scene.screenW / s.scale, sh = s.scene.screenH / s.scale;
			w.h = (std::min)(sh - 40, 240);
			w.x = (sw - w.w) / 2;
			w.y = (sh - w.h) / 2 + 12;
			w.areaY = w.y + 38;
			w.areaH = w.h - 38 - 6;
			return w;
		}

		int recipeHeight(const RVCategory &cat)
		{
			switch (cat.layout)
			{
			case Layout_Crafting: return 54;
			case Layout_Smelting: return 44;
			case Layout_Fuel: return 30;
			case Layout_Brewing: return 64;
			case Layout_Anvil: return 30;
			default:
			{
				size_t lines = 0;
				for (const RVRecipe &r : cat.recipes) lines = (std::max)(lines, r.lines.size());
				return 28 + static_cast<int>((std::min)(lines, static_cast<size_t>(4))) * 10;
			}
			}
		}

		int recipeWidth(const RVCategory &cat)
		{
			switch (cat.layout)
			{
			case Layout_Crafting: return 134;
			case Layout_Smelting: return 120;
			case Layout_Fuel: return 140;
			case Layout_Brewing: return 130;
			case Layout_Anvil: return 116;
			default: return 160;
			}
		}

		int perPage(const RVCategory &cat, const WinLayout &w)
		{
			return (std::max)(1, w.areaH / (recipeHeight(cat) + 6));
		}

		// --- crafting transfer --------------------------------------------------------------------
		struct GridInfo { int gridStart = -1, gridW = 0, invStart = 0, invEnd = 0; };

		GridInfo gridInfo()
		{
			GridInfo g;
			const int grid = S().scene.craftGrid;
			if (grid == 3)
			{
				g.gridStart = CraftingMenu::CRAFT_SLOT_START; g.gridW = 3;
				g.invStart = CraftingMenu::INV_SLOT_START; g.invEnd = CraftingMenu::USE_ROW_SLOT_END;
			}
			else if (grid == 2)
			{
				g.gridStart = InventoryMenu::CRAFT_SLOT_START; g.gridW = 2;
				g.invStart = InventoryMenu::INV_SLOT_START; g.invEnd = InventoryMenu::USE_ROW_SLOT_END;
			}
			return g;
		}

		AbstractContainerMenu *openMenu()
		{
			Minecraft *mc = Minecraft::GetInstance();
			std::shared_ptr<MultiplayerLocalPlayer> p = mc->localplayers[S().scene.pad];
			if (p == nullptr) return nullptr;
			return p->containerMenu;
		}

		// target grid cell of recipe cell c, -1 if it does not fit
		int targetCell(const RVRecipe &r, int c, int order, int gridW)
		{
			if (r.shapeless)
			{
				const int tx = order % gridW, ty = order / gridW;
				return (ty < gridW) ? tx + ty * gridW : -1;
			}
			const int x = c % 3, y = c / 3;
			if (x >= gridW || y >= gridW) return -1;
			return x + y * gridW;
		}

		// 0 ok, 1 missing items, 2 no grid, 3 too large; fills the missing cells (recipe cell indices)
		int transferState(const RVRecipe &r, std::vector<int> *missing)
		{
			GridInfo g = gridInfo();
			if (g.gridStart < 0) return 2;
			if (r.width > g.gridW || r.height > g.gridW) return 3;
			AbstractContainerMenu *menu = openMenu();
			if (menu == nullptr) return 2;

			std::vector<std::pair<int, int> > counts;	// slot -> available
			for (int i = g.invStart; i < g.invEnd && i < static_cast<int>(menu->slots.size()); ++i)
			{
				std::shared_ptr<ItemInstance> it = menu->getSlot(i)->getItem();
				if (it != nullptr) counts.push_back(std::make_pair(i, it->count));
			}
			// items already in the grid count too (they get moved back first)
			for (int i = 0; i < g.gridW * g.gridW; ++i)
			{
				std::shared_ptr<ItemInstance> it = menu->getSlot(g.gridStart + i)->getItem();
				if (it != nullptr) counts.push_back(std::make_pair(g.gridStart + i, it->count));
			}

			int state = 0;
			for (int c = 0; c < 9 && c < static_cast<int>(r.inputs.size()); ++c)
			{
				const RVIngredient &ing = r.inputs[c];
				if (ing.empty()) continue;
				bool found = false;
				for (auto &e : counts)
				{
					if (e.second <= 0) continue;
					std::shared_ptr<ItemInstance> it = menu->getSlot(e.first)->getItem();
					if (it != nullptr && Data::ingredientMatches(ing, it->id, it->getAuxValue())) { e.second--; found = true; break; }
				}
				if (!found) { state = 1; if (missing) missing->push_back(c); }
			}
			return state;
		}

		void transfer(const RVRecipe &r)
		{
			if (transferState(r, nullptr) == 2) return;
			GridInfo g = gridInfo();
			Minecraft *mc = Minecraft::GetInstance();
			const int pad = S().scene.pad;
			std::shared_ptr<MultiplayerLocalPlayer> player = mc->localplayers[pad];
			MultiPlayerGameMode *gm = mc->localgameModes[pad];
			AbstractContainerMenu *menu = openMenu();
			if (player == nullptr || gm == nullptr || menu == nullptr) return;
			if (player->inventory->getCarried() != nullptr) return;
			const int cid = menu->containerId;

			// empty the grid into the inventory
			for (int i = 0; i < g.gridW * g.gridW; ++i)
			{
				if (menu->getSlot(g.gridStart + i)->hasItem()) gm->handleInventoryMouseClick(cid, g.gridStart + i, 0, true, player);
			}

			int order = 0;
			for (int c = 0; c < 9 && c < static_cast<int>(r.inputs.size()); ++c)
			{
				const RVIngredient &ing = r.inputs[c];
				if (ing.empty()) continue;
				const int cell = targetCell(r, c, order++, g.gridW);
				if (cell < 0) continue;
				int src = -1;
				for (int i = g.invStart; i < g.invEnd && i < static_cast<int>(menu->slots.size()); ++i)
				{
					std::shared_ptr<ItemInstance> it = menu->getSlot(i)->getItem();
					if (it != nullptr && it->count > 0 && Data::ingredientMatches(ing, it->id, it->getAuxValue())) { src = i; break; }
				}
				if (src < 0) continue;
				gm->handleInventoryMouseClick(cid, src, 0, false, player);			// pick up the stack
				gm->handleInventoryMouseClick(cid, g.gridStart + cell, 1, false, player);	// drop one
				if (player->inventory->getCarried() != nullptr) gm->handleInventoryMouseClick(cid, src, 0, false, player);	// put the rest back
			}
		}

		// --- recipe drawing -------------------------------------------------------------------------
		void drawLines(const RVRecipe &r, int x, int y, int maxLines, int col = 0xFF404040)
		{
			for (int i = 0; i < maxLines && i < static_cast<int>(r.lines.size()); ++i) text(r.lines[i], x, y + i * 10, col, false);
		}

		RVStack ingStack(const RVRecipe &r, size_t i)
		{
			return i < r.inputs.size() ? cycled(r.inputs[i]) : RVStack();
		}

		void walkRecipe(const RVCategory &cat, const RVRecipe &r, int recipeIndex, int ox, int oy)
		{
			switch (cat.layout)
			{
			case Layout_Crafting:
			{
				std::vector<int> missing;
				bool overTransfer = false;
				const int state = transferState(r, &missing);
				// transfer button first so the hover over it is known when drawing the grid
				{
					const int bx = ox + 122, by = oy + 42;
					const int tint = (state == 1) ? 0xFF8A3030 : 0;
					overTransfer = button(bx, by, 12, 12, L"+", H_Transfer, state == 0 || state == 1, recipeIndex, tint);
				}
				for (int c = 0; c < 9; ++c)
				{
					const int x = ox + (c % 3) * SLOT, y = oy + (c / 3) * SLOT;
					itemSlot(x, y, ingStack(r, c), H_RecipeSlot, recipeIndex, true);
					if (g_drawing && overTransfer && std::find(missing.begin(), missing.end(), c) != missing.end())
						fill(x + 1, y + 1, x + SLOT - 1, y + SLOT - 1, 0x80FF4040);
				}
				arrowRight(ox + 61, oy + 19);
				itemSlot(ox + 90, oy + 14, r.outputs.empty() ? RVStack() : r.outputs[0], H_RecipeSlot, recipeIndex, true, true, 26);
				if (r.shapeless && g_drawing) text(L"~", ox + 122, oy + 2, 0xFF606060, false);
				break;
			}
			case Layout_Smelting:
				itemSlot(ox, oy, ingStack(r, 0), H_RecipeSlot, recipeIndex, true);
				if (g_drawing) flame(ox + 2, oy + 20);
				arrowRight(ox + 26, oy + 1);
				itemSlot(ox + 56, oy - 4, r.outputs.empty() ? RVStack() : r.outputs[0], H_RecipeSlot, recipeIndex, true, true, 26);
				drawLines(r, ox + 26, oy + 24, 2);
				break;
			case Layout_Fuel:
				itemSlot(ox, oy + 4, ingStack(r, 0), H_RecipeSlot, recipeIndex, true);
				if (g_drawing) flame(ox + 22, oy + 6);
				drawLines(r, ox + 42, oy + 4, 2);
				break;
			case Layout_Brewing:
			{
				// bubbles
				if (g_drawing)
				{
					const int bx = ox + 8, by = oy + 2;
					const int phase = static_cast<int>((GetTickCount() / 120) % 14);
					for (int i = 0; i < 5; ++i)
					{
						const int yy = by + 14 - ((phase + i * 3) % 14);
						const int xx = bx + ((i % 2) ? 3 : 0);
						fill(xx, yy, xx + 2, yy + 2, 0xFFFFFFFF);
					}
				}
				// pipes from the ingredient to the bottles
				fill(ox + 32, oy + 18, ox + 34, oy + 44, 0xFF606060);
				fill(ox + 8, oy + 30, ox + 58, oy + 32, 0xFF606060);
				fill(ox + 8, oy + 30, ox + 10, oy + 38, 0xFF606060);
				fill(ox + 56, oy + 30, ox + 58, oy + 38, 0xFF606060);
				itemSlot(ox + 24, oy, ingStack(r, 0), H_RecipeSlot, recipeIndex, true);
				itemSlot(ox + 0, oy + 38, ingStack(r, 1), H_RecipeSlot, recipeIndex, true);
				itemSlot(ox + 24, oy + 44, ingStack(r, 2), H_RecipeSlot, recipeIndex, true);
				itemSlot(ox + 48, oy + 38, ingStack(r, 3), H_RecipeSlot, recipeIndex, true);
				if (g_drawing) arrowDown(ox + 50, oy + 1, 26);
				itemSlot(ox + 84, oy, r.outputs.empty() ? RVStack() : r.outputs[0], H_RecipeSlot, recipeIndex, true, true);
				drawLines(r, ox + 74, oy + 26, 1, 0xFF808080);
				break;
			}
			case Layout_Anvil:
				itemSlot(ox, oy + 4, ingStack(r, 0), H_RecipeSlot, recipeIndex, true);
				textCentered(L"+", ox + 27, oy + 9, 0xFF404040, false);
				itemSlot(ox + 36, oy + 4, ingStack(r, 1), H_RecipeSlot, recipeIndex, true);
				arrowRight(ox + 60, oy + 5);
				itemSlot(ox + 88, oy, r.outputs.empty() ? RVStack() : r.outputs[0], H_RecipeSlot, recipeIndex, true, true, 26);
				break;
			default:	// machines
			{
				itemSlot(ox, oy + 4, ingStack(r, 0), H_RecipeSlot, recipeIndex, true);
				const bool extra = r.inputs.size() > 1 && !r.inputs[1].empty();
				if (extra) itemSlot(ox + 20, oy + 4, ingStack(r, 1), H_RecipeSlot, recipeIndex, true);
				arrowRight(ox + 44, oy + 5);
				if (!r.outputs.empty() && !r.outputs[0].empty())
					itemSlot(ox + 72, oy, r.outputs[0], H_RecipeSlot, recipeIndex, true, true, 26);
				if (r.outputs.size() > 1 && !r.outputs[1].empty())
					itemSlot(ox + 102, oy + 4, r.outputs[1], H_RecipeSlot, recipeIndex, true, true);
				drawLines(r, ox, oy + 28, 4);
				break;
			}
			}
		}

		void walkWindow()
		{
			State &s = S();
			if (!s.viewOpen) return;
			const auto &cats = Data::categories();
			View &v = s.view;
			if (v.cats.empty()) { s.viewOpen = false; return; }
			if (v.cat < 0) v.cat = 0;
			if (v.cat >= static_cast<int>(v.cats.size())) v.cat = static_cast<int>(v.cats.size()) - 1;
			const RVCategory &cat = cats[v.cats[v.cat]];
			const std::vector<int> &list = v.recipes[v.cat];

			const int sw = s.scene.screenW / s.scale, sh = s.scene.screenH / s.scale;
			WinLayout w = winLayout();

			// dim the game and the container behind the window
			fillGradient(0, 0, sw, sh, 0xC0101010, 0xD0101010);

			if (!g_drawing && inside(w.x - 26, w.y - 24, w.w + 30, w.h + 28)) g_hit.type = H_Window;

			// category tabs (scrolling so the current one is visible)
			const int maxTabs = (std::max)(1, (std::min)((sw - w.x - 4) / 24, 12));
			int first = 0;
			if (v.cat >= maxTabs) first = v.cat - maxTabs + 1;
			for (int t = first; t < static_cast<int>(v.cats.size()) && t < first + maxTabs; ++t)
			{
				const int tx = w.x + (t - first) * 24, ty = w.y - 22;
				const bool sel = (t == v.cat);
				const bool over = inside(tx, ty, 24, 24);
				if (!g_drawing && over) { g_hit.type = H_Tab; g_hit.a = t; }
				if (g_drawing)
				{
					panel(tx, ty, 24, sel ? 26 : 23);
					if (!sel) fill(tx + 1, ty + 1, tx + 23, ty + 22, over ? 0x30FFFFFF : 0x40000000);
					item(toInstance(cats[v.cats[t]].icon), tx + 4, ty + 4, false);
				}
			}

			// catalysts on the left
			if (!cat.catalysts.empty())
			{
				const int n = (std::min)(static_cast<int>(cat.catalysts.size()), (w.h - 10) / SLOT);
				const int cx = w.x - 24, cy = w.y + 4;
				panel(cx, cy, 26, n * SLOT + 8);
				for (int i = 0; i < n; ++i) itemSlot(cx + 4, cy + 4 + i * SLOT, cat.catalysts[i], H_Catalyst, i, true);
			}

			panel(w.x, w.y, w.w, w.h);
			if (g_drawing) fill(w.x + 3, w.y, w.x + w.w - 3, w.y + 3, 0xFFC6C6C6);	// joins the selected tab

			// category and page rows
			const int pp = perPage(cat, w);
			const int pages = (std::max)(1, (static_cast<int>(list.size()) + pp - 1) / pp);
			if (v.page >= pages) v.page = pages - 1;
			if (v.page < 0) v.page = 0;

			button(w.x + 5, w.y + 5, 13, 13, L"<", H_CatPrev, v.cats.size() > 1);
			button(w.x + w.w - 18, w.y + 5, 13, 13, L">", H_CatNext, v.cats.size() > 1);
			fill(w.x + 19, w.y + 5, w.x + w.w - 19, w.y + 18, 0xFF8B8B8B);
			{
				std::wstring title = cat.name;
				while (title.size() > 3 && textWidth(title) > w.w - 44) title.resize(title.size() - 1);
				textCentered(title, w.x + w.w / 2, w.y + 8, 0xFFFFFFFF);
			}
			button(w.x + 5, w.y + 21, 13, 13, L"<", H_PagePrev, pages > 1);
			button(w.x + w.w - 18, w.y + 21, 13, 13, L">", H_PageNext, pages > 1);
			fill(w.x + 19, w.y + 21, w.x + w.w - 19, w.y + 34, 0xFF8B8B8B);
			textCentered(std::to_wstring(v.page + 1) + L"/" + std::to_wstring(pages), w.x + w.w / 2, w.y + 24, 0xFFFFFFFF);

			// recipes
			const int rh = recipeHeight(cat), rw = recipeWidth(cat);
			const int used = pp * (rh + 6) - 6;
			int oy = w.areaY + (std::max)(0, (w.areaH - used) / 2);
			const int ox = w.x + (w.w - rw) / 2;
			for (int k = 0; k < pp; ++k)
			{
				const int idx = v.page * pp + k;
				if (idx >= static_cast<int>(list.size())) break;
				walkRecipe(cat, cat.recipes[list[idx]], list[idx], ox, oy);
				oy += rh + 6;
			}
		}

		// one full pass over the overlay (drawing or hit testing)
		void walk(bool drawing)
		{
			g_drawing = drawing;
			if (!drawing) g_hit = Hover();
			walkList();
			walkWindow();
		}

		void updateHover()
		{
			State &s = S();
			g_px = s.scene.pointerX / s.scale;
			g_py = s.scene.pointerY / s.scale;
			walk(false);
			s.hover = g_hit;
		}

		bool overUi()
		{
			State &s = S();
			if (s.viewOpen) return true;
			ListLayout l = listLayout();
			return l.visible && g_px >= l.x0 - 2;
		}

		// ---------------------------------------------------------------------------------------
		// actions
		// ---------------------------------------------------------------------------------------
		bool openView(const RVStack &focus, bool uses)
		{
			State &s = S();
			if (focus.empty()) return false;
			std::vector<Data::Hit> hits;
			if (uses) Data::findInputs(focus.id, focus.aux, hits);
			else Data::findOutputs(focus.id, focus.aux, hits);
			if (hits.empty()) return false;

			View v;
			v.focus = focus;
			v.uses = uses;
			for (const Data::Hit &h : hits)
			{
				auto it = std::find(v.cats.begin(), v.cats.end(), h.category);
				if (it == v.cats.end())
				{
					v.cats.push_back(h.category);
					v.recipes.push_back(std::vector<int>());
					it = v.cats.end() - 1;
				}
				v.recipes[it - v.cats.begin()].push_back(h.recipe);
			}
			if (s.viewOpen) s.history.push_back(s.view);
			if (s.history.size() > 32) s.history.erase(s.history.begin());
			s.view = v;
			s.viewOpen = true;
			ui.PlayUISFX(eSFX_Press);
			return true;
		}

		void closeView()
		{
			State &s = S();
			s.viewOpen = false;
			s.history.clear();
		}

		void back()
		{
			State &s = S();
			if (s.history.empty()) { closeView(); return; }
			s.view = s.history.back();
			s.history.pop_back();
		}

		void giveItem(const RVStack &st, bool fullStack)
		{
			State &s = S();
			if (!s.scene.creative) return;
			Minecraft *mc = Minecraft::GetInstance();
			std::shared_ptr<MultiplayerLocalPlayer> player = mc->localplayers[s.scene.pad];
			MultiPlayerGameMode *gm = mc->localgameModes[s.scene.pad];
			if (player == nullptr || gm == nullptr) return;
			std::shared_ptr<ItemInstance> it = toInstance(st);
			if (it == nullptr) return;
			it->count = fullStack ? it->getMaxStackSize() : 1;
			// first empty inventory slot (hotbar first), as inventory-menu slot numbers
			for (int i = 0; i < 36; ++i)
			{
				if (player->inventory->items[i] == nullptr)
				{
					const int menuSlot = (i < 9) ? InventoryMenu::USE_ROW_SLOT_START + i : i;
					player->inventory->items[i] = it;
					gm->handleCreativeModeItemAdd(it, menuSlot);
					ui.PlayUISFX(eSFX_Press);
					return;
				}
			}
		}

		void click(int button)	// 0 left, 1 right
		{
			State &s = S();
			const Hover h = s.hover;
			const auto &cats = Data::categories();
			ListLayout l = listLayout();
			const bool shift = g_KBMInput.IsKeyDown(VK_SHIFT) || g_KBMInput.IsKeyDown(VK_LSHIFT);

			if (h.type != H_Search && s.searchFocus) s.searchFocus = false;

			switch (h.type)
			{
			case H_ListPrev: s.listPage--; ui.PlayUISFX(eSFX_Press); break;
			case H_ListNext: s.listPage++; ui.PlayUISFX(eSFX_Press); break;
			case H_Search:
				if (button == 1) { s.search.clear(); s.filterDirty = true; s.listPage = 0; }
				s.searchFocus = true;
				g_KBMInput.ClearCharBuffer();
				break;
			case H_Wrench:
				s.cheat = !s.cheat;
				ui.PlayUISFX(eSFX_Press);
				break;
			case H_ListItem:
				if (s.cheat && s.scene.creative) giveItem(h.stack, button == 0 || shift);
				else openView(h.stack, button == 1);
				break;
			case H_CatPrev:
				if (!s.view.cats.empty()) { s.view.cat = (s.view.cat + static_cast<int>(s.view.cats.size()) - 1) % static_cast<int>(s.view.cats.size()); s.view.page = 0; }
				break;
			case H_CatNext:
				if (!s.view.cats.empty()) { s.view.cat = (s.view.cat + 1) % static_cast<int>(s.view.cats.size()); s.view.page = 0; }
				break;
			case H_PagePrev: s.view.page--; break;
			case H_PageNext: s.view.page++; break;
			case H_Tab: s.view.cat = h.a; s.view.page = 0; break;
			case H_Catalyst:
			case H_RecipeSlot:
				openView(h.stack, button == 1);
				break;
			case H_Transfer:
				if (s.viewOpen && s.view.cat < static_cast<int>(s.view.cats.size()))
				{
					const RVCategory &cat = cats[s.view.cats[s.view.cat]];
					if (h.a >= 0 && h.a < static_cast<int>(cat.recipes.size()))
					{
						const RVRecipe &r = cat.recipes[h.a];
						if (transferState(r, nullptr) <= 1)
						{
							transfer(r);
							closeView();
							ui.PlayUISFX(eSFX_Press);
						}
					}
				}
				break;
			default:
				break;
			}
			(void)l;
		}

		RVStack hoveredStack()
		{
			State &s = S();
			if (s.hover.type == H_ListItem || s.hover.type == H_RecipeSlot || s.hover.type == H_Catalyst) return s.hover.stack;
			if (!s.viewOpen && s.scene.hovered != nullptr)
			{
				RVStack st; st.id = s.scene.hovered->id; st.aux = s.scene.hovered->getAuxValue();
				return st;
			}
			return RVStack();
		}
	}

	// ===========================================================================================
	// public API
	// ===========================================================================================
	bool enabledFor(int pad)
	{
		return pad == 0 && g_KBMInput.IsKBMActive() && !S().hidden;
	}

	void updateScene(const SceneInfo &info)
	{
		State &s = S();
		const bool wasOpen = s.sceneOpen;
		s.scene = info;
		s.sceneOpen = true;
		s.frame++;
		s.scale = (std::max)(1, (info.screenH + 180) / 360);

		if (!wasOpen)
		{
			Data::ensureBuilt();
			s.filterDirty = true;
		}

		updateHover();

		// keyboard
		if (s.searchFocus)
		{
			wchar_t c;
			while (g_KBMInput.ConsumeChar(c))
			{
				if (c == 0x08) { if (!s.search.empty()) s.search.erase(s.search.size() - 1); }
				else if (c == 0x0D) { s.searchFocus = false; }
				else if (c >= 0x20 && s.search.size() < 48) s.search.push_back(c);
				s.filterDirty = true;
				s.listPage = 0;
			}
			if (g_KBMInput.IsKeyPressed(VK_ESCAPE)) s.searchFocus = false;
			return;
		}

		const bool ctrl = g_KBMInput.IsKeyDown(VK_CONTROL);
		if (ctrl && g_KBMInput.IsKeyPressed('F')) { s.searchFocus = true; g_KBMInput.ClearCharBuffer(); return; }
		if (g_KBMInput.IsKeyPressed('R') || g_KBMInput.IsKeyPressed('U'))
		{
			const RVStack st = hoveredStack();
			if (!st.empty()) openView(st, g_KBMInput.IsKeyPressed('U'));
		}
		if (s.viewOpen && g_KBMInput.IsKeyPressed(VK_BACK)) back();
	}

	void sceneClosed(int pad)
	{
		State &s = S();
		if (pad != s.scene.pad) return;
		s.sceneOpen = false;
		s.viewOpen = false;
		s.searchFocus = false;
		s.history.clear();
		s.scene.hovered = nullptr;
	}

	bool wantsFreePointer()
	{
		return S().sceneOpen && !S().hidden;
	}

	bool handleAction(int pad, int action, bool pressed, bool released, bool repeat)
	{
		State &s = S();
		if (!s.sceneOpen || s.hidden || pad != s.scene.pad || !g_KBMInput.IsKBMActive()) return false;

		g_px = s.scene.pointerX / s.scale;
		g_py = s.scene.pointerY / s.scale;
		const bool over = overUi() || s.hover.type != H_None;

		switch (action)
		{
		case ACTION_MENU_A:
		case ACTION_MENU_OK:
			if (!over)
			{
				if (pressed && s.searchFocus) s.searchFocus = false;
				return false;
			}
			if (pressed && !repeat && s.lastClickFrame != s.frame)
			{
				s.lastClickFrame = s.frame;
				click(0);
			}
			return true;
		case ACTION_MENU_X:
			if (!over) return false;
			if (pressed && !repeat) click(1);
			return true;
		case ACTION_MENU_B:
		case ACTION_MENU_CANCEL:
			if (s.searchFocus) { if (pressed) s.searchFocus = false; return true; }
			if (s.viewOpen)
			{
				if (pressed && s.lastCancelFrame != s.frame) { s.lastCancelFrame = s.frame; closeView(); }
				return true;
			}
			return false;
		case ACTION_MENU_UP:
		case ACTION_MENU_DOWN:
		case ACTION_MENU_OTHER_STICK_UP:
		case ACTION_MENU_OTHER_STICK_DOWN:
		{
			const bool up = (action == ACTION_MENU_UP || action == ACTION_MENU_OTHER_STICK_UP);
			if (s.viewOpen) { if (pressed) s.view.page += up ? -1 : 1; return true; }
			if (over) { if (pressed) s.listPage += up ? -1 : 1; return true; }
			return false;
		}
		case ACTION_MENU_LEFT:
		case ACTION_MENU_RIGHT:
			if (s.viewOpen) { if (pressed) s.view.page += (action == ACTION_MENU_LEFT) ? -1 : 1; return true; }
			return false;
		case ACTION_MENU_LEFT_SCROLL:
		case ACTION_MENU_RIGHT_SCROLL:
			if (s.viewOpen)
			{
				if (pressed && !s.view.cats.empty())
				{
					const int n = static_cast<int>(s.view.cats.size());
					s.view.cat = (s.view.cat + (action == ACTION_MENU_LEFT_SCROLL ? n - 1 : 1)) % n;
					s.view.page = 0;
				}
				return true;
			}
			return false;
		default:
			return s.viewOpen;	// nothing reaches the container while the recipe window is open
		}
	}

	bool capturesKeyboard()
	{
		return S().sceneOpen && S().searchFocus;
	}

	bool reservesKey(int vk)
	{
		if (!S().sceneOpen || S().hidden) return false;
		return vk == 'R' || vk == 'U';
	}

	void render()
	{
		State &s = S();
		if (!s.sceneOpen || s.hidden) return;
		Minecraft *mc = Minecraft::GetInstance();
		if (mc == nullptr || mc->font == nullptr || mc->textures == nullptr) return;
		if (s.scene.screenW <= 0 || s.scene.screenH <= 0) return;

		ui.setupCustomDrawGameState();
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, static_cast<float>(s.scene.screenW), static_cast<float>(s.scene.screenH), 0, 1000, 3000);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glTranslatef(0, 0, -2000);
		glScalef(static_cast<float>(s.scale), static_cast<float>(s.scale), 1.0f);
		glClear(GL_DEPTH_BUFFER_BIT);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		updateHover();
		walk(true);

		// tooltips
		const Hover &h = s.hover;
		if (h.type == H_ListItem || h.type == H_RecipeSlot || h.type == H_Catalyst)
		{
			if (!h.stack.empty()) tooltip(itemTooltip(h.stack), static_cast<int>(g_px), static_cast<int>(g_py));
		}
		else if (h.type == H_Transfer && s.viewOpen)
		{
			const auto &cats = Data::categories();
			const RVCategory &cat = cats[s.view.cats[s.view.cat]];
			std::vector<std::pair<std::wstring, int> > lines;
			if (h.a >= 0 && h.a < static_cast<int>(cat.recipes.size()))
			{
				switch (transferState(cat.recipes[h.a], nullptr))
				{
				case 0: lines.push_back(std::make_pair(std::wstring(L"Move Items"), 0xFFFFFFFF)); break;
				case 1: lines.push_back(std::make_pair(std::wstring(L"Missing Items"), 0xFFFF5555)); break;
				case 2: lines.push_back(std::make_pair(std::wstring(L"Open a crafting grid first"), 0xFFFF5555)); break;
				default: lines.push_back(std::make_pair(std::wstring(L"Recipe too large for this grid"), 0xFFFF5555)); break;
				}
			}
			tooltip(lines, static_cast<int>(g_px), static_cast<int>(g_py));
		}
		else if (h.type == H_Wrench)
		{
			std::vector<std::pair<std::wstring, int> > lines;
			lines.push_back(std::make_pair(std::wstring(s.cheat ? L"Cheat mode: ON" : L"Cheat mode: OFF"), s.cheat ? 0xFFFF5555 : 0xFFFFFFFF));
			lines.push_back(std::make_pair(std::wstring(L"Creative: click an item to get it"), 0xFFAAAAAA));
			tooltip(lines, static_cast<int>(g_px), static_cast<int>(g_py));
		}
		else if (h.type == H_Tab && s.viewOpen && h.a < static_cast<int>(s.view.cats.size()))
		{
			std::vector<std::pair<std::wstring, int> > lines;
			lines.push_back(std::make_pair(Data::categories()[s.view.cats[h.a]].name, 0xFFFFFFFF));
			tooltip(lines, static_cast<int>(g_px), static_cast<int>(g_py));
		}

		// our own pointer over the recipe window (the container's pointer is underneath it)
		if (s.viewOpen)
		{
			const int x = static_cast<int>(g_px), y = static_cast<int>(g_py);
			std::shared_ptr<MultiplayerLocalPlayer> player = mc->localplayers[s.scene.pad];
			if (player != nullptr && player->inventory->getCarried() != nullptr)
			{
				g_drawing = true;
				item(player->inventory->getCarried(), x - 8, y - 8, true);
			}
			g_drawing = true;
			fill(x - 5, y - 1, x + 6, y + 2, 0xFF000000);
			fill(x - 1, y - 5, x + 2, y + 6, 0xFF000000);
			fill(x - 4, y, x + 5, y + 1, 0xFFFFFFFF);
			fill(x, y - 4, x + 1, y + 5, 0xFFFFFFFF);
		}

		g_drawing = false;
		glEnable(GL_DEPTH_TEST);
		glDepthMask(false);
		glDisable(GL_ALPHA_TEST);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	}
}
