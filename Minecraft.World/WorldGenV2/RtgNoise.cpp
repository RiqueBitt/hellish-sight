#include "stdafx.h"
#include "RtgNoise.h"
#include "../Random.h"

#include <cmath>
#include <cstring>

namespace WorldGenV2
{
	namespace
	{
		// 2D Gradients -- new scheme (Dodecagon)
		const double GRADIENTS_2D[24] = {
			0.114251372530929, 0.065963060686016,
			0.131926121372032, 0.000000000000000,
			0.114251372530929, -0.065963060686016,
			0.065963060686016, -0.114251372530929,
			0.000000000000000, -0.131926121372032,
			-0.065963060686016, -0.114251372530929,
			-0.114251372530929, -0.065963060686016,
			-0.131926121372032, -0.000000000000000,
			-0.114251372530929, 0.065963060686016,
			-0.065963060686016, 0.114251372530929,
			-0.000000000000000, 0.131926121372032,
			0.065963060686016, 0.114251372530929,
		};

		// 3D Gradients -- new scheme (Normalized expanded cuboctahedron)
		const double GRADIENTS_3D[144] = {
			-0.009192019279820, 0.061948581592974, 0.105513124626310,
			0.061948581592974, -0.009192019279820, 0.105513124626310,
			0.052339395980958, 0.052339395980958, 0.097858646551677,
			0.002784312704445, 0.002784312704445, 0.122636188189934,
			-0.009192019279820, 0.105513124626310, 0.061948581592974,
			0.061948581592974, 0.105513124626310, -0.009192019279820,
			0.052339395980958, 0.097858646551677, 0.052339395980958,
			0.002784312704445, 0.122636188189934, 0.002784312704445,
			0.105513124626310, -0.009192019279820, 0.061948581592974,
			0.105513124626310, 0.061948581592974, -0.009192019279820,
			0.097858646551677, 0.052339395980958, 0.052339395980958,
			0.122636188189934, 0.002784312704445, 0.002784312704445,
			-0.067278076657600, 0.090991610281865, 0.047427067248529,
			-0.090991610281865, 0.067278076657600, -0.047427067248529,
			-0.057908021389848, 0.107463104666361, -0.012388770819128,
			-0.107463104666361, 0.057908021389848, 0.012388770819128,
			-0.067278076657600, 0.047427067248529, 0.090991610281865,
			-0.090991610281865, -0.047427067248529, 0.067278076657600,
			-0.057908021389848, -0.012388770819128, 0.107463104666361,
			-0.107463104666361, 0.012388770819128, 0.057908021389848,
			0.047427067248529, -0.067278076657600, 0.090991610281865,
			-0.047427067248529, -0.090991610281865, 0.067278076657600,
			-0.012388770819128, -0.057908021389848, 0.107463104666361,
			0.012388770819128, -0.107463104666361, 0.057908021389848,
			0.067278076657600, -0.090991610281865, -0.047427067248529,
			0.090991610281865, -0.067278076657600, 0.047427067248529,
			0.107463104666361, -0.057908021389848, -0.012388770819128,
			0.057908021389848, -0.107463104666361, 0.012388770819128,
			0.067278076657600, -0.047427067248529, -0.090991610281865,
			0.090991610281865, 0.047427067248529, -0.067278076657600,
			0.107463104666361, -0.012388770819128, -0.057908021389848,
			0.057908021389848, 0.012388770819128, -0.107463104666361,
			-0.047427067248529, 0.067278076657600, -0.090991610281865,
			0.047427067248529, 0.090991610281865, -0.067278076657600,
			-0.012388770819128, 0.107463104666361, -0.057908021389848,
			0.012388770819128, 0.057908021389848, -0.107463104666361,
			0.009192019279820, -0.061948581592974, -0.105513124626310,
			-0.061948581592974, 0.009192019279820, -0.105513124626310,
			-0.002784312704445, -0.002784312704445, -0.122636188189934,
			-0.052339395980958, -0.052339395980958, -0.097858646551677,
			0.009192019279820, -0.105513124626310, -0.061948581592974,
			-0.061948581592974, -0.105513124626310, 0.009192019279820,
			-0.002784312704445, -0.122636188189934, -0.002784312704445,
			-0.052339395980958, -0.097858646551677, -0.052339395980958,
			-0.105513124626310, 0.009192019279820, -0.061948581592974,
			-0.105513124626310, -0.061948581592974, 0.009192019279820,
			-0.122636188189934, -0.002784312704445, -0.002784312704445,
			-0.097858646551677, -0.052339395980958, -0.052339395980958
		};

		const double STRETCH_2D = -0.211324865405187;
		const double SQUISH_2D = 0.366025403784439;
		const double STRETCH_3D = -1.0 / 6.0;
		const double SQUISH_3D = 1.0 / 3.0;

		struct LatticePoint2D
		{
			int xsv = 0, ysv = 0;
			double dx = 0, dy = 0;
			LatticePoint2D() {}
			LatticePoint2D(int x, int y) : xsv(x), ysv(y)
			{
				dx = -x - ((x + y) * SQUISH_2D);
				dy = -y - ((x + y) * SQUISH_2D);
			}
		};

		struct Contribution3D
		{
			int xsb = 0, ysb = 0, zsb = 0;
			double dx = 0, dy = 0, dz = 0;
			const Contribution3D *next = nullptr;
		};

		struct Tables
		{
			LatticePoint2D lookup2D[32];
			const Contribution3D *lookup3D[2048];
			std::vector<Contribution3D> storage;

			Tables()
			{
				for (int i = 0; i < 8; i++)
				{
					int i1, j1, i2, j2;
					if ((i & 1) == 0)
					{
						if ((i & 2) == 0) { i1 = 0; j1 = 0; } else { i1 = 2; j1 = 0; }
						if ((i & 4) == 0) { i2 = 1; j2 = -1; } else { i2 = 1; j2 = 1; }
					}
					else
					{
						if ((i & 2) == 0) { i1 = -1; j1 = 1; } else { i1 = 1; j1 = 1; }
						if ((i & 4) == 0) { i2 = 0; j2 = 0; } else { i2 = 0; j2 = 2; }
					}
					lookup2D[i * 4] = LatticePoint2D(1, 0);
					lookup2D[i * 4 + 1] = LatticePoint2D(0, 1);
					lookup2D[i * 4 + 2] = LatticePoint2D(i1, j1);
					lookup2D[i * 4 + 3] = LatticePoint2D(i2, j2);
				}

				static const int base0[] = {0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1};
				static const int base1[] = {2, 1, 1, 0, 2, 1, 0, 1, 2, 0, 1, 1, 3, 1, 1, 1};
				static const int base2[] = {1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 2, 1, 1, 0, 2, 1, 0, 1, 2, 0, 1, 1};
				const int *bases[3] = {base0, base1, base2};
				const int baseLen[3] = {16, 16, 24};

				static const int p3D[] = {
					0, 0, 1, -1, 0, 0, 1, 0, -1, 0, 0, -1,
					1, 0, 0, 0, 1, -1, 0, 0, -1, 0, 1, 0,
					0, -1, 1, 0, 2, 1, 1, 0, 1, 1, 1, -1,
					0, 2, 1, 0, 1, 1, 1, -1, 1, 0, 2, 0,
					1, 1, 1, -1, 1, 1, 1, 3, 2, 1, 0, 3,
					1, 2, 0, 1, 3, 2, 0, 1, 3, 1, 0, 2,
					1, 3, 0, 2, 1, 3, 0, 1, 2, 1, 1, 1,
					0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 0, 2,
					0, 2, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2,
					2, 0, 0, 0, 0, 1, 1, -1, 1, 2, 0, 0,
					0, 0, 1, -1, 1, 1, 2, 0, 0, 0, 0, 1,
					1, 1, -1, 2, 3, 1, 1, 1, 2, 0, 0, 2,
					2, 3, 1, 1, 1, 2, 2, 0, 0, 2, 3, 1,
					1, 1, 2, 0, 2, 0, 2, 1, 1, -1, 1, 2,
					0, 0, 2, 2, 1, 1, -1, 1, 2, 2, 0, 0,
					2, 1, -1, 1, 1, 2, 0, 0, 2, 2, 1, -1,
					1, 1, 2, 0, 2, 0, 2, 1, 1, 1, -1, 2,
					2, 0, 0, 2, 1, 1, 1, -1, 2, 0, 2, 0
				};
				static const int lookupPairs3D[] = {
					0, 2, 1, 1, 2, 2, 5, 1,
					6, 0, 7, 0, 32, 2, 34, 2,
					129, 1, 133, 1, 160, 5, 161, 5,
					518, 0, 519, 0, 546, 4, 550, 4,
					645, 3, 647, 3, 672, 5, 673, 5,
					674, 4, 677, 3, 678, 4, 679, 3,
					680, 13, 681, 13, 682, 12, 685, 14,
					686, 12, 687, 14, 712, 20, 714, 18,
					809, 21, 813, 23, 840, 20, 841, 21,
					1198, 19, 1199, 22, 1226, 18, 1230, 19,
					1325, 23, 1327, 22, 1352, 15, 1353, 17,
					1354, 15, 1357, 17, 1358, 16, 1359, 16,
					1360, 11, 1361, 10, 1362, 11, 1365, 10,
					1366, 9, 1367, 9, 1392, 11, 1394, 11,
					1489, 10, 1493, 10, 1520, 8, 1521, 8,
					1878, 9, 1879, 9, 1906, 7, 1910, 7,
					2005, 6, 2007, 6, 2032, 8, 2033, 8,
					2034, 7, 2037, 6, 2038, 7, 2039, 6
				};

				const int p3DLen = sizeof(p3D) / sizeof(p3D[0]);
				const int contributionCount = p3DLen / 9;

				// Reserve everything up front so element addresses stay stable.
				storage.reserve(contributionCount * 8);
				std::vector<const Contribution3D *> heads(contributionCount, nullptr);

				auto make = [&](double multiplier, int xsb, int ysb, int zsb) -> Contribution3D * {
					Contribution3D c;
					c.xsb = xsb; c.ysb = ysb; c.zsb = zsb;
					c.dx = -xsb - multiplier * SQUISH_3D;
					c.dy = -ysb - multiplier * SQUISH_3D;
					c.dz = -zsb - multiplier * SQUISH_3D;
					storage.push_back(c);
					return &storage.back();
				};

				for (int i = 0; i < p3DLen; i += 9)
				{
					const int *baseSet = bases[p3D[i]];
					const int len = baseLen[p3D[i]];
					Contribution3D *previous = nullptr;
					Contribution3D *current = nullptr;
					for (int j = 0; j < len; j += 4)
					{
						current = make(baseSet[j], baseSet[j + 1], baseSet[j + 2], baseSet[j + 3]);
						if (previous == nullptr) heads[i / 9] = current;
						else previous->next = current;
						previous = current;
					}
					if (current != nullptr)
					{
						Contribution3D *a = make(p3D[i + 1], p3D[i + 2], p3D[i + 3], p3D[i + 4]);
						current->next = a;
						Contribution3D *b = make(p3D[i + 5], p3D[i + 6], p3D[i + 7], p3D[i + 8]);
						a->next = b;
					}
				}

				for (int i = 0; i < 2048; i++) lookup3D[i] = nullptr;
				const int pairsLen = sizeof(lookupPairs3D) / sizeof(lookupPairs3D[0]);
				for (int i = 0; i < pairsLen; i += 2)
				{
					lookup3D[lookupPairs3D[i]] = heads[lookupPairs3D[i + 1]];
				}
			}
		};

		const Tables &tables()
		{
			static Tables t;
			return t;
		}

		inline int fastFloor(double d)
		{
			int i = static_cast<int>(d);
			return (d < i) ? i - 1 : i;
		}

		// java.lang.Double.doubleToLongBits for finite values
		inline int64_t doubleBits(double d)
		{
			int64_t bits;
			std::memcpy(&bits, &d, sizeof(bits));
			return bits;
		}

		// java.awt.Point.hashCode() (inherited from Point2D) for integer coordinates
		inline int javaPointHash(int x, int y)
		{
			int64_t bits = doubleBits(static_cast<double>(x));
			bits ^= doubleBits(static_cast<double>(y)) * 31;
			return static_cast<int>(bits) ^ static_cast<int>(bits >> 32);
		}
	}

	const double OpenSimplexNoise::GRADIENTS_SPH2[24] = {
		0, 1.000000000000000,
		0.500000000000000, 0.866025403784439,
		0.866025403784439, 0.500000000000000,
		1.000000000000000, 0,
		0.866025403784439, -0.500000000000000,
		0.500000000000000, -0.866025403784439,
		0, -1.000000000000000,
		-0.500000000000000, -0.866025403784439,
		-0.866025403784439, -0.500000000000000,
		-1.000000000000000, 0,
		-0.866025403784439, 0.500000000000000,
		-0.500000000000000, 0.866025403784439
	};

	void SimplexData2D::apply(double attn, double extrapolation, double gx, double gy, int giSph2, double dx, double dy)
	{
		const double attnSq = attn * attn;
		if (kind == Disk)
		{
			const double extrap = attnSq * attnSq * extrapolation;
			deltaX += extrap * OpenSimplexNoise::GRADIENTS_SPH2[giSph2];
			deltaY += extrap * OpenSimplexNoise::GRADIENTS_SPH2[giSph2 + 1];
		}
		else
		{
			deltaX += (gx * attn - 8 * dx * extrapolation) * attnSq * attn;
			deltaY += (gy * attn - 8 * dy * extrapolation) * attnSq * attn;
		}
	}

	OpenSimplexNoise::OpenSimplexNoise(int64_t seed)
	{
		tables(); // make sure the shared tables exist before any concurrent use

		int source[1024];
		for (int i = 0; i < 1024; i++) source[i] = i;

		uint64_t s = static_cast<uint64_t>(seed);
		for (int i = 1023; i >= 0; i--)
		{
			s = s * 6364136223846793005ULL + 1442695040888963407ULL;
			// Java: (int) ((seed + 31) % (i + 1)) with signed 64-bit arithmetic
			int64_t signedSeed = static_cast<int64_t>(s + 31ULL);
			int r = static_cast<int>(signedSeed % (i + 1));
			if (r < 0) r += (i + 1);

			m_perm[i] = source[r];
			m_perm2D[i] = m_perm[i] % 12 * 2;
			m_perm2DSph2[i] = m_perm[i] / 12 % 12 * 2;
			m_perm3D[i] = m_perm[i] % 48 * 3;
			source[r] = source[i];
		}
	}

	double OpenSimplexNoise::noise2d(double x, double y) const
	{
		const Tables &t = tables();
		double value = 0;

		double s = STRETCH_2D * (x + y);
		double xs = x + s;
		double ys = y + s;

		int xsb = fastFloor(xs);
		int ysb = fastFloor(ys);

		double xsi = xs - xsb;
		double ysi = ys - ysb;

		int a = static_cast<int>(ysi - xsi + 1);
		int index = (a << 2)
			| static_cast<int>(xsi + ysi / 2.0 + a / 2.0) << 3
			| static_cast<int>(ysi + xsi / 2.0 + 1.0 / 2.0 - a / 2.0) << 4;

		double ssi = (xsi + ysi) * SQUISH_2D;
		double xi = xsi + ssi;
		double yi = ysi + ssi;

		for (int i = 0; i < 4; i++)
		{
			const LatticePoint2D &lattice = t.lookup2D[index + i];

			double dx = xi + lattice.dx;
			double dy = yi + lattice.dy;

			double attn = 2.0 - (dx * dx) - (dy * dy);
			if (attn <= 0) continue;

			int pxm = (xsb + lattice.xsv) & 1023;
			int pym = (ysb + lattice.ysv) & 1023;
			int gi = m_perm2D[m_perm[pxm] ^ pym];

			double extrp = GRADIENTS_2D[gi] * dx + GRADIENTS_2D[gi + 1] * dy;

			attn *= attn;
			value += attn * attn * extrp;
		}
		return value;
	}

	double OpenSimplexNoise::noise3d(double x, double y, double z) const
	{
		const Tables &t = tables();

		double stretchOffset = (x + y + z) * STRETCH_3D;
		double xs = x + stretchOffset;
		double ys = y + stretchOffset;
		double zs = z + stretchOffset;

		int xsb = fastFloor(xs);
		int ysb = fastFloor(ys);
		int zsb = fastFloor(zs);

		double squishOffset = (xsb + ysb + zsb) * SQUISH_3D;
		double dx0 = x - (xsb + squishOffset);
		double dy0 = y - (ysb + squishOffset);
		double dz0 = z - (zsb + squishOffset);

		double xins = xs - xsb;
		double yins = ys - ysb;
		double zins = zs - zsb;

		double inSum = xins + yins + zins;

		int hash = static_cast<int>(yins - zins + 1)
			| static_cast<int>(xins - yins + 1) << 1
			| static_cast<int>(xins - zins + 1) << 2
			| static_cast<int>(inSum) << 3
			| static_cast<int>(inSum + zins) << 5
			| static_cast<int>(inSum + yins) << 7
			| static_cast<int>(inSum + xins) << 9;

		const Contribution3D *c = (hash >= 0 && hash < 2048) ? t.lookup3D[hash] : nullptr;

		double value = 0.0;
		while (c != nullptr)
		{
			double dx = dx0 + c->dx;
			double dy = dy0 + c->dy;
			double dz = dz0 + c->dz;

			double attn = 2 - dx * dx - dy * dy - dz * dz;
			if (attn > 0)
			{
				int px = xsb + c->xsb;
				int py = ysb + c->ysb;
				int pz = zsb + c->zsb;

				int i = m_perm3D[(m_perm[(m_perm[px & 0x3FF] ^ py) & 0x3FF] ^ pz) & 0x3FF];

				double valuePart = GRADIENTS_3D[i] * dx + GRADIENTS_3D[i + 1] * dy + GRADIENTS_3D[i + 2] * dz;

				attn *= attn;
				value += attn * attn * valuePart;
			}
			c = c->next;
		}
		return value;
	}

	void OpenSimplexNoise::multiEval2D(double x, double y, SimplexData2D &data) const
	{
		const Tables &t = tables();

		double s = STRETCH_2D * (x + y);
		double xs = x + s;
		double ys = y + s;

		int xsb = fastFloor(xs);
		int ysb = fastFloor(ys);
		double xsi = xs - xsb;
		double ysi = ys - ysb;

		int a = static_cast<int>(ysi - xsi + 1);
		int index = (a << 2) | static_cast<int>(xsi + ysi / 2.0 + a / 2.0) << 3 | static_cast<int>(ysi + xsi / 2.0 + 1.0 / 2.0 - a / 2.0) << 4;

		double ssi = (xsi + ysi) * SQUISH_2D;
		double xi = xsi + ssi;
		double yi = ysi + ssi;

		data.clear();

		for (int i = 0; i < 4; i++)
		{
			const LatticePoint2D &lattice = t.lookup2D[index + i];

			double dx = xi + lattice.dx;
			double dy = yi + lattice.dy;
			double attn = 2.0 - (dx * dx) - (dy * dy);
			if (attn <= 0) continue;

			int pxm = (xsb + lattice.xsv) & 1023;
			int pym = (ysb + lattice.ysv) & 1023;
			int giP = m_perm[pxm] ^ pym;
			int gi = m_perm2D[giP];
			double gx = GRADIENTS_2D[gi];
			double gy = GRADIENTS_2D[gi + 1];
			double extrp = gx * dx + gy * dy;
			int giSph2 = m_perm2DSph2[giP];
			data.apply(attn, extrp, gx, gy, giSph2, dx, dy);
		}
	}

	// --- Voronoi -------------------------------------------------------------------------

	void VoronoiResult::toLength(float x, float z, float radius, float &outX, float &outZ) const
	{
		double xDist = x - closestX;
		double zDist = z - closestZ;
		double distance = std::sqrt(xDist * xDist + zDist * zDist);
		xDist *= radius / distance;
		zDist *= radius / distance;
		outX = static_cast<float>(closestX + xDist);
		outZ = static_cast<float>(closestZ + zDist);
	}

	void VoronoiResult::evaluate(const std::vector<double> &points, double x, double z)
	{
		for (size_t i = 0; i + 1 < points.size(); i += 2)
		{
			const double px = points[i];
			const double pz = points[i + 1];
			const double distance = (px - x) * (px - x) + (pz - z) * (pz - z);
			if (distance < shortestDistance)
			{
				nextDistance = shortestDistance;
				shortestDistance = distance;
				closestX = px;
				closestZ = pz;
			}
			else if (distance < nextDistance)
			{
				nextDistance = distance;
			}
		}
	}

	SpacedCellularNoise::SpacedCellularNoise(int64_t seed) : m_xSeed(seed)
	{
		Random seeded(seed);
		m_ySeed = seeded.nextLong();

		// setPoints(): points on the unit torus, none closer than sqrt(minDistanceSq)
		const double minDistanceSq = 0.005;
		Random xRandom(m_xSeed);
		Random yRandom(m_ySeed);
		m_allPoints.reserve(kTotalPoints * 2);
		int count = 0;
		while (count < kTotalPoints)
		{
			const double nx = xRandom.nextDouble();
			const double ny = yRandom.nextDouble();
			double minimal = 1.0;
			for (int i = 0; i < count; i++)
			{
				double xd = std::fabs(nx - m_allPoints[i * 2]);
				if (xd > 0.5) xd = 1.0 - xd;
				double yd = std::fabs(ny - m_allPoints[i * 2 + 1]);
				if (yd > 0.5) yd = 1.0 - yd;
				const double d = xd * xd + yd * yd;
				if (d < minimal) minimal = d;
			}
			if (minimal >= minDistanceSq)
			{
				m_allPoints.push_back(nx);
				m_allPoints.push_back(ny);
				++count;
			}
		}
	}

	const std::vector<double> &SpacedCellularNoise::areaPoints(int ax, int ay)
	{
		// caller holds m_lock
		const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(ax)) << 32) | static_cast<uint32_t>(ay);
		auto it = m_cache.find(key);
		if (it != m_cache.end()) return it->second;

		if (m_cache.size() > 4096) m_cache.clear(); // bounded, like RTG's LimitedArrayCacheMap

		Random random(javaPointHash(ax, ay));
		bool used[kTotalPoints] = {};
		std::vector<double> result;
		result.reserve(kPointsPerTorus * 2);
		int index = 0;
		for (int i = 0; i < kPointsPerTorus; i++)
		{
			const int advance = random.nextInt(kTotalPoints);
			for (int j = 0; j < advance; j++)
			{
				while (used[index])
				{
					index++;
					if (index >= kTotalPoints) index = 0;
				}
			}
			result.push_back(m_allPoints[index * 2] + ax);
			result.push_back(m_allPoints[index * 2 + 1] + ay);
			used[i] = true; // (sic) RTG marks i, not index
		}
		return m_cache.emplace(key, std::move(result)).first->second;
	}

	VoronoiResult SpacedCellularNoise::eval2D(double x, double y)
	{
		x = x / 5.0;
		y = y / 5.0;

		const int xInt = (x > 0.0 ? static_cast<int>(x) : static_cast<int>(x) - 1);
		const int yInt = (y > 0.0 ? static_cast<int>(y) : static_cast<int>(y) - 1);

		VoronoiResult result;
		std::lock_guard<std::mutex> guard(m_lock);

		result.evaluate(areaPoints(xInt, yInt), x, y);

		double distance = y - yInt;
		if (distance != result.nextDistance) result.evaluate(areaPoints(xInt, yInt - 1), x, y);
		distance = x - xInt;
		if (distance != result.nextDistance) result.evaluate(areaPoints(xInt - 1, yInt), x, y);
		distance = yInt - y + 1.0;
		if (distance != result.nextDistance) result.evaluate(areaPoints(xInt, yInt + 1), x, y);
		distance = xInt - x + 1.0;
		if (distance != result.nextDistance) result.evaluate(areaPoints(xInt + 1, yInt), x, y);

		distance = (std::min)(y - yInt, x - xInt);
		if (distance != result.nextDistance) result.evaluate(areaPoints(xInt - 1, yInt - 1), x, y);
		distance = (std::min)(yInt - y + 1.0, x - xInt);
		if (distance != result.nextDistance) result.evaluate(areaPoints(xInt - 1, yInt + 1), x, y);
		distance = (std::min)(yInt - y + 1.0, xInt - x + 1.0);
		if (distance != result.nextDistance) result.evaluate(areaPoints(xInt + 1, yInt + 1), x, y);
		distance = (std::min)(y - yInt, xInt - x + 1.0);
		if (distance != result.nextDistance) result.evaluate(areaPoints(xInt + 1, yInt - 1), x, y);

		return result;
	}
}
