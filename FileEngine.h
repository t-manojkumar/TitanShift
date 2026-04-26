#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include "../include/TitanShift.h"

/*
 * FileEngine — Windows-native high-performance file operations
 *
 * Uses:
 *   CopyFileEx()       — kernel copy with progress callback, cancel flag
 *   MoveFileWithProgress() — kernel move (rename on same volume = zero-copy)
 *   FILE_FLAG_NO_BUFFERING + FILE_FLAG_WRITE_THROUGH  — bypass cache for large files
 *   FILE_FLAG_SEQUENTIAL_SCAN — prefetch hint for sequential reads
 *   SetFileInformationByHandle(FileRenameInfo) — atomic rename
 *   FindFirstFileEx(FIND_FIRST_EX_LARGE_FETCH) — fast directory enumeration
 *   DeviceIoControl(FSCTL_SET_SPARSE)          — sparse file support
 *   Multi-threaded with per-thread IOCP overlap  — max disk parallelism
 */

struct CopyOptions {
    bool overwrite{true};
    bool verify{false};       // CRC32 verify after copy
    bool preserveTimestamps{true};
    bool preserveACL{false};
    bool useUnbuffered{false}; // auto-selected for files > 128MB
    int  threadCount{4};      // parallel file threads
};

class FileEngine {
public:
    FileEngine(TitanShiftApp* app, OpStats* stats, std::atomic<bool>* cancel, std::atomic<bool>* pause);
    ~FileEngine();

    // Main operations — all blocking (run in worker thread)
    void Copy(const std::vector<FileEntry>& sources, const std::wstring& dest, const CopyOptions& opts);
    void Move(const std::vector<FileEntry>& sources, const std::wstring& dest, const CopyOptions& opts);
    void Rename(const std::vector<FileEntry>& sources, const std::wstring& pattern);
    void Delete(const std::vector<FileEntry>& sources);
    void Dedupe(const std::vector<FileEntry>& sources, const std::wstring& dest);
    void Sync(const std::vector<FileEntry>& sources, const std::wstring& dest, const CopyOptions& opts);

    // Scanning
    static std::vector<FileEntry> ScanPaths(const std::vector<std::wstring>& paths, std::atomic<bool>* cancel);
    static ULONGLONG              TotalSize(const std::vector<FileEntry>& files);

private:
    TitanShiftApp*      m_app;
    OpStats*            m_stats;
    std::atomic<bool>*  m_cancel;
    std::atomic<bool>*  m_pause;

    // Per-file copy with Windows CopyFileEx
    bool CopyOneFile(const std::wstring& src, const std::wstring& dst,
                     bool overwrite, bool unbuffered, bool verify);

    // Move: try rename first (same-volume zero-cost), fallback to copy+delete
    bool MoveOneFile(const std::wstring& src, const std::wstring& dst, bool overwrite);

    // Delete to recycle bin or permanent
    bool DeleteOneFile(const std::wstring& path, bool permanent);

    // Ensure destination directory exists
    bool EnsureDir(const std::wstring& dir);

    // Build destination path preserving relative structure
    std::wstring BuildDestPath(const std::wstring& srcRoot, const std::wstring& srcFile,
                                const std::wstring& destRoot);

    // Apply rename pattern: {name}, {ext}, {n}, {date}, {size}
    std::wstring ApplyPattern(const std::wstring& original, const std::wstring& pattern, int index);

    // CRC32 verification
    DWORD CRC32File(const std::wstring& path);

    // CopyFileEx progress callback
    static DWORD CALLBACK CopyProgressCallback(
        LARGE_INTEGER TotalFileSize, LARGE_INTEGER TotalBytesTransferred,
        LARGE_INTEGER StreamSize, LARGE_INTEGER StreamBytesTransferred,
        DWORD StreamNumber, DWORD CallbackReason,
        HANDLE SourceFile, HANDLE DestFile, LPVOID lpData);

    // Pause wait
    void WaitIfPaused();

    // Throughput tracker
    struct ThroughputTracker {
        ULONGLONG lastBytes{0};
        ULONGLONG lastTick{0};
        ULONGLONG bytesPerSec{0};
        void Update(ULONGLONG totalBytes);
    } m_throughput;

    // Active copy cancel flag (per-file, reset each file)
    BOOL m_copyCancel{FALSE};
};
