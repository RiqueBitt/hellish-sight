#include "stdafx.h"
#include "net.minecraft.network.packet.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.commands.h"
#include "../Minecraft.Client/MinecraftServer.h"
#include "CommandBlockEntity.h"

CommandBlockEntity::Executor CommandBlockEntity::s_executor = nullptr;

CommandBlockEntity::CommandBlockEntity()
{
	successCount = 0;
	command = L"";
	name = L"@";
}

void CommandBlockEntity::setCommand(const wstring &command)
{
	this->command = command;
	setChanged();
}

wstring CommandBlockEntity::getCommand()
{
	return command;
}

int CommandBlockEntity::performCommand(Level *level)
{
	// Hellish Sight: commands run through HSCommands (Minecraft.Client/HSCommands)
	if (level == nullptr || level->isClientSide || s_executor == nullptr || command.empty())
	{
		return 0;
	}
	wstring output;
	const int result = s_executor(this, level, command, output);
	lastOutput = output;
	return result;
}

wstring CommandBlockEntity::getName()
{
	return name;
}

void CommandBlockEntity::setName(const wstring &name)
{
	this->name = name;
}

void CommandBlockEntity::sendMessage(const wstring& message, ChatPacket::EChatPacketMessage type, int customData , const wstring& additionalMessage)
{
}

bool CommandBlockEntity::hasPermission(EGameCommand command)
{
	return false;
}

void CommandBlockEntity::save(CompoundTag *tag)
{
	TileEntity::save(tag);
	tag->putString(L"Command", command);
	tag->putInt(L"SuccessCount", successCount);
	tag->putString(L"CustomName", name);
	tag->putBoolean(L"auto", autoActive);
	tag->putBoolean(L"conditional", conditional);
	tag->putBoolean(L"powered", powered);
	tag->putString(L"LastOutput", lastOutput);
}

void CommandBlockEntity::load(CompoundTag *tag)
{
	TileEntity::load(tag);
	command = tag->getString(L"Command");
	successCount = tag->getInt(L"SuccessCount");
	if (tag->contains(L"CustomName")) name = tag->getString(L"CustomName");
	autoActive = tag->getBoolean(L"auto");
	conditional = tag->getBoolean(L"conditional");
	powered = tag->getBoolean(L"powered");
	lastOutput = tag->getString(L"LastOutput");
}

Pos *CommandBlockEntity::getCommandSenderWorldPosition()
{
	return new Pos(x, y, z);
}

Level *CommandBlockEntity::getCommandSenderWorld()
{
	return getLevel();
}

shared_ptr<Packet> CommandBlockEntity::getUpdatePacket()
{
	CompoundTag *tag = new CompoundTag();
	save(tag);
	return std::make_shared<TileEntityDataPacket>(x, y, z, TileEntityDataPacket::TYPE_ADV_COMMAND, tag);
}

int CommandBlockEntity::getSuccessCount()
{
	return successCount;
}

void CommandBlockEntity::setSuccessCount(int successCount)
{
	this->successCount = successCount;
}

// 4J Added
shared_ptr<TileEntity> CommandBlockEntity::clone()
{
	shared_ptr<CommandBlockEntity> result = std::make_shared<CommandBlockEntity>();
	TileEntity::clone(result);

	result->successCount = successCount;
	result->command = command;
	result->name = name;
	result->autoActive = autoActive;
	result->conditional = conditional;
	result->powered = powered;
	result->lastOutput = lastOutput;

	return result;
}