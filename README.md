# TitanShift

**Bulk file operations at TB scale — Copy · Move · Rename · Delete**
with a live hardware dashboard (CPU, Memory, Disk throughput).

---

## Setup

```bash
# 1. Install dependencies
npm install

# 2. Run in development
npm start

# 3. Build installer (Windows)
npm run build:win

# 4. Build for Mac / Linux
npm run build:mac
npm run build:linux
```

Requires **Node.js 18+** and **npm**.

---

## Features

| Feature | Detail |
|---------|--------|
| Copy | Streams files in 64KB chunks — low memory even for TB data |
| Move | Copy + delete source, removes empty dirs |
| Rename | Bulk rename with pattern: `name_0.ext, name_1.ext…` |
| Delete | Permanent delete (no trash) |
| Pause / Resume | Pause mid-operation, resume anytime |
| Cancel | Abort operation gracefully |
| Live CPU graph | Win11 Task Manager style throughput graph |
| Disk R/W meters | Real-time MB/s read and write bars |
| Memory ring | % used with absolute used/total |
| Log panel | Timestamped operation log with error highlighting |

---

## Architecture

```
main.js       — Electron main process, file I/O, IPC handlers
preload.js    — Secure context bridge (no nodeIntegration)
renderer.js   — All UI logic, metrics polling, canvas graph
index.html    — App shell
style.css     — Industrial dark theme
```

---

## Low-resource design

- Files are streamed in **64KB chunks** — never loads whole file into RAM
- Metrics polled every **1.5 seconds** — minimal CPU overhead
- Graph canvas redraws only on new data
- No React/Vue/webpack — pure HTML+JS, fast startup

---

## Destination folder: `E:\mano_github_pvt\TitanShift`
