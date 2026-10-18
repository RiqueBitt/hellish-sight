#include "stdafx.h"
#include "RtgCore.h"

#include "../Random.h"
#include "../Biome.h"
#include "../BiomeSource.h"
#include "../Tile.h"

#include <cmath>
#include <cstring>
#include <algorithm>

namespace WorldGenV2
{
	namespace
	{
		const int NO_BIOME = -1;
		const int MAX_BIOMES = 256;

		// rtg.api.util.CircularSearchCreator
		std::vector<int> circularPattern(float maxRadius, int size)
		{
			const float center = (size - 1.0f) / 2.0f;
			std::vector<int> result;
			result.reserve(size * size);
			std::vector<bool> found(size * size, false);
			const int smallerHalfSize = size / 2;
			const int largerHalfSize = (size + 1) / 2;

			auto distanceFromCenter = [&](int x, int y) {
				return static_cast<float>(std::sqrt(std::pow(static_cast<double>(x - center), 2) + std::pow(static_cast<double>(y - center), 2)));
			};
			auto consider = [&](int x, int y, float radius) {
				const int index = x * size + y;
				if (found[index]) return;
				if (distanceFromCenter(x, y) > radius) return;
				result.push_back(index);
				found[index] = true;
			};

			for (float radius = 0; radius < maxRadius; radius = radius + 0.01f)
			{
				for (int y = 0; y < largerHalfSize; y++)
					for (int x = smallerHalfSize; x < size; x++) consider(x, y, radius);
				for (int x = size - 1; x >= smallerHalfSize; x--)
					for (int y = largerHalfSize; y < size; y++) consider(x, y, radius);
				for (int y = size - 1; y >= largerHalfSize - 1; y--)
					for (int x = smallerHalfSize - 1; x > -1; x--) consider(x, y, radius);
				for (int x = 0; x < smallerHalfSize; x++)
					for (int y = largerHalfSize - 1; y > -1; y--) consider(x, y, radius);
			}
			return result;
		}

		// rtg.world.biome.BiomeAnalyzer.SmoothingSearchStatus
		class SmoothingSearch
		{
		public:
			int biomes[MAX_BIOMES];

			SmoothingSearch(bool (*desired)(int), const std::vector<int> &pattern, int arraySize)
				: m_desired(desired), m_pattern(pattern), m_arraySize(arraySize) {}

			void reset() { m_hunted = false; }

			void hunt(const int *neighborhood)
			{
				if (m_hunted) return;	// results only depend on the neighbourhood: hunt once per chunk
				for (int i = 0; i < 9; i++) m_findings[i] = -1;
				for (int xOffset = -1; xOffset <= 1; xOffset++)
					for (int zOffset = -1; zOffset <= 1; zOffset++)
						search(xOffset, zOffset, neighborhood);
				smoothQuadrant(biomeIndex(0, 0), 0);
				smoothQuadrant(biomeIndex(8, 0), 3);
				smoothQuadrant(biomeIndex(0, 8), 1);
				smoothQuadrant(biomeIndex(8, 8), 4);
				m_hunted = true;
			}

		private:
			void search(int xOffset, int zOffset, const int *neighborhood)
			{
				const int offset = xOffset * m_arraySize + zOffset;
				const int location = (xOffset + 1) * 3 + zOffset + 1;
				m_findings[location] = NO_BIOME;
				m_weightings[location] = 2.0f;
				for (size_t i = 0; i < m_pattern.size(); i++)
				{
					const int biome = neighborhood[m_pattern[i] + offset];
					if (m_desired(biome))
					{
						m_findings[location] = biome;
						m_weightings[location] = static_cast<float>(std::sqrt(static_cast<double>(m_pattern.size()))) - static_cast<float>(std::sqrt(static_cast<double>(i))) + 2.0f;
						break;
					}
				}
			}

			void smoothQuadrant(int biomesOffset, int findingsOffset)
			{
				const int upperLeft = m_findings[0 + findingsOffset];
				const int upperRight = m_findings[3 + findingsOffset];
				const int lowerLeft = m_findings[1 + findingsOffset];
				const int lowerRight = m_findings[4 + findingsOffset];
				if (upperLeft == upperRight && upperLeft == lowerLeft && upperLeft == lowerRight)
				{
					for (int x = 0; x < 8; x++)
						for (int z = 0; z < 8; z++)
							biomes[biomeIndex(x, z) + biomesOffset] = upperLeft;
					return;
				}
				m_biomeCount = 0;
				addBiome(upperLeft);
				addBiome(upperRight);
				addBiome(lowerLeft);
				addBiome(lowerRight);
				for (int x = 0; x < 8; x++)
				{
					for (int z = 0; z < 8; z++)
					{
						addBiome(lowerRight);
						for (int i = 0; i < 4; i++) m_quadrantWeighting[i] = 0;
						addWeight(upperLeft, m_weightings[0 + findingsOffset] * (7 - x) * (7 - z));
						addWeight(upperRight, m_weightings[3 + findingsOffset] * x * (7 - z));
						addWeight(lowerLeft, m_weightings[1 + findingsOffset] * (7 - x) * z);
						addWeight(lowerRight, m_weightings[4 + findingsOffset] * x * z);
						biomes[biomeIndex(x, z) + biomesOffset] = preferredBiome();
					}
				}
			}

			void addBiome(int biome)
			{
				for (int i = 0; i < m_biomeCount; i++) if (biome == m_quadrantBiome[i]) return;
				m_quadrantBiome[m_biomeCount++] = biome;
			}

			void addWeight(int biome, float weight)
			{
				for (int i = 0; i < m_biomeCount; i++)
				{
					if (biome == m_quadrantBiome[i])
					{
						m_quadrantWeighting[i] += weight;
						return;
					}
				}
			}

			int preferredBiome() const
			{
				float bestWeight = 0;
				int result = -2;
				for (int i = 0; i < m_biomeCount; i++)
				{
					if (m_quadrantWeighting[i] > bestWeight)
					{
						bestWeight = m_quadrantWeighting[i];
						result = m_quadrantBiome[i];
					}
				}
				return result;
			}

			static int biomeIndex(int x, int z) { return x * 16 + z; }

			bool (*m_desired)(int);
			const std::vector<int> &m_pattern;
			int m_arraySize;
			bool m_hunted = false;
			int m_findings[9];
			float m_weightings[9];
			int m_quadrantBiome[4];
			float m_quadrantWeighting[4];
			int m_biomeCount = 0;
		};

		float riverAdjusted(float top, float river)
		{
			if (river >= 1) return top;
			float erodedRiver = river / RtgWorld::ACTUAL_RIVER_PROPORTION;
			if (erodedRiver <= 1.0f) top = top * (1 - erodedRiver) + 62.0f * erodedRiver;
			top = top * (1 - river) + 62.0f * river;
			return top;
		}

		inline int biomeIdAt(BiomeSource *source, int x, int z)
		{
			Biome *b = source->getBiome(x, z);
			return b != nullptr ? b->id : Biome::plains->id;
		}
	}

	RtgGenerator::RtgGenerator(int64_t seed, int revision) : m_world(seed), m_revision(revision)
	{
		Biomes::init();
		if (m_revision >= 2) m_enhancer.reset(new RtgEnhancer(seed));

		// setWeightings()
		for (int x = 0; x < 16; x++)
		{
			for (int z = 0; z < 16; z++)
			{
				const float limit = static_cast<float>(std::pow(56.0f * 56.0f, 0.7));
				for (int mapX = 0; mapX < SAMPLE_ARRAY_SIZE; mapX++)
				{
					for (int mapZ = 0; mapZ < SAMPLE_ARRAY_SIZE; mapZ++)
					{
						float xDist = static_cast<float>(x - (mapX - SAMPLE_SIZE) * 8);
						float zDist = static_cast<float>(z - (mapZ - SAMPLE_SIZE) * 8);
						float distanceSquared = xDist * xDist + zDist * zDist;
						float distance = static_cast<float>(std::pow(static_cast<double>(distanceSquared), 0.7));
						float weight = 1.0f - distance / limit;
						if (weight < 0) weight = 0;
						m_weightings[mapX * SAMPLE_ARRAY_SIZE + mapZ][x * 16 + z] = weight;
					}
				}
			}
		}

		m_searchPattern = circularPattern(SAMPLE_ARRAY_SIZE / 2.0f - 1, SAMPLE_ARRAY_SIZE);
	}

	// BiomeAnalyzer.newRepair
	void RtgGenerator::repairBiomes(const int *genLayerBiomes, const int *neighborhood, const float *noise,
		const float *riverStrength, RealisticBiome **jittered)
	{
		SmoothingSearch beachSearch(&Biomes::isBeach, m_searchPattern, SAMPLE_ARRAY_SIZE);
		SmoothingSearch landSearch(&Biomes::isLand, m_searchPattern, SAMPLE_ARRAY_SIZE);
		SmoothingSearch oceanSearch(&Biomes::isOcean, m_searchPattern, SAMPLE_ARRAY_SIZE);

		for (int i = 0; i < MAX_BIOMES; i++)
		{
			RealisticBiome *realistic = Biomes::get(genLayerBiomes[i]);
			const int realisticId = realistic->baseBiomeId;
			const bool canBeRiver = riverStrength[i] > 0.7;
			if (noise[i] > 61.5)
			{
				jittered[i] = realistic;
			}
			else if (canBeRiver && !Biomes::isOcean(realisticId) && !Biomes::isSwamp(realisticId))
			{
				jittered[i] = realistic->riverBiome();
			}
			else
			{
				jittered[i] = realistic;
			}
		}

		// beaches on shores
		const float beachTop = 64.5f;
		for (int i = 0; i < MAX_BIOMES; i++)
		{
			const float beachBottom = 61.5f;
			if (noise[i] < beachBottom || noise[i] > riverAdjusted(beachTop, riverStrength[i])) continue;
			const int biomeId = jittered[i]->baseBiomeId;
			if (Biomes::isSwamp(biomeId)) continue;
			beachSearch.hunt(neighborhood);
			landSearch.hunt(neighborhood);
			int foundBiome = beachSearch.biomes[i];
			if (foundBiome != NO_BIOME)
			{
				const int nearestLandBiome = landSearch.biomes[i];
				if (nearestLandBiome > -1) foundBiome = Biomes::get(nearestLandBiome)->beachRtgId;
				jittered[i] = Biomes::get(foundBiome);
			}
		}

		// land higher up
		for (int i = 0; i < MAX_BIOMES; i++)
		{
			if (noise[i] < riverAdjusted(beachTop, riverStrength[i])) continue;
			const int biomeId = jittered[i]->baseBiomeId;
			if (Biomes::isLand(jittered[i]->rtgId) && Biomes::isLand(biomeId)) continue;
			if (Biomes::isSwamp(biomeId)) continue;
			landSearch.hunt(neighborhood);
			int foundBiome = landSearch.biomes[i];
			if (foundBiome == NO_BIOME)
			{
				beachSearch.hunt(neighborhood);
				foundBiome = beachSearch.biomes[i];
			}
			if (foundBiome != NO_BIOME) jittered[i] = Biomes::get(foundBiome);
		}

		// ocean below sea level
		for (int i = 0; i < MAX_BIOMES; i++)
		{
			const float oceanTop = 61.5f;
			if (noise[i] > oceanTop) continue;
			const int biomeId = jittered[i]->baseBiomeId;
			if (Biomes::isOcean(biomeId)) continue;
			if (Biomes::isSwamp(biomeId)) continue;
			if (Biomes::isRiver(biomeId)) continue;
			oceanSearch.hunt(neighborhood);
			const int foundBiome = oceanSearch.biomes[i];
			if (foundBiome != NO_BIOME) jittered[i] = Biomes::get(foundBiome);
		}

		// remainder below sea level becomes a scenic lake
		for (int i = 0; i < MAX_BIOMES; i++)
		{
			const int biomeId = jittered[i]->baseBiomeId;
			if (noise[i] <= 61.5 && !Biomes::isRiver(biomeId))
			{
				if (!Biomes::isOcean(biomeId) && !Biomes::isSwamp(biomeId) && !Biomes::isBeach(jittered[i]->rtgId))
				{
					jittered[i] = jittered[i]->frozenRiver ? Biomes::get(Biome::frozenRiver->id) : Biomes::get(Biome::river->id);
				}
			}
		}
	}

	void RtgGenerator::generate(int cx, int cz, BiomeSource *biomeSource, Random *random,
		uint8_t *blocks, uint8_t *biomeIds, std::vector<DataPatch> &patches)
	{
		RtgWorld &w = m_world;
		w.rand = random;

		const int worldX = cx * 16;
		const int worldZ = cz * 16;

		// ---- landscape: biome samples around the chunk ---------------------------
		int biomeData[SAMPLE_ARRAY_SIZE * SAMPLE_ARRAY_SIZE];
		for (int x = -SAMPLE_SIZE; x < SAMPLE_SIZE + 5; x++)
		{
			for (int z = -SAMPLE_SIZE; z < SAMPLE_SIZE + 5; z++)
			{
				biomeData[(x + SAMPLE_SIZE) * SAMPLE_ARRAY_SIZE + (z + SAMPLE_SIZE)] = biomeIdAt(biomeSource, worldX + x * 8, worldZ + z * 8);
			}
		}

		// ---- landscape: blended height per column ---------------------------------
		float noise[256];
		float riverLand[256];
		float weightedBiomes[256];
		std::memset(weightedBiomes, 0, sizeof(weightedBiomes));

		for (int x = 0; x < 16; x++)
		{
			for (int z = 0; z < 16; z++)
			{
				float totalWeight = 0;
				for (int mapX = 0; mapX < SAMPLE_ARRAY_SIZE; mapX++)
				{
					for (int mapZ = 0; mapZ < SAMPLE_ARRAY_SIZE; mapZ++)
					{
						const float weight = m_weightings[mapX * SAMPLE_ARRAY_SIZE + mapZ][x * 16 + z];
						if (weight > 0)
						{
							totalWeight += weight;
							weightedBiomes[biomeData[mapX * SAMPLE_ARRAY_SIZE + mapZ] & 0xFF] += weight;
						}
					}
				}
				for (int i = 0; i < 256; i++) weightedBiomes[i] /= totalWeight;

				noise[x * 16 + z] = 0.0f;
				const float river = Terrain::getRiverStrength(worldX + x, worldZ + z, w);
				riverLand[x * 16 + z] = -river;
				for (int i = 0; i < 256; i++)
				{
					if (weightedBiomes[i] > 0.0f)
					{
						noise[x * 16 + z] += Biomes::get(i)->rNoise(w, worldX + x, worldZ + z, weightedBiomes[i], river + 1.0f) * weightedBiomes[i];
						weightedBiomes[i] = 0.0f;
					}
				}
			}
		}

		// ---- revision 2: mountains, valleys, rolling lowlands ----------------------
		if (m_enhancer) m_enhancer->shapeHeights(worldX, worldZ, noise, riverLand);

		// ---- biome repair (beaches, rivers, lakes) --------------------------------
		int genLayer[256];
		for (int x = 0; x < 16; x++)
			for (int z = 0; z < 16; z++)
				genLayer[x * 16 + z] = biomeIdAt(biomeSource, worldX + x, worldZ + z);

		RealisticBiome *landscapeBiome[256];
		repairBiomes(genLayer, biomeData, noise, riverLand, landscapeBiome);

		// ---- terrain fill ------------------------------------------------------------
		std::unique_ptr<ChunkPrimer> primerHolder(new ChunkPrimer());
		ChunkPrimer &primer = *primerHolder;
		float slope[256];
		if (m_enhancer)
		{
			RtgEnhancer::computeSlopes(noise, slope);
			m_enhancer->fillTerrain(worldX, worldZ, noise, slope, w.seaLevel, primer);
		}
		else for (int x = 0; x < 16; x++)
		{
			for (int z = 0; z < 16; z++)
			{
				const int height = static_cast<int>(noise[x * 16 + z]);
				for (int y = 0; y < 256; y++)
				{
					if (y > height)
					{
						if (y < w.seaLevel) primer.setBlock(x, y, z, BlockState(Tile::calmWater_Id));
					}
					else
					{
						primer.setBlock(x, y, z, BlockState(Tile::stone_Id));
					}
				}
			}
		}

		// ---- surface jitter between biomes ---------------------------------------------
		RealisticBiome *jitteredBiomes[256];
		{
			SimplexData2D jitter(SimplexData2D::Disk);
			for (int i = 0; i < 16; i++)
			{
				for (int j = 0; j < 16; j++)
				{
					const int x = worldX + i;
					const int z = worldZ + j;
					w.simplex(0).multiEval2D(x, z, jitter);
					const int pX = static_cast<int>(std::floor(x + jitter.deltaX * w.surfaceBlendRadius + 0.5));
					const int pZ = static_cast<int>(std::floor(z + jitter.deltaY * w.surfaceBlendRadius + 0.5));
					RealisticBiome *actual = landscapeBiome[(x & 15) * 16 + (z & 15)];
					RealisticBiome *jitterBiome = landscapeBiome[(pX & 15) * 16 + (pZ & 15)];
					jitteredBiomes[i * 16 + j] = (actual->surfaceBleedIn && jitterBiome->surfaceBleedOut) ? jitterBiome : actual;
				}
			}
		}

		// ---- surfaces + bedrock --------------------------------------------------------
		for (int x = 0; x < 16; x++)
		{
			for (int z = 0; z < 16; z++)
			{
				const float river = -Terrain::getRiverStrength(worldX + x, worldZ + z, w);
				jitteredBiomes[x * 16 + z]->rReplace(primer, worldX + x, worldZ + z, x, z, -1, w, noise, river);

				if (w.bedrockLayers > 1)
				{
					for (int bl = 9; bl >= 0; --bl)
					{
						if (bl <= random->nextInt(w.bedrockLayers)) primer.setBlock(x, bl, z, BlockState(Tile::unbreakable_Id));
					}
				}
				else
				{
					primer.setBlock(x, 0, z, BlockState(Tile::unbreakable_Id));
				}
			}
		}

		// ---- revision 2: slope/peak detail and noise caves ---------------------------------
		if (m_enhancer)
		{
			m_enhancer->paintDetail(worldX, worldZ, slope, primer);
			m_enhancer->carveCaves(worldX, worldZ, noise, w.seaLevel, primer);
		}

		// ---- output ------------------------------------------------------------------------
		const uint8_t *pb = primer.blocks();
		const uint8_t *pd = primer.data();
		for (int x = 0; x < 16; x++)
		{
			for (int z = 0; z < 16; z++)
			{
				for (int y = 0; y < 256; y++)
				{
					const int src = ChunkPrimer::index(x, y, z);
					const int section = y >> 7;
					const int dst = section * (128 * 16 * 16) + ((x << 11) | (z << 7) | (y & 127));
					blocks[dst] = pb[src];
					if (pd[src] != 0)
					{
						DataPatch patch;
						patch.x = static_cast<uint8_t>(x);
						patch.z = static_cast<uint8_t>(z);
						patch.y = static_cast<uint8_t>(y);
						patch.data = pd[src];
						patches.push_back(patch);
					}
				}
				// chunk biome array is (z << 4) | x
				biomeIds[(z << 4) | x] = static_cast<uint8_t>(landscapeBiome[x * 16 + z]->baseBiomeId);
			}
		}
	}
}
