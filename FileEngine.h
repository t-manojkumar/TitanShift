#pragma once
#include "TitanShift.h"
#include <string>
#include <vector>
#include <atomic>
#include <functional>

struct CopyOptions {
    bool overwrite{true};
    bool verify{false};
    bool preserveTimestamps{true};
    bool preserveACL{false};
    bool useUnbuffered{false};
    int  threadCount{4};
};

class FileEngine {
public:
    // Public so the static C callback (which receives a void*) can name the type
    struct ThroughputTracker {
        ULONGLONG lastBytes{0};
        ULONGLONG lastTick{0};
        ULONGLONG bytesPerSec{0};
        void Update(ULONGLONG totalBytes);
    };

    FileEngine(TitanShiftApp* app, OpStats* stats,
               std::atomic<bool>* cancel, std::atomic<bool>* pause);
    ~FileEngine();

    void Copy(const std::vector<FileEntry>& sources, const std::wstring& dest, const CopyOptions& opts);
    void Move(const std::vector<FileEntry>& sources, const std::wstring& dest, const CopyOptions& opts);
    void Rename(const std::vector<FileEntry>& sources, const std::wstring& pattern);
    void Delete(const std::vector<FileEntry>& sources);
    void Dedupe(const std::vector<FileEntry>& sources, const std::wstring& dest);
    void Sync(const std::vector<FileEntry>& sources, const std::wstring& dest, const CopyOptions& opts);

    static std::vector<FileEntry> ScanPaths(const std::vector<std::wstring>& paths, std::atomic<bool>* cancel);
    static ULONGLONG              TotalSize(const std::vector<FileEntry>& files);

private:
    TitanShiftApp*      m_app;
    OpStats*            m_stats;
    std::atomic<bool>*  m_cancel;
    std::atomic<bool>*  m_pause;
    ThroughputTracker   m_throughput;

    bool CopyOneFile(const std::wstring& src, const std::wstring& dst,
                     bool overwrite, bool unbuffered, bool verify);
    bool MoveOneFile(const std::wstring& src, const std::wstring& dst, bool overwrite);
    bool EnsureDir(const std::wstring& dir);
    std::wstring BuildDestPath(const std::wstring& srcRoot, const std::wstring& srcFile,
                                const std::wstring& destRoot);
    std::wstring ApplyPattern(const std::wstring& original, const std::wstring& pattern, int index);
    DWORD CRC32File(const std::wstring& path);

    static DWORD CALLBACK CopyProgressCallback(
        LARGE_INTEGER TotalFileSize, LARGE_INTEGER TotalBytesTransferred,
        LARGE_INTEGER StreamSize, LARGE_INTEGER StreamBytesTransferred,
        DWORD StreamNumber, DWORD CallbackReason,
        HANDLE SourceFile, HANDLE DestFile, LPVOID lpData);

    void WaitIfPaused();
};
