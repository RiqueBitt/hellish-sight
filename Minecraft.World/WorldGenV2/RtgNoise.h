#pragma once

// WorldGen V2 (Hellish Sight) - noise generators ported from Realistic Terrain Generation
// (RTG, https://github.com/Team-RTG/Realistic-Terrain-Generation, GPL-3.0).
//
//  * OpenSimplexNoise: 2D/3D OpenSimplex (KdotJPG / DigitalShadow lookup-table variant with
//    1024-entry permutation tables) including multiEval2D (disk / derivative data).
//  * SpacedCellularNoise: Voronoi noise with spaced points (libnoise / zeno410 variant).
//
// Behaviour (including quirks) matches the Java originals so terrain looks the same.

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace WorldGenV2
{
	// --- multiEval2D accumulators (ISimplexData2D) ---------------------------------
	struct SimplexData2D
	{
		enum Kind { Disk, Derivative };

		explicit SimplexData2D(Kind kind) : kind(kind) {}

		Kind kind;
		double deltaX = 0.0;
		double deltaY = 0.0;

		void clear() { deltaX = 0.0; deltaY = 0.0; }
		void apply(double attn, double extrapolation, double gx, double gy, int giSph2, double dx, double dy);
	};

	class OpenSimplexNoise
	{
	public:
		explicit OpenSimplexNoise(int64_t seed);

		float noise2f(float x, float y) const { return static_cast<float>(noise2d(x, y)); }
		float noise3f(float x, float y, float z) const { return static_cast<float>(noise3d(x, y, z)); }
		double noise2d(double x, double y) const;
		double noise3d(double x, double y, double z) const;
		void multiEval2D(double x, double y, SimplexData2D &data) const;

		static const double GRADIENTS_SPH2[24];

	private:
		int m_perm[1024];
		int m_perm2D[1024];
		int m_perm2DSph2[1024];
		int m_perm3D[1024];
	};

	struct VoronoiResult
	{
		double shortestDistance = 32000000.0;
		double nextDistance = 32000000.0;
		double closestX = 32000000.0;
		double closestZ = 32000000.0;

		// 0 in the middle of a cell and 1 on the border
		double borderValue() const { return shortestDistance / nextDistance; }
		// 1 in the middle of a cell and 0 on the border
		double interiorValue() const { return (nextDistance - shortestDistance) / nextDistance; }

		// point on the vector from the closest point towards (x, z), at the given length
		void toLength(float x, float z, float radius, float &outX, float &outZ) const;

		void evaluate(const std::vector<double> &points, double x, double z);	// points: x0,y0,x1,y1,...
	};

	class SpacedCellularNoise
	{
	public:
		explicit SpacedCellularNoise(int64_t seed);

		VoronoiResult eval2D(double x, double y);

	private:
		static const int kTotalPoints = 100;
		static const int kPointsPerTorus = 25;

		const std::vector<double> &areaPoints(int ax, int ay);

		std::vector<double> m_allPoints;	// x0,y0,...
		int64_t m_xSeed;
		int64_t m_ySeed;

		std::mutex m_lock;
		std::unordered_map<uint64_t, std::vector<double>> m_cache;
	};
}
