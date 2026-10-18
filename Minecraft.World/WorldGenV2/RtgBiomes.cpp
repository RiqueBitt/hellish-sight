#include "stdafx.h"
#include "RtgCore.h"

#include "../Random.h"
#include "../Biome.h"
#include "../Tile.h"

#include <cmath>
#include <mutex>
#include <algorithm>
#include <functional>

namespace WorldGenV2
{
	using namespace Terrain;

	// =================================================================================
	// Height effects (rtg.api.world.terrain.heighteffect)
	// =================================================================================
	namespace
	{
		class HeightEffect
		{
		public:
			virtual ~HeightEffect() {}
			virtual float added(const RtgWorld &w, float x, float y) const = 0;
		};
		typedef std::shared_ptr<HeightEffect> HeightEffectPtr;

		class SummedHeightEffect : public HeightEffect
		{
		public:
			SummedHeightEffect(HeightEffectPtr a, HeightEffectPtr b) : one(a), two(b) {}
			float added(const RtgWorld &w, float x, float y) const override { return one->added(w, x, y) + two->added(w, x, y); }
		private:
			HeightEffectPtr one, two;
		};

		HeightEffectPtr plus(HeightEffectPtr a, HeightEffectPtr b) { return std::make_shared<SummedHeightEffect>(a, b); }

		class GroundEffect : public HeightEffect
		{
		public:
			explicit GroundEffect(float amplitude) : amplitude(amplitude) {}
			float added(const RtgWorld &w, float x, float y) const override { return groundNoise(x, y, amplitude, w); }
		private:
			float amplitude;
		};

		class RaiseEffect : public HeightEffect
		{
		public:
			explicit RaiseEffect(float height) : height(height) {}
			float added(const RtgWorld &, float, float) const override { return height; }
		private:
			float height;
		};

		class SpikeEverywhereEffect : public HeightEffect
		{
		public:
			float wavelength = 0;
			float minimumSimplex = 2147483647.0f;
			int octave = 0;
			float power = 1.6f;
			HeightEffectPtr spiked;

			float added(const RtgWorld &w, float x, float y) const override
			{
				float noise = w.simplex(octave).noise2f(x / wavelength, y / wavelength);
				noise = std::fabs(noise);
				noise = blendedHillHeight(noise, minimumSimplex);
				noise = unsignedPower(noise, power);
				return noise * spiked->added(w, x, y);
			}
		};

		class JitterEffect : public HeightEffect
		{
		public:
			float amplitude = 2147483647.0f;
			float wavelength = 0;
			HeightEffectPtr jittered;

			float added(const RtgWorld &w, float x, float y) const override
			{
				SimplexData2D jitter(SimplexData2D::Disk);
				w.simplex(1).multiEval2D(x / wavelength, y / wavelength, jitter);
				// Java Math.round(double) -> long, then (int)
				int pX = static_cast<int>(std::floor(x + jitter.deltaX * amplitude + 0.5));
				int pY = static_cast<int>(std::floor(y + jitter.deltaY * amplitude + 0.5));
				return jittered->added(w, static_cast<float>(pX), static_cast<float>(pY));
			}
		};

		class VoronoiBorderEffect : public HeightEffect
		{
		public:
			float pointWavelength = 0;
			float floor = 3.4e38f;
			float minimumDivisor = 0;

			float added(const RtgWorld &w, float x, float y) const override
			{
				VoronoiResult points = w.cellular(1).eval2D(x / pointWavelength, y / pointWavelength);
				float raise = static_cast<float>(points.interiorValue());
				return 1.0f - raise;
			}
		};

		// RealisticBiomeVanillaExtremeHills.RidgedExtremeHills
		class RidgedExtremeHills
		{
		public:
			RidgedExtremeHills(float landHeight, float baseHeight, float hillWidth)
				: height(landHeight), base(baseHeight), width(hillWidth)
			{
				const float ridgeWidth = 300.0f;
				const float valleyFloor = -0.2f;

				auto baseHills = std::make_shared<SpikeEverywhereEffect>();
				baseHills->spiked = std::make_shared<RaiseEffect>(height * 2.0f / 3.0f);
				baseHills->wavelength = width;
				baseHills->minimumSimplex = -0.2f;
				baseHills->octave = 3;
				baseHills->power = 1.9f;

				auto additionalHeightSpikes = std::make_shared<SpikeEverywhereEffect>();
				additionalHeightSpikes->spiked = std::make_shared<RaiseEffect>(height / 3.0f);
				additionalHeightSpikes->wavelength = width / 3.0f;
				additionalHeightSpikes->minimumSimplex = -0.2f;
				additionalHeightSpikes->octave = 4;
				additionalHeightSpikes->power = 1.9f;

				auto additionalHeight = std::make_shared<JitterEffect>();
				additionalHeight->amplitude = additionalHeightSpikes->wavelength / 3;
				additionalHeight->wavelength = additionalHeightSpikes->wavelength / 2;
				additionalHeight->jittered = additionalHeightSpikes;

				auto rougheningSpikes = std::make_shared<SpikeEverywhereEffect>();
				rougheningSpikes->spiked = std::make_shared<RaiseEffect>(height / 8.0f);
				rougheningSpikes->wavelength = width / 10.0f;
				rougheningSpikes->minimumSimplex = -0.2f;
				rougheningSpikes->octave = 5;
				rougheningSpikes->power = 1.9f;

				auto roughening = std::make_shared<JitterEffect>();
				roughening->amplitude = rougheningSpikes->wavelength / 3;
				roughening->wavelength = rougheningSpikes->wavelength / 2;
				roughening->jittered = rougheningSpikes;

				auto hillJitter = std::make_shared<JitterEffect>();
				hillJitter->amplitude = 15.0f;
				hillJitter->wavelength = 50.0f;
				hillJitter->jittered = plus(plus(baseHills, additionalHeight), roughening);
				heightIncrease = hillJitter;

				auto ridging = std::make_shared<VoronoiBorderEffect>();
				ridging->pointWavelength = ridgeWidth;
				ridging->floor = valleyFloor;
				ridging->minimumDivisor = .2f;

				auto ridgeJitter = std::make_shared<JitterEffect>();
				ridgeJitter->amplitude = 15.0f;
				ridgeJitter->wavelength = 50.0f;
				ridgeJitter->jittered = ridging;

				auto ridgeJitterrette = std::make_shared<JitterEffect>();
				ridgeJitterrette->amplitude = 5.0f;
				ridgeJitterrette->wavelength = 20.0f;
				ridgeJitterrette->jittered = ridgeJitter;

				multiplier = ridgeJitterrette;
				groundEffect = std::make_shared<GroundEffect>(6.0f);
			}

			float generateNoise(const RtgWorld &w, int x, int y) const
			{
				const float fx = static_cast<float>(x), fy = static_cast<float>(y);
				float groundEffectLevel = groundEffect->added(w, fx, fy);
				float ridging = multiplier->added(w, fx, fy);
				ridging = bayesianAdjustment(ridging, 2);
				float result = base + ridging * (groundEffectLevel + heightIncrease->added(w, fx, fy)) + groundEffectLevel;
				return mountainCap(result);
			}

		private:
			float height, base, width;
			HeightEffectPtr heightIncrease, multiplier, groundEffect;
		};

		// -----------------------------------------------------------------------------
		// Blocks
		// -----------------------------------------------------------------------------
		const BlockState AIR(0);
		const BlockState STONE(Tile::stone_Id);
		const BlockState GRASS(Tile::grass_Id);
		const BlockState DIRT(Tile::dirt_Id);
		const BlockState COBBLE(Tile::cobblestone_Id);
		const BlockState SAND(Tile::sand_Id);
		const BlockState GRAVEL(Tile::gravel_Id);
		const BlockState SANDSTONE(Tile::sandStone_Id);
		const BlockState SNOW_BLOCK(Tile::snow_Id);
		const BlockState ICE(Tile::ice_Id);
		const BlockState MYCELIUM(Tile::mycel_Id);
		const BlockState PODZOL = DIRT;		// no podzol in this version of the game
		const BlockState PACKED_ICE = ICE;	// no packed ice either
		const BlockState SHADOW_STONE(Tile::clayHardened_colored_Id, 9);	// cyan stained hardened clay (RTG default)
		const int WATER_ID = Tile::calmWater_Id;

		inline bool isAir(int id) { return id == 0; }
		inline bool isStone(int id) { return id == Tile::stone_Id; }
		inline bool isWater(int id) { return id == Tile::water_Id || id == Tile::calmWater_Id; }

		BlockState biomeTop(const Biome *b) { return BlockState(static_cast<uint8_t>(b->topMaterial)); }
		BlockState biomeFiller(const Biome *b) { return BlockState(static_cast<uint8_t>(b->material)); }

		inline int nextInt(const RtgWorld &w, int bound) { return w.rand->nextInt(bound); }

		void calcSnowHeight(int x, int y, int z, ChunkPrimer &primer, const float *noise)
		{
			if (y < 254)
			{
				int h = static_cast<signed char>(static_cast<int>((noise[x * 16 + z] - static_cast<int>(noise[x * 16 + z])) * 8));
				if (h > 7)
				{
					primer.setBlock(x, y + 2, z, BlockState(Tile::topSnow_Id, 0));
					primer.setBlock(x, y + 1, z, BlockState(Tile::topSnow_Id, 6));
				}
				else if (h > 0)
				{
					primer.setBlock(x, y + 1, z, BlockState(Tile::topSnow_Id, h - 1));
				}
			}
		}
	}

	// =================================================================================
	// RealisticBiomeBase
	// =================================================================================
	float RealisticBiome::lakePressure(const RtgWorld &w, int x, int y) const
	{
		if (!allowScenicLakes) return 1.0f;
		double pX = x;
		double pY = y;
		SimplexData2D jitter(SimplexData2D::Disk);
		w.simplex(1).multiEval2D(x / 240.0, y / 240.0, jitter);
		pX += jitter.deltaX * w.lakeBendSizeLarge();
		pY += jitter.deltaY * w.lakeBendSizeLarge();
		w.simplex(0).multiEval2D(x / 80.0, y / 80.0, jitter);
		pX += jitter.deltaX * w.lakeBendSizeMedium();
		pY += jitter.deltaY * w.lakeBendSizeMedium();
		w.simplex(4).multiEval2D(x / 30.0, y / 30.0, jitter);
		pX += jitter.deltaX * w.lakeBendSizeSmall();
		pY += jitter.deltaY * w.lakeBendSizeSmall();
		VoronoiResult lakeResults = w.cellular(0).eval2D(pX / w.lakeFrequency(), pY / w.lakeFrequency());
		return static_cast<float>(1.0 - lakeResults.interiorValue());
	}

	float RealisticBiome::lakeToRiverProportions(float pressure, float shoreLevel, float topLevel)
	{
		if (pressure > topLevel) return 1;
		if (pressure < shoreLevel) return (pressure / shoreLevel) * RtgWorld::ACTUAL_RIVER_PROPORTION;
		float proportion = (pressure - shoreLevel) / (topLevel - shoreLevel);
		return RtgWorld::ACTUAL_RIVER_PROPORTION + proportion * (1.0f - RtgWorld::ACTUAL_RIVER_PROPORTION);
	}

	float RealisticBiome::erodedNoise(const RtgWorld &w, int x, int y, float river, float biomeHeight)
	{
		float riverFlattening = 1.0f - river;
		riverFlattening = riverFlattening - (1 - RtgWorld::ACTUAL_RIVER_PROPORTION);
		if (riverFlattening < 0) return biomeHeight;
		riverFlattening /= RtgWorld::ACTUAL_RIVER_PROPORTION;
		float r = 1.0f - riverFlattening;
		if (r < 1.0f && biomeHeight > RtgWorld::LAKE_BOTTOM)
		{
			float irregularity = w.simplex(0).noise2f(x / 12.0f, y / 12.0f) * 2.0f + w.simplex(0).noise2f(x / 8.0f, y / 8.0f);
			irregularity = irregularity * (1 + r);
			return (biomeHeight * r) + ((RtgWorld::LAKE_BOTTOM + irregularity) * 1.0f) * (1.0f - r);
		}
		return biomeHeight;
	}

	float RealisticBiome::rNoise(const RtgWorld &w, int x, int y, float border, float river) const
	{
		if (!allowRivers)
		{
			float borderForRiver = border * 2;
			if (borderForRiver > 1.0f) borderForRiver = 1;
			river = 1.0f - (1.0f - borderForRiver) * (1.0f - river);
			return terrainNoise(w, x, y, border, river);
		}

		float lakeStrength = lakePressure(w, x, y);
		float adjustedLake = lakeToRiverProportions(lakeStrength, w.lakeShoreLevel(), w.lakeDepressionLevel());

		if (river < 0) river = 0;
		river = RtgWorld::riverAdjustedForDepthDifference(river);

		if (adjustedLake < RtgWorld::ACTUAL_RIVER_PROPORTION)
		{
			adjustedLake -= RtgWorld::ACTUAL_RIVER_PROPORTION;
			adjustedLake *= 2.0f;
			adjustedLake += RtgWorld::ACTUAL_RIVER_PROPORTION;
			if (adjustedLake < 0) adjustedLake = 0;
		}

		if ((river < 1) && (adjustedLake < 1))
		{
			float leastLowering = (std::min)(adjustedLake, river);
			river = (1.0f - river) / river + (1.0f - adjustedLake) / adjustedLake;
			river = (1.0f / (river + 1.0f));
			river = (river + leastLowering) / 2.0f;
		}
		else if (adjustedLake < river)
		{
			river = adjustedLake;
		}

		river = 1.0f - river;
		river = river * (river / (river + 0.05f) * (1.05f));
		river = 1.0f - river;

		float riverFlattening = river * (1.0f + RtgWorld::RIVER_FLATTENING_ADDEND) - RtgWorld::RIVER_FLATTENING_ADDEND;
		if (riverFlattening < 0) riverFlattening = 0;

		float terrain = terrainNoise(w, x, y, border, riverFlattening);
		return erodedNoise(w, x, y, river, terrain);
	}

	void RealisticBiome::rReplace(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, const float *noise, float river) const
	{
		const float riverRegion = !allowRivers ? 0.0f : river;
		paintSurface(primer, i, j, x, z, depth, w, noise, riverRegion);
	}

	void RealisticBiome::paintRiverOasis(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, float river) const
	{
		const float cutOffScale = w.riverCutOffScale;
		const float cutOffAmplitude = w.riverCutOffAmpl;

		int highestY;
		for (highestY = 255; highestY > 0; highestY--)
		{
			if (!isAir(primer.getBlock(x, highestY, z))) break;
		}

		float amplitude = 0.05f;
		float noiseValue = w.simplex(0).noise2f(i / 8.0f, j / 8.0f) * amplitude / 1.0f + w.simplex(1).noise2f(i / 3.0f, j / 3.0f) * amplitude / 2.0f;
		float noiseNeg = w.simplex(2).noise2f(i / cutOffScale, j / cutOffScale) * cutOffAmplitude;
		noiseValue -= noiseNeg;
		if (highestY > 62) noiseValue -= (highestY - 62) * (1 / 12.0f);

		if (river > 0.70 && river + noiseValue > 0.85)
		{
			for (int k = 255; k > -1; k--)
			{
				int b = primer.getBlock(x, k, z);
				if (isAir(b)) depth = -1;
				else if (!isWater(b))
				{
					depth++;
					if (depth == 0 && k > 61) primer.setBlock(x, k, z, GRASS);
					else if (depth < 4) primer.setBlock(x, k, z, DIRT);
					else if (depth > 4) return;
				}
			}
		}
	}

	// =================================================================================
	// Surface painters (one per RTG surface class)
	// =================================================================================
	namespace
	{
		// RTG cobble/stone cliff column used by most simple surfaces
		void paintCliffColumn(ChunkPrimer &primer, int x, int k, int z, int depth, const RtgWorld &w)
		{
			if (depth > -1 && depth < 2)
			{
				primer.setBlock(x, k, z, nextInt(w, 3) == 0 ? COBBLE : STONE);
			}
			else if (depth < 10)
			{
				primer.setBlock(x, k, z, STONE);
			}
		}

		// SurfaceVanillaPlains / Swampland (cliff > 1.4, swamp only above y 64)
		void paintPlainsLike(ChunkPrimer &primer, int x, int z, int depth, const RtgWorld &w, const float *noise, float river,
			const BlockState &top, const BlockState &filler, int minCliffY)
		{
			float c = calcCliff(x, z, noise, river);
			bool cliff = c > 1.4f;
			for (int k = 255; k > -1; k--)
			{
				int b = primer.getBlock(x, k, z);
				if (isAir(b)) depth = -1;
				else if (isStone(b))
				{
					depth++;
					if (cliff && k > minCliffY)
					{
						paintCliffColumn(primer, x, k, z, depth, w);
					}
					else
					{
						if (depth == 0 && k > 61) primer.setBlock(x, k, z, top);
						else if (depth < 4) primer.setBlock(x, k, z, filler);
					}
				}
			}
		}

		struct MixedCliffParams
		{
			float min = 0.0f;
			float sCliff = 1.5f;
			float sHeight = 60.0f;
			float sStrength = 65.0f;
			float cCliff = 1.5f;
			// mix variants
			enum Mix { None, Forest, Jungle, DesertHills, Mushroom } mix = None;
			BlockState mixBlock, mix2Block;
			float mixHeight = 0, mix2Height = 0;
		};

		// SurfaceVanillaForest / ForestHills / Jungle / JungleHills / DesertHills / MushroomIsland(+Shore)
		void paintMixedCliff(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, const float *noise, float river,
			const BlockState &top, const BlockState &filler, const MixedCliffParams &p)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			float c = calcCliff(x, z, noise, river);
			int cliff = 0;

			const bool desert = p.mix == MixedCliffParams::DesertHills;
			const BlockState cliff1Top = desert ? SANDSTONE : STONE;
			const BlockState cliff2 = desert ? SANDSTONE : SHADOW_STONE;

			for (int k = 255; k > -1; k--)
			{
				int b = primer.getBlock(x, k, z);
				if (isAir(b))
				{
					depth = -1;
				}
				else if (isStone(b))
				{
					depth++;
					if (depth == 0)
					{
						float pn = simplex.noise3f(i / 8.0f, j / 8.0f, k / 8.0f) * 0.5f;
						if (c > p.min && c > p.sCliff - ((k - p.sHeight) / p.sStrength) + pn) cliff = 1;
						if (c > p.cCliff) cliff = 2;

						if (cliff == 1)
						{
							if (desert) primer.setBlock(x, k, z, SANDSTONE);
							else primer.setBlock(x, k, z, nextInt(w, 3) == 0 ? COBBLE : STONE);
						}
						else if (cliff == 2)
						{
							primer.setBlock(x, k, z, cliff2);
						}
						else if (p.mix == MixedCliffParams::Mushroom)
						{
							primer.setBlock(x, k, z, top);
						}
						else if (k < 63)
						{
							primer.setBlock(x, k, z, k < 62 ? filler : top);
						}
						else if (p.mix == MixedCliffParams::Forest)
						{
							float mixNoise = simplex.noise2f(i / 12.0f, j / 12.0f);
							if (mixNoise < p.mix2Height) primer.setBlock(x, k, z, p.mix2Block);
							else if (mixNoise > p.mixHeight) primer.setBlock(x, k, z, p.mixBlock);
							else primer.setBlock(x, k, z, top);
						}
						else if (p.mix == MixedCliffParams::Jungle)
						{
							if (simplex.noise2f(i / 12.0f, j / 12.0f) > p.mixHeight) primer.setBlock(x, k, z, p.mixBlock);
							else primer.setBlock(x, k, z, top);
						}
						else
						{
							primer.setBlock(x, k, z, top);
						}
					}
					else if (depth < 6)
					{
						if (cliff == 1) primer.setBlock(x, k, z, cliff1Top);
						else if (cliff == 2) primer.setBlock(x, k, z, cliff2);
						else primer.setBlock(x, k, z, filler);
					}
				}
			}
		}

		// SurfaceVanillaTaiga / TaigaHills
		void paintTaiga(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, const float *noise, float river, const BlockState &mixBlock)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			float p = simplex.noise2f(i / 8.0f, j / 8.0f) * 0.5f;
			float c = calcCliff(x, z, noise, river);
			int cliff = 0;

			for (int k = 255; k > -1; k--)
			{
				int b = primer.getBlock(x, k, z);
				if (isAir(b))
				{
					depth = -1;
				}
				else if (isStone(b))
				{
					depth++;
					if (depth == 0)
					{
						if (c > 0.45f && c > 1.5f - ((k - 60.0f) / 65.0f) + p) cliff = 1;
						if (c > 1.5f) cliff = 2;
						if (k > 110 + (p * 4) && c < 0.3f + ((k - 100.0f) / 50.0f) + p) cliff = 3;

						if (cliff == 1) primer.setBlock(x, k, z, nextInt(w, 3) == 0 ? COBBLE : STONE);
						else if (cliff == 2) primer.setBlock(x, k, z, SHADOW_STONE);
						else if (cliff == 3) primer.setBlock(x, k, z, SNOW_BLOCK);
						else if (simplex.noise2f(i / 50.0f, j / 50.0f) + p * 0.6f > 0.24f) primer.setBlock(x, k, z, mixBlock);
						else primer.setBlock(x, k, z, GRASS);
					}
					else if (depth < 6)
					{
						if (cliff == 1) primer.setBlock(x, k, z, STONE);
						else if (cliff == 2) primer.setBlock(x, k, z, SHADOW_STONE);
						else if (cliff == 3) primer.setBlock(x, k, z, SNOW_BLOCK);
						else primer.setBlock(x, k, z, DIRT);
					}
				}
			}
		}

		// SurfaceVanillaExtremeHills / ExtremeHillsEdge / IceMountains
		void paintMixedHills(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, const float *noise, float river,
			const BlockState &top, const BlockState &filler, const BlockState &mixTop, const BlockState &mixFill,
			bool customCliff, const BlockState &cliff1, const BlockState &cliff2,
			float width, float height, float smallW, float smallS)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			float c = calcCliff(x, z, noise, river);
			bool cliff = c > 1.4f;
			bool mix = false;

			for (int k = 255; k > -1; k--)
			{
				int b = primer.getBlock(x, k, z);
				if (isAir(b))
				{
					depth = -1;
				}
				else if (isStone(b))
				{
					depth++;
					if (cliff)
					{
						if (customCliff)
						{
							if (depth > -1 && depth < 2) primer.setBlock(x, k, z, nextInt(w, 3) == 0 ? cliff2 : cliff1);
							else if (depth < 10) primer.setBlock(x, k, z, cliff1);
						}
						else
						{
							paintCliffColumn(primer, x, k, z, depth, w);
						}
					}
					else
					{
						if (depth == 0 && k > 61)
						{
							if (simplex.noise2f(i / width, j / width) + simplex.noise2f(i / smallW, j / smallW) * smallS > height)
							{
								primer.setBlock(x, k, z, mixTop);
								mix = true;
							}
							else
							{
								primer.setBlock(x, k, z, top);
							}
						}
						else if (depth < 4)
						{
							primer.setBlock(x, k, z, mix ? mixFill : filler);
						}
					}
				}
			}
		}

		// SurfaceVanillaOcean / FrozenOcean
		void paintOcean(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w,
			const BlockState &top, const BlockState &filler, const BlockState &mix, float width, float height)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			for (int k = 255; k > -1; k--)
			{
				int b = primer.getBlock(x, k, z);
				if (isAir(b))
				{
					depth = -1;
				}
				else if (isStone(b))
				{
					depth++;
					if (depth == 0 && k > 0 && k < 63)
					{
						float mixCheck = simplex.noise2f(i / width, j / width);
						primer.setBlock(x, k, z, mixCheck > height ? mix : top);
					}
					else if (depth < 4 && k < 63)
					{
						primer.setBlock(x, k, z, filler);
					}
					else if (depth == 0 && k < 69)
					{
						primer.setBlock(x, k, z, SAND);
					}
				}
			}
		}

		// SurfaceVanillaDesert (riverbank sandstone/grass) and SurfacePolar (ice plains)
		void paintDesertOrPolar(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, const float *noise, float river,
			const BlockState &top, const BlockState &filler, bool polar)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			bool water = false;
			bool riverPaint = false;
			bool grass = false;

			if (river > 0.05f && river + (simplex.noise2f(i / 10.0f, j / 10.0f) * 0.1f) > 0.86f)
			{
				riverPaint = true;
				if (simplex.noise2f(i / 12.0f, j / 12.0f) > 0.25f) grass = true;
			}

			for (int k = 255; k > -1; k--)
			{
				int b = primer.getBlock(x, k, z);
				if (isAir(b))
				{
					depth = -1;
				}
				else if (isStone(b))
				{
					depth++;
					if (riverPaint)
					{
						if (grass && depth < 4)
						{
							primer.setBlock(x, k, z, filler);
						}
						else if (depth == 0)
						{
							if (polar) primer.setBlock(x, k, z, nextInt(w, 2) == 0 ? STONE : COBBLE);
							else primer.setBlock(x, k, z, nextInt(w, 2) == 0 ? top : SANDSTONE);
						}
					}
					else if (polar)
					{
						if (depth > -1 && depth < 9)
						{
							primer.setBlock(x, k, z, top);
							if (depth == 0 && k > 61 && k < 254) calcSnowHeight(x, k, z, primer, noise);
						}
					}
					else if (depth > -1 && depth < 5)
					{
						primer.setBlock(x, k, z, top);
					}
					else if (depth < 8)
					{
						primer.setBlock(x, k, z, filler);
					}
				}
				else if (polar && !water && b == WATER_ID)
				{
					primer.setBlock(x, k, z, ICE);
					water = true;
				}
			}
		}

		// SurfaceVanillaRiver / FrozenRiver
		void paintRiver(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, float river)
		{
			const OpenSimplexNoise &simplex = w.simplex(0);
			if (river > 0.05f && river + (simplex.noise2f(i / 10.0f, j / 10.0f) * 0.15f) > 0.8f)
			{
				for (int k = 255; k > -1; k--)
				{
					int b = primer.getBlock(x, k, z);
					if (isAir(b)) depth = -1;
					else if (b != WATER_ID)
					{
						depth++;
						if (depth == 0 && k > 61) primer.setBlock(x, k, z, GRASS);
						else if (depth < 4) primer.setBlock(x, k, z, DIRT);
						else if (depth > 4) return;
					}
				}
			}
		}

		// SurfaceVanillaBeach (also stone / cold beach)
		void paintBeach(ChunkPrimer &primer, int x, int z, int depth, const BlockState &top, const BlockState &filler, const BlockState &mixFill)
		{
			for (int k = 255; k > -1; k--)
			{
				int b = primer.getBlock(x, k, z);
				if (isAir(b))
				{
					depth = -1;
				}
				else if (isStone(b))
				{
					depth++;
					if (depth == 0 && k > 61) primer.setBlock(x, k, z, top);
					else if (k > 63 && depth > 3 && depth < 6) primer.setBlock(x, k, z, mixFill);
					else if (depth < 4) primer.setBlock(x, k, z, filler);
				}
			}
		}
	}

	// =================================================================================
	// The biomes
	// =================================================================================
	namespace
	{
		typedef float (*TerrainFn)(const RtgWorld &w, int x, int y, float border, float river);

		// A generic biome assembled from a terrain function and a surface painter.
		class BasicBiome : public RealisticBiome
		{
		public:
			TerrainFn terrain = nullptr;
			const RidgedExtremeHills *ridged = nullptr;
			std::function<void(ChunkPrimer &, int, int, int, int, int, const RtgWorld &, const float *, float)> surface;
			bool riverOasis = false;	// rReplaceWithRiver (desert biomes)

			void rReplace(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, const float *noise, float river) const override
			{
				RealisticBiome::rReplace(primer, i, j, x, z, depth, w, noise, river);
				if (riverOasis && w.lushRiverbanksInDesert)
				{
					paintRiverOasis(primer, i, j, x, z, depth, w, !allowRivers ? 0.0f : river);
				}
			}

		protected:
			float terrainNoise(const RtgWorld &w, int x, int y, float border, float river) const override
			{
				if (ridged != nullptr) return ridged->generateNoise(w, x, y);
				return terrain(w, x, y, border, river);
			}

			void paintSurface(ChunkPrimer &primer, int i, int j, int x, int z, int depth, const RtgWorld &w, const float *noise, float river) const override
			{
				surface(primer, i, j, x, z, depth, w, noise, river);
			}
		};

		std::mutex g_initLock;
		bool g_initialised = false;
		RealisticBiome *g_biomes[256] = {};
		bool g_ocean[256] = {}, g_river[256] = {}, g_swamp[256] = {}, g_beach[256] = {}, g_land[256] = {};
		std::vector<std::unique_ptr<RealisticBiome>> g_storage;

		BasicBiome *make(int rtgId, Biome *base)
		{
			BasicBiome *b = new BasicBiome();
			b->rtgId = rtgId;
			b->baseBiomeId = base->id;
			b->beachRtgId = Biome::beaches->id;
			g_storage.emplace_back(b);
			g_biomes[rtgId] = b;
			return b;
		}

		const RidgedExtremeHills &ridgedExtremeHills()
		{
			static RidgedExtremeHills r(150.0f, 67.0f, 200.0f);
			return r;
		}

		const RidgedExtremeHills &ridgedExtremeHillsEdge()
		{
			static RidgedExtremeHills r(125.0f, 67.0f, 200.0f);
			return r;
		}

		void build()
		{
			const int STONE_BEACH = Biomes::VIRTUAL_STONE_BEACH;
			const int COLD_BEACH = Biomes::VIRTUAL_COLD_BEACH;

			// --- Ocean ------------------------------------------------------------
			{
				BasicBiome *b = make(Biome::ocean->id, Biome::ocean);
				b->allowRivers = false;
				b->allowScenicLakes = false;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainOcean(x, y, w, river, 50.0f); };
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *, float) {
					paintOcean(p, i, j, x, z, d, w, SAND, SAND, GRAVEL, 20.0f, 0.2f);
				};
			}
			// --- Frozen Ocean -------------------------------------------------------
			{
				BasicBiome *b = make(Biome::frozenOcean->id, Biome::frozenOcean);
				b->allowScenicLakes = false;
				b->frozenRiver = true;
				b->beachRtgId = COLD_BEACH;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainOcean(x, y, w, river, 50.0f); };
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *, float) {
					paintOcean(p, i, j, x, z, d, w, SAND, SAND, GRAVEL, 20.0f, 0.2f);
				};
			}
			// --- Plains -------------------------------------------------------------
			{
				BasicBiome *b = make(Biome::plains->id, Biome::plains);
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) {
					return riverized(65.0f + groundNoise(static_cast<float>(x), static_cast<float>(y), 4.0f, w), river);
				};
				b->surface = [](ChunkPrimer &p, int, int, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					paintPlainsLike(p, x, z, d, w, n, r, biomeTop(Biome::plains), biomeFiller(Biome::plains), -1);
				};
			}
			// --- Desert -------------------------------------------------------------
			{
				BasicBiome *b = make(Biome::desert->id, Biome::desert);
				b->allowScenicLakes = false;
				b->riverOasis = true;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) {
					const float base = 64.0f;
					float duneHeight = (21.0f + w.sandDuneHeight);
					duneHeight *= (1.0f + w.simplex(2).noise2f(x / 330.0f, y / 330.0f)) / 2.0f;
					return terrainPolar(static_cast<float>(x), static_cast<float>(y), w, river, 200.0f, duneHeight, 70.0f, 40.0f, base)
						+ groundNoise(static_cast<float>(x), static_cast<float>(y), 1.0f, w);
				};
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					paintDesertOrPolar(p, i, j, x, z, d, w, n, r, SAND, SANDSTONE, false);	// filler: sandstone (RTG config)
				};
			}
			// --- Desert Hills ------------------------------------------------------
			{
				BasicBiome *b = make(Biome::desertHills->id, Biome::desertHills);
				b->allowScenicLakes = false;
				b->riverOasis = true;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) {
					return terrainHighland(static_cast<float>(x), static_cast<float>(y), w, river, 10.0f, 200.0f, 80.0f, 68.0f - 62.0f);
				};
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					MixedCliffParams mp;
					mp.mix = MixedCliffParams::DesertHills;
					paintMixedCliff(p, i, j, x, z, d, w, n, r, SAND, SANDSTONE, mp);
				};
			}
			// --- Extreme Hills -----------------------------------------------------
			{
				BasicBiome *b = make(Biome::extremeHills->id, Biome::extremeHills);
				b->allowRivers = false;
				b->allowScenicLakes = false;
				b->beachRtgId = STONE_BEACH;
				b->ridged = &ridgedExtremeHills();
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					paintMixedHills(p, i, j, x, z, d, w, n, r, biomeTop(Biome::extremeHills), biomeFiller(Biome::extremeHills),
						GRASS, DIRT, false, STONE, COBBLE, 60.0f, -0.14f, 14.0f, 0.25f);
				};
			}
			// --- Extreme Hills Edge ------------------------------------------------
			{
				BasicBiome *b = make(Biome::smallerExtremeHills->id, Biome::smallerExtremeHills);
				b->allowRivers = false;
				b->allowScenicLakes = false;
				b->beachRtgId = STONE_BEACH;
				b->ridged = &ridgedExtremeHillsEdge();
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					paintMixedHills(p, i, j, x, z, d, w, n, r, biomeTop(Biome::smallerExtremeHills), biomeFiller(Biome::smallerExtremeHills),
						GRASS, DIRT, false, STONE, COBBLE, 60.0f, -0.14f, 14.0f, 0.25f);
				};
			}
			// --- Forest --------------------------------------------------------------
			{
				BasicBiome *b = make(Biome::forest->id, Biome::forest);
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) {
					float gn = groundNoise(static_cast<float>(x), static_cast<float>(y), 2.0f, w);
					float m = hills(static_cast<float>(x), static_cast<float>(y), 10.0f, w);
					return riverized(65.0f + gn + m, river);
				};
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					MixedCliffParams mp;
					mp.mix = MixedCliffParams::Forest;
					mp.mixBlock = PODZOL; mp.mixHeight = 0.6f;
					mp.mix2Block = PODZOL; mp.mix2Height = -0.4f;
					paintMixedCliff(p, i, j, x, z, d, w, n, r, GRASS, DIRT, mp);
				};
			}
			// --- Forest Hills -------------------------------------------------------
			{
				BasicBiome *b = make(Biome::forestHills->id, Biome::forestHills);
				b->allowScenicLakes = false;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) {
					return terrainHighland(static_cast<float>(x), static_cast<float>(y), w, river, 10.0f, 68.0f, 30.0f, 72.0f - 62.0f);
				};
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					MixedCliffParams mp;
					mp.mix = MixedCliffParams::Forest;
					mp.mixBlock = PODZOL; mp.mixHeight = 0.6f;
					mp.mix2Block = PODZOL; mp.mix2Height = -0.4f;
					paintMixedCliff(p, i, j, x, z, d, w, n, r, GRASS, DIRT, mp);
				};
			}
			// --- Taiga --------------------------------------------------------------
			{
				BasicBiome *b = make(Biome::taiga->id, Biome::taiga);
				b->beachRtgId = STONE_BEACH;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainFlatLakes(x, y, w, river, 68.0f); };
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					paintTaiga(p, i, j, x, z, d, w, n, r, PODZOL);
				};
			}
			// --- Taiga Hills -------------------------------------------------------
			{
				BasicBiome *b = make(Biome::taigaHills->id, Biome::taigaHills);
				b->allowScenicLakes = false;
				b->beachRtgId = STONE_BEACH;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) {
					return terrainHighland(static_cast<float>(x), static_cast<float>(y), w, river, 10.0f, 68.0f, 30.0f, 72.0f - 62.0f);
				};
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					paintTaiga(p, i, j, x, z, d, w, n, r, PODZOL);
				};
			}
			// --- Swampland -----------------------------------------------------------
			{
				BasicBiome *b = make(Biome::swampland->id, Biome::swampland);
				b->beachRtgId = Biome::swampland->id;	// swamps are their own beach
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainMarsh(x, y, w, 61.5f, river); };
				b->surface = [](ChunkPrimer &p, int, int, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					paintPlainsLike(p, x, z, d, w, n, r, biomeTop(Biome::swampland), biomeFiller(Biome::swampland), 64);
				};
			}
			// --- River / Frozen River ------------------------------------------------
			{
				BasicBiome *b = make(Biome::river->id, Biome::river);
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainFlatLakes(x, y, w, river, 60.0f); };
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *, float r) {
					paintRiver(p, i, j, x, z, d, w, r);
				};
			}
			{
				BasicBiome *b = make(Biome::frozenRiver->id, Biome::frozenRiver);
				b->frozenRiver = true;
				b->beachRtgId = COLD_BEACH;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainFlatLakes(x, y, w, river, 60.0f); };
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *, float r) {
					paintRiver(p, i, j, x, z, d, w, r);
				};
			}
			// --- Ice Plains (arctic surface) ----------------------------------------
			{
				BasicBiome *b = make(Biome::iceFlats->id, Biome::iceFlats);
				b->frozenRiver = true;
				b->beachRtgId = COLD_BEACH;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) {
					return terrainPlains(x, y, w, river, 160.0f, 10.0f, 60.0f, 200.0f, 65.0f);
				};
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					paintDesertOrPolar(p, i, j, x, z, d, w, n, r, SNOW_BLOCK, biomeFiller(Biome::iceFlats), true);
				};
			}
			// --- Ice Mountains --------------------------------------------------------
			{
				BasicBiome *b = make(Biome::iceMountains->id, Biome::iceMountains);
				b->allowScenicLakes = false;
				b->allowRivers = false;
				b->frozenRiver = true;
				b->beachRtgId = COLD_BEACH;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) {
					return terrainLonelyMountain(x, y, w, river, 60.0f, 230.0f, 68.0f);
				};
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					paintMixedHills(p, i, j, x, z, d, w, n, r, SNOW_BLOCK, SNOW_BLOCK, SNOW_BLOCK, SNOW_BLOCK,
						true, PACKED_ICE, ICE, 60.0f, -0.14f, 14.0f, 0.25f);
				};
			}
			// --- Mushroom Island (+ shore) --------------------------------------------
			{
				BasicBiome *b = make(Biome::mushroomIsland->id, Biome::mushroomIsland);
				b->allowScenicLakes = false;
				b->beachRtgId = Biome::mushroomIslandShore->id;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainGrasslandFlats(x, y, w, river, 40.0f, 68.0f); };
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					MixedCliffParams mp;
					mp.mix = MixedCliffParams::Mushroom;
					paintMixedCliff(p, i, j, x, z, d, w, n, r, MYCELIUM, biomeFiller(Biome::mushroomIsland), mp);
				};
			}
			{
				BasicBiome *b = make(Biome::mushroomIslandShore->id, Biome::mushroomIslandShore);
				b->allowScenicLakes = false;
				b->beachRtgId = Biome::mushroomIslandShore->id;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainMarsh(x, y, w, 61.5f, river); };
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					MixedCliffParams mp;
					mp.mix = MixedCliffParams::Mushroom;
					paintMixedCliff(p, i, j, x, z, d, w, n, r, MYCELIUM, biomeFiller(Biome::mushroomIslandShore), mp);
				};
			}
			// --- Beach (+ RTG stone / cold beaches, mapped onto the game's beach) -----
			{
				BasicBiome *b = make(Biome::beaches->id, Biome::beaches);
				b->surfaceBleedIn = true;
				b->surfaceBleedOut = true;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainBeach(x, y, w, river, 63.0f); };
				b->surface = [](ChunkPrimer &p, int, int, int x, int z, int d, const RtgWorld &, const float *, float) {
					paintBeach(p, x, z, d, SAND, SAND, SANDSTONE);
				};
			}
			{
				BasicBiome *b = make(STONE_BEACH, Biome::beaches);
				b->surfaceBleedIn = true;
				b->surfaceBleedOut = true;
				b->beachRtgId = STONE_BEACH;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainBeach(x, y, w, river, 63.0f); };
				b->surface = [](ChunkPrimer &p, int, int, int x, int z, int d, const RtgWorld &, const float *, float) {
					paintBeach(p, x, z, d, GRAVEL, GRAVEL, GRAVEL);
				};
			}
			{
				BasicBiome *b = make(COLD_BEACH, Biome::beaches);
				b->frozenRiver = true;
				b->beachRtgId = COLD_BEACH;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainBeach(x, y, w, river, 63.0f); };
				b->surface = [](ChunkPrimer &p, int, int, int x, int z, int d, const RtgWorld &, const float *, float) {
					paintBeach(p, x, z, d, SAND, SAND, SANDSTONE);
				};
			}
			// --- Jungle / Jungle Hills -------------------------------------------------
			{
				BasicBiome *b = make(Biome::jungle->id, Biome::jungle);
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) { return terrainFlatLakes(x, y, w, river, 66.0f); };
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					MixedCliffParams mp;
					mp.mix = MixedCliffParams::Jungle;
					mp.mixBlock = PODZOL; mp.mixHeight = 0.09f;
					paintMixedCliff(p, i, j, x, z, d, w, n, r, GRASS, DIRT, mp);
				};
			}
			{
				BasicBiome *b = make(Biome::jungleHills->id, Biome::jungleHills);
				b->allowScenicLakes = false;
				b->terrain = [](const RtgWorld &w, int x, int y, float, float river) {
					return terrainHighland(static_cast<float>(x), static_cast<float>(y), w, river, 10.0f, 68.0f, 40.0f, 72.0f - 62.0f);
				};
				b->surface = [](ChunkPrimer &p, int i, int j, int x, int z, int d, const RtgWorld &w, const float *n, float r) {
					MixedCliffParams mp;
					mp.min = 1.0f;
					paintMixedCliff(p, i, j, x, z, d, w, n, r, GRASS, DIRT, mp);
				};
			}

			// Biome classes used by the BiomeAnalyzer (Forge BiomeDictionary equivalents)
			for (int id = 0; id < 256; id++)
			{
				if (id == Biome::ocean->id || id == Biome::frozenOcean->id) g_ocean[id] = true;
				else if (id == Biome::river->id || id == Biome::frozenRiver->id) g_river[id] = true;
				else if (id == Biome::swampland->id) g_swamp[id] = true;
				else if (id == Biome::beaches->id || id == Biome::mushroomIslandShore->id || id == STONE_BEACH || id == COLD_BEACH) g_beach[id] = true;
				else g_land[id] = true;
			}
		}
	}

	RealisticBiome *RealisticBiome::riverBiome() const
	{
		return Biomes::get(frozenRiver ? Biome::frozenRiver->id : Biome::river->id);
	}

	namespace Biomes
	{
		void init()
		{
			std::lock_guard<std::mutex> guard(g_initLock);
			if (g_initialised) return;
			build();
			g_initialised = true;
		}

		RealisticBiome *get(int rtgId)
		{
			if (rtgId >= 0 && rtgId < 256 && g_biomes[rtgId] != nullptr) return g_biomes[rtgId];
			return g_biomes[Biome::plains->id];
		}

		bool isOcean(int id) { return id >= 0 && id < 256 && g_ocean[id]; }
		bool isRiver(int id) { return id >= 0 && id < 256 && g_river[id]; }
		bool isSwamp(int id) { return id >= 0 && id < 256 && g_swamp[id]; }
		bool isBeach(int id) { return id >= 0 && id < 256 && g_beach[id]; }
		bool isLand(int id) { return id >= 0 && id < 256 && g_land[id]; }
	}
}
