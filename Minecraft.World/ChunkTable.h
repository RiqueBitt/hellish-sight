#pragma once

// SparseChunkGrid / ChunkTable - sparse storage for per-chunk pointers (Hellish Sight).
//
// Replaces the flat `T *cache[XZSIZE * XZSIZE]` arrays, whose size
// grows with the square of the world width (a ±30M-block world would need
// terabytes). Memory is only allocated around chunks that actually exist.
//
// Layout (three levels, all power-of-two):
//   Super  : 32 x 32 Regions  (1024 x 1024 chunks), found through a hash table
//   Region : 32 x 32 slots    (one std::atomic<T*> per chunk, 8 KB)
//
// Concurrency contract (matches what the 4J code expects from the old array):
//   * get() is lock-free and may run on any thread at any time.
//   * slot() returns a stable address for the lifetime of the table, so the
//     existing InterlockedCompareExchange-style code keeps working unchanged
//     through compareExchange().
//   * Regions/Supers are created under a lock and are NEVER freed or moved
//     until the table is destroyed. Readers can therefore never observe a
//     dangling pointer. (An empty region costs 8 KB; reclaiming them would
//     need a quiescent point and is intentionally not done here.)
//   * The table never owns the chunks; callers keep their existing ownership.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

class LevelChunk;

// Generic sparse grid of atomic T* indexed by chunk coordinates.
template <typename T>
class SparseChunkGrid
{
public:
	static const int REGION_SHIFT = 5;                         // 32 chunks
	static const int REGION_SIZE = 1 << REGION_SHIFT;
	static const int REGION_MASK = REGION_SIZE - 1;
	static const int SUPER_SHIFT = 5;                          // 32 regions
	static const int SUPER_SIZE = 1 << SUPER_SHIFT;
	static const int SUPER_MASK = SUPER_SIZE - 1;
	static const int SUPER_CHUNK_SHIFT = REGION_SHIFT + SUPER_SHIFT; // 1024 chunks

	SparseChunkGrid()
	{
		Directory *initial = newDirectory(kInitialDirCapacity);
		m_allDirs.push_back(initial);
		m_dir.store(initial, std::memory_order_release);
	}

	~SparseChunkGrid()
	{
		// Every directory ever published is kept in m_allDirs; supers are
		// shared between them, so free supers once (from the newest).
		Directory *dir = m_dir.load(std::memory_order_acquire);
		for (uint32_t i = 0; i < dir->capacity; ++i)
		{
			Super *super = dir->entries[i].load(std::memory_order_relaxed);
			if (super == nullptr) continue;
			for (int r = 0; r < SUPER_SIZE * SUPER_SIZE; ++r)
			{
				delete super->regions[r].load(std::memory_order_relaxed);
			}
			delete super;
		}
		for (Directory *d : m_allDirs) freeDirectory(d);
	}

	SparseChunkGrid(const SparseChunkGrid &) = delete;
	SparseChunkGrid &operator=(const SparseChunkGrid &) = delete;

	// Lock-free lookup. Returns nullptr when nothing is stored at (cx, cz).
	T *get(int cx, int cz) const
	{
		Region *region = findRegion(cx, cz);
		if (region == nullptr) return nullptr;
		return region->slots[slotIndex(cx, cz)].load(std::memory_order_acquire);
	}

	// Stable address of the slot for (cx, cz), creating the containing
	// region on demand. Never returns nullptr.
	std::atomic<T *> *slot(int cx, int cz)
	{
		Region *region = findRegion(cx, cz);
		if (region == nullptr) region = createRegion(cx, cz);
		return &region->slots[slotIndex(cx, cz)];
	}

	// Same as slot() but does not create anything; nullptr if absent.
	std::atomic<T *> *findSlot(int cx, int cz) const
	{
		Region *region = findRegion(cx, cz);
		return region != nullptr ? &region->slots[slotIndex(cx, cz)] : nullptr;
	}

	void set(int cx, int cz, T *chunk)
	{
		if (chunk == nullptr)
		{
			std::atomic<T *> *s = findSlot(cx, cz);
			if (s != nullptr) s->store(nullptr, std::memory_order_release);
			return;
		}
		slot(cx, cz)->store(chunk, std::memory_order_release);
	}

	// Atomically replace `expected` with `desired`. Returns true on success
	// (same semantics as InterlockedCompareExchange(...) == expected).
	bool compareExchange(int cx, int cz, T *expected, T *desired)
	{
		return slot(cx, cz)->compare_exchange_strong(expected, desired,
			std::memory_order_acq_rel, std::memory_order_acquire);
	}

	// Atomically take whatever is stored at (cx, cz), leaving nullptr.
	T *take(int cx, int cz)
	{
		std::atomic<T *> *s = findSlot(cx, cz);
		return s != nullptr ? s->exchange(nullptr, std::memory_order_acq_rel) : nullptr;
	}

	// Visits every non-null chunk. Not synchronised against concurrent
	// writers: use only at quiescent points (shutdown, full save).
	template <typename F>
	void forEach(F fn) const
	{
		Directory *dir = m_dir.load(std::memory_order_acquire);
		for (uint32_t i = 0; i < dir->capacity; ++i)
		{
			Super *super = dir->entries[i].load(std::memory_order_acquire);
			if (super == nullptr) continue;
			for (int r = 0; r < SUPER_SIZE * SUPER_SIZE; ++r)
			{
				Region *region = super->regions[r].load(std::memory_order_acquire);
				if (region == nullptr) continue;
				for (int s = 0; s < REGION_SIZE * REGION_SIZE; ++s)
				{
					T *chunk = region->slots[s].load(std::memory_order_acquire);
					if (chunk != nullptr) fn(chunk);
				}
			}
		}
	}

	// Number of allocated regions (diagnostics / memory stats).
	size_t regionCount() const { return m_regionCount.load(std::memory_order_relaxed); }

private:
	struct Region
	{
		std::atomic<T *> slots[REGION_SIZE * REGION_SIZE];
		Region()
		{
			for (auto &s : slots) s.store(nullptr, std::memory_order_relaxed);
		}
	};

	struct Super
	{
		int sx, sz;
		std::atomic<Region *> regions[SUPER_SIZE * SUPER_SIZE];
		Super(int x, int z) : sx(x), sz(z)
		{
			for (auto &r : regions) r.store(nullptr, std::memory_order_relaxed);
		}
	};

	// Open-addressing hash of Super*, insert-only. Grows by publishing a new,
	// larger directory; old directories stay alive (readers may hold them).
	struct Directory
	{
		uint32_t capacity; // power of two
		std::atomic<Super *> *entries;
	};

	static const uint32_t kInitialDirCapacity = 64;

	static int floorShift(int v, int shift) { return v >> shift; } // arithmetic shift == floor for negatives

	static int slotIndex(int cx, int cz)
	{
		return ((cx & REGION_MASK) << REGION_SHIFT) | (cz & REGION_MASK);
	}

	static int regionIndexInSuper(int cx, int cz)
	{
		const int rx = floorShift(cx, REGION_SHIFT) & SUPER_MASK;
		const int rz = floorShift(cz, REGION_SHIFT) & SUPER_MASK;
		return (rx << SUPER_SHIFT) | rz;
	}

	static uint32_t hashSuper(int sx, int sz)
	{
		uint64_t k = (static_cast<uint64_t>(static_cast<uint32_t>(sx)) << 32) | static_cast<uint32_t>(sz);
		k ^= k >> 33; k *= 0xff51afd7ed558ccdULL;
		k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ULL;
		k ^= k >> 33;
		return static_cast<uint32_t>(k);
	}

	static Directory *newDirectory(uint32_t capacity)
	{
		Directory *d = new Directory;
		d->capacity = capacity;
		d->entries = new std::atomic<Super *>[capacity];
		for (uint32_t i = 0; i < capacity; ++i) d->entries[i].store(nullptr, std::memory_order_relaxed);
		return d;
	}

	static void freeDirectory(Directory *d)
	{
		delete[] d->entries;
		delete d;
	}

	static Super *probe(Directory *dir, int sx, int sz)
	{
		const uint32_t mask = dir->capacity - 1;
		for (uint32_t i = hashSuper(sx, sz) & mask, n = 0; n < dir->capacity; i = (i + 1) & mask, ++n)
		{
			Super *super = dir->entries[i].load(std::memory_order_acquire);
			if (super == nullptr) return nullptr;
			if (super->sx == sx && super->sz == sz) return super;
		}
		return nullptr;
	}

	Region *findRegion(int cx, int cz) const
	{
		const int sx = floorShift(cx, SUPER_CHUNK_SHIFT);
		const int sz = floorShift(cz, SUPER_CHUNK_SHIFT);
		Super *super = probe(m_dir.load(std::memory_order_acquire), sx, sz);
		if (super == nullptr) return nullptr;
		return super->regions[regionIndexInSuper(cx, cz)].load(std::memory_order_acquire);
	}

	// Caller holds m_lock.
	void insertSuperLocked(Directory *dir, Super *super)
	{
		const uint32_t mask = dir->capacity - 1;
		for (uint32_t i = hashSuper(super->sx, super->sz) & mask;; i = (i + 1) & mask)
		{
			if (dir->entries[i].load(std::memory_order_relaxed) == nullptr)
			{
				dir->entries[i].store(super, std::memory_order_release);
				return;
			}
		}
	}

	Region *createRegion(int cx, int cz)
	{
		std::lock_guard<std::mutex> guard(m_lock);

		const int sx = floorShift(cx, SUPER_CHUNK_SHIFT);
		const int sz = floorShift(cz, SUPER_CHUNK_SHIFT);

		Directory *dir = m_dir.load(std::memory_order_acquire);
		Super *super = probe(dir, sx, sz);
		if (super == nullptr)
		{
			// Keep load factor <= 50% so probes stay short and always terminate.
			if ((m_superCount + 1) * 2 > dir->capacity)
			{
				Directory *bigger = newDirectory(dir->capacity * 2);
				for (uint32_t i = 0; i < dir->capacity; ++i)
				{
					Super *s = dir->entries[i].load(std::memory_order_relaxed);
					if (s != nullptr) insertSuperLocked(bigger, s);
				}
				m_allDirs.push_back(bigger);
				m_dir.store(bigger, std::memory_order_release);
				dir = bigger;
			}
			super = new Super(sx, sz);
			insertSuperLocked(dir, super);
			++m_superCount;
		}

		std::atomic<Region *> &regionSlot = super->regions[regionIndexInSuper(cx, cz)];
		Region *region = regionSlot.load(std::memory_order_acquire);
		if (region == nullptr)
		{
			region = new Region();
			regionSlot.store(region, std::memory_order_release);
			m_regionCount.fetch_add(1, std::memory_order_relaxed);
		}
		return region;
	}

	std::atomic<Directory *> m_dir;
	std::vector<Directory *> m_allDirs;     // every directory ever published; guarded by m_lock
	uint32_t m_superCount = 0;              // guarded by m_lock
	std::atomic<size_t> m_regionCount{0};
	std::mutex m_lock;
};

// The chunk cache table used by ServerChunkCache / MultiPlayerChunkCache / Level.
typedef SparseChunkGrid<LevelChunk> ChunkTable;

// Presence flags (e.g. "client has received data for this chunk").
typedef SparseChunkGrid<const char> ChunkFlagGrid;
inline const char *chunkFlagSet() { static const char marker = 1; return &marker; }
