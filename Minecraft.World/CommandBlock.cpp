#include "stdafx.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.redstone.h"
#include "net.minecraft.world.level.tile.entity.h"
#include "net.minecraft.world.entity.player.h"
#include "Facing.h"
#include "Mth.h"
#include "CommandBlock.h"

CommandBlock::CommandBlock(int id, EType type) : BaseEntityTile(id, Material::metal, isSolidRender()), m_type(type)
{
}

shared_ptr<TileEntity> CommandBlock::newTileEntity(Level *level)
{
	return std::make_shared<CommandBlockEntity>();
}

int CommandBlock::tintColour() const
{
	// One texture for the three blocks: the repeating one is tinted purple and the
	// chain one green, like their Java textures.
	switch (m_type)
	{
	case eType_Repeating: return 0xB088FF;
	case eType_Chain: return 0x8CF0A8;
	default: return 0xFFFFFF;
	}
}

int CommandBlock::getColor(int auxData)
{
	return tintColour();
}

int CommandBlock::getColor(LevelSource *level, int x, int y, int z)
{
	return tintColour();
}

int CommandBlock::getColor(LevelSource *level, int x, int y, int z, int data)
{
	return tintColour();
}

void CommandBlock::neighborChanged(Level *level, int x, int y, int z, int type)
{
	if (level->isClientSide) return;
	shared_ptr<CommandBlockEntity> block = dynamic_pointer_cast<CommandBlockEntity>(level->getTileEntity(x, y, z));
	if (block == nullptr) return;

	const bool signal = level->hasNeighborSignal(x, y, z);
	const bool was = block->powered;
	block->powered = signal;
	if (signal && !was && !block->autoActive && m_type != eType_Chain)
	{
		level->addToTickNextTick(x, y, z, id, getTickDelay(level));
	}
}

bool CommandBlock::execute(Level *level, int x, int y, int z, shared_ptr<CommandBlockEntity> block)
{
	const int64_t now = level->getGameTime();
	if (block->lastExecutionTick == now) return block->getSuccessCount() > 0;	// once per tick
	block->lastExecutionTick = now;

	const int data = level->getData(x, y, z);
	if (block->conditional || (data & CONDITIONAL_BIT) != 0)
	{
		const int facing = getFacingFromData(data);
		const int bx = x - Facing::STEP_X[facing], by = y - Facing::STEP_Y[facing], bz = z - Facing::STEP_Z[facing];
		shared_ptr<CommandBlockEntity> behind = dynamic_pointer_cast<CommandBlockEntity>(level->getTileEntity(bx, by, bz));
		if (behind == nullptr || behind->getSuccessCount() <= 0)
		{
			block->setSuccessCount(0);
			level->updateNeighbourForOutputSignal(x, y, z, level->getTile(x, y, z));
			return false;
		}
	}

	block->setSuccessCount(block->performCommand(level));
	level->updateNeighbourForOutputSignal(x, y, z, level->getTile(x, y, z));
	block->setChanged();
	return block->getSuccessCount() > 0;
}

void CommandBlock::runChain(Level *level, int x, int y, int z)
{
	int cx = x, cy = y, cz = z;
	for (int i = 0; i < MAX_CHAIN; i++)
	{
		const int facing = getFacingFromData(level->getData(cx, cy, cz));
		cx += Facing::STEP_X[facing];
		cy += Facing::STEP_Y[facing];
		cz += Facing::STEP_Z[facing];
		if (cy < 0 || cy >= Level::maxBuildHeight) return;
		if (level->getTile(cx, cy, cz) != CHAIN_ID) return;
		shared_ptr<CommandBlockEntity> next = dynamic_pointer_cast<CommandBlockEntity>(level->getTileEntity(cx, cy, cz));
		if (next == nullptr) return;
		if (!next->autoActive && !next->powered) return;
		if (next->lastExecutionTick == level->getGameTime()) return;	// loop
		execute(level, cx, cy, cz, next);
	}
}

void CommandBlock::tick(Level *level, int x, int y, int z, Random *random)
{
	if (level->isClientSide) return;
	shared_ptr<CommandBlockEntity> block = dynamic_pointer_cast<CommandBlockEntity>(level->getTileEntity(x, y, z));
	if (block == nullptr) return;

	switch (m_type)
	{
	case eType_Impulse:
		execute(level, x, y, z, block);
		runChain(level, x, y, z);
		break;
	case eType_Repeating:
		if (block->autoActive || block->powered)
		{
			execute(level, x, y, z, block);
			runChain(level, x, y, z);
			level->addToTickNextTick(x, y, z, id, 1);
		}
		break;
	case eType_Chain:
		break;	// chains run from the block before them
	}
}

void CommandBlock::onSettingsChanged(Level *level, int x, int y, int z)
{
	if (level == nullptr || level->isClientSide) return;
	const int tile = level->getTile(x, y, z);
	CommandBlock *cb = dynamic_cast<CommandBlock *>(Tile::tiles[tile]);
	shared_ptr<CommandBlockEntity> block = dynamic_pointer_cast<CommandBlockEntity>(level->getTileEntity(x, y, z));
	if (cb == nullptr || block == nullptr) return;
	const int data = level->getData(x, y, z);
	level->setData(x, y, z, block->conditional ? (data | CONDITIONAL_BIT) : (data & ~CONDITIONAL_BIT), Tile::UPDATE_CLIENTS);
	block->powered = level->hasNeighborSignal(x, y, z);
	if (cb->m_type != eType_Chain && block->autoActive)
	{
		level->addToTickNextTick(x, y, z, tile, 1);
	}
}

int CommandBlock::getTickDelay(Level *level)
{
	return 1;
}

bool CommandBlock::use(Level *level, int x, int y, int z, shared_ptr<Player> player, int clickedFace, float clickX, float clickY, float clickZ, bool soundOnly)
{
	if (soundOnly) return false;
	shared_ptr<CommandBlockEntity> amce = dynamic_pointer_cast<CommandBlockEntity>(level->getTileEntity(x, y, z));

	if (amce != nullptr && player->abilities.instabuild)
	{
		player->openTextEdit(amce);
		return true;
	}
	return false;
}

bool CommandBlock::hasAnalogOutputSignal()
{
	return true;
}

int CommandBlock::getAnalogOutputSignal(Level *level, int x, int y, int z, int dir)
{
	shared_ptr<CommandBlockEntity> block = dynamic_pointer_cast<CommandBlockEntity>(level->getTileEntity(x, y, z));
	if (block != nullptr)
	{
		const int s = block->getSuccessCount();
		return s > 15 ? 15 : (s < 0 ? 0 : s);
	}
	return Redstone::SIGNAL_NONE;
}

void CommandBlock::setPlacedBy(Level *level, int x, int y, int z, shared_ptr<LivingEntity> by, shared_ptr<ItemInstance> itemInstance)
{
	// facing: away from the player (where the chain continues)
	int facing = 3;
	if (by != nullptr)
	{
		if (by->xRot > 45.0f) facing = Facing::DOWN;
		else if (by->xRot < -45.0f) facing = Facing::UP;
		else
		{
			const int dir = Mth::floor(by->yRot * 4.0f / 360.0f + 0.5) & 3;
			static const int map[4] = { Facing::SOUTH, Facing::WEST, Facing::NORTH, Facing::EAST };
			facing = map[dir];
		}
	}
	level->setData(x, y, z, facing, Tile::UPDATE_CLIENTS);

	shared_ptr<CommandBlockEntity> cblock = dynamic_pointer_cast<CommandBlockEntity>(level->getTileEntity(x, y, z));
	if (cblock != nullptr && itemInstance != nullptr && itemInstance->hasCustomHoverName())
	{
		cblock->setName(itemInstance->getHoverName());
	}
	if (cblock != nullptr && m_type == eType_Chain)
	{
		cblock->autoActive = true;	// Java places chain blocks as "Always Active"
	}
}

int CommandBlock::getResourceCount(Random *random)
{
	return 0;
}
