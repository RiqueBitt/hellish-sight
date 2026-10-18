#pragma once

#include "TileEntity.h"
#include "CommandSender.h"

class ChatMessageComponent;

class CommandBlockEntity : public TileEntity, public CommandSender
{
public:
	eINSTANCEOF GetType() { return eTYPE_COMMANDBLOCKTILEENTITY; }
	static TileEntity *create() { return new CommandBlockEntity(); }

	// 4J Added
	virtual shared_ptr<TileEntity> clone();

private:
	int successCount;
	wstring command;
	wstring name;

public:
	// Hellish Sight: Java 1.9+ command block state
	bool autoActive = false;		// "Always Active" (no redstone needed)
	bool conditional = false;		// runs only if the block behind succeeded
	bool powered = false;			// receiving redstone
	wstring lastOutput;				// "Previous Output"
	int64_t lastExecutionTick = -1;	// avoids running twice in one tick (chains / loops)

	// Executes a command for a command block (set by the client/server code: HSCommands)
	typedef int (*Executor)(CommandBlockEntity *block, Level *level, const wstring &command, wstring &output);
	static Executor s_executor;

public:
	CommandBlockEntity();

	void setCommand(const wstring &command);
	wstring getCommand();
	int performCommand(Level *level);
	wstring getName();
	void setName(const wstring &name);
	virtual void sendMessage(const wstring& message, ChatPacket::EChatPacketMessage type = ChatPacket::e_ChatCustom, int customData = -1, const wstring& additionalMessage = L"");
	virtual bool hasPermission(EGameCommand command);
	//void sendMessage(ChatMessageComponent *message);
	//bool hasPermission(int permissionLevel, const wstring &commandName);
	void save(CompoundTag *tag);
	void load(CompoundTag *tag);
	Pos *getCommandSenderWorldPosition();
	Level *getCommandSenderWorld();
	shared_ptr<Packet> getUpdatePacket();
	int getSuccessCount();
	void setSuccessCount(int successCount);
};