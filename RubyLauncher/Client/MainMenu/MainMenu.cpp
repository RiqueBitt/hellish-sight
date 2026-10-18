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

		void layout()
		{
			State &s = S();
			s.buttons.clear();
			if (s.page == Page_Main)
			{
				const int x = 28, w = 196;
				int y = (std::max)(114, s.sh / 2 - 66);
				auto add = [&](int action, const wchar_t *label, const wchar_t *hint, int iconId, bool big) {
					const int h = big ? 26 : 19;
					s.buttons.push_back({ action, label, hint, iconId, x, y, w, h, big });
					y += h + 3;
				};
				add(Act_Play, L"Jogar", L"Carregar, criar ou entrar em um mundo", Tile::grass_Id, true);
				add(Act_JavaServers, L"Servidores Java", L"Entrar em servidores Java 1.8 e Reunion", Item::compass_Id, false);
				add(Local_Mods, L"Mods", L"Ver os mods instalados", Item::book_Id, false);
				add(Act_TexturePacks, L"Texturas", L"Pacotes de recursos Java 1.8", Item::painting_Id, false);
				add(Act_Options, L"Ajuda e Op\u00E7\u00F5es", L"Configura\u00E7\u00F5es, controles, como jogar", Item::redStone_Id, false);
				add(Act_Achievements, L"Conquistas", L"Suas conquistas", Item::diamond_Id, false);
				add(Act_Leaderboards, L"Placares", L"Placares e estat\u00EDsticas", Item::goldIngot_Id, false);
				add(Act_Exit, L"Sair do jogo", L"Fechar o Minecraft", Item::door_wood_Id, false);

				// small buttons under the info card (right)
				const int cw = 190, cx = s.sw - cw - 24;
				int cy = (std::max)(114, s.sh / 2 - 66) + 118;
				s.buttons.push_back({ Local_ModsFolder, L"Abrir pasta de mods", L"Abre a pasta mods no Explorador", Item::minecart_chest_Id, cx, cy, cw, 20, false });
				cy += 24;
				s.buttons.push_back({ Local_InputMode, L"Modo de controle", L"Autom\u00E1tico, teclado e mouse ou controle (F9)", Tile::lever_Id, cx, cy, cw, 20, false });
				cy += 24;
				s.buttons.push_back({ Act_Store, L"Loja", L"Pacotes de conte\u00FAdo", Item::emerald_Id, cx, cy, cw, 20, false });
			}
			else
			{
				const int w = 120;
				s.buttons.push_back({ Local_ModsFolder, L"Abrir pasta", L"Abre a pasta mods no Explorador", Item::minecart_chest_Id, s.sw / 2 - w - 4, s.sh - 44, w, 20, false });
				s.buttons.push_back({ Local_Back, L"Voltar", L"Voltar ao menu", Item::arrow_Id, s.sw / 2 + 4, s.sh - 44, w, 20, false });
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

		void drawButton(const Button &b, int index)
		{
			State &s = S();
			const bool focused = index == s.focus;
			float &anim = s.hoverAnim[index & 15];
			anim += ((focused ? 1.0f : 0.0f) - anim) * 0.25f;
			const int slide = static_cast<int>(anim * 6.0f);
			const int x = b.x + slide;
			const int bg = lerpColor(0xB0141418, 0xE02A1A1A, anim);
			P().rect(x, b.y, x + b.w, b.y + b.h, bg);
			// accent bar + glow
			const int accent = b.action == Act_Play ? 0xFFFF5A1E : 0xFFC8321E;
			P().rect(x, b.y, x + 3, b.y + b.h, lerpColor(0xFF5A1E14, accent, anim));
			if (anim > 0.05f) P().gradient(x + 3, b.y, x + 3 + static_cast<int>(40 * anim), b.y + b.h, (static_cast<int>(0x60 * anim) << 24) | 0xFF5A1E, 0x00FF5A1E);
			P().rect(x, b.y + b.h - 1, x + b.w, b.y + b.h, 0x40FFFFFF);
			const int iconY = b.y + (b.h - 16) / 2;
			icon(b.iconId, x + 8, iconY);
			const int textCol = lerpColor(0xFFD8D8D8, 0xFFFFFFFF, anim);
			if (b.big)
			{
				bigText(b.label, static_cast<float>(x + 30), static_cast<float>(b.y + 7), 1.6f, textCol, 0xFF3A0A00);
			}
			else
			{
				text(b.label, x + 30, b.y + (b.h - 8) / 2, textCol);
			}
		}

		void drawLogo(const std::wstring &splash)
		{
			State &s = S();
			const float t = (GetTickCount() - s.start) / 1000.0f;
			const int x = 28, y = (std::max)(16, s.sh / 2 - 150);
			// "HELLISH" with a fire gradient made of two passes
			const std::wstring title = L"HELLISH";
			const float k = 4.0f;
			bigText(title, static_cast<float>(x), static_cast<float>(y), k, 0xFFFF6A1E, 0xFF2A0500);
			glPushMatrix();
			glTranslatef(static_cast<float>(x), static_cast<float>(y), 0);
			glScalef(k, k, 1);
			// bright top half
			font()->draw(title, 0, 0, 0x60FFE070);
			glPopMatrix();
			bigText(L"SIGHT", static_cast<float>(x + 4), static_cast<float>(y + 34), 2.6f, 0xFFFFC040, 0xFF3A1000);
			text(L"MINECRAFT \u2022 LEGACY EDITION", x + 4, y + 62, 0xFFB0B0B0);

			if (!splash.empty())
			{
				const float pulse = 1.0f + 0.06f * sinf(t * 6.0f);
				glPushMatrix();
				glTranslatef(static_cast<float>(x + 170), static_cast<float>(y + 44), 0);
				glRotatef(-14.0f, 0, 0, 1);
				glScalef(pulse, pulse, 1);
				const int w = font()->width(splash);
				font()->drawShadow(splash, -w / 2, -4, 0xFFFFFF00);
				glPopMatrix();
			}
		}

		void drawEmbers()
		{
			State &s = S();
			if (s.embers.size() < 40 && (rand() % 3) == 0)
				s.embers.push_back({ static_cast<float>(rand() % (std::max)(1, s.sw)), static_cast<float>(s.sh + 4), 0.3f + (rand() % 100) / 120.0f, 1.0f });
			for (size_t i = 0; i < s.embers.size();)
			{
				State::Ember &e = s.embers[i];
				e.y -= e.vy;
				e.x += sinf(e.y * 0.05f) * 0.3f;
				e.life -= 0.004f;
				if (e.y < -4 || e.life <= 0) { s.embers.erase(s.embers.begin() + i); continue; }
				const int a = static_cast<int>(200 * e.life);
				const int col = (a << 24) | (e.life > 0.5f ? 0xFFA030 : 0xFF4010);
				P().rect(static_cast<int>(e.x), static_cast<int>(e.y), static_cast<int>(e.x) + 2, static_cast<int>(e.y) + 2, col);
				++i;
			}
		}

		void drawInfoCard()
		{
			State &s = S();
			const int w = 190, x = s.sw - w - 24, y = (std::max)(114, s.sh / 2 - 66);
			const int h = 110;
			P().rect(x, y, x + w, y + h, 0xC0141418);
			P().rect(x, y, x + w, y + 2, 0xFFC8321E);
			text(L"Hellish Sight 2.0", x + 8, y + 8, 0xFFFFC040);
			const std::vector<RubyModInfo> mods = RubyLoader::getInstalledMods();
			int loaded = 0;
			for (const RubyModInfo &m : mods) if (m.loaded) ++loaded;
			text(L"Mods carregados: " + std::to_wstring(loaded) + L" / " + std::to_wstring(mods.size()), x + 8, y + 24, 0xFFE0E0E0);
			static const wchar_t *modes[] = { L"Autom\u00E1tico", L"Teclado e mouse", L"Controle" };
			text(std::wstring(L"Controle: ") + modes[HSPcInput::mode()], x + 8, y + 38, 0xFFE0E0E0);
			static const wchar_t *tips[] = {
				L"R / U sobre um item: receitas e usos",
				L"Bot\u00E3o do meio: pegar o bloco",
				L"F7 liga os shaders, F8 o LOD",
				L"F9 troca entre teclado e controle",
				L"Ctrl+Q solta a pilha inteira",
				L"Use /help para ver os comandos",
			};
			const int tipCount = sizeof(tips) / sizeof(tips[0]);
			const int tip = static_cast<int>((GetTickCount() - s.start) / 6000) % tipCount;
			text(L"Dica:", x + 8, y + 60, 0xFFFFC040);
			std::wstring t = tips[tip];
			// wrap in two lines
			if (font()->width(t) > w - 16)
			{
				size_t cut = t.size();
				while (cut > 0 && (t[cut - 1] != L' ' || font()->width(t.substr(0, cut)) > w - 16)) --cut;
				if (cut == 0) cut = t.size() / 2;
				text(t.substr(0, cut), x + 8, y + 74, 0xFFD0D0D0);
				text(t.substr(cut), x + 8, y + 86, 0xFFD0D0D0);
			}
			else text(t, x + 8, y + 74, 0xFFD0D0D0);
		}

		void drawModsPage()
		{
			State &s = S();
			const int w = (std::min)(420, s.sw - 40), x = (s.sw - w) / 2, y = 28, h = s.sh - 84;
			P().rect(0, 0, s.sw, s.sh, 0xC0000000);
			P().rect(x, y, x + w, y + h, 0xFA141418);
			P().rect(x, y, x + w, y + 2, 0xFFC8321E);
			const std::wstring title = L"Mods instalados";
			text(title, x + (w - font()->width(title)) / 2, y + 8, 0xFFFFC040);
			const std::vector<RubyModInfo> mods = RubyLoader::getInstalledMods();
			const int rowH = 24, top = y + 26, rows = (h - 32) / rowH;
			s.modsScroll = (std::max)(0, (std::min)(s.modsScroll, static_cast<int>(mods.size()) - rows));
			for (int i = 0; i < rows && s.modsScroll + i < static_cast<int>(mods.size()); ++i)
			{
				const RubyModInfo &m = mods[s.modsScroll + i];
				const int ry = top + i * rowH;
				P().rect(x + 6, ry, x + w - 6, ry + rowH - 2, (i & 1) ? 0x40000000 : 0x60000000);
				P().rect(x + 6, ry, x + 8, ry + rowH - 2, m.loaded ? 0xFF3CC83C : 0xFFC83C3C);
				const std::wstring name = RubyPaths::toWide(m.name.empty() ? m.folderName : m.name);
				text(name, x + 14, ry + 3, 0xFFFFFFFF);
				std::wstring info = (m.version.empty() ? L"" : L"v" + RubyPaths::toWide(m.version) + L"   ") + (m.modId.empty() ? L"" : RubyPaths::toWide(m.modId));
				const std::wstring status = m.loaded ? L"carregado" : (m.error.empty() ? L"desativado" : L"erro: " + RubyPaths::toWide(m.error));
				text(info, x + 14, ry + 13, 0xFF909090);
				std::wstring st = status;
				while (st.size() > 4 && font()->width(st) > w / 2) st.resize(st.size() - 1);
				text(st, x + w - 12 - font()->width(st), ry + 3, m.loaded ? 0xFF7CE07C : 0xFFFF7070);
			}
			if (mods.empty()) text(L"Nenhum mod na pasta mods", x + 14, top + 4, 0xFFA0A0A0);
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

		// readable left side over the panorama
		P().gradient(0, 0, s.sw, s.sh, 0x30000000, 0x90000000);
		glPushMatrix();
		P().gradient(0, 0, 250, s.sh, 0x30000000, 0x60000000);
		glPopMatrix();
		drawEmbers();

		if (s.page == Page_Main)
		{
			(void)splash;	// the Minecraft / Hellish Sight logo layer (with its splash) is drawn above
			drawInfoCard();
			for (size_t i = 0; i < s.buttons.size(); ++i) drawButton(s.buttons[i], static_cast<int>(i));
			if (s.focus >= 0 && s.focus < static_cast<int>(s.buttons.size()))
			{
				const std::wstring &hint = s.buttons[s.focus].hint;
				text(hint, 28, s.sh - 24, 0xFFB0B0B0);
			}
		}
		else
		{
			drawModsPage();
			for (size_t i = 0; i < s.buttons.size(); ++i) drawButton(s.buttons[i], static_cast<int>(i));
		}
		text(L"Hellish Sight 2.0  \u2022  Minecraft LCE", 4, s.sh - 10, 0x80FFFFFF);
		const std::wstring legal = L"N\u00E3o oficial. Minecraft \u00E9 da Mojang Studios.";
		text(legal, s.sw - 4 - font()->width(legal), s.sh - 10, 0x80FFFFFF);

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
