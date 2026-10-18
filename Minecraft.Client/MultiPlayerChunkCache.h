#pragma once
#include "../Minecraft.World/net.minecraft.world.level.h"
#include "../Minecraft.World/net.minecraft.world.level.chunk.h"
#include "../Minecraft.World/RandomLevelSource.h"
#include <deque>
#include <unordered_map>

using namespace std;
class ServerChunkCache;

// 4J - various alterations here to make this thread safe, and operate as a fixed sized cache
class MultiPlayerChunkCache : public ChunkSource
{
	friend class LevelRenderer;
private:
	LevelChunk *emptyChunk;
	LevelChunk *waterChunk;

	vector<LevelChunk *> loadedChunkList;

	ChunkTable m_table;			// Hellish Sight: sparse table (was LevelChunk **cache[XZSIZE*XZSIZE])
	// 4J - added for multithreaded support
	CRITICAL_SECTION m_csLoadCreate;
	// 4J - size of cache is defined by size of one side - must be even
	int XZSIZE;
	int XZOFFSET;
	ChunkFlagGrid m_hasData;	// Hellish Sight: sparse flags (was bool hasData[XZSIZE*XZSIZE])

	Level *level;

	// Hellish Sight: the 4J client kept every chunk it ever received ("keep chunk in cache
	// with structural data intact"), bounded only by the world size. Chunks the server told
	// us to drop are now released: after DROP_GRACE_MS they leave the table, and after a
	// further FREE_GRACE_MS (render/update threads may still hold the pointer) they are freed.
	// The server re-sends a chunk when it comes back into view.
	unordered_map<LevelChunk *, int64_t> m_droppedAt;			// guarded by m_csLoadCreate
	deque<pair<LevelChunk *, int64_t>> m_graveyard;				// main thread only
	int m_evictTickCounter = 0;
	static const int64_t DROP_GRACE_MS = 20 * 1000;
	static const int64_t FREE_GRACE_MS = 30 * 1000;
	static const int EVICT_EVERY_TICKS = 40;
	void evictDroppedChunks();

public:
	MultiPlayerChunkCache(Level *level);
	~MultiPlayerChunkCache();
	virtual bool hasChunk(int x, int z);
	virtual bool reallyHasChunk(int x, int z);
	virtual void drop(int x, int z);
	virtual LevelChunk *create(int x, int z);
	virtual LevelChunk *getChunk(int x, int z);
	virtual bool save(bool force, ProgressListener *progressListener);
	virtual bool tick();
	virtual bool shouldSave();
	virtual void postProcess(ChunkSource *parent, int x, int z);
	virtual wstring gatherStats();
	virtual vector<Biome::MobSpawnerData *> *getMobsAt(MobCategory *mobCategory, int x, int y, int z);
	virtual TilePos *findNearestMapFeature(Level *level, const wstring &featureName, int x, int y, int z);
	virtual void recreateLogicStructuresForChunk(int chunkX, int chunkZ);
	virtual void dataReceived(int x, int z);	// 4J added

	virtual ChunkTable *getChunkTable() { return &m_table; }	// Hellish Sight
};