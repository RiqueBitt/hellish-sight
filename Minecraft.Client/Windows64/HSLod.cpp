// Hellish Sight - Distant Terrain (LOD). See Common/HSLod.h for the overview.
// Original implementation for this engine; inspired by the idea behind Voxy / Distant
// Horizons, no code from them is used.

#include "stdafx.h"
#include "..\Common\HSLod.h"
#include "..\Common\HSSettings.h"
#include "..\Minecraft.h"
#include "..\MultiPlayerLevel.h"
#include "..\MultiPlayerLocalPlayer.h"
#include "..\GameRenderer.h"
#include "..\Common\Colours\ColourTable.h"
#include "..\..\Minecraft.World\Level.h"
#include "..\..\Minecraft.World\LevelChunk.h"
#include "..\..\Minecraft.World\Tile.h"
#include "..\..\Minecraft.World\Material.h"
#include "..\..\Minecraft.World\MaterialColor.h"
#include "..\..\Minecraft.World\Dimension.h"
#include "..\..\Minecraft.World\Mth.h"
#include "..\Common\Network\GameNetworkManager.h"

#include <d3dcompiler.h>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")

extern ID3D11Device *g_pd3dDevice;
extern ID3D11DeviceContext *g_pImmediateContext;

namespace HSLod
{
namespace
{
	// =====================================================================================
	// Data layout
	// =====================================================================================
	// A chunk is stored as 8x8 cells of 2x2 blocks.
	const int CELLS = 8;
	const int CELLS_PER_CHUNK = CELLS * CELLS;
	const int REGION_CHUNKS = 32;							// region = 32x32 chunks
	const int REGION_SLOTS = REGION_CHUNKS * REGION_CHUNKS;
	const int SECTION_CHUNKS = 4;							// mesh section = 4x4 chunks (64x64 blocks)
	const int SECTION_BLOCKS = SECTION_CHUNKS * 16;
	const int SECTION_BASE_CELLS = SECTION_BLOCKS / 2;		// 32 base cells per side
	const uint32_t FILE_MAGIC = 0x314C5348;					// "HSL1"
	const int ORIGIN_SNAP = 1024;							// vertex origin snapped to 1024 blocks

	enum { CELL_PRESENT = 1, CELL_WATER = 2 };

	struct Cell
	{
		uint8_t top;		// y of the surface (water surface for water cells)
		uint8_t flags;
		uint32_t color;		// 0x00RRGGBB, already tinted
	};

	struct ChunkLod
	{
		Cell cells[CELLS_PER_CHUNK];	// index z * 8 + x
	};

	struct Region
	{
		bool present[REGION_SLOTS] = {};
		ChunkLod chunks[REGION_SLOTS];
		bool dirty = false;
		bool loadedFromDisk = false;
	};

	inline int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
	inline int64_t key2(int x, int z) { return (static_cast<int64_t>(x) << 32) ^ static_cast<uint32_t>(z); }

	// =====================================================================================
	// Settings
	// =====================================================================================
	std::atomic<int> g_distance(-1);	// chunks, -1 = not read
	std::atomic<int> g_enabled(-1);

	// =====================================================================================
	// Store (shared between main thread and worker, guarded by g_storeLock)
	// =====================================================================================
	std::mutex g_storeLock;
	std::unordered_map<int64_t, std::unique_ptr<Region>> g_regions;
	std::unordered_map<int64_t, uint32_t> g_sectionVersion;
	std::string g_worldDir;		// "" = no world

	std::string regionPath(int rx, int rz)
	{
		char name[64];
		sprintf_s(name, "\\r.%d.%d.lod", rx, rz);
		return g_worldDir + name;
	}

	void ensureDir(const std::string &path)
	{
		// create every directory of the path
		std::string cur;
		for (size_t i = 0; i < path.size(); i++)
		{
			cur.push_back(path[i]);
			if (path[i] == '\\' || path[i] == '/' || i + 1 == path.size()) CreateDirectoryA(cur.c_str(), nullptr);
		}
	}

	// Region file: magic, count, then count * (u16 slot, ChunkLod)
	void loadRegion(Region &r, int rx, int rz)
	{
		r.loadedFromDisk = true;
		if (g_worldDir.empty()) return;
		FILE *f = nullptr;
		if (fopen_s(&f, regionPath(rx, rz).c_str(), "rb") != 0 || !f) return;
		uint32_t magic = 0, count = 0;
		if (fread(&magic, 4, 1, f) == 1 && magic == FILE_MAGIC && fread(&count, 4, 1, f) == 1 && count <= REGION_SLOTS)
		{
			for (uint32_t i = 0; i < count; i++)
			{
				uint16_t slot = 0;
				ChunkLod lod;
				if (fread(&slot, 2, 1, f) != 1 || fread(&lod, sizeof(lod), 1, f) != 1 || slot >= REGION_SLOTS) break;
				if (!r.present[slot])	// data ingested this session wins
				{
					r.chunks[slot] = lod;
					r.present[slot] = true;
				}
			}
		}
		fclose(f);
	}

	void saveRegion(const Region &r, int rx, int rz)
	{
		if (g_worldDir.empty()) return;
		ensureDir(g_worldDir);
		const std::string path = regionPath(rx, rz);
		const std::string tmp = path + ".tmp";
		FILE *f = nullptr;
		if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f) return;
		uint32_t count = 0;
		for (int i = 0; i < REGION_SLOTS; i++) if (r.present[i]) count++;
		fwrite(&FILE_MAGIC, 4, 1, f);
		fwrite(&count, 4, 1, f);
		for (int i = 0; i < REGION_SLOTS; i++)
		{
			if (!r.present[i]) continue;
			const uint16_t slot = static_cast<uint16_t>(i);
			fwrite(&slot, 2, 1, f);
			fwrite(&r.chunks[i], sizeof(ChunkLod), 1, f);
		}
		fclose(f);
		MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
	}

	// caller holds g_storeLock
	Region &regionFor(int rx, int rz)
	{
		std::unique_ptr<Region> &slot = g_regions[key2(rx, rz)];
		if (!slot)
		{
			slot.reset(new Region());
			loadRegion(*slot, rx, rz);
		}
		return *slot;
	}

	// caller holds g_storeLock; returns nullptr when the chunk was never seen
	const ChunkLod *chunkFor(int cx, int cz)
	{
		const int rx = floorDiv(cx, REGION_CHUNKS), rz = floorDiv(cz, REGION_CHUNKS);
		Region &r = regionFor(rx, rz);
		const int slot = (cz - rz * REGION_CHUNKS) * REGION_CHUNKS + (cx - rx * REGION_CHUNKS);
		return r.present[slot] ? &r.chunks[slot] : nullptr;
	}

	void saveDirtyLocked()
	{
		for (auto &kv : g_regions)
		{
			if (!kv.second->dirty) continue;
			const int rx = static_cast<int>(kv.first >> 32);
			const int rz = static_cast<int>(static_cast<uint32_t>(kv.first));
			saveRegion(*kv.second, rx, rz);
			kv.second->dirty = false;
		}
	}

	// =====================================================================================
	// Block colours (average texture colours of the vanilla 1.8 look)
	// =====================================================================================
	uint32_t mulColor(uint32_t c, uint32_t tint, float scale = 1.0f)
	{
		const float r = ((c >> 16) & 255) * ((tint >> 16) & 255) / 255.0f * scale;
		const float g = ((c >> 8) & 255) * ((tint >> 8) & 255) / 255.0f * scale;
		const float b = (c & 255) * (tint & 255) / 255.0f * scale;
		auto cl = [](float v) { return static_cast<uint32_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
		return (cl(r) << 16) | (cl(g) << 8) | cl(b);
	}

	uint32_t lerpColor(uint32_t a, uint32_t b, float t)
	{
		auto ch = [&](int s) {
			const float va = static_cast<float>((a >> s) & 255), vb = static_cast<float>((b >> s) & 255);
			return static_cast<uint32_t>(va + (vb - va) * t) << s;
		};
		return ch(16) | ch(8) | ch(0);
	}

	bool baseColor(int id, int data, uint32_t &out, bool &tinted)
	{
		static const uint32_t stained[16] = {
			0xD1B2A1, 0xA15325, 0x95586C, 0x716C89, 0xBA8523, 0x677534, 0xA14E4E, 0x392A23,
			0x876A61, 0x575B5B, 0x764656, 0x4A3B5B, 0x4D3323, 0x4C532A, 0x8F3D2E, 0x251610 };
		static const uint32_t wool[16] = {
			0xE9ECEC, 0xF07613, 0xBD44B3, 0x3AAFD9, 0xF8C627, 0x70B919, 0xED8DAC, 0x3E4447,
			0x8E8E86, 0x158991, 0x792AAC, 0x35399D, 0x724728, 0x546D1B, 0xA12722, 0x141519 };
		tinted = false;
		switch (id)
		{
		case 1:
			switch (data) { case 1: case 2: out = 0x9A6E5A; break; case 3: case 4: out = 0xBDBDBD; break; case 5: case 6: out = 0x888888; break; default: out = 0x7D7D7D; }
			return true;
		case 2: out = 0xB4B4B4; tinted = true; return true;		// grass top (grey, biome tinted)
		case 3: out = data == 2 ? 0x5A3F1C : (data == 1 ? 0x77553B : 0x866043); return true;
		case 4: out = 0x7A7A7A; return true;
		case 5: out = 0x9C7F4E; return true;
		case 7: out = 0x555555; return true;
		case 8: case 9: out = 0xFFFFFF; tinted = true; return true;	// water, tinted below
		case 10: case 11: out = 0xCF5B13; return true;
		case 12: out = data == 1 ? 0xA9581F : 0xDBD3A0; return true;
		case 13: out = 0x857F7E; return true;
		case 17: case 162: out = 0x6B5534; return true;
		case 18: case 161: out = 0xA0A0A0; tinted = true; return true;
		case 24: out = 0xD8CB9B; return true;
		case 35: out = wool[data & 15]; return true;
		case 48: out = 0x677A63; return true;
		case 49: out = 0x14121D; return true;
		case 78: case 80: out = 0xF0FBFB; return true;
		case 79: out = 0x91B4FE; return true;
		case 81: out = 0x0D6418; return true;
		case 82: out = 0xA0A6B3; return true;
		case 86: out = 0xC07615; return true;
		case 87: out = 0x6F3634; return true;
		case 88: out = 0x544033; return true;
		case 98: out = 0x7A7A7A; return true;
		case 99: case 100: out = 0x8E6A4E; return true;
		case 103: out = 0x8A9124; return true;
		case 106: out = 0x909090; tinted = true; return true;
		case 110: out = 0x6F6265; return true;
		case 121: out = 0xDBDE9E; return true;
		case 159: out = stained[data & 15]; return true;
		case 172: out = 0x965C42; return true;
		case 174: out = 0x7FA9F9; return true;
		case 179: out = 0xB5621F; return true;
		default: return false;
		}
	}

	bool skipForSurface(int id)
	{
		switch (id)
		{
		case 0: case 6: case 27: case 28: case 30: case 31: case 32: case 37: case 38: case 39: case 40:
		case 50: case 51: case 55: case 59: case 63: case 64: case 65: case 66: case 68: case 69: case 70:
		case 71: case 72: case 75: case 76: case 77: case 83: case 90: case 93: case 94: case 96: case 101:
		case 102: case 104: case 105: case 111: case 115: case 131: case 132: case 141: case 142: case 143:
		case 147: case 148: case 157: case 160: case 171: case 175: case 177:
			return true;
		default:
			return false;
		}
	}

	uint32_t blockColor(Level *level, int id, int data, int wx, int wy, int wz)
	{
		uint32_t c;
		bool tinted;
		if (!baseColor(id, data, c, tinted))
		{
			Tile *t = (id >= 0 && id < 4096) ? Tile::tiles[id] : nullptr;
			Minecraft *mc = Minecraft::GetInstance();
			if (t && t->material && t->material->color && mc && mc->getColourTable())
				c = mc->getColourTable()->getColor(t->material->color->col) & 0xFFFFFF;
			else
				c = 0x7D7D7D;
			if (c == 0) c = 0x7D7D7D;
		}
		if (tinted)
		{
			Tile *t = Tile::tiles[id];
			const uint32_t tint = t ? (static_cast<uint32_t>(t->getColor(level, wx, wy, wz)) & 0xFFFFFF) : 0xFFFFFF;
			if (id == 8 || id == 9)
				c = (tint == 0xFFFFFF) ? 0x3F76E4 : mulColor(0x4C80F0, tint == 0 ? 0x3F76E4 : tint);
			else
				c = mulColor(c, tint);
		}
		return c;
	}

	// Samples one chunk into 8x8 cells. Main thread only.
	bool sampleChunk(Level *level, LevelChunk *chunk, ChunkLod &out)
	{
		const int baseX = chunk->x * 16, baseZ = chunk->z * 16;
		bool any = false;
		for (int cz = 0; cz < CELLS; cz++)
		{
			for (int cx = 0; cx < CELLS; cx++)
			{
				int bestTop = -1;
				bool water = false;
				uint32_t r = 0, g = 0, b = 0, n = 0;
				for (int dz = 0; dz < 2; dz++)
				{
					for (int dx = 0; dx < 2; dx++)
					{
						const int x = cx * 2 + dx, z = cz * 2 + dz;
						int y = chunk->getHeightmap(x, z);
						if (y > 255) y = 255;
						if (y < 1) continue;
						int waterTop = -1;
						int top = -1;
						uint32_t col = 0;
						for (int yy = y; yy >= 1; yy--)
						{
							const int id = chunk->getTile(x, yy, z);
							if (id == 8 || id == 9)
							{
								if (waterTop < 0) waterTop = yy;
								if (waterTop - yy > 24) { top = yy; col = 0x203060; break; }
								continue;
							}
							if (skipForSurface(id)) continue;
							top = yy;
							col = blockColor(level, id, chunk->getData(x, yy, z), baseX + x, yy, baseZ + z);
							break;
						}
						if (waterTop >= 0)
						{
							const uint32_t wc = blockColor(level, 9, 0, baseX + x, waterTop, baseZ + z);
							const int depth = waterTop - (top < 0 ? waterTop - 24 : top);
							const float vis = std::exp(-depth * 0.22f);
							col = lerpColor(mulColor(wc, 0xFFFFFF, 0.72f), col, vis * 0.55f);
							top = waterTop;
							water = true;
						}
						if (top < 0) continue;
						if (top > bestTop) bestTop = top;
						r += (col >> 16) & 255; g += (col >> 8) & 255; b += col & 255; n++;
					}
				}
				Cell &cell = out.cells[cz * CELLS + cx];
				if (n == 0)
				{
					cell.flags = 0;
					cell.top = 0;
					cell.color = 0;
					continue;
				}
				cell.flags = CELL_PRESENT | (water ? CELL_WATER : 0);
				cell.top = static_cast<uint8_t>(bestTop);
				cell.color = ((r / n) << 16) | ((g / n) << 8) | (b / n);
				any = true;
			}
		}
		return any;
	}

	// =====================================================================================
	// Mesh building (worker thread)
	// =====================================================================================
	struct Vertex
	{
		float x, y, z;
		uint32_t rgba;
	};

	struct BuiltMesh
	{
		int64_t key = 0;
		int sx = 0, sz = 0;
		int level = 0;
		uint32_t version = 0;
		int originId = 0;
		std::vector<Vertex> verts;	// 4 per quad
		float minY = 0, maxY = 0;
	};

	inline uint32_t packRGBA(uint32_t rgb, float shade)
	{
		const uint32_t r = static_cast<uint32_t>(((rgb >> 16) & 255) * shade);
		const uint32_t g = static_cast<uint32_t>(((rgb >> 8) & 255) * shade);
		const uint32_t b = static_cast<uint32_t>((rgb & 255) * shade);
		return r | (g << 8) | (b << 16) | 0xFF000000u;
	}

	// One coarse cell, aggregated from base cells. caller holds g_storeLock.
	struct Agg
	{
		bool present = false;
		int top = 0;
		uint32_t color = 0;
	};

	Agg aggregate(int baseCellX, int baseCellZ, int span)
	{
		// base cell coordinates are in 2-block units (world block = cell * 2)
		Agg a;
		uint32_t r = 0, g = 0, b = 0, n = 0;
		int best = -1;
		int lastCx = INT32_MIN, lastCz = INT32_MIN;
		const ChunkLod *lod = nullptr;
		for (int dz = 0; dz < span; dz++)
		{
			for (int dx = 0; dx < span; dx++)
			{
				const int bx = baseCellX + dx, bz = baseCellZ + dz;
				const int cx = floorDiv(bx, CELLS), cz = floorDiv(bz, CELLS);
				if (cx != lastCx || cz != lastCz)
				{
					lod = chunkFor(cx, cz);
					lastCx = cx;
					lastCz = cz;
				}
				if (!lod) continue;
				const Cell &c = lod->cells[(bz - cz * CELLS) * CELLS + (bx - cx * CELLS)];
				if (!(c.flags & CELL_PRESENT)) continue;
				if (c.top > best) best = c.top;
				r += (c.color >> 16) & 255; g += (c.color >> 8) & 255; b += c.color & 255; n++;
			}
		}
		if (n == 0) return a;
		a.present = true;
		a.top = best;
		a.color = ((r / n) << 16) | ((g / n) << 8) | (b / n);
		return a;
	}

	void addQuad(std::vector<Vertex> &v, const float p[4][3], uint32_t rgba)
	{
		for (int i = 0; i < 4; i++) v.push_back({ p[i][0], p[i][1], p[i][2], rgba });
	}

	void buildSection(BuiltMesh &m, int originX, int originZ)
	{
		const int span = 1 << m.level;				// base cells per LOD cell
		const int cellsPerSide = SECTION_BASE_CELLS / span;
		const int cellBlocks = span * 2;
		const int baseCellX0 = m.sx * SECTION_BASE_CELLS;
		const int baseCellZ0 = m.sz * SECTION_BASE_CELLS;

		// grid with a 1-cell border for the walls
		const int G = cellsPerSide + 2;
		std::vector<Agg> grid(G * G);
		{
			std::lock_guard<std::mutex> guard(g_storeLock);
			for (int j = 0; j < G; j++)
				for (int i = 0; i < G; i++)
					grid[j * G + i] = aggregate(baseCellX0 + (i - 1) * span, baseCellZ0 + (j - 1) * span, span);
		}

		m.verts.clear();
		m.minY = 256;
		m.maxY = 0;
		const float ox = static_cast<float>(m.sx * SECTION_BLOCKS - originX);
		const float oz = static_cast<float>(m.sz * SECTION_BLOCKS - originZ);
		const size_t maxVerts = 65536;

		for (int j = 1; j <= cellsPerSide; j++)
		{
			for (int i = 1; i <= cellsPerSide; i++)
			{
				const Agg &c = grid[j * G + i];
				if (!c.present) continue;
				if (m.verts.size() + 20 > maxVerts) return;
				const float x0 = ox + (i - 1) * cellBlocks, x1 = x0 + cellBlocks;
				const float z0 = oz + (j - 1) * cellBlocks, z1 = z0 + cellBlocks;
				const float y = static_cast<float>(c.top + 1);
				m.minY = (std::min)(m.minY, y - 64.0f);
				m.maxY = (std::max)(m.maxY, y);

				const float top[4][3] = { { x0, y, z0 }, { x0, y, z1 }, { x1, y, z1 }, { x1, y, z0 } };
				addQuad(m.verts, top, packRGBA(c.color, 1.0f));

				// walls toward lower neighbours (the neighbour draws the opposite case)
				struct N { int di, dj; float shade; } nb[4] = { { -1, 0, 0.6f }, { 1, 0, 0.6f }, { 0, -1, 0.8f }, { 0, 1, 0.8f } };
				for (const N &d : nb)
				{
					const Agg &o = grid[(j + d.dj) * G + (i + d.di)];
					// unknown neighbour: short skirt so holes do not show gaps
					const float yb = o.present ? static_cast<float>(o.top + 1) : y - 6.0f;
					if (yb >= y) continue;
					const uint32_t col = packRGBA(c.color, d.shade);
					if (d.di == -1) { const float q[4][3] = { { x0, y, z0 }, { x0, yb, z0 }, { x0, yb, z1 }, { x0, y, z1 } }; addQuad(m.verts, q, col); }
					if (d.di == 1)  { const float q[4][3] = { { x1, y, z1 }, { x1, yb, z1 }, { x1, yb, z0 }, { x1, y, z0 } }; addQuad(m.verts, q, col); }
					if (d.dj == -1) { const float q[4][3] = { { x1, y, z0 }, { x1, yb, z0 }, { x0, yb, z0 }, { x0, y, z0 } }; addQuad(m.verts, q, col); }
					if (d.dj == 1)  { const float q[4][3] = { { x0, y, z1 }, { x0, yb, z1 }, { x1, yb, z1 }, { x1, y, z1 } }; addQuad(m.verts, q, col); }
				}
			}
		}
	}

	// =====================================================================================
	// Worker
	// =====================================================================================
	struct ViewState
	{
		int camChunkX = 0, camChunkZ = 0;
		int distance = 0;		// chunks
		int realRadius = 0;		// chunks covered by real terrain
		int originX = 0, originZ = 0, originId = 0;
		bool active = false;
	};

	std::mutex g_viewLock;
	ViewState g_view;
	std::condition_variable g_wake;
	std::mutex g_resultLock;
	std::deque<std::unique_ptr<BuiltMesh>> g_results;
	std::thread g_worker;
	std::atomic<bool> g_running(false);
	std::atomic<bool> g_resetBuilt(false);

	int desiredLevel(int distChunks)
	{
		if (distChunks < 48) return 0;		// 2-block cells
		if (distChunks < 96) return 1;		// 4
		if (distChunks < 160) return 2;		// 8
		return 3;							// 16
	}

	void workerMain()
	{
		struct BuiltInfo { int level; uint32_t version; int originId; };
		std::unordered_map<int64_t, BuiltInfo> built;
		auto lastSave = GetTickCount64();

		while (g_running.load())
		{
			ViewState v;
			{
				std::unique_lock<std::mutex> lk(g_viewLock);
				g_wake.wait_for(lk, std::chrono::milliseconds(40));
				v = g_view;
			}
			if (!g_running.load()) break;
			if (g_resetBuilt.exchange(false)) built.clear();

			if (GetTickCount64() - lastSave > 20000)
			{
				lastSave = GetTickCount64();
				std::lock_guard<std::mutex> guard(g_storeLock);
				saveDirtyLocked();
				// drop regions far away from the player
				for (auto it = g_regions.begin(); it != g_regions.end();)
				{
					const int rx = static_cast<int>(it->first >> 32);
					const int rz = static_cast<int>(static_cast<uint32_t>(it->first));
					const int dx = rx * REGION_CHUNKS + 16 - v.camChunkX, dz = rz * REGION_CHUNKS + 16 - v.camChunkZ;
					const int lim = v.distance + REGION_CHUNKS * 2;
					if (std::abs(dx) > lim || std::abs(dz) > lim) it = g_regions.erase(it);
					else ++it;
				}
			}
			if (!v.active || v.distance <= 0) continue;

			// sections ordered by distance, nearest first; build a handful per pass
			const int camSX = floorDiv(v.camChunkX, SECTION_CHUNKS), camSZ = floorDiv(v.camChunkZ, SECTION_CHUNKS);
			const int rs = v.distance / SECTION_CHUNKS + 1;
			int builtThisPass = 0;
			for (int ring = 0; ring <= rs && builtThisPass < 12 && g_running.load(); ring++)
			{
				for (int dz = -ring; dz <= ring && builtThisPass < 12; dz++)
				{
					for (int dx = -ring; dx <= ring && builtThisPass < 12; dx++)
					{
						if (std::abs(dx) != ring && std::abs(dz) != ring) continue;
						const int sx = camSX + dx, sz = camSZ + dz;
						// distance (chunks) from camera to section centre
						const float cdx = (sx * SECTION_CHUNKS + 2) - static_cast<float>(v.camChunkX);
						const float cdz = (sz * SECTION_CHUNKS + 2) - static_cast<float>(v.camChunkZ);
						const int dist = static_cast<int>(std::sqrt(cdx * cdx + cdz * cdz));
						if (dist > v.distance + 2) continue;
						// fully covered by real chunks: nothing to draw
						const int ex0 = sx * SECTION_CHUNKS, ex1 = ex0 + SECTION_CHUNKS - 1;
						const int ez0 = sz * SECTION_CHUNKS, ez1 = ez0 + SECTION_CHUNKS - 1;
						if (ex0 >= v.camChunkX - v.realRadius && ex1 <= v.camChunkX + v.realRadius &&
							ez0 >= v.camChunkZ - v.realRadius && ez1 <= v.camChunkZ + v.realRadius) continue;

						const int level = desiredLevel(dist);
						const int64_t key = key2(sx, sz);
						uint32_t version = 0;
						{
							std::lock_guard<std::mutex> guard(g_storeLock);
							auto it = g_sectionVersion.find(key);
							version = it == g_sectionVersion.end() ? 0 : it->second;
						}
						auto b = built.find(key);
						if (b != built.end() && b->second.level == level && b->second.version == version && b->second.originId == v.originId) continue;

						std::unique_ptr<BuiltMesh> m(new BuiltMesh());
						m->key = key;
						m->sx = sx;
						m->sz = sz;
						m->level = level;
						m->version = version;
						m->originId = v.originId;
						buildSection(*m, v.originX, v.originZ);
						built[key] = { level, version, v.originId };
						{
							std::lock_guard<std::mutex> guard(g_resultLock);
							g_results.push_back(std::move(m));
						}
						builtThisPass++;
					}
				}
			}

			// forget built info for sections far away so they rebuild when we come back
			if (built.size() > 20000)
			{
				for (auto it = built.begin(); it != built.end();)
				{
					const int sx = static_cast<int>(it->first >> 32), sz = static_cast<int>(static_cast<uint32_t>(it->first));
					if (std::abs(sx - camSX) > rs + 2 || std::abs(sz - camSZ) > rs + 2) it = built.erase(it);
					else ++it;
				}
			}
		}
	}

	void startWorker()
	{
		if (g_running.load()) return;
		g_running = true;
		g_worker = std::thread(workerMain);
	}

	void stopWorker()
	{
		if (!g_running.load()) return;
		g_running = false;
		g_wake.notify_all();
		if (g_worker.joinable()) g_worker.join();
	}

	// =====================================================================================
	// GPU (main thread)
	// =====================================================================================
	template <typename T> void SafeRelease(T *&p) { if (p) { p->Release(); p = nullptr; } }

	struct GpuMesh
	{
		ID3D11Buffer *vb = nullptr;
		UINT quads = 0;
		int sx = 0, sz = 0;
		int originId = 0;
		float minY = 0, maxY = 0;
		~GpuMesh() { SafeRelease(vb); }
	};

	struct GpuState
	{
		bool tried = false, ok = false;
		ID3D11VertexShader *vs = nullptr;
		ID3D11PixelShader *ps = nullptr;
		ID3D11InputLayout *layout = nullptr;
		ID3D11Buffer *ib = nullptr;
		ID3D11Buffer *cb = nullptr;
		ID3D11RasterizerState *rs = nullptr;
		ID3D11DepthStencilState *ds = nullptr;
		ID3D11BlendState *bs = nullptr;
		std::unordered_map<int64_t, std::unique_ptr<GpuMesh>> meshes;
	};
	GpuState g_gpu;

	struct CBData
	{
		float modelView[16];
		float projection[16];
		float offset[4];		// origin - camera (xyz), depth bias (w)
		float fog[4];			// start, end, sky light, unused
		float fogColor[4];
		float clip[4];			// real-terrain square relative to camera: minX, minZ, maxX, maxZ
	};

	const char *kHLSL = R"(
cbuffer CB : register(b0)
{
	float4x4 matWorldView;
	float4x4 matProjection;
	float4 offset;
	float4 fog;
	float4 fogColor;
	float4 clipRect;
};
struct VSIn { float3 pos : POSITION; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position; float4 col : COLOR0; float3 rel : TEXCOORD0; };
VSOut VSMain(VSIn i)
{
	VSOut o;
	float3 rel = i.pos + offset.xyz;
	float4 view = mul(matWorldView, float4(rel, 1.0));
	o.pos = mul(matProjection, view);
	o.pos.z += offset.w * o.pos.w;	// push slightly behind real terrain
	o.col = i.col;
	o.rel = rel;
	return o;
}
float4 PSMain(VSOut i) : SV_Target
{
	// real chunks cover this area: let them draw it
	if (i.rel.x > clipRect.x && i.rel.x < clipRect.z && i.rel.z > clipRect.y && i.rel.z < clipRect.w) discard;
	float3 c = i.col.rgb * fog.z;
	float d = length(i.rel);
	float f = saturate((d - fog.x) / max(fog.y - fog.x, 1.0));
	c = lerp(c, fogColor.rgb, f);
	return float4(c, 1.0);
}
)";

	bool initGpu()
	{
		if (g_gpu.tried) return g_gpu.ok;
		g_gpu.tried = true;
		if (!g_pd3dDevice) return false;

		auto compile = [](const char *entry, const char *target, ID3DBlob **out) -> bool {
			ID3DBlob *err = nullptr;
			HRESULT hr = D3DCompile(kHLSL, strlen(kHLSL), "HS_Lod", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
			if (err) { if (FAILED(hr)) OutputDebugStringA(static_cast<const char *>(err->GetBufferPointer())); err->Release(); }
			return SUCCEEDED(hr);
		};

		ID3DBlob *vsb = nullptr, *psb = nullptr;
		if (!compile("VSMain", "vs_4_0", &vsb)) return false;
		if (!compile("PSMain", "ps_4_0", &psb)) { vsb->Release(); return false; }
		bool ok = SUCCEEDED(g_pd3dDevice->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_gpu.vs))
			&& SUCCEEDED(g_pd3dDevice->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_gpu.ps));
		const D3D11_INPUT_ELEMENT_DESC il[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		ok = ok && SUCCEEDED(g_pd3dDevice->CreateInputLayout(il, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_gpu.layout));
		vsb->Release();
		psb->Release();
		if (!ok) return false;

		// shared quad index buffer (16384 quads = 65536 vertices)
		std::vector<uint16_t> idx(16384 * 6);
		for (int q = 0; q < 16384; q++)
		{
			const uint16_t b = static_cast<uint16_t>(q * 4);
			uint16_t *p = &idx[q * 6];
			p[0] = b; p[1] = b + 1; p[2] = b + 2; p[3] = b; p[4] = b + 2; p[5] = b + 3;
		}
		D3D11_BUFFER_DESC ibd = {};
		ibd.ByteWidth = static_cast<UINT>(idx.size() * sizeof(uint16_t));
		ibd.Usage = D3D11_USAGE_IMMUTABLE;
		ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		D3D11_SUBRESOURCE_DATA isd = { idx.data(), 0, 0 };
		if (FAILED(g_pd3dDevice->CreateBuffer(&ibd, &isd, &g_gpu.ib))) return false;

		D3D11_BUFFER_DESC cbd = {};
		cbd.ByteWidth = sizeof(CBData);
		cbd.Usage = D3D11_USAGE_DYNAMIC;
		cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(g_pd3dDevice->CreateBuffer(&cbd, nullptr, &g_gpu.cb))) return false;

		D3D11_RASTERIZER_DESC rd = {};
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_NONE;
		rd.DepthClipEnable = TRUE;
		if (FAILED(g_pd3dDevice->CreateRasterizerState(&rd, &g_gpu.rs))) return false;

		D3D11_DEPTH_STENCIL_DESC dsd = {};
		dsd.DepthEnable = TRUE;
		dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		if (FAILED(g_pd3dDevice->CreateDepthStencilState(&dsd, &g_gpu.ds))) return false;

		D3D11_BLEND_DESC bd = {};
		bd.RenderTarget[0].BlendEnable = FALSE;
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(g_pd3dDevice->CreateBlendState(&bd, &g_gpu.bs))) return false;

		g_gpu.ok = true;
		return true;
	}

	void releaseMeshes() { g_gpu.meshes.clear(); }

	void uploadResults(int originId)
	{
		std::deque<std::unique_ptr<BuiltMesh>> results;
		{
			std::lock_guard<std::mutex> guard(g_resultLock);
			const size_t n = (std::min)(g_results.size(), static_cast<size_t>(24));	// per frame budget
			for (size_t i = 0; i < n; i++)
			{
				results.push_back(std::move(g_results.front()));
				g_results.pop_front();
			}
		}
		for (auto &m : results)
		{
			if (m->originId != originId) continue;
			if (m->verts.empty())
			{
				g_gpu.meshes.erase(m->key);
				continue;
			}
			D3D11_BUFFER_DESC vbd = {};
			vbd.ByteWidth = static_cast<UINT>(m->verts.size() * sizeof(Vertex));
			vbd.Usage = D3D11_USAGE_IMMUTABLE;
			vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA sd = { m->verts.data(), 0, 0 };
			std::unique_ptr<GpuMesh> g(new GpuMesh());
			if (FAILED(g_pd3dDevice->CreateBuffer(&vbd, &sd, &g->vb))) continue;
			g->quads = static_cast<UINT>(m->verts.size() / 4);
			g->sx = m->sx;
			g->sz = m->sz;
			g->originId = m->originId;
			g->minY = m->minY;
			g->maxY = m->maxY;
			g_gpu.meshes[m->key] = std::move(g);
		}
	}

	// =====================================================================================
	// Per-world state (main thread)
	// =====================================================================================
	Level *g_lastLevel = nullptr;
	int64_t g_lastSeed = 0;
	int g_lastDim = 0;
	int g_sweepIndex = 0;
	int g_originX = 0, g_originZ = 0, g_originId = 1;

	void readSettings()
	{
		if (g_distance.load() < 0) g_distance = (std::max)(0, (std::min)(kMaxDistanceChunks, HSSettings::getInt("lodDistance", 96)));
		if (g_enabled.load() < 0) g_enabled = HSSettings::getInt("lodEnabled", 1) != 0 ? 1 : 0;
	}

	void beginWorld(Minecraft *mc)
	{
		Level *level = mc->level;
		char dir[256];
		const bool host = g_NetworkManager.IsHost();
		sprintf_s(dir, "hellish-lod\\%s%016llx\\dim%d", host ? "" : "mp_", static_cast<unsigned long long>(level->getSeed()), level->dimension->id);
		std::lock_guard<std::mutex> guard(g_storeLock);
		g_worldDir = dir;
		g_regions.clear();
		g_sectionVersion.clear();
	}

	void endWorld()
	{
		{
			std::lock_guard<std::mutex> guard(g_storeLock);
			saveDirtyLocked();
			g_regions.clear();
			g_sectionVersion.clear();
			g_worldDir.clear();
		}
		{
			std::lock_guard<std::mutex> guard(g_resultLock);
			g_results.clear();
		}
		g_resetBuilt = true;
		releaseMeshes();
	}

	void ingest(Minecraft *mc, int budget)
	{
		Level *level = mc->level;
		const int r = GameRenderer::s_renderChunks;
		const int side = 2 * r + 1;
		const int total = side * side;
		const int pcx = Mth::floor(mc->player->x) >> 4, pcz = Mth::floor(mc->player->z) >> 4;
		for (int n = 0; n < budget; n++)
		{
			g_sweepIndex = (g_sweepIndex + 1) % total;
			// walk the square from the centre outwards (index -> ring position)
			const int cx = pcx + (g_sweepIndex % side) - r;
			const int cz = pcz + (g_sweepIndex / side) - r;
			if (!level->hasChunk(cx, cz)) continue;
			LevelChunk *chunk = level->getChunk(cx, cz);
			if (!chunk || chunk->isEmpty()) continue;
			ChunkLod lod;
			if (!sampleChunk(level, chunk, lod)) continue;

			std::lock_guard<std::mutex> guard(g_storeLock);
			const int rx = floorDiv(cx, REGION_CHUNKS), rz = floorDiv(cz, REGION_CHUNKS);
			Region &reg = regionFor(rx, rz);
			const int slot = (cz - rz * REGION_CHUNKS) * REGION_CHUNKS + (cx - rx * REGION_CHUNKS);
			if (reg.present[slot] && memcmp(&reg.chunks[slot], &lod, sizeof(lod)) == 0) continue;
			reg.chunks[slot] = lod;
			reg.present[slot] = true;
			reg.dirty = true;
			// this chunk (and the neighbouring sections whose walls touch it) must be rebuilt
			const int sx = floorDiv(cx, SECTION_CHUNKS), sz = floorDiv(cz, SECTION_CHUNKS);
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++)
					g_sectionVersion[key2(sx + dx, sz + dz)]++;
		}
	}
}

// =========================================================================================
// Public API
// =========================================================================================
int GetDistanceChunks()
{
	readSettings();
	return g_distance.load();
}

void SetDistanceChunks(int chunks)
{
	chunks = (std::max)(0, (std::min)(kMaxDistanceChunks, chunks));
	g_distance = chunks;
	HSSettings::setInt("lodDistance", chunks);
}

bool IsEnabled()
{
	readSettings();
	return g_enabled.load() != 0 && g_distance.load() > 0;
}

void Toggle()
{
	readSettings();
	g_enabled = g_enabled.load() ? 0 : 1;
	HSSettings::setInt("lodEnabled", g_enabled.load());
}

float ActiveDistanceBlocks(Minecraft *mc)
{
	if (!IsEnabled() || !mc || !mc->level || !mc->level->dimension) return 0.0f;
	if (mc->level->dimension->id != 0) return 0.0f;	// overworld only (the Nether has a ceiling, the End is small)
	const int d = g_distance.load();
	if (d <= GameRenderer::s_renderChunks) return 0.0f;
	return static_cast<float>(d * 16);
}

void Tick(Minecraft *mc)
{
	if (!mc || !mc->player) return;
	ingest(mc, 12);
}

void Render(Minecraft *mc, double camX, double camY, double camZ, float a)
{
	if (!mc || !mc->level || !mc->player) return;

	// world changes: save the old one, start the new one
	Level *level = mc->level;
	const int dim = level->dimension ? level->dimension->id : 0;
	if (level != g_lastLevel || level->getSeed() != g_lastSeed || dim != g_lastDim)
	{
		if (g_lastLevel) endWorld();
		g_lastLevel = level;
		g_lastSeed = level->getSeed();
		g_lastDim = dim;
		beginWorld(mc);
	}

	const float lodBlocks = ActiveDistanceBlocks(mc);
	{
		std::lock_guard<std::mutex> lk(g_viewLock);
		g_view.active = lodBlocks > 0.0f;
	}
	if (lodBlocks <= 0.0f) return;
	if (!initGpu()) return;
	startWorker();

	// sample loaded chunks
	Tick(mc);

	// vertex origin near the camera keeps float precision in far-out worlds
	const int camBX = static_cast<int>(std::floor(camX)), camBZ = static_cast<int>(std::floor(camZ));
	if (std::abs(camBX - g_originX) > ORIGIN_SNAP * 2 || std::abs(camBZ - g_originZ) > ORIGIN_SNAP * 2 || g_originId == 1 && g_gpu.meshes.empty())
	{
		const int nx = floorDiv(camBX, ORIGIN_SNAP) * ORIGIN_SNAP, nz = floorDiv(camBZ, ORIGIN_SNAP) * ORIGIN_SNAP;
		if (nx != g_originX || nz != g_originZ)
		{
			g_originX = nx;
			g_originZ = nz;
			g_originId++;
			releaseMeshes();
		}
	}

	const int distChunks = g_distance.load();
	const int realR = GameRenderer::s_renderChunks;
	const int camCX = camBX >> 4, camCZ = camBZ >> 4;
	{
		std::lock_guard<std::mutex> lk(g_viewLock);
		g_view.camChunkX = camCX;
		g_view.camChunkZ = camCZ;
		g_view.distance = distChunks;
		g_view.realRadius = realR;
		g_view.originX = g_originX;
		g_view.originZ = g_originZ;
		g_view.originId = g_originId;
	}
	g_wake.notify_one();

	uploadResults(g_originId);

	// drop meshes out of range
	for (auto it = g_gpu.meshes.begin(); it != g_gpu.meshes.end();)
	{
		const int dx = it->second->sx * SECTION_CHUNKS + 2 - camCX, dz = it->second->sz * SECTION_CHUNKS + 2 - camCZ;
		const int lim = distChunks + SECTION_CHUNKS * 2;
		if (it->second->originId != g_originId || std::abs(dx) > lim || std::abs(dz) > lim) it = g_gpu.meshes.erase(it);
		else ++it;
	}
	if (g_gpu.meshes.empty()) return;

	ID3D11DeviceContext *ctx = g_pImmediateContext;
	if (!ctx) return;

	// constants
	CBData cbd;
	memcpy(cbd.modelView, RenderManager.MatrixGet(GL_MODELVIEW), sizeof(cbd.modelView));
	memcpy(cbd.projection, RenderManager.MatrixGet(GL_PROJECTION), sizeof(cbd.projection));
	cbd.offset[0] = static_cast<float>(g_originX - camX);
	cbd.offset[1] = static_cast<float>(-camY);
	cbd.offset[2] = static_cast<float>(g_originZ - camZ);
	cbd.offset[3] = 0.00002f;
	cbd.fog[0] = lodBlocks * 0.45f;
	cbd.fog[1] = lodBlocks;
	cbd.fog[2] = level->getSkyDarken(a);
	cbd.fog[3] = 0.0f;
	GameRenderer *gr = mc->gameRenderer;
	cbd.fogColor[0] = gr ? gr->fr : 0.6f;
	cbd.fogColor[1] = gr ? gr->fg : 0.7f;
	cbd.fogColor[2] = gr ? gr->fb : 1.0f;
	cbd.fogColor[3] = 1.0f;
	cbd.clip[0] = static_cast<float>((camCX - realR) * 16 - camX);
	cbd.clip[1] = static_cast<float>((camCZ - realR) * 16 - camZ);
	cbd.clip[2] = static_cast<float>((camCX + realR + 1) * 16 - camX);
	cbd.clip[3] = static_cast<float>((camCZ + realR + 1) * 16 - camZ);

	D3D11_MAPPED_SUBRESOURCE map;
	if (FAILED(ctx->Map(g_gpu.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
	memcpy(map.pData, &cbd, sizeof(cbd));
	ctx->Unmap(g_gpu.cb, 0);

	// save the state the game expects
	ID3D11InputLayout *oldLayout = nullptr; D3D11_PRIMITIVE_TOPOLOGY oldTopo;
	ID3D11VertexShader *oldVS = nullptr; ID3D11PixelShader *oldPS = nullptr;
	ID3D11Buffer *oldVSCB = nullptr, *oldPSCB = nullptr, *oldIB = nullptr, *oldVB = nullptr;
	DXGI_FORMAT oldIBFmt; UINT oldIBOff = 0, oldStride = 0, oldOff = 0;
	ID3D11RasterizerState *oldRS = nullptr; ID3D11DepthStencilState *oldDS = nullptr; UINT oldRef = 0;
	ID3D11BlendState *oldBS = nullptr; FLOAT oldBF[4]; UINT oldMask = 0;
	ctx->IAGetInputLayout(&oldLayout);
	ctx->IAGetPrimitiveTopology(&oldTopo);
	ctx->VSGetShader(&oldVS, nullptr, nullptr);
	ctx->PSGetShader(&oldPS, nullptr, nullptr);
	ctx->VSGetConstantBuffers(0, 1, &oldVSCB);
	ctx->PSGetConstantBuffers(0, 1, &oldPSCB);
	ctx->IAGetIndexBuffer(&oldIB, &oldIBFmt, &oldIBOff);
	ctx->IAGetVertexBuffers(0, 1, &oldVB, &oldStride, &oldOff);
	ctx->RSGetState(&oldRS);
	ctx->OMGetDepthStencilState(&oldDS, &oldRef);
	ctx->OMGetBlendState(&oldBS, oldBF, &oldMask);

	ctx->IASetInputLayout(g_gpu.layout);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->IASetIndexBuffer(g_gpu.ib, DXGI_FORMAT_R16_UINT, 0);
	ctx->VSSetShader(g_gpu.vs, nullptr, 0);
	ctx->PSSetShader(g_gpu.ps, nullptr, 0);
	ctx->VSSetConstantBuffers(0, 1, &g_gpu.cb);
	ctx->PSSetConstantBuffers(0, 1, &g_gpu.cb);
	ctx->RSSetState(g_gpu.rs);
	ctx->OMSetDepthStencilState(g_gpu.ds, 0);
	const FLOAT bf[4] = { 0, 0, 0, 0 };
	ctx->OMSetBlendState(g_gpu.bs, bf, 0xFFFFFFFF);

	// frustum planes from the row-vector matrix M = MV * P (v' = v * M)
	float M[16];
	{
		const float *A = cbd.modelView, *B = cbd.projection;
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
				M[r * 4 + c] = A[r * 4 + 0] * B[0 * 4 + c] + A[r * 4 + 1] * B[1 * 4 + c] + A[r * 4 + 2] * B[2 * 4 + c] + A[r * 4 + 3] * B[3 * 4 + c];
	}
	float planes[5][4];
	for (int r = 0; r < 4; r++)
	{
		const float c0 = M[r * 4 + 0], c1 = M[r * 4 + 1], c3 = M[r * 4 + 3];
		planes[0][r] = c3 + c0;		// left
		planes[1][r] = c3 - c0;		// right
		planes[2][r] = c3 + c1;		// bottom
		planes[3][r] = c3 - c1;		// top
		planes[4][r] = M[r * 4 + 2];	// near (D3D 0..1 depth)
	}

	const UINT stride = sizeof(Vertex), off = 0;
	const float ox = cbd.offset[0], oy = cbd.offset[1], oz = cbd.offset[2];
	for (auto &kv : g_gpu.meshes)
	{
		GpuMesh &g = *kv.second;
		// section AABB relative to the camera
		const float x0 = g.sx * SECTION_BLOCKS - g_originX + ox, z0 = g.sz * SECTION_BLOCKS - g_originZ + oz;
		const float x1 = x0 + SECTION_BLOCKS, z1 = z0 + SECTION_BLOCKS;
		const float y0 = g.minY + oy, y1 = g.maxY + oy;
		bool visible = true;
		for (int p = 0; p < 5 && visible; p++)
		{
			const float *pl = planes[p];
			const float px = pl[0] >= 0 ? x1 : x0, py = pl[1] >= 0 ? y1 : y0, pz = pl[2] >= 0 ? z1 : z0;
			if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0) visible = false;
		}
		if (!visible) continue;
		ctx->IASetVertexBuffers(0, 1, &g.vb, &stride, &off);
		ctx->DrawIndexed(g.quads * 6, 0, 0);
	}

	// restore
	ctx->IASetInputLayout(oldLayout);
	ctx->IASetPrimitiveTopology(oldTopo);
	ctx->VSSetShader(oldVS, nullptr, 0);
	ctx->PSSetShader(oldPS, nullptr, 0);
	ctx->VSSetConstantBuffers(0, 1, &oldVSCB);
	ctx->PSSetConstantBuffers(0, 1, &oldPSCB);
	ctx->IASetIndexBuffer(oldIB, oldIBFmt, oldIBOff);
	ctx->IASetVertexBuffers(0, 1, &oldVB, &oldStride, &oldOff);
	ctx->RSSetState(oldRS);
	ctx->OMSetDepthStencilState(oldDS, oldRef);
	ctx->OMSetBlendState(oldBS, oldBF, oldMask);
	SafeRelease(oldLayout); SafeRelease(oldVS); SafeRelease(oldPS); SafeRelease(oldVSCB); SafeRelease(oldPSCB);
	SafeRelease(oldIB); SafeRelease(oldVB); SafeRelease(oldRS); SafeRelease(oldDS); SafeRelease(oldBS);
}

void Reset()
{
	stopWorker();
	if (g_lastLevel) endWorld();
	g_lastLevel = nullptr;
}
}
