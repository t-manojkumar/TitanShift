#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <pdh.h>
#include <psapi.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include "TitanShift.h"

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")

/*
 * MetricsEngine — Real-time hardware metrics via Windows PDH & PSAPI
 *
 * APIs used:
 *   PdhOpenQuery / PdhAddCounter / PdhCollectQueryData
 *     — \Processor(_Total)\% Processor Time
 *     — \PhysicalDisk(_Total)\Disk Read Bytes/sec
 *     — \PhysicalDisk(_Total)\Disk Write Bytes/sec
 *   GlobalMemoryStatusEx  — RAM usage
 *   GetNetworkParams / GetIfTable2  — network throughput
 *
 * Runs in a dedicated background thread, pushes updates via WM_METRICS_UPDATE
 */
class MetricsEngine {
public:
    explicit MetricsEngine(HWND targetHwnd);
    ~MetricsEngine();

    void Start();
    void Stop();

    SystemMetrics GetMetrics();

private:
    HWND              m_hwnd;
    std::thread       m_thread;
    std::atomic<bool> m_running{false};

    // PDH
    PDH_HQUERY   m_query{nullptr};
    PDH_HCOUNTER m_cpuCounter{nullptr};
    PDH_HCOUNTER m_diskReadCounter{nullptr};
    PDH_HCOUNTER m_diskWriteCounter{nullptr};

    // State
    SystemMetrics m_metrics{};
    std::mutex    m_mutex;

    // History (60 samples = 60 seconds at 1s interval)
    static const int HISTORY = 60;

    void ThreadProc();
    bool InitPDH();
    void CollectSample();
    void CollectMemory();
    void CollectNetwork();
};

// ─────────────────────────────────────────────────────────────────
// Implementation
// ─────────────────────────────────────────────────────────────────
MetricsEngine::MetricsEngine(HWND hwnd) : m_hwnd(hwnd) {}

MetricsEngine::~MetricsEngine() { Stop(); }

void MetricsEngine::Start() {
    m_running = true;
    m_thread  = std::thread(&MetricsEngine::ThreadProc, this);
}

void MetricsEngine::Stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
    if (m_query) { PdhCloseQuery(m_query); m_query = nullptr; }
}

SystemMetrics MetricsEngine::GetMetrics() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_metrics;
}

bool MetricsEngine::InitPDH() {
    PDH_STATUS st = PdhOpenQueryW(nullptr, 0, &m_query);
    if (st != ERROR_SUCCESS) return false;

    // CPU load
    PdhAddCounterW(m_query,
        L"\\Processor(_Total)\\% Processor Time", 0, &m_cpuCounter);

    // Disk throughput — try PhysicalDisk first, fallback to LogicalDisk
    if (PdhAddCounterW(m_query,
        L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &m_diskReadCounter) != ERROR_SUCCESS) {
        PdhAddCounterW(m_query,
            L"\\LogicalDisk(_Total)\\Disk Read Bytes/sec", 0, &m_diskReadCounter);
    }
    if (PdhAddCounterW(m_query,
        L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &m_diskWriteCounter) != ERROR_SUCCESS) {
        PdhAddCounterW(m_query,
            L"\\LogicalDisk(_Total)\\Disk Write Bytes/sec", 0, &m_diskWriteCounter);
    }

    // Prime the counters — PDH needs two collections to compute a rate
    PdhCollectQueryData(m_query);
    return true;
}

void MetricsEngine::ThreadProc() {
    if (!InitPDH()) {
        // PDH failed — run with memory-only metrics
        while (m_running) {
            CollectMemory();
            if (m_hwnd) PostMessageW(m_hwnd, WM_METRICS_UPDATE, 0, 0);
            Sleep(1500);
        }
        return;
    }

    while (m_running) {
        Sleep(1500); // 1.5s interval
        CollectSample();
        CollectMemory();

        if (m_hwnd) PostMessageW(m_hwnd, WM_METRICS_UPDATE, 0, 0);
    }
}

void MetricsEngine::CollectSample() {
    PDH_STATUS st = PdhCollectQueryData(m_query);
    if (st != ERROR_SUCCESS) return;

    PDH_FMT_COUNTERVALUE val{};

    std::lock_guard<std::mutex> lk(m_mutex);

    // CPU
    if (m_cpuCounter &&
        PdhGetFormattedCounterValue(m_cpuCounter, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
        m_metrics.cpuLoad = (float)std::min(100.0, std::max(0.0, val.doubleValue));
        m_metrics.cpuHistory.push_back(m_metrics.cpuLoad);
        if ((int)m_metrics.cpuHistory.size() > HISTORY) m_metrics.cpuHistory.pop_front();
    }

    // Disk read
    if (m_diskReadCounter &&
        PdhGetFormattedCounterValue(m_diskReadCounter, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
        m_metrics.diskReadMBs = (float)(val.doubleValue / 1e6);
        m_metrics.diskReadHistory.push_back(m_metrics.diskReadMBs);
        if ((int)m_metrics.diskReadHistory.size() > HISTORY) m_metrics.diskReadHistory.pop_front();
    }

    // Disk write
    if (m_diskWriteCounter &&
        PdhGetFormattedCounterValue(m_diskWriteCounter, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
        m_metrics.diskWriteMBs = (float)(val.doubleValue / 1e6);
        m_metrics.diskWriteHistory.push_back(m_metrics.diskWriteMBs);
        if ((int)m_metrics.diskWriteHistory.size() > HISTORY) m_metrics.diskWriteHistory.pop_front();
    }
}

void MetricsEngine::CollectMemory() {
    MEMORYSTATUSEX ms{ sizeof(ms) };
    if (GlobalMemoryStatusEx(&ms)) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_metrics.memTotal    = ms.ullTotalPhys;
        m_metrics.memUsed     = ms.ullTotalPhys - ms.ullAvailPhys;
        m_metrics.memUsedPct  = (float)ms.dwMemoryLoad;
    }
}
