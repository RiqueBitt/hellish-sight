#pragma once

// ConsoleSaveFileRegionDisk - Hellish Sight (stage 2: region files on disk).
//
// The 4J save container keeps the whole world (every region file included)
// inside one in-memory blob capped at 2 GB, which a huge world outgrows.
// This decorator keeps the original container for everything small
// (level.dat, players, maps, ...) and stores region files (*.mcr) as real
// files on disk:
//
//   <root>/<worldId>/<region path>             committed region files
//   <root>/<worldId>/_session/<region path>    changes since the last save
//
// Save semantics are preserved:
//   * Region writes go to the session copy (copy-on-write from the committed
//     file, or migrated from the old blob for pre-existing worlds).
//   * Flush() (manual save / autosave) commits the session into place, then
//     flushes the inner container.
//   * Leaving without saving never calls Flush(); the stale session is
//     discarded the next time the world is opened, exactly like the blob.
//
// All entry points are serialised with one recursive mutex, because chunk
// saving writes region files from background threads.

#include "ConsoleSaveFile.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class ConsoleSaveFileRegionDisk : public ConsoleSaveFile
{
public:
	// Takes ownership of `inner`. `root` is the directory holding all worlds'
	// region folders (created on demand).
	ConsoleSaveFileRegionDisk(ConsoleSaveFile *inner, const std::wstring &root);
	virtual ~ConsoleSaveFileRegionDisk();

	// Convenience used at the storage creation sites.
	static ConsoleSaveFile *wrap(ConsoleSaveFile *inner);
	static std::wstring defaultRoot();

	// --- ConsoleSaveFile ---------------------------------------------------
	virtual FileEntry *createFile(const ConsoleSavePath &fileName);
	virtual void deleteFile(FileEntry *file);
	virtual void setFilePointer(FileEntry *file, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);
	virtual BOOL writeFile(FileEntry *file, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten);
	virtual BOOL zeroFile(FileEntry *file, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten);
	virtual BOOL readFile(FileEntry *file, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead);
	virtual BOOL closeHandle(FileEntry *file);
	virtual void finalizeWrite();
	virtual void tick();

	virtual bool doesFileExist(ConsoleSavePath file);

	virtual void Flush(bool autosave, bool updateThumbnail = true);

#ifndef _CONTENT_PACKAGE
	virtual void DebugFlushToFile(void *compressedData = nullptr, unsigned int compressedDataSize = 0);
#endif
	virtual unsigned int getSizeOnDisk();
	virtual wstring getFilename();
	virtual vector<FileEntry *> *getFilesWithPrefix(const wstring &prefix);
	virtual vector<FileEntry *> *getRegionFilesByDimension(unsigned int dimensionIndex);

#if defined(__PS3__) || defined(__ORBIS__) || defined(__PSVITA__)
	virtual wstring getPlayerDataFilenameForLoad(const PlayerUID &pUID);
	virtual wstring getPlayerDataFilenameForSave(const PlayerUID &pUID);
	virtual vector<FileEntry *> *getValidPlayerDatFiles();
#endif

	virtual int getSaveVersion();
	virtual int getOriginalSaveVersion();

	virtual void LockSaveAccess();
	virtual void ReleaseSaveAccess();

	virtual ESavePlatform getSavePlatform();
	virtual bool isSaveEndianDifferent();
	virtual void setLocalPlatform();
	virtual void setPlatform(ESavePlatform plat);
	virtual ByteOrder getSaveEndian();
	virtual ByteOrder getLocalEndian();
	virtual void setEndian(ByteOrder endian);

	virtual void ConvertRegionFile(File sourceFile);
	virtual void ConvertToLocalPlatform();

	virtual void *getWritePointer(FileEntry *file);

	// --- Hellish Sight -----------------------------------------------------
	const std::wstring &worldDir() const { return m_worldDir; }
	static bool isRegionName(const std::wstring &name);

private:
	struct DiskFile
	{
		FileEntry entry;          // handed to callers; startOffset is always 0
		std::wstring name;        // save-relative name, e.g. "DIM-1/r.0.0.mcr"
		void *fp = nullptr;       // FILE*, opened on the session or committed file
		bool fpIsSession = false; // fp points at the session copy (writable)
		bool open = false;        // between createFile() and closeHandle()
	};

	DiskFile *findByEntry(FileEntry *entry);
	DiskFile *openDiskFile(const std::wstring &name);
	bool ensureWritable(DiskFile *df);
	void closeFp(DiskFile *df);
	void commitSession();
	void loadOrCreateWorldId();
	std::wstring committedPath(const std::wstring &name) const;
	std::wstring sessionPath(const std::wstring &name) const;
	void collectDiskRegionNames(const std::wstring &prefix, std::set<std::wstring> &out) const;

	ConsoleSaveFile *m_inner;
	std::wstring m_root;
	std::wstring m_worldDir;     // <root>/<worldId>/
	std::wstring m_sessionDir;   // <worldDir>/_session/

	std::map<std::wstring, DiskFile *> m_files;   // by name; owned
	std::unordered_map<const FileEntry *, DiskFile *> m_byEntry;   // index into m_files
	std::set<std::wstring> m_sessionNames;        // names with a session copy
	std::set<std::wstring> m_pendingDeletes;      // committed files to remove on commit
	std::set<std::wstring> m_migratedFromBlob;    // blob entries to drop on commit

	std::recursive_mutex m_lock;
};
