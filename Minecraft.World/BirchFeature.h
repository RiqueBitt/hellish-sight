#pragma once
#include "Feature.h"
#include "net.minecraft.world.level.tile.h"

class BirchFeature : public Feature
{
private:
    bool useTallBirch;
    const int baseHeight;
    const int trunkType;
    const int leafType;
    const int trunkTile;
    const int leafTile;

public:
    BirchFeature(bool doUpdate, bool useTallBirch = false);
    // Ruby Launcher: custom trunk/leaf blocks for mod trees
    BirchFeature(bool doUpdate, int baseHeight, int trunkType, int leafType, int trunkTile = Tile::treeTrunk_Id, int leafTile = Tile::leaves_Id);

    virtual bool place(Level *level, Random *random, int x, int y, int z) override;
};
