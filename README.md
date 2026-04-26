# TitanShift Native — C++ Win32 Build Guide

## Requirements
- **Visual Studio 2022** (Community or higher) with:
  - "Desktop development with C++" workload
  - Windows 11 SDK (10.0.22000.0 or later)
- **CMake 3.20+** (included with VS2022)
- Windows 10 v1903+ / Windows 11

---

## Build (CMake + MSVC)

```bat
cd E:\mano_github_pvt\TitanShift_Native

:: Configure (Release, x64)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release

:: Build
cmake --build build --config Release --parallel

:: Output
build\Release\TitanShift.exe
```

## Build (VS Developer Command Prompt — fastest)

```bat
cd E:\mano_github_pvt\TitanShift_Native

:: One-shot build
cl /O2 /Ob2 /Oi /GL /arch:AVX2 /W3 /MP /std:c++20 ^
   /I include ^
   src\main.cpp src\TitanShiftApp.cpp src\FileEngine.cpp ^
   /link /LTCG /SUBSYSTEM:WINDOWS /OPT:REF ^
   d2d1.lib dwrite.lib comctl32.lib shlwapi.lib shell32.lib ^
   pdh.lib psapi.lib ntdll.lib dwmapi.lib ole32.lib oleaut32.lib uuid.lib ^
   /OUT:TitanShift.exe
```

---

## Architecture

```
TitanShift_Native/
├── src/
│   ├── main.cpp              — wWinMain, COM init, DPI setup
│   ├── TitanShiftApp.cpp     — Win32 window, Direct2D rendering, all UI
│   └── FileEngine.cpp        — All file operations (kernel APIs)
├── include/
│   ├── TitanShift.h          — App class, structs, enums, WM constants
│   ├── FileEngine.h          — FileEngine declaration
│   └── MetricsEngine.h       — PDH metrics (header-only impl)
├── resources/
│   └── titanshift.rc         — Version info + DPI manifest
└── CMakeLists.txt
```

---

## Windows APIs used (zero dependencies)

| Subsystem | API |
|-----------|-----|
| Rendering | Direct2D, DirectWrite |
| File copy | `CopyFileEx` with progress callback + cancel flag |
| File move | `MoveFileWithProgressW` (atomic rename on same volume) |
| File scan | `FindFirstFileExW(FIND_FIRST_EX_LARGE_FETCH)` |
| Metrics — CPU | PDH `\Processor(_Total)\% Processor Time` |
| Metrics — Disk | PDH `\PhysicalDisk(_Total)\Disk Read/Write Bytes/sec` |
| Metrics — RAM | `GlobalMemoryStatusEx` |
| Directory create | `SHCreateDirectoryExW` |
| File picker | `IFileOpenDialog` (modern shell dialog) |
| DPI | `SetProcessDpiAwarenessContext(PER_MONITOR_V2)` |
| Threading | `std::thread` + `std::atomic` + Win32 PostMessage |

---

## Features

| Feature | Detail |
|---------|--------|
| COPY | `CopyFileEx` — kernel copy, cancel flag, progress callback |
| MOVE | `MoveFileWithProgressW` — zero-cost same-volume rename |
| RENAME | Bulk rename with tokens: `{name}` `{ext}` `{n}` `{date}` |
| DELETE | Permanent delete, clears read-only attr automatically |
| DEDUPE | CRC32-based duplicate detection and removal |
| SYNC | One-way sync (copy new/modified only) |
| Verify | CRC32 post-copy verification |
| Pause/Resume | Atomic pause flag checked per-file |
| Cancel | `CopyFileEx` cancel flag — kernel-level abort |
| Multi-thread | Configurable 1-8 file worker threads |
| Metrics | PDH counters — CPU, RAM, Disk R/W in MB/s |
| Graph | 60-sample rolling Direct2D throughput graph |
| DPI | Per-monitor DPI v2 — crisp on any screen density |
| Long paths | Manifest `longPathAware=true` — supports >260 char paths |

---

## Performance notes

- **Same-volume move**: Instantaneous — uses `MoveFileExW` rename, zero bytes copied
- **Large file copy**: `FILE_FLAG_NO_BUFFERING` auto-selected for files >128MB, bypasses OS cache
- **Directory scan**: `FIND_FIRST_EX_LARGE_FETCH` hint fetches many entries per syscall
- **UI**: Direct2D hardware-accelerated — renders at display framerate, never blocks I/O
- **Thread model**: Worker thread does all I/O; UI thread only renders via `PostMessage`
- **Memory**: No buffering of file contents — 64KB streaming chunks via kernel callback

---

## Destination

Place in: `E:\mano_github_pvt\TitanShift_Native\`
