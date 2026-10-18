#pragma once
// Hellish Sight: block models made of boxes ("elements", the Java block model format), used by
// mod blocks such as the Mekanism machines. Mods register a model per tile id; tiles that have
// one return Tile::SHAPE_HS_MODEL and TileRenderer draws the boxes in the world, in the
// inventory, in the hand and for dropped items.
//
// Coordinates are in pixels (0..16) like the Java format. Faces: 0 down, 1 up, 2 north,
// 3 south, 4 west, 5 east (the Facing order). A model faces north; blocks that store a
// horizontal facing in data & 7 (2..5) are turned around the Y axis.

#include <string>
#include <vector>

class Icon;
class LevelSource;

struct HSModelFace
{
	bool present = false;
	int tex = 0;					// index into HSModel::icons
	float uv[4] = { 0, 0, 16, 16 };	// u0, v0, u1, v1 in pixels of the texture
	int rotation = 0;				// 0, 90, 180, 270
	int cull = -1;					// face to cull against (-1: never culled)
	bool tint = false;
};

struct HSModelElement
{
	float from[3] = { 0, 0, 0 };
	float to[3] = { 16, 16, 16 };
	int rotAxis = -1;				// 0 x, 1 y, 2 z
	float rotAngle = 0.0f;			// degrees
	float rotCos = 1.0f, rotSin = 0.0f;	// cached from rotAngle (set by the loader)
	float origin[3] = { 8, 8, 8 };
	bool rescale = false;
	bool shade = true;
	int emission = 0;				// 0..15: drawn at least this bright
	HSModelFace faces[6];
};

struct HSModel
{
	std::vector<std::wstring> iconNames;
	std::vector<Icon *> icons;
	std::vector<HSModelElement> elements;
	bool rotateWithFacing = true;
	int activeModel = -1;			// model used while data & 8 is set (machines that are running)

	// connectors (cables, pipes): a core box plus one arm per connected side
	bool connector = false;
	float thickness = 6.0f;			// pixels
	float coreUV[4] = { 5, 5, 11, 11 };	// texture 0: faces of the core
	float armU[2] = { 5, 11 };			// texture 1 (or 0): sides of the arms, v runs along the arm (0..16)
	bool (*connects)(LevelSource *level, int x, int y, int z, int face) = nullptr;
};

namespace HSModels
{
	inline std::vector<HSModel *> &all()
	{
		static std::vector<HSModel *> list;
		return list;
	}

	inline std::vector<int> &tileIndex()
	{
		static std::vector<int> index(4096, -1);
		return index;
	}

	inline int add(HSModel *model)
	{
		all().push_back(model);
		return static_cast<int>(all().size()) - 1;
	}

	inline void setForTile(int tileId, int modelIndex)
	{
		if (tileId >= 0 && tileId < static_cast<int>(tileIndex().size())) tileIndex()[tileId] = modelIndex;
	}

	inline bool hasModel(int tileId)
	{
		return tileId >= 0 && tileId < static_cast<int>(tileIndex().size()) && tileIndex()[tileId] >= 0;
	}

	inline HSModel *forTile(int tileId, int data = 0)
	{
		if (!hasModel(tileId)) return nullptr;
		HSModel *m = all()[tileIndex()[tileId]];
		if ((data & 8) && m->activeModel >= 0 && m->activeModel < static_cast<int>(all().size())) m = all()[m->activeModel];
		return m;
	}
}
