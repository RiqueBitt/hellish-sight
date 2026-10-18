#include "stdafx.h"
#include "RtgCore.h"

#include "../Tile.h"

#include <cmath>
#include <algorithm>

// WorldGen V2 revision 2 (Hellish Sight) - terrain enhancements layered on top of the
// RTG landscape. Only worlds created with revision 2 or later use this; older V2 worlds
// keep generating exactly as before, so their new chunks still line up with the old ones.
//
//  * Alpine relief: ridged multifractal peaks and eroded gullies on high ground.
//  * Rolling lowlands: gentle hills that break up flat plains (never touches beaches/rivers).
//  * Cliffs and overhangs: a thin 3D density band on steep mountain faces.
//  * Caves: spaghetti tunnels and deep "cheese" caverns (on top of the game's own caves),
//    with lava pools at the bottom, kept away from oceans, rivers and lakes.
//  * Surface detail: exposed stone and gravel on steep slopes, snow on high peaks.

namespace WorldGenV2
{
	namespace
	{
		inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
		inline float smoothstep(float a, float b, float v)
		{
			const float t = clamp01((v - a) / (b - a));
			return t * t * (3.0f - 2.0f * t);
		}
		inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

		// Coarse 3D noise lattice (every 4 blocks horizontally, 8 vertically) with
		// trilinear interpolation - ~800 noise evaluations per chunk instead of 65536.
		class Lattice3D
		{
		public:
			static const int NX = 5, NZ = 5, NY = 33;

			template <typename F>
			void fill(F &&sample)
			{
				for (int ix = 0; ix < NX; ix++)
					for (int iz = 0; iz < NZ; iz++)
						for (int iy = 0; iy < NY; iy++)
							m_v[(ix * NZ + iz) * NY + iy] = sample(ix * 4, iy * 8, iz * 4);
			}

			float at(int x, int y, int z) const
			{
				const int ix = x >> 2, iz = z >> 2, iy = y >> 3;
				const float fx = (x & 3) * 0.25f, fz = (z & 3) * 0.25f, fy = (y & 7) * 0.125f;
				const int ix1 = std::min(ix + 1, NX - 1), iz1 = std::min(iz + 1, NZ - 1), iy1 = std::min(iy + 1, NY - 1);
				const float c000 = v(ix, iy, iz), c100 = v(ix1, iy, iz), c010 = v(ix, iy1, iz), c110 = v(ix1, iy1, iz);
				const float c001 = v(ix, iy, iz1), c101 = v(ix1, iy, iz1), c011 = v(ix, iy1, iz1), c111 = v(ix1, iy1, iz1);
				const float c00 = lerp(c000, c100, fx), c10 = lerp(c010, c110, fx);
				const float c01 = lerp(c001, c101, fx), c11 = lerp(c011, c111, fx);
				return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
			}

		private:
			float v(int ix, int iy, int iz) const { return m_v[(ix * NZ + iz) * NY + iy]; }
			float m_v[NX * NZ * NY];
		};
	}

	RtgEnhancer::RtgEnhancer(int64_t seed)
	{
		for (int i = 0; i < NOISES; i++)
		{
			m_noise[i].reset(new OpenSimplexNoise(seed ^ (0x5DEECE66DLL * (i + 11)) ^ 0x48534C56LL));
		}
	}

	float RtgEnhancer::ridged(float x, float z) const
	{
		// ridged multifractal: sharp crests, smooth valleys
		float sum = 0.0f, amp = 1.0f, freq = 1.0f, weight = 1.0f, norm = 0.0f;
		for (int o = 0; o < 4; o++)
		{
			float n = 1.0f - std::fabs(m_noise[0]->noise2f(x * freq, z * freq));
			n *= n;
			n *= weight;
			weight = clamp01(n * 1.6f);
			sum += n * amp;
			norm += amp;
			amp *= 0.5f;
			freq *= 2.03f;
		}
		return sum / norm;	// ~[0,1]
	}

	void RtgEnhancer::shapeHeights(int worldX, int worldZ, float *height, const float *riverLand) const
	{
		for (int x = 0; x < 16; x++)
		{
			for (int z = 0; z < 16; z++)
			{
				const int i = x * 16 + z;
				const float wx = static_cast<float>(worldX + x);
				const float wz = static_cast<float>(worldZ + z);
				float h = height[i];
				const float riverMask = clamp01(1.0f - riverLand[i] * 1.6f);
				if (riverMask <= 0.0f) continue;

				// --- alpine relief on high ground ---------------------------------------
				const float mountain = smoothstep(82.0f, 125.0f, h);
				if (mountain > 0.0f)
				{
					// domain warp so ridges meander instead of running in straight lines
					const float warpX = m_noise[1]->noise2f(wx / 220.0f, wz / 220.0f) * 40.0f;
					const float warpZ = m_noise[2]->noise2f(wx / 220.0f + 31.7f, wz / 220.0f - 17.3f) * 40.0f;
					const float r = ridged((wx + warpX) / 190.0f, (wz + warpZ) / 190.0f);
					// gullies: narrow eroded channels running down the slopes
					const float g = std::fabs(m_noise[3]->noise2f(wx / 46.0f, wz / 46.0f));
					const float gully = (1.0f - smoothstep(0.0f, 0.12f, g)) * 7.0f;
					h += (r * 34.0f - 9.0f - gully) * mountain * riverMask;
				}

				// --- rolling lowlands (keeps beaches, rivers and swamps intact) ------------
				const float lowland = smoothstep(67.0f, 74.0f, h) * (1.0f - smoothstep(88.0f, 100.0f, h));
				if (lowland > 0.0f)
				{
					const float roll = m_noise[4]->noise2f(wx / 96.0f, wz / 96.0f) * 3.2f
						+ m_noise[4]->noise2f(wx / 31.0f + 50.0f, wz / 31.0f) * 1.1f;
					// broad valleys: occasionally sink large areas a little
					const float valley = smoothstep(0.35f, 0.75f, m_noise[5]->noise2f(wx / 520.0f, wz / 520.0f)) * -4.5f;
					h += (roll + valley) * lowland * riverMask;
				}

				height[i] = h;
			}
		}
	}

	void RtgEnhancer::computeSlopes(const float *height, float *slope)
	{
		for (int x = 0; x < 16; x++)
		{
			for (int z = 0; z < 16; z++)
			{
				const int x0 = std::max(x - 1, 0), x1 = std::min(x + 1, 15);
				const int z0 = std::max(z - 1, 0), z1 = std::min(z + 1, 15);
				const float dx = (height[x1 * 16 + z] - height[x0 * 16 + z]) / static_cast<float>(x1 - x0);
				const float dz = (height[x * 16 + z1] - height[x * 16 + z0]) / static_cast<float>(z1 - z0);
				slope[x * 16 + z] = std::sqrt(dx * dx + dz * dz);
			}
		}
	}

	bool RtgEnhancer::solidAt(int x, int y, int z, float height, float cliff, const void *latticePtr) const
	{
		const Lattice3D &lat = *static_cast<const Lattice3D *>(latticePtr);
		if (cliff <= 0.0f) return y <= height;
		const float density = (height - static_cast<float>(y)) + lat.at(x, y, z) * cliff;
		return density > 0.0f;
	}

	void RtgEnhancer::fillTerrain(int worldX, int worldZ, const float *height, const float *slope, int seaLevel, ChunkPrimer &primer) const
	{
		Lattice3D lat;
		const OpenSimplexNoise &n = *m_noise[6];
		lat.fill([&](int lx, int ly, int lz) {
			return n.noise3f((worldX + lx) / 22.0f, ly / 13.0f, (worldZ + lz) / 22.0f);
		});

		for (int x = 0; x < 16; x++)
		{
			for (int z = 0; z < 16; z++)
			{
				const int i = x * 16 + z;
				const float h = height[i];
				// cliff band only on steep, high mountain faces
				const float cliff = smoothstep(95.0f, 130.0f, h) * smoothstep(1.1f, 2.6f, slope[i]) * 11.0f;
				const int top = static_cast<int>(h + cliff + 1.0f);
				const int bottom = static_cast<int>(h - cliff - 1.0f);
				for (int y = 0; y < 256; y++)
				{
					bool solid;
					if (y <= bottom) solid = true;
					else if (y > top) solid = false;
					else solid = solidAt(x, y, z, h, cliff, &lat);

					if (solid) primer.setBlock(x, y, z, BlockState(Tile::stone_Id));
					else if (y < seaLevel) primer.setBlock(x, y, z, BlockState(Tile::calmWater_Id));
				}
			}
		}
	}

	void RtgEnhancer::carveCaves(int worldX, int worldZ, const float *height, int seaLevel, ChunkPrimer &primer) const
	{
		Lattice3D spagA, spagB, cheese;
		const OpenSimplexNoise &na = *m_noise[7];
		const OpenSimplexNoise &nb = *m_noise[8];
		const OpenSimplexNoise &nc = *m_noise[9];
		spagA.fill([&](int lx, int ly, int lz) { return na.noise3f((worldX + lx) / 58.0f, ly / 34.0f, (worldZ + lz) / 58.0f); });
		spagB.fill([&](int lx, int ly, int lz) { return nb.noise3f((worldX + lx) / 58.0f, ly / 34.0f, (worldZ + lz) / 58.0f); });
		cheese.fill([&](int lx, int ly, int lz) { return nc.noise3f((worldX + lx) / 92.0f, ly / 44.0f, (worldZ + lz) / 92.0f); });

		const int lavaLevel = 10;
		for (int x = 0; x < 16; x++)
		{
			for (int z = 0; z < 16; z++)
			{
				const float h = height[x * 16 + z];
				// keep a solid lid under water and near shores so caves never drain oceans/rivers/lakes
				const bool wet = h < seaLevel + 3;
				const int ceiling = wet ? static_cast<int>(h) - 7 : static_cast<int>(h) + 2;
				const int maxY = std::min(ceiling, 250);
				for (int y = 6; y <= maxY; y++)
				{
					const int id = primer.getBlock(x, y, z);
					if (id != Tile::stone_Id && id != Tile::dirt_Id && id != Tile::grass_Id && id != Tile::gravel_Id
						&& id != Tile::sand_Id && id != Tile::sandStone_Id && id != Tile::clayHardened_Id && id != Tile::clayHardened_colored_Id)
						continue;

					// tunnels narrow toward the surface so few of them punch through
					const float depthFade = 1.0f - smoothstep(h - 14.0f, h + 1.0f, static_cast<float>(y)) * 0.55f;
					const float t = 0.055f * depthFade;
					bool carve = std::fabs(spagA.at(x, y, z)) < t && std::fabs(spagB.at(x, y, z)) < t;

					if (!carve && y < 52)
					{
						// big caverns deep underground, strongest around y = 25
						const float band = 1.0f - std::fabs(y - 25.0f) / 27.0f;
						carve = cheese.at(x, y, z) > 0.62f - band * 0.12f;
					}
					if (!carve) continue;

					// never open a cave right under water (neighbour columns may be wet too)
					if (y + 1 < 256)
					{
						const int above = primer.getBlock(x, y + 1, z);
						if (above == Tile::calmWater_Id || above == Tile::water_Id) continue;
					}

					primer.setBlock(x, y, z, y <= lavaLevel ? BlockState(Tile::calmLava_Id) : BlockState(0));
				}
			}
		}
	}

	void RtgEnhancer::paintDetail(int worldX, int worldZ, const float *slope, ChunkPrimer &primer) const
	{
		for (int x = 0; x < 16; x++)
		{
			for (int z = 0; z < 16; z++)
			{
				const float s = slope[x * 16 + z];
				// find the surface block
				int top = 255;
				while (top > 0 && primer.getBlock(x, top, z) == 0) top--;
				const int id = primer.getBlock(x, top, z);
				if (id == Tile::calmWater_Id || id == Tile::water_Id || id == Tile::calmLava_Id || id == 0) continue;

				const float wx = static_cast<float>(worldX + x), wz = static_cast<float>(worldZ + z);
				const float patch = m_noise[5]->noise2f(wx / 9.0f, wz / 9.0f);

				// exposed rock on steep slopes above the lowlands
				if (top > 74 && s > 1.7f && (id == Tile::grass_Id || id == Tile::dirt_Id || id == Tile::snow_Id))
				{
					const BlockState rock = (patch > 0.45f) ? BlockState(Tile::gravel_Id) : BlockState(Tile::stone_Id);
					for (int d = 0; d < 4 && top - d > 0; d++)
					{
						const int b = primer.getBlock(x, top - d, z);
						if (b == Tile::grass_Id || b == Tile::dirt_Id || b == Tile::snow_Id) primer.setBlock(x, top - d, z, rock);
					}
				}

				// snow caps on high peaks (a little lower where the noise says so)
				const int snowLine = 158 + static_cast<int>(patch * 10.0f);
				if (top >= snowLine && top < 255 && s < 2.4f)
				{
					const int b = primer.getBlock(x, top, z);
					if (b == Tile::stone_Id || b == Tile::grass_Id || b == Tile::dirt_Id || b == Tile::gravel_Id)
					{
						if (top > snowLine + 12) primer.setBlock(x, top, z, BlockState(Tile::snow_Id));
						primer.setBlock(x, top + 1, z, BlockState(Tile::topSnow_Id));
					}
				}
			}
		}
	}
}
