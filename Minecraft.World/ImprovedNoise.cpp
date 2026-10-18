#include "stdafx.h"
#include "ImprovedNoise.h"
#include <climits>
#include <cstdint>

// Hellish Sight: reproduce Java's numeric semantics exactly so the Far Lands
// appear (around +-12,550,821 blocks) the same way they do in Java.
//  - Java's (int) cast of a double saturates to INT_MIN/INT_MAX (NaN -> 0);
//    C++ static_cast is undefined there and MSVC returns 0x80000000 instead.
//  - Java int arithmetic wraps on overflow (xf-- at INT_MIN -> INT_MAX).
// For every in-range value these are identical to the old code, so existing
// terrain is unchanged.
namespace
{
	inline int javaD2I(double v)
	{
		if (v != v) return 0;
		if (v >= 2147483647.0) return INT_MAX;
		if (v <= -2147483648.0) return INT_MIN;
		return static_cast<int>(v);
	}
	inline int javaDec(int v) { return static_cast<int>(static_cast<uint32_t>(v) - 1u); }
}

ImprovedNoise::ImprovedNoise()
{
	Random random;
	init(&random);
}

ImprovedNoise::ImprovedNoise(Random *random)
{
	init(random);
}

void ImprovedNoise::init(Random *random)
{
	p = new int[512];

    xo = random->nextDouble() * 256;
    yo = random->nextDouble() * 256;
    zo = random->nextDouble() * 256;
    for (int i = 0; i < 256; i++)
	{
        p[i] = i;
    }

    for (int i = 0; i < 256; i++)
	{
        int j = random->nextInt(256 - i) + i;
        int tmp = p[i];
        p[i] = p[j];
        p[j] = tmp;

        p[i + 256] = p[i];
    }
}

ImprovedNoise::~ImprovedNoise()
{
	delete [] p;
}

double ImprovedNoise::noise(double _x, double _y, double _z)
{
        double x = _x + xo;
        double y = _y + yo;
        double z = _z + zo;

        int xf = javaD2I(x);
        int yf = javaD2I(y);
        int zf = javaD2I(z);

        if (x < xf) xf = javaDec(xf);
        if (y < yf) yf = javaDec(yf);
        if (z < zf) zf = javaDec(zf);

        int X = xf & 255, // FIND UNIT CUBE THAT
        Y = yf & 255, // CONTAINS POINT.
        Z = zf & 255;

        x -= xf; // FIND RELATIVE X,Y,Z
        y -= yf; // OF POINT IN CUBE.
        z -= zf;

        double u = x * x * x * (x * (x * 6 - 15) + 10), // COMPUTE FADE CURVES
        v = y * y * y * (y * (y * 6 - 15) + 10), // FOR EACH OF X,Y,Z.
        w = z * z * z * (z * (z * 6 - 15) + 10);

        int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z, // HASH COORDINATES OF
        B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z; // THE 8 CUBE CORNERS,

        return lerp(w, lerp(v, lerp(u, grad(p[AA], x, y, z), // AND ADD
                grad(p[BA], x - 1, y, z)), // BLENDED
                lerp(u, grad(p[AB], x, y - 1, z), // RESULTS
                        grad(p[BB], x - 1, y - 1, z))),// FROM  8
                lerp(v, lerp(u, grad(p[AA + 1], x, y, z - 1), // CORNERS
                        grad(p[BA + 1], x - 1, y, z - 1)), // OF CUBE
                        lerp(u, grad(p[AB + 1], x, y - 1, z - 1), grad(p[BB + 1], x - 1, y - 1, z - 1))));

}

double ImprovedNoise::lerp(double t, double a, double b)
{
   return a + t * (b - a);
}

double ImprovedNoise::grad2(int hash, double x, double z)
{
    int h = hash & 15; // CONVERT LO 4 BITS OF HASH CODE
        
    double u = (1-((h&8)>>3))*x, // INTO 12 GRADIENT DIRECTIONS.
    v = h < 4 ? 0 : h == 12 || h == 14 ? x : z;
                
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

double ImprovedNoise::grad(int hash, double x, double y, double z)
{
    int h = hash & 15; // CONVERT LO 4 BITS OF HASH CODE
        
    double u = h < 8 ? x : y, // INTO 12 GRADIENT DIRECTIONS.
    v = h < 4 ? y : h == 12 || h == 14 ? x : z;
                
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

double ImprovedNoise::getValue(double x, double y)
{
	return noise(x, y, 0);
}

double ImprovedNoise::getValue(double x, double y, double z)
{
	return noise(x, y, z);
}

void ImprovedNoise::add(doubleArray buffer, double _x, double _y, double _z, int xSize, int ySize, int zSize, double xs, double ys, double zs, double pow)
{
    if (ySize==1)
	{
        int A = 0, AA = 0, B = 0, BA = 0;
        double vv0 = 0, vv2 = 0;
        int pp = 0;
        double scale = 1.0 / pow;
        for (int xx = 0; xx < xSize; xx++)
		{
            double x = _x + (xx) * xs + xo;
            int xf = javaD2I(x);
            if (x < xf) xf = javaDec(xf);
            int X = xf & 255;
            x -= xf;
            double u = x * x * x * (x * (x * 6 - 15) + 10);

            for (int zz = 0; zz < zSize; zz++)
			{
                double z = _z + (zz) * zs + zo;
                int zf = javaD2I(z);
                if (z < zf) zf = javaDec(zf);
                int Z = zf & 255;
                z -= zf;
                double w = z * z * z * (z * (z * 6 - 15) + 10);
                    
                A = p[X] + 0;
                AA = p[A] + Z;
                B = p[X + 1] + 0;
                BA = p[B] + Z;
                vv0 = lerp(u, grad2(p[AA], x, z), grad(p[BA], x - 1, 0, z));
                vv2 = lerp(u, grad(p[AA + 1], x, 0, z - 1), grad(p[BA + 1], x - 1, 0, z - 1));

                double val = lerp(w, vv0, vv2);

                buffer[pp++] += val * scale;
            }
        }
        return;
    }
    int pp = 0;
    double scale = 1 / pow;
    int yOld = -1;
    int A = 0, AA = 0, AB = 0, B = 0, BA = 0, BB = 0;
    double vv0 = 0, vv1 = 0, vv2 = 0, vv3 = 0;

    for (int xx = 0; xx < xSize; xx++)
	{
        double x = _x + (xx) * xs + xo;
        int xf = javaD2I(x);
        if (x < xf) xf = javaDec(xf);
        int X = xf & 255;
        x -= xf;
        double u = x * x * x * (x * (x * 6 - 15) + 10);


        for (int zz = 0; zz < zSize; zz++)
		{
            double z = _z + (zz) * zs + zo;
            int zf = javaD2I(z);
            if (z < zf) zf = javaDec(zf);
            int Z = zf & 255;
            z -= zf;
            double w = z * z * z * (z * (z * 6 - 15) + 10);


            for (int yy = 0; yy < ySize; yy++)
			{
                double y = _y + (yy) * ys + yo;
                int yf = javaD2I(y);
                if (y < yf) yf = javaDec(yf);
                int Y = yf & 255;
                y -= yf;
                double v = y * y * y * (y * (y * 6 - 15) + 10);

                if (yy == 0 || Y != yOld)
				{
                    yOld = Y;
                    A = p[X] + Y;
                    AA = p[A] + Z;
                    AB = p[A + 1] + Z;
                    B = p[X + 1] + Y;
                    BA = p[B] + Z;
                    BB = p[B + 1] + Z;
                    vv0 = lerp(u, grad(p[AA], x, y, z), grad(p[BA], x - 1, y, z));
                    vv1 = lerp(u, grad(p[AB], x, y - 1, z), grad(p[BB], x - 1, y - 1, z));
                    vv2 = lerp(u, grad(p[AA + 1], x, y, z - 1), grad(p[BA + 1], x - 1, y, z - 1));
                    vv3 = lerp(u, grad(p[AB + 1], x, y - 1, z - 1), grad(p[BB + 1], x - 1, y - 1, z - 1));
                }


                double v0 = lerp(v, vv0, vv1);
                double v1 = lerp(v, vv2, vv3);
                double val = lerp(w, v0, v1);

                buffer[pp++] += val * scale;
            }
        }
	}
}
