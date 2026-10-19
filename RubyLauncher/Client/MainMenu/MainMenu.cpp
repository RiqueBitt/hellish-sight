#include "stdafx.h"
#include "MainMenu.h"

#include "Minecraft.h"
#include "GuiComponent.h"
#include "Font.h"
#include "Textures.h"
#include "ItemRenderer.h"
#include "Lighting.h"
#include "Common/UI/UI.h"
#include "net.minecraft.world.item.h"
#include "net.minecraft.world.level.tile.h"
#include "Host/RubyLauncherHost.h"
#include "Common/ModPaths.h"
#include "Client/PcInput/PcInput.h"

#include <shellapi.h>

void gdraw_D3D11_setViewport_4J();	// Iggy viewport (Windows64/Iggy/gdraw)
#include <algorithm>
#include <cmath>
#include <vector>

extern HWND g_hWnd;

namespace HSMainMenu
{
	namespace
	{
		enum Page { Page_Main = 0, Page_Mods };

		struct Button
		{
			int action;			// Action, or one of the local ids below
			std::wstring label;
			std::wstring hint;
			int iconId;			// item id for the icon (0 = none)
			int x, y, w, h;
			bool big;
		};

		enum LocalAction { Local_Mods = 100, Local_ModsFolder, Local_InputMode, Local_Back };

		struct State
		{
			Page page = Page_Main;
			int focus = 0;
			int hover = -1;
			float hoverAnim[16] = { 0 };
			std::vector<Button> buttons;
			int scale = 2, sw = 640, sh = 360;
			int modsScroll = 0;
			unsigned int start = GetTickCount();
			float mx = -1, my = -1;
			struct Ember { float x, y, vy, life; };
			std::vector<Ember> embers;
		};

		State &S()
		{
			static State *s = new State();
			return *s;
		}

		class Painter : public GuiComponent
		{
		public:
			void rect(int x0, int y0, int x1, int y1, int col) { fill(x0, y0, x1, y1, col); }
			void gradient(int x0, int y0, int x1, int y1, int a, int b) { fillGradient(x0, y0, x1, y1, a, b); }
		};
		Painter &P() { static Painter *p = new Painter(); return *p; }
		Font *font() { return Minecraft::GetInstance()->font; }

		ItemRenderer *IR()
		{
			static ItemRenderer *r = nullptr;
			if (r == nullptr) r = new ItemRenderer();
			return r;
		}

		void icon(int id, int x, int y)
		{
			if (id <= 0 || id >= Item::ITEM_NUM_COUNT || Item::items[id] == nullptr) return;
			Minecraft *mc = Minecraft::GetInstance();
			std::shared_ptr<ItemInstance> it = std::make_shared<ItemInstance>(id, 1, 0);
			glEnable(GL_DEPTH_TEST);
			glDepthMask(true);
			glEnable(GL_RESCALE_NORMAL);
			Lighting::turnOnGui();
			IR()->renderAndDecorateItem(mc->font, mc->textures, it, x, y);
			Lighting::turnOff();
			glDisable(GL_RESCALE_NORMAL);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(false);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(1, 1, 1, 1);
		}

		void text(const std::wstring &t, int x, int y, int col) { font()->drawShadow(t, x, y, col); }

		void bigText(const std::wstring &t, float x, float y, float k, int col, int shadow)
		{
			glPushMatrix();
			glTranslatef(x, y, 0);
			glScalef(k, k, 1);
			font()->draw(t, 1, 1, shadow);
			font()->draw(t, 0, 0, col);
			glPopMatrix();
		}

		int lerpColor(int a, int b, float t)
		{
			t = (std::max)(0.0f, (std::min)(1.0f, t));
			int out = 0;
			for (int s = 0; s < 32; s += 8)
			{
				const int ca = (a >> s) & 0xFF, cb = (b >> s) & 0xFF;
				out |= (static_cast<int>(ca + (cb - ca) * t) & 0xFF) << s;
			}
			return out;
		}

		// ---- layout (GUI units) ------------------------------------------------------------------
		// Left: menu card. Right: info card + shortcuts card. Bottom: footer bar with the hint.
		// Everything is computed from the screen size so nothing overlaps on small windows.
		struct Metrics
		{
			int top, bottom;			// usable band (below the game logo, above the footer)
			int leftX, leftW, rightX, rightW;
			int pad, gap, bigH, rowH;
		} M;

		void computeMetrics()
		{
			State &s = S();
			M.pad = 8;
			M.gap = 4;
			M.top = (std::max)(112, s.sh / 2 - 68);
			M.bottom = s.sh - 26;
			const int band = M.bottom - M.top;
			// 1 big + 7 small rows, a header and paddings must fit the band
			M.rowH = 20;
			M.bigH = 28;
			int need = M.pad * 2 + 14 + M.bigH + 7 * M.rowH + 7 * M.gap;
			if (need > band) { M.rowH = 18; M.bigH = 24; M.gap = 3; }
			need = M.pad * 2 + 14 + M.bigH + 7 * M.rowH + 7 * M.gap;
			if (need > band) { M.rowH = 16; M.bigH = 20; M.gap = 2; M.pad = 6; }
			const int margin = (std::max)(16, s.sw / 24);
			M.leftX = margin;
			M.leftW = (std::min)(220, (s.sw - margin * 3) / 2);
			M.rightW = (std::min)(200, s.sw - margin * 3 - M.leftW);
			M.rightX = s.sw - margin - M.rightW;
		}

		int infoCardHeight() { return M.pad * 2 + 14 + 3 * 12 + 26; }

		void layout()
		{
			State &s = S();
			computeMetrics();
			s.buttons.clear();
			if (s.page == Page_Main)
			{
				const int x = M.leftX + M.pad, w = M.leftW - M.pad * 2;
				int y = M.top + M.pad + 14;
				auto add = [&](int action, const wchar_t *label, const wchar_t *hint, int iconId, bool big) {
					const int h = big ? M.bigH : M.rowH;
					s.buttons.push_back({ action, label, hint, iconId, x, y, w, h, big });
					y += h + M.gap;
				};
				add(Act_Play, L"Jogar", L"Carregar, criar ou entrar em um mundo", Tile::grass_Id, true);
				add(Act_JavaServers, L"Servidores Java", L"Entrar em servidores Java 1.8 e Reunion", Item::compass_Id, false);
				add(Local_Mods, L"Mods", L"Ver os mods instalados", Item::book_Id, false);
				add(Act_TexturePacks, L"Texturas", L"Pacotes de recursos Java 1.8", Item::painting_Id, false);
				add(Act_Options, L"Ajuda e Op\u00E7\u00F5es", L"Configura\u00E7\u00F5es, controles, como jogar", Item::redStone_Id, false);
				add(Act_Achievements, L"Conquistas", L"Suas conquistas", Item::diamond_Id, false);
				add(Act_Leaderboards, L"Placares", L"Placares e estat\u00EDsticas", Item::goldIngot_Id, false);
				add(Act_Exit, L"Sair do jogo", L"Fechar o Minecraft", Item::door_wood_Id, false);

				// shortcuts card (right, under the info card)
				const int cx = M.rightX + M.pad, cw = M.rightW - M.pad * 2;
				int cy = M.top + infoCardHeight() + M.gap * 2 + M.pad + 14;
				const int h = M.rowH;
				s.buttons.push_back({ Local_ModsFolder, L"Abrir pasta de mods", L"Abre a pasta mods no Explorador", Item::minecart_chest_Id, cx, cy, cw, h, false });
				cy += h + M.gap;
				s.buttons.push_back({ Local_InputMode, L"Modo de controle", L"Autom\u00E1tico, teclado e mouse ou controle (F9)", Tile::lever_Id, cx, cy, cw, h, false });
				cy += h + M.gap;
				s.buttons.push_back({ Act_Store, L"Loja", L"Pacotes de conte\u00FAdo", Item::emerald_Id, cx, cy, cw, h, false });
			}
			else
			{
				const int w = 120;
				const int y = s.sh - 26 - M.rowH - 6;
				s.buttons.push_back({ Local_ModsFolder, L"Abrir pasta", L"Abre a pasta mods no Explorador", Item::minecart_chest_Id, s.sw / 2 - w - 4, y, w, M.rowH, false });
				s.buttons.push_back({ Local_Back, L"Voltar", L"Voltar ao menu", Item::arrow_Id, s.sw / 2 + 4, y, w, M.rowH, false });
			}
			if (s.focus >= static_cast<int>(s.buttons.size())) s.focus = 0;
		}

		void updateMouse(int renderW, int renderH)
		{
			State &s = S();
			RECT rc;
			if (g_hWnd == nullptr || !GetClientRect(g_hWnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
			const float px = g_KBMInput.GetMouseX() * static_cast<float>(renderW) / rc.right;
			const float py = g_KBMInput.GetMouseY() * static_cast<float>(renderH) / rc.bottom;
			const float nx = px / s.scale, ny = py / s.scale;
			if (nx != s.mx || ny != s.my)
			{
				s.mx = nx; s.my = ny;
				s.hover = -1;
				for (size_t i = 0; i < s.buttons.size(); ++i)
				{
					const Button &b = s.buttons[i];
					if (nx >= b.x && nx < b.x + b.w && ny >= b.y && ny < b.y + b.h) { s.hover = static_cast<int>(i); s.focus = s.hover; }
				}
			}
		}

		// ---- drawing helpers ----------------------------------------------------------------------
		// rounded rectangle (2 px corners)
		void rrect(int x0, int y0, int x1, int y1, int col)
		{
			P().rect(x0 + 2, y0, x1 - 2, y1, col);
			P().rect(x0 + 1, y0 + 1, x0 + 2, y1 - 1, col);
			P().rect(x1 - 2, y0 + 1, x1 - 1, y1 - 1, col);
			P().rect(x0, y0 + 2, x0 + 1, y1 - 2, col);
			P().rect(x1 - 1, y0 + 2, x1, y1 - 2, col);
		}

		void rgradient(int x0, int y0, int x1, int y1, int top, int bottom)
		{
			P().gradient(x0 + 2, y0, x1 - 2, y1, top, bottom);
			P().gradient(x0, y0 + 2, x0 + 2, y1 - 2, top, bottom);
			P().gradient(x1 - 2, y0 + 2, x1, y1 - 2, top, bottom);
		}

		// glass card: shadow, translucent body, light top edge, accent line, small header
		void card(int x, int y, int w, int h, const wchar_t *title)
		{
			rrect(x + 2, y + 3, x + w + 2, y + h + 3, 0x50000000);					// drop shadow
			rgradient(x, y, x + w, y + h, 0xC8181A20, 0xD80E0F13);						// body
			P().rect(x + 2, y, x + w - 2, y + 1, 0x30FFFFFF);							// top highlight
			P().gradient(x + 2, y + h - 1, x + w - 2, y + h, 0x00000000, 0x40000000);
			if (title != nullptr)
			{
				text(title, x + M.pad, y + M.pad - 1, 0xFF8A8F99);
				P().gradient(x + M.pad, y + M.pad + 9, x + w - M.pad, y + M.pad + 10, 0x60FF6A1E, 0x00FF6A1E);
			}
		}

		void drawButton(const Button &b, int index)
		{
			State &s = S();
			const bool focused = index == s.focus;
			float &anim = s.hoverAnim[index & 15];
			anim += ((focused ? 1.0f : 0.0f) - anim) * 0.25f;
			const int x = b.x, y = b.y, w = b.w, h = b.h;
			const bool primary = b.action == Act_Play;

			if (primary)
			{
				// filled accent button
				const int top = lerpColor(0xFFE0501E, 0xFFFF7A2E, anim), bottom = lerpColor(0xFFA0300E, 0xFFD04A14, anim);
				rgradient(x, y, x + w, y + h, top, bottom);
				P().rect(x + 2, y, x + w - 2, y + 1, 0x60FFFFFF);
				P().rect(x + 2, y + h - 1, x + w - 2, y + h, 0x60000000);
			}
			else
			{
				const int bg = lerpColor(0x00FFFFFF, 0x24FFFFFF, anim);
				if ((bg >> 24) & 0xFF) rrect(x, y, x + w, y + h, bg);
				// accent pill on the left (grows on focus)
				const int pillH = 4 + static_cast<int>((h - 8) * anim);
				const int py = y + (h - pillH) / 2;
				if (anim > 0.02f) rrect(x, py, x + 3, py + pillH, lerpColor(0x00FF6A1E, 0xFFFF6A1E, anim));
			}

			const int slide = static_cast<int>(anim * 3.0f);
			const int iconY = y + (h - 16) / 2;
			const int iconX = x + 7 + (primary ? 0 : slide);
			glPushMatrix();
			if (!b.big && h < 18)
			{	// small rows: 12 px icons
				glTranslatef(static_cast<float>(iconX), static_cast<float>(y + (h - 12) / 2), 0);
				glScalef(0.75f, 0.75f, 1);
				icon(b.iconId, 0, 0);
			}
			else icon(b.iconId, iconX, iconY);
			glPopMatrix();

			const int tx = x + 30 + (primary ? 0 : slide);
			const int textCol = primary ? 0xFFFFFFFF : lerpColor(0xFFC8CCD4, 0xFFFFFFFF, anim);
			if (b.big)
			{
				const float k = h >= 26 ? 1.5f : 1.25f;
				bigText(b.label, static_cast<float>(tx), static_cast<float>(y + (h - 8 * k) / 2), k, textCol, 0x80300800);
			}
			else
			{
				text(b.label, tx, y + (h - 8) / 2, textCol);
			}
			// chevron on the focused row
			if (anim > 0.3f && !primary)
			{
				const int a = static_cast<int>(255 * anim);
				text(L">", x + w - 12, y + (h - 8) / 2, (a << 24) | 0xFF8A4A);
			}
		}

		void drawEmbers()
		{
			State &s = S();
			if (s.embers.size() < 50 && (rand() % 3) == 0)
				s.embers.push_back({ static_cast<float>(rand() % (std::max)(1, s.sw)), static_cast<float>(s.sh + 4), 0.25f + (rand() % 100) / 140.0f, 1.0f });
			for (size_t i = 0; i < s.embers.size();)
			{
				State::Ember &e = s.embers[i];
				e.y -= e.vy;
				e.x += sinf(e.y * 0.05f) * 0.3f;
				e.life -= 0.0035f;
				if (e.y < -4 || e.life <= 0) { s.embers.erase(s.embers.begin() + i); continue; }
				const int a = static_cast<int>(170 * e.life);
				const int col = (a << 24) | (e.life > 0.5f ? 0xFFA030 : 0xFF4010);
				const int size = e.life > 0.7f ? 2 : 1;
				P().rect(static_cast<int>(e.x), static_cast<int>(e.y), static_cast<int>(e.x) + size, static_cast<int>(e.y) + size, col);
				++i;
			}
		}


		void drawInfoCard()
		{
			State &s = S();
			const int x = M.rightX, y = M.top, w = M.rightW, h = infoCardHeight();
			card(x, y, w, h, L"HELLISH SIGHT 2.0");
			const int ix = x + M.pad;
			int iy = y + M.pad + 15;
			const std::vector<RubyModInfo> mods = RubyLoader::getInstalledMods();
			int loaded = 0;
			for (const RubyModInfo &m : mods) if (m.loaded) ++loaded;
			auto row = [&](const std::wstring &label, const std::wstring &value, int col) {
				text(label, ix, iy, 0xFF8A8F99);
				text(value, x + w - M.pad - font()->width(value), iy, col);
				iy += 12;
			};
			row(L"Mods", std::to_wstring(loaded) + L" / " + std::to_wstring(mods.size()), loaded == static_cast<int>(mods.size()) ? 0xFF7CE07C : 0xFFFFC040);
			static const wchar_t *modes[] = { L"Autom\u00E1tico", L"Teclado e mouse", L"Controle" };
			row(L"Controle", modes[HSPcInput::mode()], 0xFFE0E0E0);
			row(L"Edi\u00E7\u00E3o", L"Legacy Console", 0xFFE0E0E0);

			static const wchar_t *tips[] = {
				L"R / U sobre um item mostra receitas e usos",
				L"Bot\u00E3o do meio do mouse pega o bloco",
				L"F7 liga os shaders e F8 o LOD",
				L"F9 troca entre teclado e controle",
				L"Ctrl+Q solta a pilha inteira",
				L"Use /help para ver os comandos",
				L"Ctrl+F busca itens na lista de receitas",
			};
			const int tipCount = sizeof(tips) / sizeof(tips[0]);
			const int tip = static_cast<int>((GetTickCount() - s.start) / 6000) % tipCount;
			iy += 4;
			P().rect(ix, iy - 2, x + w - M.pad, iy - 1, 0x20FFFFFF);
			text(L"DICA", ix, iy + 1, 0xFFFF8A4A);
			std::wstring t = tips[tip];
			const int maxW = w - M.pad * 2 - 30;
			int ty = iy + 1;
			while (!t.empty() && ty < y + h - 8)
			{
				size_t cut = t.size();
				while (cut > 0 && font()->width(t.substr(0, cut)) > maxW)
				{
					size_t sp = t.rfind(L' ', cut - 1);
					cut = (sp == std::wstring::npos || sp == 0) ? cut - 1 : sp;
				}
				if (cut == 0) cut = 1;
				text(t.substr(0, cut), ix + 30, ty, 0xFFD0D4DC);
				t = t.substr(cut);
				while (!t.empty() && t[0] == L' ') t.erase(0, 1);
				ty += 10;
			}
		}

		void drawShortcutsCard()
		{
			const int x = M.rightX, y = M.top + infoCardHeight() + M.gap * 2;
			const int h = M.pad * 2 + 14 + 3 * M.rowH + 2 * M.gap;
			card(x, y, M.rightW, h, L"ATALHOS");
		}

		void drawMenuCard()
		{
			const int h = M.pad * 2 + 14 + M.bigH + 7 * M.rowH + 7 * M.gap - M.gap;
			card(M.leftX, M.top, M.leftW, h, L"MENU");
		}

		void drawFooter(const std::wstring &hint)
		{
			State &s = S();
			const int y = s.sh - 18;
			P().gradient(0, y - 6, s.sw, s.sh, 0x00000000, 0xA0000000);
			text(L"Hellish Sight 2.0", 8, y + 4, 0x90FFFFFF);
			const std::wstring legal = L"N\u00E3o oficial \u2022 Minecraft \u00E9 da Mojang";
			text(legal, s.sw - 8 - font()->width(legal), y + 4, 0x70FFFFFF);
			if (!hint.empty())
			{
				std::wstring h = hint;
				const int maxW = s.sw - 2 * 130;
				while (h.size() > 4 && font()->width(h) > maxW) h.resize(h.size() - 1);
				text(h, (s.sw - font()->width(h)) / 2, y + 4, 0xFFFFC890);
			}
		}

		void drawModsPage()
		{
			State &s = S();
			const int w = (std::min)(440, s.sw - 40), x = (s.sw - w) / 2, y = 24, h = s.sh - 24 - 26 - M.rowH - 14;
			P().rect(0, 0, s.sw, s.sh, 0xC0000000);
			card(x, y, w, h, L"MODS INSTALADOS");
			const std::vector<RubyModInfo> mods = RubyLoader::getInstalledMods();
			const int rowH = 26, top = y + M.pad + 16, rows = (std::max)(1, (h - 16 - M.pad * 2) / rowH);
			s.modsScroll = (std::max)(0, (std::min)(s.modsScroll, static_cast<int>(mods.size()) - rows));
			for (int i = 0; i < rows && s.modsScroll + i < static_cast<int>(mods.size()); ++i)
			{
				const RubyModInfo &m = mods[s.modsScroll + i];
				const int ry = top + i * rowH;
				rrect(x + M.pad, ry, x + w - M.pad, ry + rowH - 3, 0x18FFFFFF);
				rrect(x + M.pad, ry + 4, x + M.pad + 3, ry + rowH - 7, m.loaded ? 0xFF3CC83C : 0xFFC83C3C);
				const std::wstring name = RubyPaths::toWide(m.name.empty() ? m.folderName : m.name);
				text(name, x + M.pad + 10, ry + 4, 0xFFFFFFFF);
				std::wstring info = (m.version.empty() ? L"" : L"v" + RubyPaths::toWide(m.version) + L"   ") + (m.modId.empty() ? L"" : RubyPaths::toWide(m.modId));
				text(info, x + M.pad + 10, ry + 14, 0xFF8A8F99);
				std::wstring st = m.loaded ? L"Carregado" : (m.error.empty() ? L"Desativado" : L"Erro: " + RubyPaths::toWide(m.error));
				while (st.size() > 4 && font()->width(st) > w / 2) st.resize(st.size() - 1);
				const int sw2 = font()->width(st) + 10;
				const int bx = x + w - M.pad - 6 - sw2;
				rrect(bx, ry + 5, bx + sw2, ry + 17, m.loaded ? 0x403CC83C : 0x40C83C3C);
				text(st, bx + 5, ry + 7, m.loaded ? 0xFF9CF09C : 0xFFFF9090);
			}
			if (mods.empty()) text(L"Nenhum mod na pasta mods", x + M.pad + 4, top + 4, 0xFFA0A0A0);
			if (static_cast<int>(mods.size()) > rows)
			{	// scroll bar
				const int trackY = top, trackH = rows * rowH - 3;
				const int thumbH = (std::max)(10, trackH * rows / static_cast<int>(mods.size()));
				const int thumbY = trackY + (trackH - thumbH) * s.modsScroll / (std::max)(1, static_cast<int>(mods.size()) - rows);
				P().rect(x + w - 4, trackY, x + w - 2, trackY + trackH, 0x20FFFFFF);
				P().rect(x + w - 4, thumbY, x + w - 2, thumbY + thumbH, 0xA0FF6A1E);
			}
		}

		void openModsFolder()
		{
			ShellExecuteW(nullptr, L"open", RubyPaths::modsRootW().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}

		int activate(int index)
		{
			State &s = S();
			if (index < 0 || index >= static_cast<int>(s.buttons.size())) return Act_None;
			const int a = s.buttons[index].action;
			switch (a)
			{
			case Local_Mods: s.page = Page_Mods; s.focus = 1; s.modsScroll = 0; ui.PlayUISFX(eSFX_Press); layout(); return Act_None;
			case Local_Back: s.page = Page_Main; s.focus = 2; ui.PlayUISFX(eSFX_Press); layout(); return Act_None;
			case Local_ModsFolder: ui.PlayUISFX(eSFX_Press); openModsFolder(); return Act_None;
			case Local_InputMode: ui.PlayUISFX(eSFX_Press); HSPcInput::cycleMode(); return Act_None;
			default: return a;
			}
		}
	}

	bool enabled()
	{
		return true;
	}

	void reset()
	{
		State &s = S();
		s.page = Page_Main;
		s.focus = 0;
		s.hover = -1;
	}

	void render(int pad, int renderW, int renderH, const std::wstring &splash)
	{
		State &s = S();
		Minecraft *mc = Minecraft::GetInstance();
		if (mc == nullptr || mc->font == nullptr || renderW <= 0 || renderH <= 0) return;
		s.scale = (std::max)(1, (renderH + 180) / 360);
		s.sw = renderW / s.scale;
		s.sh = renderH / s.scale;
		layout();
		if (pad == 0) updateMouse(renderW, renderH);

		ui.setupCustomDrawGameState();
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, static_cast<float>(renderW), static_cast<float>(renderH), 0, 1000, 3000);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glTranslatef(0, 0, -2000);
		glScalef(static_cast<float>(s.scale), static_cast<float>(s.scale), 1.0f);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(false);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_CULL_FACE);

		// darken the panorama. The UI viewport is 16:9 inside the window while the panorama fills
		// the whole window, so the overlay is drawn with the full-window viewport (no light borders)
		RenderManager.StateSetViewport(C4JRender::VIEWPORT_TYPE_FULLSCREEN);
		P().gradient(0, 0, s.sw, s.sh, 0x40000000, 0x98000000);
		gdraw_D3D11_setViewport_4J();
		drawEmbers();

		std::wstring hint;
		if (s.focus >= 0 && s.focus < static_cast<int>(s.buttons.size())) hint = s.buttons[s.focus].hint;
		if (s.page == Page_Main)
		{
			(void)splash;	// the Minecraft / Hellish Sight logo layer (with its splash) is drawn above
			drawMenuCard();
			drawInfoCard();
			drawShortcutsCard();
			for (size_t i = 0; i < s.buttons.size(); ++i) drawButton(s.buttons[i], static_cast<int>(i));
		}
		else
		{
			drawModsPage();
			for (size_t i = 0; i < s.buttons.size(); ++i) drawButton(s.buttons[i], static_cast<int>(i));
		}
		drawFooter(hint);

		glColor4f(1, 1, 1, 1);
		ui.endCustomDrawGameState();
	}

	int handleInput(int pad, int key, bool pressed, bool repeat, bool &handled)
	{
		State &s = S();
		handled = false;
		if (!pressed) return Act_None;
		const int count = static_cast<int>(s.buttons.size());
		if (count == 0) return Act_None;
		switch (key)
		{
		case ACTION_MENU_UP:
			s.focus = (s.focus + count - 1) % count; handled = true; ui.PlayUISFX(eSFX_Focus); return Act_None;
		case ACTION_MENU_DOWN:
			s.focus = (s.focus + 1) % count; handled = true; ui.PlayUISFX(eSFX_Focus); return Act_None;
		case ACTION_MENU_OTHER_STICK_UP:
			if (s.page == Page_Mods) { s.modsScroll--; handled = true; }
			return Act_None;
		case ACTION_MENU_OTHER_STICK_DOWN:
			if (s.page == Page_Mods) { s.modsScroll++; handled = true; }
			return Act_None;
		case ACTION_MENU_B:
		case ACTION_MENU_CANCEL:
			if (s.page == Page_Mods) { s.page = Page_Main; s.focus = 2; layout(); handled = true; }
			return Act_None;
		case ACTION_MENU_A:
		case ACTION_MENU_OK:
		{
			if (repeat) { handled = true; return Act_None; }
			handled = true;
			// a mouse click uses the button under the pointer, keys / pad use the focused one
			if (g_KBMInput.IsMouseButtonPressed(KeyboardMouseInput::MOUSE_LEFT) || g_KBMInput.IsMouseButtonDown(KeyboardMouseInput::MOUSE_LEFT))
			{
				if (s.hover < 0) return Act_None;
				return activate(s.hover);
			}
			return activate(s.focus);
		}
		default:
			return Act_None;
		}
	}
}
