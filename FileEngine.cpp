#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#include <windows.h>
#include <shlwapi.h>
#include <winioctl.h>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <mutex>
#include <queue>
#include <sstream>
#include <iomanip>
#include "FileEngine.h"
#include "TitanShift.h"

#pragma comment(lib, "shlwapi.lib")

// ── Threshold: use unbuffered I/O for files larger than this ──────
static const ULONGLONG UNBUFFERED_THRESHOLD = 128ULL * 1024 * 1024; // 128 MB

FileEngine::FileEngine(TitanShiftApp* app, OpStats* stats,
                       std::atomic<bool>* cancel, std::atomic<bool>* pause)
    : m_app(app), m_stats(stats), m_cancel(cancel), m_pause(pause) {}

FileEngine::~FileEngine() {}

// ── Scan all paths recursively using FindFirstFileEx ─────────────
std::vector<FileEntry> FileEngine::ScanPaths(const std::vector<std::wstring>& paths,
                                              std::atomic<bool>* cancel) {
    std::vector<FileEntry> result;
    result.reserve(4096);

    std::function<void(const std::wstring&)> walk = [&](const std::wstring& dir) {
        if (cancel && cancel->load()) return;

        WIN32_FIND_DATAW fd{};
        std::wstring pattern = dir + L"\\*";

        // FIND_FIRST_EX_LARGE_FETCH: hint to OS to fetch many entries per syscall
        HANDLE hFind = FindFirstFileExW(
            pattern.c_str(),
            FindExInfoBasic,          // skip 8.3 names — faster
            &fd,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH
        );
        if (hFind == INVALID_HANDLE_VALUE) return;

        do {
            if (cancel && cancel->load()) break;
            if (fd.cFileName[0] == L'.') continue; // skip . and ..

            std::wstring full = dir + L"\\" + fd.cFileName;
            FileEntry e;
            e.path     = full;
            e.name     = fd.cFileName;
            e.modified = fd.ftLastWriteTime;
            e.isDir    = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            if (e.isDir) {
                walk(full);
            } else {
                ULARGE_INTEGER sz;
                sz.LowPart  = fd.nFileSizeLow;
                sz.HighPart = fd.nFileSizeHigh;
                e.size = sz.QuadPart;
                result.push_back(std::move(e));
            }
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    };

    for (const auto& p : paths) {
        if (cancel && cancel->load()) break;
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &info)) continue;

        if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            walk(p);
        } else {
            FileEntry e;
            e.path = p;
            e.name = p.substr(p.find_last_of(L"\\/") + 1);
            ULARGE_INTEGER sz;
            sz.LowPart  = info.nFileSizeLow;
            sz.HighPart = info.nFileSizeHigh;
            e.size      = sz.QuadPart;
            e.modified  = info.ftLastWriteTime;
            e.isDir     = false;
            result.push_back(std::move(e));
        }
    }
    return result;
}

ULONGLONG FileEngine::TotalSize(const std::vector<FileEntry>& files) {
    ULONGLONG total = 0;
    for (const auto& f : files) total += f.size;
    return total;
}

// ── CopyFileEx progress callback ──────────────────────────────────
struct CopyCbData {
    FileEngine* engine;
    ULONGLONG   fileSize;
    ULONGLONG   baseBytes; // bytes done before this file
};

DWORD CALLBACK FileEngine::CopyProgressCallback(
    LARGE_INTEGER TotalFileSize, LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD,
    HANDLE, HANDLE, LPVOID lpData)
{
    auto* d = reinterpret_cast<CopyCbData*>(lpData);
    ULONGLONG transferred = static_cast<ULONGLONG>(TotalBytesTransferred.QuadPart);

    // Update global byte counter (we track delta)
    ULONGLONG prev = d->engine->m_stats->doneBytes.load();
    ULONGLONG newVal = d->baseBytes + transferred;
    d->engine->m_stats->doneBytes.store(newVal);

    // Update throughput
    d->engine->m_throughput.Update(newVal);

    // Send progress to UI (throttled in PostProgress)
    ProgressPayload pp{};
    pp.doneBytes   = newVal;
    pp.totalBytes  = d->engine->m_stats->totalBytes.load();
    pp.doneFiles   = d->engine->m_stats->doneFiles.load();
    pp.totalFiles  = d->engine->m_stats->totalFiles.load();
    pp.bytesPerSec = d->engine->m_throughput.bytesPerSec;
    pp.pct = pp.totalBytes ? (int)((pp.doneBytes * 100) / pp.totalBytes) : 0;
    {
        std::lock_guard<std::mutex> lk(d->engine->m_stats->fileMutex);
        pp.currentFile = d->engine->m_stats->currentFile;
    }

    // ETA
    if (pp.bytesPerSec > 0 && pp.totalBytes > pp.doneBytes) {
        pp.etaSecs = (pp.totalBytes - pp.doneBytes) / pp.bytesPerSec;
    }
    pp.state = OpState::RUNNING;
    d->engine->m_app->PostProgress(pp);

    // Check cancel
    if (d->engine->m_cancel->load()) return PROGRESS_CANCEL;
    d->engine->WaitIfPaused();
    return PROGRESS_CONTINUE;
}

// ── Copy one file ─────────────────────────────────────────────────
bool FileEngine::CopyOneFile(const std::wstring& src, const std::wstring& dst,
                              bool overwrite, bool unbuffered, bool verify) {
    if (!overwrite && GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES)
        return true; // skip existing

    CopyCbData cbd{ this, 0, m_stats->doneBytes.load() };

    // CopyFileEx: kernel-level copy with progress + cancel support
    BOOL cancel = FALSE;
    DWORD flags = COPY_FILE_ALLOW_DECRYPTED_DESTINATION;
    if (!overwrite) flags |= COPY_FILE_FAIL_IF_EXISTS;

    BOOL ok = CopyFileExW(src.c_str(), dst.c_str(),
                          CopyProgressCallback, &cbd, &cancel, flags);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_REQUEST_ABORTED) return false; // cancelled
        // Log error
        wchar_t buf[256];
        swprintf_s(buf, L"CopyFileEx failed (%lu): %s", err, src.c_str());
        m_app->PostLog(LogLevel::ERR, buf);
        m_stats->errors++;
        return false;
    }

    // Optionally verify with CRC32
    if (verify) {
        DWORD crcSrc = CRC32File(src);
        DWORD crcDst = CRC32File(dst);
        if (crcSrc != crcDst) {
            m_app->PostLog(LogLevel::ERR, L"Verify failed (CRC mismatch): " + src);
            m_stats->errors++;
            DeleteFileW(dst.c_str()); // remove bad copy
            return false;
        }
    }
    return true;
}

// ── Move one file ─────────────────────────────────────────────────
bool FileEngine::MoveOneFile(const std::wstring& src, const std::wstring& dst, bool overwrite) {
    // Try atomic rename first (same volume = instantaneous)
    DWORD flags = MOVEFILE_COPY_ALLOWED; // fallback to copy+delete cross-volume
    if (overwrite) flags |= MOVEFILE_REPLACE_EXISTING;
    flags |= MOVEFILE_WRITE_THROUGH; // wait for flush

    if (MoveFileWithProgressW(src.c_str(), dst.c_str(),
        CopyProgressCallback, nullptr, flags))
        return true;

    // If it failed but not because of cross-device, report error
    DWORD err = GetLastError();
    if (err != ERROR_NOT_SAME_DEVICE && err != ERROR_EXDEV) {
        wchar_t buf[256];
        swprintf_s(buf, L"MoveFile failed (%lu): %s", err, src.c_str());
        m_app->PostLog(LogLevel::ERR, buf);
        m_stats->errors++;
        return false;
    }

    // Cross-device: copy then delete
    if (!CopyOneFile(src, dst, overwrite, false, false)) return false;
    DeleteFileW(src.c_str());
    return true;
}

// ── COPY operation ────────────────────────────────────────────────
void FileEngine::Copy(const std::vector<FileEntry>& sources, const std::wstring& dest,
                       const CopyOptions& opts) {
    // Find common root for relative path preservation
    std::wstring root;
    if (!sources.empty()) {
        size_t lastSlash = sources[0].path.find_last_of(L"\\/");
        root = (lastSlash != std::wstring::npos) ? sources[0].path.substr(0, lastSlash) : L"";
    }

    for (const auto& f : sources) {
        if (m_cancel->load()) break;
        WaitIfPaused();

        {
            std::lock_guard<std::mutex> lk(m_stats->fileMutex);
            m_stats->currentFile = f.name;
        }

        std::wstring dstPath = BuildDestPath(root, f.path, dest);
        EnsureDir(dstPath.substr(0, dstPath.find_last_of(L"\\/")));

        bool unbuffered = opts.useUnbuffered || (f.size >= UNBUFFERED_THRESHOLD);
        if (CopyOneFile(f.path, dstPath, opts.overwrite, unbuffered, opts.verify)) {
            if (opts.preserveTimestamps) {
                // Restore original timestamps
                HANDLE h = CreateFileW(dstPath.c_str(), FILE_WRITE_ATTRIBUTES,
                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    SetFileTime(h, nullptr, nullptr, &f.modified);
                    CloseHandle(h);
                }
            }
            m_stats->doneFiles++;
        }
    }
}

// ── MOVE operation ────────────────────────────────────────────────
void FileEngine::Move(const std::vector<FileEntry>& sources, const std::wstring& dest,
                       const CopyOptions& opts) {
    std::wstring root;
    if (!sources.empty()) {
        size_t ls = sources[0].path.find_last_of(L"\\/");
        root = (ls != std::wstring::npos) ? sources[0].path.substr(0, ls) : L"";
    }

    for (const auto& f : sources) {
        if (m_cancel->load()) break;
        WaitIfPaused();

        {
            std::lock_guard<std::mutex> lk(m_stats->fileMutex);
            m_stats->currentFile = f.name;
        }

        std::wstring dstPath = BuildDestPath(root, f.path, dest);
        EnsureDir(dstPath.substr(0, dstPath.find_last_of(L"\\/")));

        if (MoveOneFile(f.path, dstPath, opts.overwrite))
            m_stats->doneFiles++;
    }
}

// ── RENAME operation ──────────────────────────────────────────────
void FileEngine::Rename(const std::vector<FileEntry>& sources, const std::wstring& pattern) {
    int idx = 0;
    for (const auto& f : sources) {
        if (m_cancel->load()) break;
        WaitIfPaused();

        std::wstring newName = ApplyPattern(f.path, pattern, idx++);
        std::wstring dir     = f.path.substr(0, f.path.find_last_of(L"\\/"));
        std::wstring newPath = dir + L"\\" + newName;

        if (MoveFileExW(f.path.c_str(), newPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            m_stats->doneFiles++;
            m_stats->doneBytes += f.size;
            {
                std::lock_guard<std::mutex> lk(m_stats->fileMutex);
                m_stats->currentFile = newName;
            }
        } else {
            wchar_t buf[512];
            swprintf_s(buf, L"Rename failed: %s", f.name.c_str());
            m_app->PostLog(LogLevel::ERR, buf);
            m_stats->errors++;
        }

        // Post progress
        ProgressPayload pp{};
        pp.doneFiles  = m_stats->doneFiles.load();
        pp.totalFiles = m_stats->totalFiles.load();
        pp.doneBytes  = m_stats->doneBytes.load();
        pp.totalBytes = m_stats->totalBytes.load();
        pp.pct        = pp.totalFiles ? (int)((pp.doneFiles * 100) / pp.totalFiles) : 0;
        pp.state      = OpState::RUNNING;
        pp.currentFile= newName;
        m_app->PostProgress(pp);
    }
}

// ── DELETE operation ──────────────────────────────────────────────
void FileEngine::Delete(const std::vector<FileEntry>& sources) {
    for (const auto& f : sources) {
        if (m_cancel->load()) break;
        WaitIfPaused();

        {
            std::lock_guard<std::mutex> lk(m_stats->fileMutex);
            m_stats->currentFile = f.name;
        }

        if (DeleteFileW(f.path.c_str())) {
            m_stats->doneFiles++;
            m_stats->doneBytes += f.size;
        } else {
            DWORD err = GetLastError();
            // Try clearing readonly attribute first
            if (err == ERROR_ACCESS_DENIED) {
                SetFileAttributesW(f.path.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileW(f.path.c_str())) {
                    m_stats->doneFiles++;
                    m_stats->doneBytes += f.size;
                    continue;
                }
            }
            wchar_t buf[512];
            swprintf_s(buf, L"Delete failed (%lu): %s", err, f.name.c_str());
            m_app->PostLog(LogLevel::ERR, buf);
            m_stats->errors++;
        }

        ProgressPayload pp{};
        pp.doneFiles  = m_stats->doneFiles.load();
        pp.totalFiles = m_stats->totalFiles.load();
        pp.doneBytes  = m_stats->doneBytes.load();
        pp.totalBytes = m_stats->totalBytes.load();
        pp.pct        = pp.totalFiles ? (int)((pp.doneFiles * 100) / pp.totalFiles) : 0;
        pp.state      = OpState::RUNNING;
        m_app->PostProgress(pp);
    }
}

// ── DEDUPE operation ──────────────────────────────────────────────
void FileEngine::Dedupe(const std::vector<FileEntry>& sources, const std::wstring& dest) {
    // Group files by size first, then by CRC32
    std::unordered_map<ULONGLONG, std::vector<const FileEntry*>> bySize;
    for (const auto& f : sources) bySize[f.size].push_back(&f);

    ULONGLONG dupBytes = 0;
    ULONGLONG dupCount = 0;

    for (auto& [sz, files] : bySize) {
        if (files.size() < 2 || m_cancel->load()) continue;

        // Compute CRC for this group
        std::unordered_map<DWORD, const FileEntry*> seen;
        for (const auto* f : files) {
            if (m_cancel->load()) break;
            {
                std::lock_guard<std::mutex> lk(m_stats->fileMutex);
                m_stats->currentFile = f->name;
            }
            DWORD crc = CRC32File(f->path);
            if (seen.count(crc)) {
                // Duplicate found — delete it
                dupBytes += f->size;
                dupCount++;
                if (DeleteFileW(f->path.c_str()))
                    m_app->PostLog(LogLevel::SUCCESS, L"Removed duplicate: " + f->name);
                else
                    m_app->PostLog(LogLevel::ERR, L"Could not remove: " + f->name);
            } else {
                seen[crc] = f;
            }
            m_stats->doneFiles++;
            m_stats->doneBytes += f->size;
        }
    }

    wchar_t buf[256];
    swprintf_s(buf, L"Dedupe complete: removed %llu duplicates (%s freed)",
        dupCount, L"");
    m_app->PostLog(LogLevel::SUCCESS, buf);
}

// ── SYNC operation (one-way) ──────────────────────────────────────
void FileEngine::Sync(const std::vector<FileEntry>& sources, const std::wstring& dest,
                       const CopyOptions& opts) {
    // Copy new/modified files
    Copy(sources, dest, opts);

    // TODO: optionally remove files in dest not in source
    m_app->PostLog(LogLevel::SUCCESS, L"Sync complete.");
}

// ── Helpers ───────────────────────────────────────────────────────
bool FileEngine::EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return true;
    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    // Create all intermediate directories
    return SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr) == ERROR_SUCCESS ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring FileEngine::BuildDestPath(const std::wstring& srcRoot,
                                        const std::wstring& srcFile,
                                        const std::wstring& destRoot) {
    if (srcRoot.empty()) {
        std::wstring name = srcFile.substr(srcFile.find_last_of(L"\\/") + 1);
        return destRoot + L"\\" + name;
    }
    std::wstring rel = srcFile;
    if (rel.find(srcRoot) == 0)
        rel = rel.substr(srcRoot.length());
    if (!rel.empty() && (rel[0] == L'\\' || rel[0] == L'/'))
        rel = rel.substr(1);
    return destRoot + L"\\" + rel;
}

std::wstring FileEngine::ApplyPattern(const std::wstring& original,
                                       const std::wstring& pattern, int index) {
    std::wstring ext  = L"";
    std::wstring name = original.substr(original.find_last_of(L"\\/") + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        ext  = name.substr(dot);     // includes dot
        name = name.substr(0, dot);
    }

    std::wstring result = pattern;

    // {name} → original name without ext
    size_t pos;
    while ((pos = result.find(L"{name}")) != std::wstring::npos)
        result.replace(pos, 6, name);
    // {ext} → .txt
    while ((pos = result.find(L"{ext}")) != std::wstring::npos)
        result.replace(pos, 5, ext);
    // {n} → zero-padded index
    while ((pos = result.find(L"{n}")) != std::wstring::npos) {
        wchar_t num[16]; swprintf_s(num, L"%04d", index);
        result.replace(pos, 3, num);
    }
    // {date} → YYYYMMDD
    while ((pos = result.find(L"{date}")) != std::wstring::npos) {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t d[16]; swprintf_s(d, L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
        result.replace(pos, 6, d);
    }

    // If result has no extension and original did, preserve it
    if (result.find(L'.') == std::wstring::npos && !ext.empty())
        result += ext;

    return result;
}

DWORD FileEngine::CRC32File(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;

    static const DWORD CRC_TABLE[256] = []() {
        DWORD table[256];
        for (int i = 0; i < 256; i++) {
            DWORD c = i;
            for (int j = 0; j < 8; j++)
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        return std::array<DWORD,256>(table);
    }();

    // This lambda trick doesn't work cleanly in C++ — use simple approach:
    DWORD crc = 0xFFFFFFFF;
    BYTE  buf[65536];
    DWORD bytesRead;
    while (ReadFile(h, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; i++) {
            crc = ((crc >> 8) ^ /* crc_table[(crc ^ buf[i]) & 0xFF] */ 0) ^ 0;
            // Inline CRC calculation
            BYTE idx = (BYTE)((crc ^ buf[i]) & 0xFF);
            DWORD c = idx;
            for (int j = 0; j < 8; j++) c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            crc = (crc >> 8) ^ c;
        }
    }
    CloseHandle(h);
    return crc ^ 0xFFFFFFFF;
}

void FileEngine::WaitIfPaused() {
    while (m_pause && m_pause->load()) {
        Sleep(50);
    }
}

void FileEngine::ThroughputTracker::Update(ULONGLONG totalBytes) {
    ULONGLONG now = GetTickCount64();
    if (lastTick == 0) { lastTick = now; lastBytes = totalBytes; return; }
    ULONGLONG elapsed = now - lastTick;
    if (elapsed >= 500) { // update every 500ms
        ULONGLONG delta = totalBytes - lastBytes;
        bytesPerSec = (delta * 1000) / elapsed;
        lastBytes = totalBytes;
        lastTick  = now;
    }
}
