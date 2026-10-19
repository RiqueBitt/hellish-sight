#include "stdafx.h"
#include "MachineGui.h"

#include "Minecraft.h"
#include "GuiComponent.h"
#include "Font.h"
#include "Textures.h"
#include "Tesselator.h"
#include "BufferedImage.h"
#include "Common/UI/UI.h"
#include "Host/RubyLauncherHost.h"
#include "Common/ModPaths.h"

#include <algorithm>
#include <cstring>
#include <map>

void gdraw_D3D11_setViewport_4J();	// Iggy viewport (Windows64/Iggy/gdraw)

namespace HSMachineGui
{
	namespace
	{
		struct Sprite
		{
			int id = -1;
			int w = 0, h = 0;
		};

		std::map<std::string, Sprite> &sprites()
		{
			static std::map<std::string, Sprite> *m = new std::map<std::string, Sprite>();
			return *m;
		}

		bool g_open = false;
		Screen g_screen;
		int g_found = -1;	// -1 unknown, 0 no sprites, 1 sprites

		std::string findFile(const std::string &name)
		{
			for (const RubyModInfo &info : RubyLoader::getInstalledMods())
			{
				if (info.folderName.empty()) continue;
				const std::string p = RubyPaths::resolveModTexturePath(info.folderName, "res/gui/" + name + ".png");
				if (!p.empty()) return p;
			}
			return std::string();
		}

		const Sprite &sprite(const std::string &name)
		{
			auto it = sprites().find(name);
			if (it != sprites().end()) return it->second;
			Sprite s;
			const std::string path = findFile(name);
			if (!path.empty())
			{
				D3DXIMAGE_INFO info;
				ZeroMemory(&info, sizeof(info));
				int *data = nullptr;
				if (RenderManager.LoadTextureData(wstringtofilename(RubyPaths::toWide(path)), &info, &data) == ERROR_SUCCESS && data != nullptr)
				{
					BufferedImage *img = new BufferedImage(info.Width, info.Height, BufferedImage::TYPE_INT_ARGB);	// kept: the texture refers to it
					memcpy(img->getData(), data, info.Width * info.Height * sizeof(int));
					delete[] data;
					s.id = Minecraft::GetInstance()->textures->getTexture(img, C4JRender::TEXTURE_FORMAT_RxGyBzAw, false);
					s.w = info.Width;
					s.h = info.Height;
				}
			}
			return sprites()[name] = s;
		}

		class Painter : public GuiComponent
		{
		public:
			void rect(int x0, int y0, int x1, int y1, int col) { fill(x0, y0, x1, y1, col); }
		};

		Painter &P()
		{
			static Painter *p = new Painter();
			return *p;
		}

		void setColor(int rgb, float a = 1.0f)
		{
			glColor4f(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
		}

		// textured quad: screen rect (GUI units) <- texture rect (texture pixels)
		void quad(const Sprite &s, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
		{
			if (s.id < 0 || x1 <= x0 || y1 <= y0) return;
			Tesselator *t = Tesselator::getInstance();
			const float iw = 1.0f / s.w, ih = 1.0f / s.h;
			t->begin();
			t->vertexUV(x0, y1, 0.0f, u0 * iw, v1 * ih);
			t->vertexUV(x1, y1, 0.0f, u1 * iw, v1 * ih);
			t->vertexUV(x1, y0, 0.0f, u1 * iw, v0 * ih);
			t->vertexUV(x0, y0, 0.0f, u0 * iw, v0 * ih);
			t->end();
		}

		void bind(const Sprite &s)
		{
			if (s.id >= 0) Minecraft::GetInstance()->textures->bind(s.id);
		}

		void nineSlice(const std::string &name, int x, int y, int w, int h, int border, int tint = 0xFFFFFF)
		{
			const Sprite &s = sprite(name);
			if (s.id < 0) return;
			bind(s);
			setColor(tint);
			const float b = static_cast<float>(border);
			const float tw = static_cast<float>(s.w), th = static_cast<float>(s.h);
			const float xs[4] = { (float)x, x + b, x + w - b, (float)(x + w) };
			const float ys[4] = { (float)y, y + b, y + h - b, (float)(y + h) };
			const float us[4] = { 0, b, tw - b, tw };
			const float vs[4] = { 0, b, th - b, th };
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c)
					quad(s, xs[c], ys[r], xs[c + 1], ys[r + 1], us[c], vs[r], us[c + 1], vs[r + 1]);
		}

		void tiled(const std::string &name, int x, int y, int w, int h, int tint)
		{
			const Sprite &s = sprite(name);
			if (s.id < 0 || w <= 0 || h <= 0) return;
			bind(s);
			setColor(tint);
			for (int yy = 0; yy < h; yy += s.h)
				for (int xx = 0; xx < w; xx += s.w)
				{
					const int cw = (std::min)(s.w, w - xx), ch = (std::min)(s.h, h - yy);
					quad(s, (float)(x + xx), (float)(y + yy), (float)(x + xx + cw), (float)(y + yy + ch), 0, 0, (float)cw, (float)ch);
				}
		}

		void stretched(const std::string &name, int x, int y, int w, int h, int tint = 0xFFFFFF)
		{
			const Sprite &s = sprite(name);
			if (s.id < 0) return;
			bind(s);
			setColor(tint);
			quad(s, (float)x, (float)y, (float)(x + w), (float)(y + h), 0, 0, (float)s.w, (float)s.h);
		}

		void tooltip(const std::wstring &text, int x, int y, int sw, int sh)
		{
			if (text.empty()) return;
			Font *font = Minecraft::GetInstance()->font;
			const int w = font->width(text), h = 8;
			int tx = x + 12, ty = y - 12;
			if (tx + w + 4 > sw) tx = x - 16 - w;
			if (ty < 4) ty = 4;
			if (ty + h + 4 > sh) ty = sh - h - 4;
			glDisable(GL_TEXTURE_2D);
			P().rect(tx - 3, ty - 3, tx + w + 3, ty + h + 3, 0xF0100010);
			P().rect(tx - 3, ty - 3, tx + w + 3, ty - 2, 0x505000FF);
			P().rect(tx - 3, ty + h + 2, tx + w + 3, ty + h + 3, 0x5028007F);
			glEnable(GL_TEXTURE_2D);
			font->drawShadow(text, tx, ty, 0xFFFFFFFF);
		}
	}

	bool available()
	{
		if (g_found < 0) g_found = (!findFile("gui_base").empty() && !findFile("gui_bar_power").empty()) ? 1 : 0;
		return g_found == 1;
	}

	void update(const Screen &screen)
	{
		g_screen = screen;
		g_open = true;
	}

	void clear(int pad)
	{
		if (pad == g_screen.pad) g_open = false;
	}

	void render()
	{
		if (!g_open || !available() || g_screen.bars.empty()) return;
		if (g_screen.screenW <= 0 || g_screen.screenH <= 0) return;
		Minecraft *mc = Minecraft::GetInstance();
		if (mc == nullptr || mc->textures == nullptr || mc->font == nullptr) return;

		const int scale = (std::max)(1, (g_screen.screenH + 180) / 360);
		const int sw = g_screen.screenW / scale, sh = g_screen.screenH / scale;

		ui.setupCustomDrawGameState();
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, static_cast<float>(g_screen.screenW), static_cast<float>(g_screen.screenH), 0, 1000, 3000);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glTranslatef(0, 0, -2000);
		glScalef(static_cast<float>(scale), static_cast<float>(scale), 1.0f);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(false);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_TEXTURE_2D);

		// side panel at the left of the machine screen (the recipe viewer uses the right side)
		const int barH = 54, pad = 6, gap = 4;
		int w = pad * 2;
		for (size_t i = 0; i < g_screen.bars.size(); ++i) w += (g_screen.bars[i].kind == Bar_Tank ? 18 : 8) + (i ? gap : 0);
		const int h = barH + pad * 2;
		int x = static_cast<int>(g_screen.panelX0 / scale) - w + 2;
		if (x < 2) x = 2;
		const int y = static_cast<int>(g_screen.panelY0 / scale) + 10;

		nineSlice("gui_base", x, y, w, h, 4);

		const float px = g_screen.pointerX / scale, py = g_screen.pointerY / scale;
		std::wstring hoverText;
		int bx = x + pad;
		for (const Bar &bar : g_screen.bars)
		{
			const int bw = (bar.kind == Bar_Tank) ? 18 : 8;
			const int by = y + pad;
			const float frac = (std::max)(0.0f, (std::min)(1.0f, bar.frac));
			const int fillH = static_cast<int>((barH - 2) * frac + 0.5f);
			if (bar.kind == Bar_Tank)
			{
				nineSlice("gui_gauge", bx, by, bw, barH, 2);
				if (fillH > 0)
				{
					glDisable(GL_TEXTURE_2D);
					P().rect(bx + 1, by + barH - 1 - fillH, bx + bw - 1, by + barH - 1, 0xFF000000 | bar.color);
					P().rect(bx + 1, by + barH - 1 - fillH, bx + 3, by + barH - 1, 0x30FFFFFF);
					glEnable(GL_TEXTURE_2D);
				}
				stretched("gui_gauge_overlay", bx + 1, by + 1, bw - 2, barH - 2);
			}
			else
			{
				nineSlice("gui_bar_base", bx, by, bw, barH, 1);
				if (fillH > 0) tiled("gui_bar_power", bx + 1, by + barH - 1 - fillH, bw - 2, fillH, bar.kind == Bar_Heat ? bar.color : 0xFFFFFF);
			}
			if (px >= bx && px < bx + bw && py >= by && py < by + barH) hoverText = bar.tooltip;
			bx += bw + gap;
		}

		if (!hoverText.empty()) tooltip(hoverText, static_cast<int>(px), static_cast<int>(py), sw, sh);

		glColor4f(1, 1, 1, 1);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(false);
		glDisable(GL_ALPHA_TEST);
	}
}

namespace HSMachineGui
{
	void renderBackground(int screenW, int screenH, int originX, int originY, int fullW, int fullH, const Rect &panel, const std::vector<Rect> &slots, int outputSlot)
	{
		if (!available() || screenW <= 0 || screenH <= 0) return;
		if (fullW <= 0 || fullH <= 0) { fullW = screenW; fullH = screenH; originX = originY = 0; }
		ui.setupCustomDrawGameState();
		// before the scene's own draw the Iggy viewport is still the previous layer's, so draw with the
		// full-window viewport and place the 16:9 scene area explicitly
		RenderManager.StateSetViewport(C4JRender::VIEWPORT_TYPE_FULLSCREEN);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, static_cast<float>(fullW), static_cast<float>(fullH), 0, 1000, 3000);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glTranslatef(static_cast<float>(originX), static_cast<float>(originY), -2000);
		const float scale = static_cast<float>((std::max)(1, (screenH + 180) / 360));
		glScalef(scale, scale, 1.0f);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(false);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_TEXTURE_2D);

		const int px0 = static_cast<int>(panel.x0 / scale), py0 = static_cast<int>(panel.y0 / scale);
		const int px1 = static_cast<int>(panel.x1 / scale), py1 = static_cast<int>(panel.y1 / scale);
		nineSlice("gui_base", px0, py0, px1 - px0, py1 - py0, 4);
		for (size_t i = 0; i < slots.size(); ++i)
		{
			const Rect &r = slots[i];
			const int x0 = static_cast<int>(r.x0 / scale), y0 = static_cast<int>(r.y0 / scale);
			const int x1 = static_cast<int>(r.x1 / scale + 0.5f), y1 = static_cast<int>(r.y1 / scale + 0.5f);
			if (x1 - x0 < 4 || y1 - y0 < 4) continue;
			stretched(static_cast<int>(i) == outputSlot ? "gui_slot_output" : "gui_slot", x0, y0, x1 - x0, y1 - y0);
		}
		glColor4f(1, 1, 1, 1);
		ui.endCustomDrawGameState();
		gdraw_D3D11_setViewport_4J();
	}
}
