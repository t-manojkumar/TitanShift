#pragma once
#include "TitanShift.h"
#include <pdh.h>
#include <psapi.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>

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

    PDH_HQUERY   m_query{nullptr};
    PDH_HCOUNTER m_cpuCounter{nullptr};
    PDH_HCOUNTER m_diskReadCounter{nullptr};
    PDH_HCOUNTER m_diskWriteCounter{nullptr};

    SystemMetrics m_metrics{};
    std::mutex    m_mutex;

    static const int HISTORY = 60;

    void ThreadProc();
    bool InitPDH();
    void CollectSample();
    void CollectMemory();
};

inline MetricsEngine::MetricsEngine(HWND hwnd) : m_hwnd(hwnd) {}
inline MetricsEngine::~MetricsEngine() { Stop(); }

inline void MetricsEngine::Start() {
    m_running = true;
    m_thread  = std::thread(&MetricsEngine::ThreadProc, this);
}

inline void MetricsEngine::Stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
    if (m_query) { PdhCloseQuery(m_query); m_query = nullptr; }
}

inline SystemMetrics MetricsEngine::GetMetrics() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_metrics;
}

inline bool MetricsEngine::InitPDH() {
    if (PdhOpenQueryW(nullptr, 0, &m_query) != ERROR_SUCCESS) return false;

    PdhAddCounterW(m_query, L"\\Processor(_Total)\\% Processor Time", 0, &m_cpuCounter);

    if (PdhAddCounterW(m_query, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",
        0, &m_diskReadCounter) != ERROR_SUCCESS) {
        PdhAddCounterW(m_query, L"\\LogicalDisk(_Total)\\Disk Read Bytes/sec",
            0, &m_diskReadCounter);
    }
    if (PdhAddCounterW(m_query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",
        0, &m_diskWriteCounter) != ERROR_SUCCESS) {
        PdhAddCounterW(m_query, L"\\LogicalDisk(_Total)\\Disk Write Bytes/sec",
            0, &m_diskWriteCounter);
    }

    PdhCollectQueryData(m_query); // prime
    return true;
}

inline void MetricsEngine::ThreadProc() {
    bool pdhOk = InitPDH();

    while (m_running) {
        Sleep(1500);
        if (pdhOk) CollectSample();
        CollectMemory();
        if (m_hwnd) PostMessageW(m_hwnd, WM_METRICS_UPDATE, 0, 0);
    }
}

inline void MetricsEngine::CollectSample() {
    if (PdhCollectQueryData(m_query) != ERROR_SUCCESS) return;

    PDH_FMT_COUNTERVALUE val{};
    std::lock_guard<std::mutex> lk(m_mutex);

    if (m_cpuCounter &&
        PdhGetFormattedCounterValue(m_cpuCounter, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
        double v = val.doubleValue;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        m_metrics.cpuLoad = (float)v;
        m_metrics.cpuHistory.push_back(m_metrics.cpuLoad);
        if ((int)m_metrics.cpuHistory.size() > HISTORY) m_metrics.cpuHistory.pop_front();
    }

    if (m_diskReadCounter &&
        PdhGetFormattedCounterValue(m_diskReadCounter, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
        m_metrics.diskReadMBs = (float)(val.doubleValue / 1e6);
        if (m_metrics.diskReadMBs < 0) m_metrics.diskReadMBs = 0;
        m_metrics.diskReadHistory.push_back(m_metrics.diskReadMBs);
        if ((int)m_metrics.diskReadHistory.size() > HISTORY) m_metrics.diskReadHistory.pop_front();
    }

    if (m_diskWriteCounter &&
        PdhGetFormattedCounterValue(m_diskWriteCounter, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
        m_metrics.diskWriteMBs = (float)(val.doubleValue / 1e6);
        if (m_metrics.diskWriteMBs < 0) m_metrics.diskWriteMBs = 0;
        m_metrics.diskWriteHistory.push_back(m_metrics.diskWriteMBs);
        if ((int)m_metrics.diskWriteHistory.size() > HISTORY) m_metrics.diskWriteHistory.pop_front();
    }
}

inline void MetricsEngine::CollectMemory() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_metrics.memTotal    = ms.ullTotalPhys;
        m_metrics.memUsed     = ms.ullTotalPhys - ms.ullAvailPhys;
        m_metrics.memUsedPct  = (float)ms.dwMemoryLoad;
    }
}
