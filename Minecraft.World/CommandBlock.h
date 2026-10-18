#pragma once

#include "BaseEntityTile.h"

class CommandBlockEntity;

// Hellish Sight: Java 1.9+ command blocks.
//   id 137 command block (impulse), 210 repeating, 211 chain.
//   data bits 0-2: facing (Facing::*), bit 3: conditional (mirrors the tile entity).
class CommandBlock : public BaseEntityTile
{
public:
	enum EType
	{
		eType_Impulse = 0,
		eType_Repeating,
		eType_Chain,
	};

	static const int REPEATING_ID = 210;
	static const int CHAIN_ID = 211;
	static const int CONDITIONAL_BIT = 8;
	static const int MAX_CHAIN = 65536;

	CommandBlock(int id, EType type = eType_Impulse);

	EType getType() const { return m_type; }
	static int getFacingFromData(int data) { return data & 7; }

	virtual shared_ptr<TileEntity> newTileEntity(Level *level);
	virtual void neighborChanged(Level *level, int x, int y, int z, int type);
	virtual void tick(Level *level, int x, int y, int z, Random *random);
	virtual int getTickDelay(Level *level);
	virtual bool use(Level *level, int x, int y, int z, shared_ptr<Player> player, int clickedFace, float clickX, float clickY, float clickZ, bool soundOnly = false);
	virtual bool hasAnalogOutputSignal();
	virtual int getAnalogOutputSignal(Level *level, int x, int y, int z, int dir);
	virtual void setPlacedBy(Level *level, int x, int y, int z, shared_ptr<LivingEntity> by, shared_ptr<ItemInstance> itemInstance);
	virtual int getResourceCount(Random *random);
	virtual int getColor(int auxData);
	virtual int getColor(LevelSource *level, int x, int y, int z);
	virtual int getColor(LevelSource *level, int x, int y, int z, int data);

	// Called after the command / mode was edited (Always Active starts running at once)
	static void onSettingsChanged(Level *level, int x, int y, int z);

private:
	EType m_type;

	static bool execute(Level *level, int x, int y, int z, shared_ptr<CommandBlockEntity> block);
	static void runChain(Level *level, int x, int y, int z);
	int tintColour() const;
};
