#include "TitanShift.h"
#include "FileEngine.h"
#include <shlobj.h>      // SHCreateDirectoryExW
#include <shlwapi.h>
#include <winioctl.h>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <array>

#ifndef ERROR_EXDEV
#define ERROR_EXDEV 17
#endif

// Threshold: use unbuffered I/O for files larger than this
static const ULONGLONG UNBUFFERED_THRESHOLD = 128ULL * 1024 * 1024;

// CRC32 table (built once at startup)
static std::array<DWORD, 256> g_crcTable = []() {
    std::array<DWORD, 256> t{};
    for (int i = 0; i < 256; i++) {
        DWORD c = (DWORD)i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        t[i] = c;
    }
    return t;
}();

FileEngine::FileEngine(TitanShiftApp* app, OpStats* stats,
                       std::atomic<bool>* cancel, std::atomic<bool>* pause)
    : m_app(app), m_stats(stats), m_cancel(cancel), m_pause(pause) {}

FileEngine::~FileEngine() {}

// ── Scan all paths recursively ─────────────────────────────────────
std::vector<FileEntry> FileEngine::ScanPaths(const std::vector<std::wstring>& paths,
                                              std::atomic<bool>* cancel) {
    std::vector<FileEntry> result;
    result.reserve(4096);

    std::function<void(const std::wstring&)> walk = [&](const std::wstring& dir) {
        if (cancel && cancel->load()) return;

        WIN32_FIND_DATAW fd{};
        std::wstring pattern = dir + L"\\*";

        HANDLE hFind = FindFirstFileExW(
            pattern.c_str(),
            FindExInfoBasic,
            &fd,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH
        );
        if (hFind == INVALID_HANDLE_VALUE) return;

        do {
            if (cancel && cancel->load()) break;
            if (fd.cFileName[0] == L'.' &&
                (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
                continue;

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
            size_t lastSlash = p.find_last_of(L"\\/");
            e.name = (lastSlash != std::wstring::npos) ? p.substr(lastSlash + 1) : p;
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

// ── Progress callback ─────────────────────────────────────────────
struct CopyCbData {
    FileEngine* engine;
    OpStats*    stats;
    TitanShiftApp* app;
    std::atomic<bool>* cancel;
    ULONGLONG   baseBytes;
    FileEngine::ThroughputTracker* tracker;
};

DWORD CALLBACK FileEngine::CopyProgressCallback(
    LARGE_INTEGER, LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD,
    HANDLE, HANDLE, LPVOID lpData)
{
    auto* d = reinterpret_cast<CopyCbData*>(lpData);
    if (!d) return PROGRESS_CONTINUE;

    ULONGLONG transferred = static_cast<ULONGLONG>(TotalBytesTransferred.QuadPart);
    ULONGLONG newVal = d->baseBytes + transferred;
    d->stats->doneBytes.store(newVal);

    d->tracker->Update(newVal);

    ProgressPayload pp{};
    pp.doneBytes   = newVal;
    pp.totalBytes  = d->stats->totalBytes.load();
    pp.doneFiles   = d->stats->doneFiles.load();
    pp.totalFiles  = d->stats->totalFiles.load();
    pp.bytesPerSec = d->tracker->bytesPerSec;
    pp.pct = pp.totalBytes ? (int)((pp.doneBytes * 100) / pp.totalBytes) : 0;
    {
        std::lock_guard<std::mutex> lk(d->stats->fileMutex);
        pp.currentFile = d->stats->currentFile;
    }

    if (pp.bytesPerSec > 0 && pp.totalBytes > pp.doneBytes) {
        pp.etaSecs = (pp.totalBytes - pp.doneBytes) / pp.bytesPerSec;
    }
    pp.state = OpState::RUNNING;
    d->app->PostProgress(pp);

    if (d->cancel->load()) return PROGRESS_CANCEL;
    return PROGRESS_CONTINUE;
}

// ── Copy one file ─────────────────────────────────────────────────
bool FileEngine::CopyOneFile(const std::wstring& src, const std::wstring& dst,
                              bool overwrite, bool, bool verify) {
    if (!overwrite && GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES)
        return true;

    CopyCbData cbd{ this, m_stats, m_app, m_cancel,
                    m_stats->doneBytes.load(), &m_throughput };

    BOOL cancel = FALSE;
    DWORD flags = COPY_FILE_ALLOW_DECRYPTED_DESTINATION;
    if (!overwrite) flags |= COPY_FILE_FAIL_IF_EXISTS;

    BOOL ok = CopyFileExW(src.c_str(), dst.c_str(),
                          CopyProgressCallback, &cbd, &cancel, flags);

    // Pause loop after kernel returns each chunk
    while (m_pause && m_pause->load() && !m_cancel->load()) Sleep(50);

    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_REQUEST_ABORTED) return false;
        wchar_t buf[256];
        swprintf(buf, 256, L"CopyFileEx failed (%lu): %s", (unsigned long)err, src.c_str());
        m_app->PostLog(LogLevel::ERR, buf);
        m_stats->errors++;
        return false;
    }

    if (verify) {
        DWORD crcSrc = CRC32File(src);
        DWORD crcDst = CRC32File(dst);
        if (crcSrc != crcDst) {
            m_app->PostLog(LogLevel::ERR, L"Verify failed (CRC mismatch): " + src);
            m_stats->errors++;
            DeleteFileW(dst.c_str());
            return false;
        }
    }
    return true;
}

bool FileEngine::MoveOneFile(const std::wstring& src, const std::wstring& dst, bool overwrite) {
    DWORD flags = MOVEFILE_COPY_ALLOWED;
    if (overwrite) flags |= MOVEFILE_REPLACE_EXISTING;
    flags |= MOVEFILE_WRITE_THROUGH;

    if (MoveFileWithProgressW(src.c_str(), dst.c_str(),
        nullptr, nullptr, flags))
        return true;

    DWORD err = GetLastError();
    if (err != ERROR_NOT_SAME_DEVICE && err != ERROR_EXDEV) {
        wchar_t buf[256];
        swprintf(buf, 256, L"MoveFile failed (%lu): %s", (unsigned long)err, src.c_str());
        m_app->PostLog(LogLevel::ERR, buf);
        m_stats->errors++;
        return false;
    }

    if (!CopyOneFile(src, dst, overwrite, false, false)) return false;
    DeleteFileW(src.c_str());
    return true;
}

void FileEngine::Copy(const std::vector<FileEntry>& sources, const std::wstring& dest,
                       const CopyOptions& opts) {
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
        size_t parentEnd = dstPath.find_last_of(L"\\/");
        if (parentEnd != std::wstring::npos)
            EnsureDir(dstPath.substr(0, parentEnd));

        bool unbuffered = opts.useUnbuffered || (f.size >= UNBUFFERED_THRESHOLD);
        if (CopyOneFile(f.path, dstPath, opts.overwrite, unbuffered, opts.verify)) {
            if (opts.preserveTimestamps) {
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
        size_t parentEnd = dstPath.find_last_of(L"\\/");
        if (parentEnd != std::wstring::npos)
            EnsureDir(dstPath.substr(0, parentEnd));

        if (MoveOneFile(f.path, dstPath, opts.overwrite)) {
            m_stats->doneFiles++;
            m_stats->doneBytes += f.size;
        }
    }
}

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
            m_app->PostLog(LogLevel::ERR, L"Rename failed: " + f.name);
            m_stats->errors++;
        }

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

void FileEngine::Delete(const std::vector<FileEntry>& sources) {
    for (const auto& f : sources) {
        if (m_cancel->load()) break;
        WaitIfPaused();

        {
            std::lock_guard<std::mutex> lk(m_stats->fileMutex);
            m_stats->currentFile = f.name;
        }

        BOOL ok = DeleteFileW(f.path.c_str());
        if (!ok && GetLastError() == ERROR_ACCESS_DENIED) {
            SetFileAttributesW(f.path.c_str(), FILE_ATTRIBUTE_NORMAL);
            ok = DeleteFileW(f.path.c_str());
        }

        if (ok) {
            m_stats->doneFiles++;
            m_stats->doneBytes += f.size;
        } else {
            m_app->PostLog(LogLevel::ERR, L"Delete failed: " + f.name);
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

void FileEngine::Dedupe(const std::vector<FileEntry>& sources, const std::wstring&) {
    std::unordered_map<ULONGLONG, std::vector<const FileEntry*>> bySize;
    for (const auto& f : sources) bySize[f.size].push_back(&f);

    ULONGLONG dupCount = 0;

    for (auto& [sz, files] : bySize) {
        if (files.size() < 2 || m_cancel->load()) continue;

        std::unordered_map<DWORD, const FileEntry*> seen;
        for (const auto* f : files) {
            if (m_cancel->load()) break;
            WaitIfPaused();
            {
                std::lock_guard<std::mutex> lk(m_stats->fileMutex);
                m_stats->currentFile = f->name;
            }
            DWORD crc = CRC32File(f->path);
            if (seen.count(crc)) {
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

    wchar_t buf[128];
    swprintf(buf, 128, L"Dedupe complete: %llu duplicates removed", dupCount);
    m_app->PostLog(LogLevel::SUCCESS, buf);
}

void FileEngine::Sync(const std::vector<FileEntry>& sources, const std::wstring& dest,
                       const CopyOptions& opts) {
    Copy(sources, dest, opts);
    m_app->PostLog(LogLevel::SUCCESS, L"Sync complete.");
}

bool FileEngine::EnsureDir(const std::wstring& dir) {
    if (dir.empty()) return true;
    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    int r = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

std::wstring FileEngine::BuildDestPath(const std::wstring& srcRoot,
                                        const std::wstring& srcFile,
                                        const std::wstring& destRoot) {
    if (srcRoot.empty()) {
        size_t ls = srcFile.find_last_of(L"\\/");
        std::wstring name = (ls != std::wstring::npos) ? srcFile.substr(ls + 1) : srcFile;
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
    size_t ls = original.find_last_of(L"\\/");
    std::wstring name = (ls != std::wstring::npos) ? original.substr(ls + 1) : original;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        ext  = name.substr(dot);
        name = name.substr(0, dot);
    }

    std::wstring result = pattern;
    size_t pos;
    while ((pos = result.find(L"{name}")) != std::wstring::npos)
        result.replace(pos, 6, name);
    while ((pos = result.find(L"{ext}")) != std::wstring::npos)
        result.replace(pos, 5, ext);
    while ((pos = result.find(L"{n}")) != std::wstring::npos) {
        wchar_t num[16]; swprintf(num, 16, L"%04d", index);
        result.replace(pos, 3, num);
    }
    while ((pos = result.find(L"{date}")) != std::wstring::npos) {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t d[16]; swprintf(d, 16, L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
        result.replace(pos, 6, d);
    }

    if (result.find(L'.') == std::wstring::npos && !ext.empty())
        result += ext;

    return result;
}

DWORD FileEngine::CRC32File(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;

    DWORD crc = 0xFFFFFFFF;
    BYTE  buf[65536];
    DWORD bytesRead;
    while (ReadFile(h, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; i++) {
            crc = (crc >> 8) ^ g_crcTable[(crc ^ buf[i]) & 0xFF];
        }
    }
    CloseHandle(h);
    return crc ^ 0xFFFFFFFF;
}

void FileEngine::WaitIfPaused() {
    while (m_pause && m_pause->load() && !m_cancel->load()) Sleep(50);
}

void FileEngine::ThroughputTracker::Update(ULONGLONG totalBytes) {
    ULONGLONG now = GetTickCount64();
    if (lastTick == 0) { lastTick = now; lastBytes = totalBytes; return; }
    ULONGLONG elapsed = now - lastTick;
    if (elapsed >= 500) {
        ULONGLONG delta = (totalBytes >= lastBytes) ? (totalBytes - lastBytes) : 0;
        bytesPerSec = (delta * 1000) / elapsed;
        lastBytes = totalBytes;
        lastTick  = now;
    }
}
