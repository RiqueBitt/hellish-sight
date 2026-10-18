#include "stdafx.h"
#include "ConsoleSaveFileRegionDisk.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <chrono>

// ---------------------------------------------------------------------------
// Small file-system layer (Win32 in the game, std::filesystem elsewhere so the
// logic can be unit-tested off-target).
// ---------------------------------------------------------------------------
#ifdef _WIN32
#include <windows.h>
namespace hsfs
{
	static std::wstring native(const std::wstring &p)
	{
		std::wstring out = p;
		for (auto &c : out) if (c == L'/') c = L'\\';
		return out;
	}
	static bool exists(const std::wstring &p)
	{
		return GetFileAttributesW(native(p).c_str()) != INVALID_FILE_ATTRIBUTES;
	}
	static void makeDirs(const std::wstring &dir)
	{
		std::wstring n = native(dir), cur;
		for (size_t i = 0; i < n.size(); ++i)
		{
			cur.push_back(n[i]);
			if (n[i] == L'\\' && cur.size() > 1) CreateDirectoryW(cur.c_str(), nullptr);
		}
		if (!cur.empty()) CreateDirectoryW(cur.c_str(), nullptr);
	}
	static FILE *open(const std::wstring &p, const wchar_t *mode) { return _wfopen(native(p).c_str(), mode); }
	static bool remove(const std::wstring &p) { return DeleteFileW(native(p).c_str()) != 0; }
	static bool replace(const std::wstring &from, const std::wstring &to)
	{
		return MoveFileExW(native(from).c_str(), native(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
	}
	static void removeTree(const std::wstring &dir)
	{
		std::wstring pattern = native(dir) + L"\\*";
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				std::wstring name = fd.cFileName;
				if (name == L"." || name == L"..") continue;
				std::wstring full = native(dir) + L"\\" + name;
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) removeTree(full);
				else DeleteFileW(full.c_str());
			} while (FindNextFileW(h, &fd));
			FindClose(h);
		}
		RemoveDirectoryW(native(dir).c_str());
	}
	// Lists files (recursively) under dir, returning paths relative to dir with '/' separators.
	static void listFiles(const std::wstring &dir, const std::wstring &rel, std::vector<std::wstring> &out)
	{
		std::wstring pattern = native(dir + rel) + L"*";
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) return;
		do
		{
			std::wstring name = fd.cFileName;
			if (name == L"." || name == L"..") continue;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) listFiles(dir, rel + name + L"/", out);
			else out.push_back(rel + name);
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
}
#else
#include <filesystem>
namespace hsfs
{
	namespace fs = std::filesystem;
	static fs::path native(const std::wstring &p) { return fs::path(p); }
	static bool exists(const std::wstring &p) { std::error_code ec; return fs::exists(native(p), ec); }
	static void makeDirs(const std::wstring &dir) { std::error_code ec; fs::create_directories(native(dir), ec); }
	static FILE *open(const std::wstring &p, const wchar_t *mode)
	{
		std::string m; for (const wchar_t *c = mode; *c; ++c) m.push_back(static_cast<char>(*c));
		return std::fopen(native(p).string().c_str(), m.c_str());
	}
	static bool remove(const std::wstring &p) { std::error_code ec; return fs::remove(native(p), ec); }
	static bool replace(const std::wstring &from, const std::wstring &to)
	{
		std::error_code ec; fs::rename(native(from), native(to), ec); return !ec;
	}
	static void removeTree(const std::wstring &dir) { std::error_code ec; fs::remove_all(native(dir), ec); }
	static void listFiles(const std::wstring &dir, const std::wstring &rel, std::vector<std::wstring> &out)
	{
		std::error_code ec;
		fs::path base = native(dir);
		if (!fs::exists(base, ec)) return;
		for (auto it = fs::recursive_directory_iterator(base, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec) break;
			if (!it->is_regular_file()) continue;
			std::wstring r = fs::relative(it->path(), base).generic_wstring();
			if (r.compare(0, 9, L"_session/") == 0) continue;
			out.push_back(rel + r);
		}
	}
}
#endif

namespace
{
	const wchar_t *kWorldIdFile = L"hellishsight_regions.id";
	const wchar_t *kSessionFolder = L"_session/";

	std::wstring normalise(const std::wstring &name)
	{
		std::wstring out = name;
		for (auto &c : out) if (c == L'\\') c = L'/';
		while (!out.empty() && out[0] == L'/') out.erase(0, 1);
		return out;
	}

	// Rejects absolute paths, drive letters and parent references.
	bool isSafeRelative(const std::wstring &name)
	{
		if (name.empty() || name.find(L"..") != std::wstring::npos || name.find(L':') != std::wstring::npos) return false;
		return name[0] != L'/';
	}

	std::wstring parentDir(const std::wstring &path)
	{
		size_t slash = path.find_last_of(L"/\\");
		return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
	}

	int64_t fileSize(FILE *fp)
	{
		if (fp == nullptr) return 0;
		long cur = std::ftell(fp);
		std::fseek(fp, 0, SEEK_END);
		long size = std::ftell(fp);
		std::fseek(fp, cur, SEEK_SET);
		return size < 0 ? 0 : size;
	}

	bool copyStream(FILE *from, FILE *to)
	{
		char buffer[64 * 1024];
		std::fseek(from, 0, SEEK_SET);
		size_t n;
		while ((n = std::fread(buffer, 1, sizeof(buffer), from)) > 0)
		{
			if (std::fwrite(buffer, 1, n, to) != n) return false;
		}
		return std::fflush(to) == 0;
	}
}

// ---------------------------------------------------------------------------

ConsoleSaveFileRegionDisk::ConsoleSaveFileRegionDisk(ConsoleSaveFile *inner, const std::wstring &root)
	: m_inner(inner), m_root(normalise(root))
{
	if (!m_root.empty() && m_root.back() != L'/') m_root.push_back(L'/');
	loadOrCreateWorldId();

	// Anything left in the session folder was never committed (the game closed
	// without saving, or crashed): discard it, like the in-memory blob would.
	hsfs::removeTree(m_sessionDir);
	hsfs::makeDirs(m_worldDir);
}

ConsoleSaveFileRegionDisk::~ConsoleSaveFileRegionDisk()
{
	{
		std::lock_guard<std::recursive_mutex> guard(m_lock);
		for (auto &it : m_files)
		{
			closeFp(it.second);
			delete it.second;
		}
		m_files.clear();
		m_byEntry.clear();
	}
	delete m_inner;
}

std::wstring ConsoleSaveFileRegionDisk::defaultRoot()
{
	return L"Windows64/GameHDD/_HellishSightRegions/";
}

ConsoleSaveFile *ConsoleSaveFileRegionDisk::wrap(ConsoleSaveFile *inner)
{
	if (inner == nullptr) return nullptr;
	return new ConsoleSaveFileRegionDisk(inner, defaultRoot());
}

bool ConsoleSaveFileRegionDisk::isRegionName(const std::wstring &name)
{
	const std::wstring ext = L".mcr";
	return name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0;
}

void ConsoleSaveFileRegionDisk::loadOrCreateWorldId()
{
	// The id lives inside the inner container, so it travels with the save.
	std::wstring id;
	if (m_inner->doesFileExist(ConsoleSavePath(kWorldIdFile)))
	{
		FileEntry *e = m_inner->createFile(ConsoleSavePath(kWorldIdFile));
		char buf[64] = {};
		DWORD read = 0;
		m_inner->setFilePointer(e, 0, nullptr, FILE_BEGIN);
		m_inner->readFile(e, buf, (std::min<DWORD>)(e->getFileSize(), sizeof(buf) - 1), &read);
		m_inner->closeHandle(e);
		for (DWORD i = 0; i < read; ++i)
		{
			const char c = buf[i];
			if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) id.push_back(static_cast<wchar_t>(c));
		}
	}

	if (id.size() != 32)
	{
		std::random_device rd;
		std::mt19937_64 rng(static_cast<uint64_t>(rd()) ^
			static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
		static const char *hex = "0123456789abcdef";
		std::string s;
		for (int i = 0; i < 32; ++i) s.push_back(hex[rng() & 15]);
		id.assign(s.begin(), s.end());

		FileEntry *e = m_inner->createFile(ConsoleSavePath(kWorldIdFile));
		DWORD written = 0;
		m_inner->setFilePointer(e, 0, nullptr, FILE_BEGIN);
		m_inner->writeFile(e, s.data(), static_cast<DWORD>(s.size()), &written);
		m_inner->closeHandle(e);
	}

	m_worldDir = m_root + id + L"/";
	m_sessionDir = m_worldDir + kSessionFolder;
}

std::wstring ConsoleSaveFileRegionDisk::committedPath(const std::wstring &name) const { return m_worldDir + name; }
std::wstring ConsoleSaveFileRegionDisk::sessionPath(const std::wstring &name) const { return m_sessionDir + name; }

ConsoleSaveFileRegionDisk::DiskFile *ConsoleSaveFileRegionDisk::findByEntry(FileEntry *entry)
{
	auto it = m_byEntry.find(entry);
	return it != m_byEntry.end() ? it->second : nullptr;
}

void ConsoleSaveFileRegionDisk::closeFp(DiskFile *df)
{
	if (df->fp != nullptr)
	{
		std::fclose(static_cast<FILE *>(df->fp));
		df->fp = nullptr;
	}
	df->fpIsSession = false;
}

ConsoleSaveFileRegionDisk::DiskFile *ConsoleSaveFileRegionDisk::openDiskFile(const std::wstring &name)
{
	auto existing = m_files.find(name);
	DiskFile *df = existing != m_files.end() ? existing->second : nullptr;
	if (df == nullptr)
	{
		df = new DiskFile();
		df->name = name;
		const std::wstring shortName = name.size() < 63 ? name : name.substr(name.size() - 63);
		std::memset(df->entry.data.filename, 0, sizeof(df->entry.data.filename));
		std::wmemcpy(df->entry.data.filename, shortName.c_str(), shortName.size());
		m_files[name] = df;
		m_byEntry[&df->entry] = df;
	}

	if (df->fp == nullptr)
	{
		const bool deleted = m_pendingDeletes.count(name) != 0;
		if (m_sessionNames.count(name) && hsfs::exists(sessionPath(name)))
		{
			df->fp = hsfs::open(sessionPath(name), L"r+b");
			df->fpIsSession = df->fp != nullptr;
		}
		else if (!deleted && hsfs::exists(committedPath(name)))
		{
			df->fp = hsfs::open(committedPath(name), L"rb");
			df->fpIsSession = false;
		}
		else if (!deleted && m_inner->doesFileExist(ConsoleSavePath(name)))
		{
			// Pre-existing world: migrate this region out of the blob into the session.
			FileEntry *src = m_inner->createFile(ConsoleSavePath(name));
			const DWORD size = src->getFileSize();
			std::vector<char> data(size);
			DWORD read = 0;
			m_inner->setFilePointer(src, 0, nullptr, FILE_BEGIN);
			if (size > 0) m_inner->readFile(src, data.data(), size, &read);
			m_inner->closeHandle(src);

			hsfs::makeDirs(parentDir(sessionPath(name)));
			FILE *out = hsfs::open(sessionPath(name), L"w+b");
			if (out != nullptr)
			{
				if (read > 0) std::fwrite(data.data(), 1, read, out);
				std::fflush(out);
				df->fp = out;
				df->fpIsSession = true;
				m_sessionNames.insert(name);
				m_migratedFromBlob.insert(name);
			}
		}
	}

	df->entry.data.startOffset = 0;
	df->entry.data.length = static_cast<unsigned int>(fileSize(static_cast<FILE *>(df->fp)));
	return df;
}

bool ConsoleSaveFileRegionDisk::ensureWritable(DiskFile *df)
{
	if (df->fp != nullptr && df->fpIsSession) return true;

	hsfs::makeDirs(parentDir(sessionPath(df->name)));
	FILE *out = hsfs::open(sessionPath(df->name), L"w+b");
	if (out == nullptr) return false;

	if (df->fp != nullptr)
	{
		// Copy-on-write from the committed file.
		if (!copyStream(static_cast<FILE *>(df->fp), out))
		{
			std::fclose(out);
			return false;
		}
		closeFp(df);
	}

	df->fp = out;
	df->fpIsSession = true;
	m_sessionNames.insert(df->name);
	m_pendingDeletes.erase(df->name);
	return true;
}

// --- region-aware entry points ---------------------------------------------

FileEntry *ConsoleSaveFileRegionDisk::createFile(const ConsoleSavePath &fileName)
{
	const std::wstring name = normalise(fileName.getName());
	if (!isRegionName(name) || !isSafeRelative(name)) return m_inner->createFile(fileName);

	std::lock_guard<std::recursive_mutex> guard(m_lock);
	DiskFile *df = openDiskFile(name);
	df->open = true;
	df->entry.currentFilePointer = 0;
	return &df->entry;
}

void ConsoleSaveFileRegionDisk::deleteFile(FileEntry *file)
{
	std::lock_guard<std::recursive_mutex> guard(m_lock);
	DiskFile *df = findByEntry(file);
	if (df == nullptr)
	{
		// A blob entry. If it is a region we also manage on disk, delete that copy too.
		const std::wstring name = normalise(file->data.filename);
		m_inner->deleteFile(file);
		if (isRegionName(name) && isSafeRelative(name))
		{
			m_migratedFromBlob.erase(name);
			if (m_sessionNames.erase(name)) hsfs::remove(sessionPath(name));
			if (hsfs::exists(committedPath(name))) m_pendingDeletes.insert(name);
		}
		return;
	}

	closeFp(df);
	if (m_sessionNames.erase(df->name)) hsfs::remove(sessionPath(df->name));
	if (hsfs::exists(committedPath(df->name))) m_pendingDeletes.insert(df->name);
	if (m_migratedFromBlob.count(df->name) == 0 && m_inner->doesFileExist(ConsoleSavePath(df->name)))
	{
		m_migratedFromBlob.insert(df->name);	// make sure the blob copy is dropped on commit
	}
	df->entry.data.length = 0;
	df->entry.currentFilePointer = 0;
}

void ConsoleSaveFileRegionDisk::setFilePointer(FileEntry *file, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
	std::lock_guard<std::recursive_mutex> guard(m_lock);
	DiskFile *df = findByEntry(file);
	if (df == nullptr) { m_inner->setFilePointer(file, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod); return; }

	long long target = lDistanceToMove;
	if (dwMoveMethod == FILE_END) target += df->entry.data.length;
	else if (dwMoveMethod == FILE_CURRENT) target += df->entry.currentFilePointer;
	if (target < 0) target = 0;
	df->entry.currentFilePointer = static_cast<unsigned int>(target);
}

BOOL ConsoleSaveFileRegionDisk::writeFile(FileEntry *file, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten)
{
	std::lock_guard<std::recursive_mutex> guard(m_lock);
	DiskFile *df = findByEntry(file);
	if (df == nullptr) return m_inner->writeFile(file, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten);

	if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = 0;
	if (!ensureWritable(df)) return FALSE;

	FILE *fp = static_cast<FILE *>(df->fp);
	std::fseek(fp, static_cast<long>(df->entry.currentFilePointer), SEEK_SET);
	const size_t n = nNumberOfBytesToWrite ? std::fwrite(lpBuffer, 1, nNumberOfBytesToWrite, fp) : 0;
	df->entry.currentFilePointer += static_cast<unsigned int>(n);
	if (df->entry.currentFilePointer > df->entry.data.length) df->entry.data.length = df->entry.currentFilePointer;
	df->entry.updateLastModifiedTime();
	if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = static_cast<DWORD>(n);
	return n == nNumberOfBytesToWrite ? TRUE : FALSE;
}

BOOL ConsoleSaveFileRegionDisk::zeroFile(FileEntry *file, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten)
{
	std::lock_guard<std::recursive_mutex> guard(m_lock);
	DiskFile *df = findByEntry(file);
	if (df == nullptr) return m_inner->zeroFile(file, nNumberOfBytesToWrite, lpNumberOfBytesWritten);

	static const char zeros[4096] = {};
	DWORD total = 0, remaining = nNumberOfBytesToWrite;
	while (remaining > 0)
	{
		const DWORD chunk = (std::min<DWORD>)(remaining, sizeof(zeros));
		DWORD written = 0;
		if (!writeFile(file, zeros, chunk, &written)) break;
		total += written;
		remaining -= chunk;
	}
	if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = total;
	return total == nNumberOfBytesToWrite ? TRUE : FALSE;
}

BOOL ConsoleSaveFileRegionDisk::readFile(FileEntry *file, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead)
{
	std::lock_guard<std::recursive_mutex> guard(m_lock);
	DiskFile *df = findByEntry(file);
	if (df == nullptr) return m_inner->readFile(file, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead);

	size_t n = 0;
	FILE *fp = static_cast<FILE *>(df->fp);
	if (fp != nullptr && nNumberOfBytesToRead > 0)
	{
		std::fseek(fp, static_cast<long>(df->entry.currentFilePointer), SEEK_SET);
		n = std::fread(lpBuffer, 1, nNumberOfBytesToRead, fp);
	}
	if (n < nNumberOfBytesToRead) std::memset(static_cast<char *>(lpBuffer) + n, 0, nNumberOfBytesToRead - n);
	df->entry.currentFilePointer += static_cast<unsigned int>(n);
	if (lpNumberOfBytesRead) *lpNumberOfBytesRead = static_cast<DWORD>(n);
	return TRUE;
}

BOOL ConsoleSaveFileRegionDisk::closeHandle(FileEntry *file)
{
	std::lock_guard<std::recursive_mutex> guard(m_lock);
	DiskFile *df = findByEntry(file);
	if (df == nullptr) return m_inner->closeHandle(file);

	if (df->fp != nullptr) std::fflush(static_cast<FILE *>(df->fp));
	df->open = false;
	return TRUE;
}

bool ConsoleSaveFileRegionDisk::doesFileExist(ConsoleSavePath file)
{
	const std::wstring name = normalise(file.getName());
	if (!isRegionName(name) || !isSafeRelative(name)) return m_inner->doesFileExist(file);

	std::lock_guard<std::recursive_mutex> guard(m_lock);
	if (m_sessionNames.count(name)) return true;
	if (m_pendingDeletes.count(name)) return false;
	return hsfs::exists(committedPath(name)) || m_inner->doesFileExist(file);
}

void ConsoleSaveFileRegionDisk::collectDiskRegionNames(const std::wstring &prefix, std::set<std::wstring> &out) const
{
	std::vector<std::wstring> committed, session;
	hsfs::listFiles(m_worldDir, L"", committed);
	hsfs::listFiles(m_sessionDir, L"", session);
	for (const auto &n : committed)
	{
		if (n.compare(0, 9, L"_session/") == 0) continue;
		if (isRegionName(n) && n.compare(0, prefix.size(), prefix) == 0 && !m_pendingDeletes.count(n)) out.insert(n);
	}
	for (const auto &n : session)
	{
		if (isRegionName(n) && n.compare(0, prefix.size(), prefix) == 0) out.insert(n);
	}
}

vector<FileEntry *> *ConsoleSaveFileRegionDisk::getFilesWithPrefix(const wstring &prefix)
{
	std::lock_guard<std::recursive_mutex> guard(m_lock);
	vector<FileEntry *> *result = m_inner->getFilesWithPrefix(prefix);
	if (result == nullptr) result = new vector<FileEntry *>();

	// Hide blob regions that now live on disk, then add the disk ones.
	std::set<std::wstring> diskNames;
	collectDiskRegionNames(normalise(prefix), diskNames);
	for (auto it = result->begin(); it != result->end(); )
	{
		const std::wstring n = normalise((*it)->data.filename);
		if (isRegionName(n) && (diskNames.count(n) || m_migratedFromBlob.count(n))) it = result->erase(it);
		else ++it;
	}
	for (const auto &n : diskNames)
	{
		DiskFile *df = openDiskFile(n);
		result->push_back(&df->entry);
	}
	return result;
}

void ConsoleSaveFileRegionDisk::commitSession()
{
	// Session copies replace the committed files.
	for (const auto &name : m_sessionNames)
	{
		auto it = m_files.find(name);
		DiskFile *df = it != m_files.end() ? it->second : nullptr;
		if (df != nullptr) closeFp(df);

		hsfs::makeDirs(parentDir(committedPath(name)));
		if (!hsfs::replace(sessionPath(name), committedPath(name)))
		{
			// Keep the session copy; it will be retried on the next save.
			if (df != nullptr) { df->fp = hsfs::open(sessionPath(name), L"r+b"); df->fpIsSession = df->fp != nullptr; }
			continue;
		}
		if (df != nullptr)
		{
			df->fp = hsfs::open(committedPath(name), L"rb");
			df->fpIsSession = false;
		}
	}

	std::set<std::wstring> retry;
	for (const auto &name : m_sessionNames)
	{
		if (hsfs::exists(sessionPath(name))) retry.insert(name);
	}
	m_sessionNames.swap(retry);

	for (const auto &name : m_pendingDeletes) hsfs::remove(committedPath(name));
	m_pendingDeletes.clear();

	// Regions migrated out of the blob no longer need their blob copy.
	for (const auto &name : m_migratedFromBlob)
	{
		if (m_sessionNames.count(name)) continue;	// commit failed; keep the blob copy
		if (m_inner->doesFileExist(ConsoleSavePath(name)))
		{
			FileEntry *e = m_inner->createFile(ConsoleSavePath(name));
			m_inner->deleteFile(e);
		}
	}
	for (auto it = m_migratedFromBlob.begin(); it != m_migratedFromBlob.end(); )
	{
		if (m_sessionNames.count(*it)) ++it; else it = m_migratedFromBlob.erase(it);
	}
}

void ConsoleSaveFileRegionDisk::Flush(bool autosave, bool updateThumbnail)
{
	{
		std::lock_guard<std::recursive_mutex> guard(m_lock);
		commitSession();
	}
	m_inner->Flush(autosave, updateThumbnail);
}

// --- pure delegation ---------------------------------------------------------

void ConsoleSaveFileRegionDisk::finalizeWrite() { m_inner->finalizeWrite(); }
void ConsoleSaveFileRegionDisk::tick() { m_inner->tick(); }
#ifndef _CONTENT_PACKAGE
void ConsoleSaveFileRegionDisk::DebugFlushToFile(void *compressedData, unsigned int compressedDataSize) { m_inner->DebugFlushToFile(compressedData, compressedDataSize); }
#endif
unsigned int ConsoleSaveFileRegionDisk::getSizeOnDisk() { return m_inner->getSizeOnDisk(); }
wstring ConsoleSaveFileRegionDisk::getFilename() { return m_inner->getFilename(); }
vector<FileEntry *> *ConsoleSaveFileRegionDisk::getRegionFilesByDimension(unsigned int dimensionIndex) { return m_inner->getRegionFilesByDimension(dimensionIndex); }
#if defined(__PS3__) || defined(__ORBIS__) || defined(__PSVITA__)
wstring ConsoleSaveFileRegionDisk::getPlayerDataFilenameForLoad(const PlayerUID &pUID) { return m_inner->getPlayerDataFilenameForLoad(pUID); }
wstring ConsoleSaveFileRegionDisk::getPlayerDataFilenameForSave(const PlayerUID &pUID) { return m_inner->getPlayerDataFilenameForSave(pUID); }
vector<FileEntry *> *ConsoleSaveFileRegionDisk::getValidPlayerDatFiles() { return m_inner->getValidPlayerDatFiles(); }
#endif
int ConsoleSaveFileRegionDisk::getSaveVersion() { return m_inner->getSaveVersion(); }
int ConsoleSaveFileRegionDisk::getOriginalSaveVersion() { return m_inner->getOriginalSaveVersion(); }
void ConsoleSaveFileRegionDisk::LockSaveAccess() { m_lock.lock(); m_inner->LockSaveAccess(); }
void ConsoleSaveFileRegionDisk::ReleaseSaveAccess() { m_inner->ReleaseSaveAccess(); m_lock.unlock(); }
ESavePlatform ConsoleSaveFileRegionDisk::getSavePlatform() { return m_inner->getSavePlatform(); }
bool ConsoleSaveFileRegionDisk::isSaveEndianDifferent() { return m_inner->isSaveEndianDifferent(); }
void ConsoleSaveFileRegionDisk::setLocalPlatform() { m_inner->setLocalPlatform(); }
void ConsoleSaveFileRegionDisk::setPlatform(ESavePlatform plat) { m_inner->setPlatform(plat); }
ByteOrder ConsoleSaveFileRegionDisk::getSaveEndian() { return m_inner->getSaveEndian(); }
ByteOrder ConsoleSaveFileRegionDisk::getLocalEndian() { return m_inner->getLocalEndian(); }
void ConsoleSaveFileRegionDisk::setEndian(ByteOrder endian) { m_inner->setEndian(endian); }
void ConsoleSaveFileRegionDisk::ConvertRegionFile(File sourceFile) { m_inner->ConvertRegionFile(sourceFile); }
void ConsoleSaveFileRegionDisk::ConvertToLocalPlatform() { m_inner->ConvertToLocalPlatform(); }
void *ConsoleSaveFileRegionDisk::getWritePointer(FileEntry *file)
{
	std::lock_guard<std::recursive_mutex> guard(m_lock);
	return findByEntry(file) != nullptr ? nullptr : m_inner->getWritePointer(file);
}
