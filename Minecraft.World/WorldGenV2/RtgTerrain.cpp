#include "stdafx.h"
#include "RtgCore.h"

#include <cmath>
#include <algorithm>

namespace WorldGenV2
{
	// ---------------------------------------------------------------------------------
	// RtgWorld
	// ---------------------------------------------------------------------------------
	const float RtgWorld::ACTUAL_RIVER_PROPORTION = 150.0f / 1600.0f;
	const float RtgWorld::RIVER_FLATTENING_ADDEND = RtgWorld::ACTUAL_RIVER_PROPORTION / (1.0f - RtgWorld::ACTUAL_RIVER_PROPORTION);
	const float RtgWorld::RIVER_BOTTOM = 57.0f;
	const float RtgWorld::LAKE_BOTTOM = 53.0f;

	RtgWorld::RtgWorld(int64_t seed) : m_seed(seed)
	{
		for (int i = 0; i < 10; i++) m_simplex[i].reset(new OpenSimplexNoise(seed + i));
		for (int i = 0; i < 5; i++) m_cellular[i].reset(new SpacedCellularNoise(seed + i));
	}

	const OpenSimplexNoise &RtgWorld::simplex(int index) const
	{
		if (index < 0 || index >= 10) index = 0;
		return *m_simplex[index];
	}

	SpacedCellularNoise &RtgWorld::cellular(int index) const
	{
		if (index < 0 || index >= 5) index = 0;
		return *m_cellular[index];
	}

	float RtgWorld::riverAdjustedForDepthDifference(float river)
	{
		if (river > ACTUAL_RIVER_PROPORTION) return river;
		river -= ACTUAL_RIVER_PROPORTION;
		river = river * (63.0f - RIVER_BOTTOM) / (63.0f - LAKE_BOTTOM);
		river += ACTUAL_RIVER_PROPORTION;
		return river;
	}

	// ---------------------------------------------------------------------------------
	// TerrainBase
	// ---------------------------------------------------------------------------------
	namespace Terrain
	{
		static const float MINIMUM_OCEAN_FLOOR = 20.01f;

		float blendedHillHeight(float simplex)
		{
			float result = simplex + 1;
			result = result * result * result + 10;
			result = static_cast<float>(std::pow(static_cast<double>(result), .33333333333333));
			result = result / 0.46631f;
			result = result - 4.62021f;
			return result;
		}

		float blendedHillHeight(float simplex, float turnAt)
		{
			float adjusted = (1.0f - (1.0f - simplex) / (1.0f - turnAt));
			return blendedHillHeight(adjusted);
		}

		float above(float limited, float limit)
		{
			return limited > limit ? limited - limit : 0.0f;
		}

		float unsignedPower(float number, float power)
		{
			if (number > 0) return static_cast<float>(std::pow(static_cast<double>(number), static_cast<double>(power)));
			return (-1.0f) * static_cast<float>(std::pow(static_cast<double>((-1.0f) * number), static_cast<double>(power)));
		}

		float hills(float x, float y, float hillStrength, const RtgWorld &w)
		{
			float m = w.simplex(0).noise2f(x / 150.0f, y / 150.0f);
			m = blendedHillHeight(m, 0.2f);
			float sm = w.simplex(2).noise2f(x / 55, y / 55);
			sm = blendedHillHeight(sm, 0.2f);
			sm *= sm * m;
			m += sm / 3.0f;
			return m * hillStrength;
		}

		float groundNoise(float x, float y, float amplitude, const RtgWorld &w)
		{
			float h = blendedHillHeight(w.simplex(0).noise2f(x / 49.0f, y / 49.0f), 0.2f) * amplitude;
			h += blendedHillHeight(w.simplex(1).noise2f(x / 23.0f, y / 23.0f), 0.2f) * amplitude / 2.0f;
			h += blendedHillHeight(w.simplex(2).noise2f(x / 11.0f, y / 11.0f), 0.2f) * amplitude / 4.0f;
			return h;
		}

		float getTerrainBase() { return 68.0f; }
		float getTerrainBase(float river) { return 62.0f + 6.0f * river; }

		float mountainCap(float m)
		{
			if (m > 160)
			{
				m = 160 + (m - 160) * .75f;
				if (m > 180) m = 180 + (m - 180.0f) * .75f;
			}
			return m;
		}

		float bayesianAdjustment(float probability, float multiplier)
		{
			if (probability >= 1) return probability;
			if (probability <= 0) return probability;
			float newConfidence = probability * multiplier / (1.0f - probability);
			return newConfidence / (1.0f + newConfidence);
		}

		float riverized(float height, float river)
		{
			if (height < 62.45f) return height;
			float adjustment = (height - 62.45f) / 10.0f + .6f;
			river = bayesianAdjustment(river, adjustment);
			return 62.45f + (height - 62.45f) * river;
		}

		float terrainBeach(int x, int y, const RtgWorld &w, float river, float baseHeight)
		{
			return riverized(baseHeight + groundNoise(static_cast<float>(x), static_cast<float>(y), 4.0f, w), river);
		}

		float terrainFlatLakes(int x, int y, const RtgWorld &w, float river, float baseHeight)
		{
			const float STANDARD_RUGGEDNESS_WAVELENGTH = 200.0f;
			float ruggedNoise = w.simplex(1).noise2f(x / STANDARD_RUGGEDNESS_WAVELENGTH, y / STANDARD_RUGGEDNESS_WAVELENGTH);
			ruggedNoise = blendedHillHeight(ruggedNoise);
			float h = groundNoise(static_cast<float>(x), static_cast<float>(y), 2.0f * (ruggedNoise + 1.0f), w);
			return riverized(baseHeight + h, river);
		}

		float terrainForest(int x, int y, const RtgWorld &w, float river, float baseHeight)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			double h = simplex.noise2d(x / 100.0, y / 100.0) * 8.0;
			h += simplex.noise2d(x / 30.0, y / 30.0) * 4.0;
			h += simplex.noise2d(x / 15.0, y / 15.0) * 2.0;
			h += simplex.noise2d(x / 7.0, y / 7.0);
			return riverized(baseHeight + 20.0f + static_cast<float>(h), river);
		}

		float terrainGrasslandFlats(int x, int y, const RtgWorld &w, float river, float mPitch, float baseHeight)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			float h = simplex.noise2f(x / 100.0f, y / 100.0f) * 7;
			h += simplex.noise2f(x / 20.0f, y / 20.0f) * 2;
			float m = simplex.noise2f(x / 180.0f, y / 180.0f) * 35.0f * river;
			m *= m / mPitch;
			float sm = blendedHillHeight(simplex.noise2f(x / 30.0f, y / 30.0f)) * 8.0f;
			sm *= m / 20.0f > 3.75f ? 3.75f : m / 20.0f;
			m += sm;
			return riverized(baseHeight + h + m, river);
		}

		float terrainHighland(float x, float y, const RtgWorld &w, float river, float start, float width, float height, float baseAdjust)
		{
			float h = w.simplex(0).noise2f(x / width, y / width) * height * river;
			h = h < start ? start + ((h - start) / 4.5f) : h;
			if (h < 0.0f) h = 0;
			if (h > 0.0f)
			{
				float st = h * 1.5f > 15.0f ? 15.0f : h * 1.5f;
				h += w.simplex(4).noise3f(x / 70.0f, y / 70.0f, 1.0f) * st;
				h = h * river;
			}
			h += blendedHillHeight(w.simplex(0).noise2f(x / 20.0f, y / 20.0f), 0.0f) * 4.0f;
			h += blendedHillHeight(w.simplex(0).noise2f(x / 12.0f, y / 12.0f), 0.0f) * 2.0f;
			h += blendedHillHeight(w.simplex(0).noise2f(x / 5.0f, y / 5.0f), 0.0f) * 1.0f;
			if (h < 0) h = h / 2.0f;
			if (h < -3) h = (h + 3.0f) / 2.0f - 3.0f;
			return getTerrainBase(river) + (h + baseAdjust) * river;
		}

		float terrainLonelyMountain(int x, int y, const RtgWorld &w, float river, float strength, float width, float terrainHeight)
		{
			const OpenSimplexNoise &simplex0 = w.simplex(0);
			float h = blendedHillHeight(simplex0.noise2f(x / 20.0f, y / 20.0f), 0) * 3;
			h += blendedHillHeight(simplex0.noise2f(x / 7.0f, y / 7.0f), 0) * 1.3f;

			float m = simplex0.noise2f(x / width, y / width) * strength * river;
			m *= m / 35.0f;
			m = m > 70.0f ? 70.0f + (m - 70.0f) / 2.5f : m;

			float st = m * 0.7f;
			st = st > 20.0f ? 20.0f : st;
			float c = w.simplex(4).noise3f(x / 30.0f, y / 30.0f, 1.0f) * (5.0f + st);

			float sm = simplex0.noise2f(x / 30.0f, y / 30.0f) * 8.0f + simplex0.noise2f(x / 8.0f, y / 8.0f);
			sm *= (m + 10.0f) / 20.0f > 2.5f ? 2.5f : (m + 10.0f) / 20.0f;
			m += sm;

			m += c;

			if (m > 90)
			{
				m = 90.0f + (m - 90.0f) * .75f;
				if (m > 110) m = 110.0f + (m - 110.0f) * .75f;
			}
			return riverized(terrainHeight + h + m, river);
		}

		float terrainMarsh(int x, int y, const RtgWorld &w, float baseHeight, float river)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			float h = simplex.noise2f(x / 130.0f, y / 130.0f) * 20.0f;
			h += simplex.noise2f(x / 12.0f, y / 12.0f) * 2.0f;
			h += simplex.noise2f(x / 18.0f, y / 18.0f) * 4.0f;
			h = h < 8.0f ? 0.0f : h - 8.0f;
			if (h == 0.0f)
			{
				h += simplex.noise2f(x / 20.0f, y / 20.0f) + simplex.noise2f(x / 5.0f, y / 5.0f);
				h *= 2.0f;
			}
			return riverized(baseHeight + h, river);
		}

		float terrainOcean(int x, int y, const RtgWorld &w, float river, float averageFloor)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			float h = simplex.noise2f(x / 300.0f, y / 300.0f) * 8.0f * river;
			h += simplex.noise2f(x / 50.0f, y / 50.0f) * 2.0f;
			h += simplex.noise2f(x / 15.0f, y / 15.0f) * 1.0f;
			float floNoise = averageFloor + h;
			return floNoise < MINIMUM_OCEAN_FLOOR ? MINIMUM_OCEAN_FLOOR : floNoise;
		}

		float terrainPlains(int x, int y, const RtgWorld &w, float river, float stPitch, float stFactor, float hPitch, float hDivisor, float baseHeight)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			float st = (simplex.noise2f(x / stPitch, y / stPitch) + 0.38f) * stFactor * river;
			st = st < 0.2f ? 0.2f : st;
			float h = simplex.noise2f(x / hPitch, y / hPitch) * st * 2.0f;
			h = h > 0.0f ? -h : h;
			h += st;
			h *= h / hDivisor;
			h += st;
			return riverized(baseHeight + h, river);
		}

		float terrainPolar(float x, float y, const RtgWorld &w, float river, float stPitch, float stFactor, float hPitch, float hDivisor, float baseHeight)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			float st = (simplex.noise2f(x / stPitch, y / stPitch) + 0.38f) * stFactor * river;
			st = st < 0.1f ? 0.1f : st;
			float h = simplex.noise2f(x / hPitch, y / hPitch) * st * 2.0f;
			h = h > 0.0f ? -h : h;
			h += st;
			h *= h / hDivisor;
			h += st;
			return riverized(baseHeight + h, river);
		}

		float getRiverStrength(int worldX, int worldZ, const RtgWorld &w)
		{
			double pX = worldX;
			double pZ = worldZ;
			SimplexData2D jitter(SimplexData2D::Disk);

			w.simplex(1).multiEval2D(static_cast<float>(worldX) / 240.0, static_cast<float>(worldZ) / 240.0, jitter);
			pX += jitter.deltaX * w.riverLargeBendSize();
			pZ += jitter.deltaY * w.riverLargeBendSize();

			w.simplex(2).multiEval2D(static_cast<float>(worldX) / 80.0, static_cast<float>(worldZ) / 80.0, jitter);
			pX += jitter.deltaX * w.riverSmallBendSize();
			pZ += jitter.deltaY * w.riverSmallBendSize();

			pX /= w.riverSeparation();
			pZ /= w.riverSeparation();

			double riverFactor = w.cellular(0).eval2D(pX, pZ).interiorValue();
			riverFactor = bayesianAdjustment(static_cast<float>(riverFactor), 0.5f);

			if (riverFactor > w.riverValleyLevel()) return 0;
			return static_cast<float>(riverFactor / w.riverValleyLevel() - 1.0);
		}

		float calcCliff(int x, int z, const float *noise, float river)
		{
			float cliff = 0.0f;
			const float here = noise[x * 16 + z];
			if (here < 64.5f && here > 61.5f)
			{
				if (river + RtgWorld::ACTUAL_RIVER_PROPORTION > 0.97f)
				{
					float xUp = 0.0f, xDown = 0.0f, zUp = 0.0f, zDown = 0.0f;
					if (x > 0) xDown = std::fabs(here - noise[(x - 1) * 16 + z]);
					if (z > 0) zDown = std::fabs(here - noise[x * 16 + z - 1]);
					if (x < 15) xUp = std::fabs(here - noise[(x + 1) * 16 + z]);
					if (z < 15) zUp = std::fabs(here - noise[x * 16 + z + 1]);
					float xCliff = (std::min)(xUp, xDown);
					float zCliff = (std::min)(zDown, zUp);
					return (std::max)(xCliff, zCliff);
				}
			}
			if (x > 0) cliff = (std::max)(cliff, std::fabs(here - noise[(x - 1) * 16 + z]));
			if (z > 0) cliff = (std::max)(cliff, std::fabs(here - noise[x * 16 + z - 1]));
			if (x < 15) cliff = (std::max)(cliff, std::fabs(here - noise[(x + 1) * 16 + z]));
			if (z < 15) cliff = (std::max)(cliff, std::fabs(here - noise[x * 16 + z + 1]));
			return cliff;
		}
	}

	// ---------------------------------------------------------------------------------
	// ChunkPrimer
	// ---------------------------------------------------------------------------------
	ChunkPrimer::ChunkPrimer()
	{
		std::fill(m_blocks, m_blocks + sizeof(m_blocks), static_cast<uint8_t>(0));
		std::fill(m_data, m_data + sizeof(m_data), static_cast<uint8_t>(0));
	}
}
