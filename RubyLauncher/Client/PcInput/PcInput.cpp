#include "stdafx.h"
#include "PcInput.h"

#include "Minecraft.h"
#include "GuiComponent.h"
#include "Font.h"
#include "Common/UI/UI.h"

#include <algorithm>
#include <cstdio>

namespace HSPcInput
{
	namespace
	{
		struct Tip { std::wstring label; bool show = false; };
		struct State
		{
			Mode mode = Mode_Auto;
			bool loaded = false;
			Tip tips[eToolTipNumButtons];
			unsigned int publishedFrame = 0;
			unsigned int frame = 0;
			bool inMenu = false;
			unsigned int toastUntil = 0;
		};

		State &S()
		{
			static State *s = new State();
			return *s;
		}

		const char *kConfig = "hs_input.cfg";

		void load()
		{
			State &s = S();
			if (s.loaded) return;
			s.loaded = true;
			FILE *f = nullptr;
			if (fopen_s(&f, kConfig, "rb") == 0 && f != nullptr)
			{
				int m = 0;
				if (fscanf_s(f, "%d", &m) == 1 && m >= 0 && m < Mode_Count) s.mode = static_cast<Mode>(m);
				fclose(f);
			}
		}

		void save()
		{
			FILE *f = nullptr;
			if (fopen_s(&f, kConfig, "wb") == 0 && f != nullptr)
			{
				fprintf(f, "%d\n", static_cast<int>(S().mode));
				fclose(f);
			}
		}

		class Painter : public GuiComponent
		{
		public:
			void rect(int x0, int y0, int x1, int y1, int col) { fill(x0, y0, x1, y1, col); }
		};
		Painter &P() { static Painter *p = new Painter(); return *p; }

		Font *font() { return Minecraft::GetInstance()->font; }

		// what does each controller button do on keyboard / mouse?
		// mouse: 1 left, 2 right, 3 wheel; otherwise a key cap with the text
		struct Binding { int mouse; const wchar_t *key; };

		Binding bindingFor(int button, bool inMenu)
		{
			if (inMenu)
			{
				switch (button)
				{
				case eToolTipButtonA: return { 1, L"" };
				case eToolTipButtonB: return { 0, L"Esc" };
				case eToolTipButtonX: return { 2, L"" };
				case eToolTipButtonY: return { 0, L"Tab" };
				case eToolTipButtonLT: return { 0, L"PgUp" };
				case eToolTipButtonRT: return { 0, L"PgDn" };
				case eToolTipButtonLB: return { 0, L"Q" };
				case eToolTipButtonRB: return { 0, L"E" };
				case eToolTipButtonLS: return { 0, L"Setas" };
				case eToolTipButtonRS: return { 3, L"" };
				default: return { 0, L"Tab" };
				}
			}
			switch (button)
			{
			case eToolTipButtonA: return { 0, L"Espaço" };
			case eToolTipButtonB: return { 0, L"Q" };
			case eToolTipButtonX: return { 0, L"C" };
			case eToolTipButtonY: return { 0, L"E" };
			case eToolTipButtonLT: return { 2, L"" };
			case eToolTipButtonRT: return { 1, L"" };
			case eToolTipButtonLB: return { 3, L"" };
			case eToolTipButtonRB: return { 3, L"" };
			case eToolTipButtonLS: return { 0, L"Ctrl" };
			case eToolTipButtonRS: return { 0, L"Shift" };
			default: return { 0, L"Tab" };
			}
		}

		int drawKeyCap(const std::wstring &text, int x, int y)
		{
			const int w = (std::max)(12, font()->width(text) + 8);
			P().rect(x, y, x + w, y + 14, 0xFF1E1E1E);
			P().rect(x + 1, y + 1, x + w - 1, y + 12, 0xFF4A4A4A);
			P().rect(x + 1, y + 1, x + w - 1, y + 2, 0xFF6E6E6E);
			P().rect(x + 1, y + 12, x + w - 1, y + 13, 0xFF2A2A2A);
			font()->drawShadow(text, x + (w - font()->width(text)) / 2, y + 3, 0xFFFFFFFF);
			return w;
		}

		int drawMouse(int which, int x, int y)
		{
			const int w = 10, h = 14;
			P().rect(x + 1, y, x + w - 1, y + h, 0xFF1E1E1E);
			P().rect(x, y + 1, x + w, y + h - 1, 0xFF1E1E1E);
			P().rect(x + 1, y + 1, x + w - 1, y + h - 1, 0xFFB0B0B0);
			P().rect(x + 1, y + 6, x + w - 1, y + 7, 0xFF1E1E1E);
			P().rect(x + w / 2, y + 1, x + w / 2 + 1, y + 6, 0xFF1E1E1E);
			const int hi = 0xFF5AC85A;
			if (which == 1) P().rect(x + 1, y + 1, x + w / 2, y + 6, hi);
			if (which == 2) P().rect(x + w / 2 + 1, y + 1, x + w - 1, y + 6, hi);
			if (which == 3) P().rect(x + w / 2 - 1, y + 2, x + w / 2 + 2, y + 5, hi);
			return w;
		}

		void beginGui(int &scale, int &sw, int &sh)
		{
			Minecraft *mc = Minecraft::GetInstance();
			const int w = mc->width_phys, h = mc->height_phys;
			scale = (std::max)(1, (h + 180) / 360);
			sw = w / scale;
			sh = h / scale;
			ui.setupCustomDrawGameState();
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glOrtho(0, static_cast<float>(w), static_cast<float>(h), 0, 1000, 3000);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			glTranslatef(0, 0, -2000);
			glScalef(static_cast<float>(scale), static_cast<float>(scale), 1.0f);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(false);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		void endGui()
		{
			glColor4f(1, 1, 1, 1);
			glEnable(GL_DEPTH_TEST);
			glDepthMask(false);
			glDisable(GL_ALPHA_TEST);
		}

		void renderTooltipBar(int sw, int sh)
		{
			State &s = S();
			int x = 10;
			const int y = sh - 22;
			for (int b = 0; b < eToolTipNumButtons; ++b)
			{
				const Tip &t = s.tips[b];
				if (!t.show || t.label.empty()) continue;
				// gameplay: both bumpers are the wheel, show it once
				if (!s.inMenu && b == eToolTipButtonRB && s.tips[eToolTipButtonLB].show && s.tips[eToolTipButtonLB].label == t.label) continue;
				const Binding bind = bindingFor(b, s.inMenu);
				int w = bind.mouse ? drawMouse(bind.mouse, x, y) : drawKeyCap(bind.key, x, y);
				x += w + 4;
				font()->drawShadow(t.label, x, y + 3, 0xFFFFFFFF);
				x += font()->width(t.label) + 12;
				if (x > sw - 40) break;
			}
		}

		void renderControlsPage(int sw, int sh)
		{
			struct Row { const wchar_t *key; const wchar_t *what; };
			static const Row rows[] = {
				{ L"W A S D", L"Andar" }, { L"Espaço", L"Pular / nadar / voar para cima" }, { L"Shift", L"Agachar / descer voando" },
				{ L"Ctrl", L"Correr" }, { L"Mouse", L"Olhar" }, { L"Botão esq.", L"Quebrar / atacar" },
				{ L"Botão dir.", L"Usar / colocar" }, { L"Botão do meio", L"Pegar o bloco (Pick Block)" }, { L"Roda / 1-9", L"Trocar item da barra" },
				{ L"E", L"Inventário" }, { L"C / R", L"Crafting / Criativo" }, { L"Q", L"Soltar item (Ctrl+Q: pilha)" },
				{ L"T", L"Chat" }, { L"/", L"Comandos" }, { L"Tab", L"Opções do anfitrião" },
				{ L"Esc", L"Pausar / voltar" }, { L"F1", L"Esconder HUD" }, { L"F2", L"Captura de tela" },
				{ L"F3", L"Informações" }, { L"F5", L"Terceira pessoa" }, { L"F7 / F8", L"Shaders / LOD" },
				{ L"F9", L"Modo de controle (auto / teclado / controle)" }, { L"F11", L"Tela cheia" },
				{ L"R / U", L"Receitas / usos (com inventário aberto)" }, { L"Ctrl+F", L"Buscar item (lista de receitas)" },
			};
			const int count = sizeof(rows) / sizeof(rows[0]);
			const int half = (count + 1) / 2;
			const int colW = (std::min)(220, (sw - 40) / 2);
			const int panelW = colW * 2 + 24, rowH = 17;
			const int panelH = half * rowH + 40;
			const int px = (sw - panelW) / 2, py = (std::max)(8, (sh - panelH) / 2);
			P().rect(0, 0, sw, sh, 0xD0101010);
			P().rect(px, py, px + panelW, py + panelH, 0xFF000000);
			P().rect(px + 1, py + 1, px + panelW - 1, py + panelH - 1, 0xFF2C2C2C);
			const std::wstring title = L"Teclado e mouse";
			font()->drawShadow(title, px + (panelW - font()->width(title)) / 2, py + 8, 0xFFFFFF55);
			for (int i = 0; i < count; ++i)
			{
				const int col = i / half, row = i % half;
				const int x = px + 12 + col * (colW + 4), y = py + 26 + row * rowH;
				const int w = drawKeyCap(rows[i].key, x, y);
				std::wstring what = rows[i].what;
				while (what.size() > 4 && x + w + 6 + font()->width(what) > x + colW) what.resize(what.size() - 1);
				font()->drawShadow(what, x + w + 6, y + 3, 0xFFE0E0E0);
			}
			const std::wstring hint = L"Esc: voltar   |   F9: trocar para controle";
			font()->drawShadow(hint, px + (panelW - font()->width(hint)) / 2, py + panelH - 12, 0xFFA0A0A0);
		}
	}

	Mode mode()
	{
		load();
		return S().mode;
	}

	void cycleMode()
	{
		load();
		State &s = S();
		s.mode = static_cast<Mode>((s.mode + 1) % Mode_Count);
		s.toastUntil = GetTickCount() + 2500;
		save();
	}

	bool keyboardPrompts(int pad)
	{
		if (pad != 0) return false;
		switch (mode())
		{
		case Mode_Keyboard: return true;
		case Mode_Controller: return false;
		default: return g_KBMInput.IsKBMActive();
		}
	}

	void setTooltip(int pad, int button, const std::wstring &label, bool show)
	{
		if (pad != 0 || button < 0 || button >= eToolTipNumButtons) return;
		S().tips[button].label = label;
		S().tips[button].show = show;
	}

	void setTooltipsVisible(int pad, bool visible, bool inMenu)
	{
		if (pad != 0 || !visible) return;
		S().publishedFrame = S().frame;
		S().inMenu = inMenu;
	}

	void tick()
	{
		if (g_KBMInput.IsKeyPressed(VK_F9)) cycleMode();
	}

	void render()
	{
		State &s = S();
		s.frame++;
		Minecraft *mc = Minecraft::GetInstance();
		if (mc == nullptr || mc->font == nullptr || mc->width_phys <= 0 || mc->height_phys <= 0) return;

		// the chat input sits where the bar is: hide the bar while a screen (chat) is open
		const bool tooltips = keyboardPrompts(0) && s.publishedFrame + 1 >= s.frame && mc->screen == nullptr;
		UIScene *top = ui.GetTopScene(0);
		const bool controls = keyboardPrompts(0) && top != nullptr && top->getSceneType() == eUIScene_ControlsMenu;
		const bool toast = GetTickCount() < s.toastUntil;
		if (!tooltips && !controls && !toast) return;

		int scale, sw, sh;
		beginGui(scale, sw, sh);
		if (controls) renderControlsPage(sw, sh);
		else if (tooltips) renderTooltipBar(sw, sh);
		if (toast)
		{
			static const wchar_t *names[Mode_Count] = { L"Automático (último usado)", L"Teclado e mouse", L"Controle" };
			const std::wstring text = std::wstring(L"Modo de controle: ") + names[s.mode];
			const int w = font()->width(text) + 16;
			const int x = (sw - w) / 2, y = 12;
			P().rect(x, y, x + w, y + 18, 0xE0000000);
			font()->drawShadow(text, x + 8, y + 5, 0xFFFFFF55);
		}
		endGui();
	}
}
