#pragma once
#include "Feature.h"
class Random;

#include "net.minecraft.world.level.tile.h"

// Ruby Launcher: trunk/leaf tiles are parameters so mods can grow trees from their own blocks (defaults = vanilla).
class SwampTreeFeature : public Feature
{
private:
	const int baseHeight;
	const int trunkType;
	const int leafType;
	const int trunkTile;
	const int leafTile;

public:
	SwampTreeFeature();
	SwampTreeFeature(bool doUpdate, int baseHeight, int trunkType, int leafType, int trunkTile = Tile::treeTrunk_Id, int leafTile = Tile::leaves_Id);
	virtual bool place(Level *level, Random *random, int x, int y, int z);
private:
	void addVine(Level *level, int xx, int yy, int zz, int dir);
};