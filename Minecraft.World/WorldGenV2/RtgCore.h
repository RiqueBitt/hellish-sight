#pragma once

// WorldGen V2 (Hellish Sight) - a port of the terrain generator of Realistic Terrain
// Generation (RTG) 1.12.2, https://github.com/Team-RTG/Realistic-Terrain-Generation
// Licensed under the GNU General Public License v3.0, like the original.
//
// What is ported: the landscape pipeline (biome sampling and weighted blending, rivers
// and scenic lakes, the BiomeAnalyzer biome repair), TerrainBase helpers, height effects,
// the per-biome terrain shapes and surface painters for every biome this game has, and
// RTG's surface jitter between biomes. Caves, structures, ores, trees and mob spawning
// keep using the game's own systems.
//
// Differences forced by this engine: no podzol / packed ice / stone & cold beach biomes
// in this version of the game, so the closest blocks/biomes are used instead.

#include "RtgNoise.h"

#include <cstdint>
#include <memory>
#include <vector>

class Random;
class Biome;
class BiomeSource;

namespace WorldGenV2
{
	// ---------------------------------------------------------------------------------
	// World-wide noise and settings (RTGWorld + RTGChunkGenSettings defaults)
	// ---------------------------------------------------------------------------------
	class RtgWorld
	{
	public:
		static const float ACTUAL_RIVER_PROPORTION;
		static const float RIVER_FLATTENING_ADDEND;
		static const float RIVER_BOTTOM;
		static const float LAKE_BOTTOM;

		explicit RtgWorld(int64_t seed);

		int64_t seed() const { return m_seed; }
		const OpenSimplexNoise &simplex(int index) const;
		SpacedCellularNoise &cellular(int index) const;

		// The generator's Random (seeded per chunk); surfaces draw from it.
		Random *rand = nullptr;

		// RTGChunkGenSettings defaults
		int seaLevel = 63;
		int sandDuneHeight = 4;
		int bedrockLayers = 5;
		float riverSizeMult = 1.0f;
		float riverFrequency = 1.0f;
		float riverBendMult = 1.0f;
		float riverCutOffAmpl = 0.5f;
		float riverCutOffScale = 350.0f;
		float lakeSizeMult = 1.0f;
		float lakeFreqMult = 1.0f;
		float lakeShoreBend = 1.0f;
		int surfaceBlendRadius = 32;	// RTGConfig default
		bool lushRiverbanksInDesert = true;

		double riverLargeBendSize() const { return 140.0 * riverBendMult; }
		double riverSmallBendSize() const { return 30.0 * riverBendMult; }
		double riverSeparation() const { return 975.0 / riverFrequency; }
		double riverValleyLevel() const { return 140.0 / 450.0 * riverSizeMult * riverFrequency; }
		float lakeFrequency() const { return 649.0f * lakeFreqMult; }
		float lakeShoreLevel() const { return 0.035f * lakeFreqMult * lakeSizeMult; }
		float lakeDepressionLevel() const { return 0.15f * lakeFreqMult * lakeSizeMult; }
		float lakeBendSizeLarge() const { return 80.0f * lakeShoreBend; }
		float lakeBendSizeMedium() const { return 30.0f * lakeShoreBend; }
		float lakeBendSizeSmall() const { return 12.0f * lakeShoreBend; }

		static float riverAdjustedForDepthDifference(float river);

	private:
		int64_t m_seed;
		std::unique_ptr<OpenSimplexNoise> m_simplex[10];
		std::unique_ptr<SpacedCellularNoise> m_cellular[5];
	};

	// ---------------------------------------------------------------------------------
	// TerrainBase static helpers
	// ---------------------------------------------------------------------------------
	namespace Terrain
	{
		float blendedHillHeight(float simplex);
		float blendedHillHeight(float simplex, float turnAt);
		float above(float limited, float limit);
		float unsignedPower(float number, float power);
		float hills(float x, float y, float hillStrength, const RtgWorld &w);
		float groundNoise(float x, float y, float amplitude, const RtgWorld &w);
		float getTerrainBase();
		float getTerrainBase(float river);
		float mountainCap(float m);
		float riverized(float height, float river);
		float bayesianAdjustment(float probability, float multiplier);

		float terrainBeach(int x, int y, const RtgWorld &w, float river, float baseHeight);
		float terrainFlatLakes(int x, int y, const RtgWorld &w, float river, float baseHeight);
		float terrainForest(int x, int y, const RtgWorld &w, float river, float baseHeight);
		float terrainGrasslandFlats(int x, int y, const RtgWorld &w, float river, float mPitch, float baseHeight);
		float terrainHighland(float x, float y, const RtgWorld &w, float river, float start, float width, float height, float baseAdjust);
		float terrainLonelyMountain(int x, int y, const RtgWorld &w, float river, float strength, float width, float terrainHeight);
		float terrainMarsh(int x, int y, const RtgWorld &w, float baseHeight, float river);
		float terrainOcean(int x, int y, const RtgWorld &w, float river, float averageFloor);
		float terrainPlains(int x, int y, const RtgWorld &w, float river, float stPitch, float stFactor, float hPitch, float hDivisor, float baseHeight);
		float terrainPolar(float x, float y, const RtgWorld &w, float river, float stPitch, float stFactor, float hPitch, float hDivisor, float baseHeight);

		// [-1, 0]: how much a river cuts in here (0 = no river)
		float getRiverStrength(int worldX, int worldZ, const RtgWorld &w);
		float calcCliff(int x, int z, const float *noise, float river);
	}

	// ---------------------------------------------------------------------------------
	// Chunk primer: full 256-high column buffer with block data
	// ---------------------------------------------------------------------------------
	struct BlockState
	{
		uint8_t id = 0;
		uint8_t data = 0;
		BlockState() {}
		BlockState(int i, int d = 0) : id(static_cast<uint8_t>(i)), data(static_cast<uint8_t>(d)) {}
	};

	class ChunkPrimer
	{
	public:
		ChunkPrimer();
		int getBlock(int x, int y, int z) const { return m_blocks[index(x, y, z)]; }
		void setBlock(int x, int y, int z, const BlockState &state)
		{
			const int i = index(x, y, z);
			m_blocks[i] = state.id;
			m_data[i] = state.data;
		}
		const uint8_t *blocks() const { return m_blocks; }
		const uint8_t *data() const { return m_data; }
		static int index(int x, int y, int z) { return (x << 12) | (z << 8) | y; }

	private:
		uint8_t m_blocks[16 * 16 * 256];
		uint8_t m_data[16 * 16 * 256];
	};

	// ---------------------------------------------------------------------------------
	// Realistic biomes
	// ---------------------------------------------------------------------------------
	class RealisticBiome
	{
	public:
		virtual ~RealisticBiome() {}

		// Id used inside WorldGen V2 (a real biome id, or a virtual one for RTG-only biomes)
		int rtgId = 0;
		// The game biome stored in the chunk (decorations, mobs, colours)
		int baseBiomeId = 0;

		bool allowRivers = true;
		bool allowScenicLakes = true;
		bool surfaceBleedIn = false;
		bool surfaceBleedOut = false;
		bool frozenRiver = false;		// RiverType
		int beachRtgId = 0;				// preferred beach (RTG id)

		float rNoise(const RtgWorld &w, int x, int y, float border, float river) const;
		virtual void rReplace(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, const float *noise, float river) const;

		RealisticBiome *riverBiome() const;

	protected:
		virtual float terrainNoise(const RtgWorld &w, int x, int y, float border, float river) const = 0;
		virtual void paintSurface(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, const float *noise, float river) const = 0;
		void paintRiverOasis(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, float river) const;

	private:
		float lakePressure(const RtgWorld &w, int x, int y) const;
		static float lakeToRiverProportions(float pressure, float shoreLevel, float topLevel);
		static float erodedNoise(const RtgWorld &w, int x, int y, float river, float biomeHeight);
	};

	namespace Biomes
	{
		const int VIRTUAL_STONE_BEACH = 200;
		const int VIRTUAL_COLD_BEACH = 201;

		void init();						// thread-safe, idempotent
		RealisticBiome *get(int rtgId);		// never null (falls back to plains)
		bool isOcean(int id);
		bool isRiver(int id);
		bool isSwamp(int id);
		bool isBeach(int id);
		bool isLand(int id);
	}

	// ---------------------------------------------------------------------------------
	// Chunk generator
	// ---------------------------------------------------------------------------------
	struct DataPatch
	{
		uint8_t x, z;
		uint8_t y;
		uint8_t data;
	};

	// ---------------------------------------------------------------------------------
	// Revision 2 terrain enhancements (RtgEnhance.cpp)
	// ---------------------------------------------------------------------------------
	class RtgEnhancer
	{
	public:
		explicit RtgEnhancer(int64_t seed);

		// alpine ridges/gullies on high ground, rolling hills and broad valleys in lowlands
		void shapeHeights(int worldX, int worldZ, float *height, const float *riverLand) const;
		static void computeSlopes(const float *height, float *slope);
		// stone/water fill with a 3D cliff/overhang band on steep mountain faces
		void fillTerrain(int worldX, int worldZ, const float *height, const float *slope, int seaLevel, ChunkPrimer &primer) const;
		// exposed rock on steep slopes, snow on high peaks
		void paintDetail(int worldX, int worldZ, const float *slope, ChunkPrimer &primer) const;
		// spaghetti tunnels + deep caverns
		void carveCaves(int worldX, int worldZ, const float *height, int seaLevel, ChunkPrimer &primer) const;

	private:
		static const int NOISES = 10;
		float ridged(float x, float z) const;
		bool solidAt(int x, int y, int z, float height, float cliff, const void *lattice) const;
		std::unique_ptr<OpenSimplexNoise> m_noise[NOISES];
	};

	// Current terrain revision for new WorldGen V2 worlds. Revision 1 = original RTG port.
	const int WORLDGEN_V2_CURRENT_REVISION = 2;

	class RtgGenerator
	{
	public:
		explicit RtgGenerator(int64_t seed, int revision = 1);

		// Fills `blocks` (65536 bytes: two 128-high sections, layout x<<11 | z<<7 | (y & 127),
		// lower section first) with terrain and surfaces for chunk (cx, cz).
		// `biomeIds` receives the repaired biome per column in chunk order (z << 4 | x).
		// `patches` receives block data values to apply once the chunk exists.
		void generate(int cx, int cz, BiomeSource *biomeSource, Random *random,
			uint8_t *blocks, uint8_t *biomeIds, std::vector<DataPatch> &patches);

	private:
		static const int SAMPLE_SIZE = 8;
		static const int SAMPLE_ARRAY_SIZE = SAMPLE_SIZE * 2 + 5;

		void repairBiomes(const int *genLayerBiomes, const int *neighborhood, const float *noise,
			const float *riverStrength, RealisticBiome **biomes);

		RtgWorld m_world;
		int m_revision = 1;
		std::unique_ptr<RtgEnhancer> m_enhancer;
		float m_weightings[SAMPLE_ARRAY_SIZE * SAMPLE_ARRAY_SIZE][256];
		std::vector<int> m_searchPattern;
	};
}
