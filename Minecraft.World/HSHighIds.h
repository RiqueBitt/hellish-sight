#pragma once
// Hellish Sight: extended tile ids for chunks.
//
// LCE stores tile ids in 8 bits (CompressedTileStorage), but mod tiles live at ids
// 256..4095. The low byte stays in the normal storage; the high byte of each position
// lives here, allocated per 16-block-high section only where it is needed (in practice
// only sections holding mod blocks). Reads are lock free; the arrays are only freed when
// the last chunk sharing this object releases it, through a small retirement queue so
// render threads that are still reading never touch freed memory.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

class HSHighIds
{
public:
	static const int SECTIONS = 16;			// 256 / 16
	static const int SECTION_SIZE = 4096;	// 16 x 16 x 16

	HSHighIds() : m_refs(1), m_any(false)
	{
		for (int i = 0; i < SECTIONS; i++) m_sec[i].store(nullptr);
	}

	~HSHighIds()
	{
		for (int i = 0; i < SECTIONS; i++) delete[] m_sec[i].load();
	}

	static int index(int x, int y, int z) { return ((y & 15) << 8) | ((z & 15) << 4) | (x & 15); }

	bool any() const { return m_any.load(std::memory_order_relaxed); }

	int get(int x, int y, int z) const
	{
		if (!m_any.load(std::memory_order_relaxed) || y < 0 || y >= SECTIONS * 16) return 0;
		const uint8_t *s = m_sec[y >> 4].load(std::memory_order_acquire);
		return s ? s[index(x, y, z)] : 0;
	}

	void set(int x, int y, int z, int high)
	{
		if (y < 0 || y >= SECTIONS * 16) return;
		uint8_t *s = m_sec[y >> 4].load(std::memory_order_acquire);
		if (s == nullptr)
		{
			if (high == 0) return;
			uint8_t *fresh = new uint8_t[SECTION_SIZE];
			memset(fresh, 0, SECTION_SIZE);
			uint8_t *expected = nullptr;
			if (m_sec[y >> 4].compare_exchange_strong(expected, fresh)) s = fresh;
			else { delete[] fresh; s = expected; }
		}
		s[index(x, y, z)] = static_cast<uint8_t>(high);
		if (high != 0) m_any.store(true, std::memory_order_release);
	}

	void clear()
	{
		for (int i = 0; i < SECTIONS; i++)
		{
			uint8_t *s = m_sec[i].load();
			if (s) memset(s, 0, SECTION_SIZE);
		}
	}

	HSHighIds *clone() const
	{
		HSHighIds *c = new HSHighIds();
		for (int i = 0; i < SECTIONS; i++)
		{
			const uint8_t *s = m_sec[i].load();
			if (s == nullptr) continue;
			uint8_t *d = new uint8_t[SECTION_SIZE];
			memcpy(d, s, SECTION_SIZE);
			c->m_sec[i].store(d);
		}
		c->m_any.store(m_any.load());
		return c;
	}

	// sparse list of (packed position, high byte) for saving / sending: pos = x << 12 | z << 8 | y
	void collect(std::vector<int> &out, int y0 = 0, int y1 = 256) const
	{
		if (!any()) return;
		for (int i = 0; i < SECTIONS; i++)
		{
			const uint8_t *s = m_sec[i].load();
			if (s == nullptr) continue;
			for (int k = 0; k < SECTION_SIZE; k++)
			{
				if (s[k] == 0) continue;
				const int y = i * 16 + (k >> 8), z = (k >> 4) & 15, x = k & 15;
				if (y < y0 || y >= y1) continue;
				out.push_back((x << 12) | (z << 8) | y);
				out.push_back(s[k]);
			}
		}
	}

	void retain() { m_refs.fetch_add(1); }

	static void release(HSHighIds *h)
	{
		if (h == nullptr) return;
		if (h->m_refs.fetch_sub(1) != 1) return;
		// retire instead of deleting right away (render threads may still be reading)
		static std::mutex s_lock;
		static std::deque<HSHighIds *> s_retired;
		std::lock_guard<std::mutex> guard(s_lock);
		s_retired.push_back(h);
		while (s_retired.size() > 512)
		{
			delete s_retired.front();
			s_retired.pop_front();
		}
	}

private:
	std::atomic<uint8_t *> m_sec[SECTIONS];
	std::atomic<int> m_refs;
	std::atomic<bool> m_any;
};
