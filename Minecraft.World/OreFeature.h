#pragma once
#include "Feature.h"

class Level;

class OreFeature : public Feature
{
private:
	int tile;
	int count;
	int targetTile;
	int data = 0;	// Ruby Launcher: block metadata for mod ores

	void _init(int tile, int count, int targetTile);
public:
	OreFeature (int tile, int count);
	OreFeature(int tile, int count, int targetTile);
	OreFeature(int tile, int data, int count, int targetTile);	// Ruby Launcher

	virtual bool place(Level *level, Random *random, int x, int y, int z);
};