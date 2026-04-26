const { app, BrowserWindow, ipcMain, dialog, shell } = require('electron');
const path = require('path');
const fs = require('fs');
const fsp = require('fs').promises;
const si = require('systeminformation');

let mainWindow;
let activeOperations = new Map(); // operationId -> { cancelled, paused }

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1100,
    height: 720,
    minWidth: 900,
    minHeight: 600,
    frame: false,
    backgroundColor: '#0a0a0a',
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      preload: path.join(__dirname, 'preload.js')
    }
  });
  mainWindow.loadFile('index.html');
}

app.whenReady().then(createWindow);
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });

// ── Window controls ──────────────────────────────────────────────
ipcMain.on('window-minimize', () => mainWindow.minimize());
ipcMain.on('window-maximize', () => mainWindow.isMaximized() ? mainWindow.unmaximize() : mainWindow.maximize());
ipcMain.on('window-close', () => mainWindow.close());

// ── Dialog: pick files/folders ────────────────────────────────────
ipcMain.handle('dialog-select-files', async (_, opts) => {
  const result = await dialog.showOpenDialog(mainWindow, {
    properties: opts.folders
      ? ['openDirectory', 'multiSelections']
      : ['openFile', 'multiSelections'],
    title: opts.title || 'Select files'
  });
  return result.canceled ? [] : result.filePaths;
});

ipcMain.handle('dialog-select-dest', async () => {
  const result = await dialog.showOpenDialog(mainWindow, {
    properties: ['openDirectory'],
    title: 'Select destination folder'
  });
  return result.canceled ? null : result.filePaths[0];
});

// ── System metrics ────────────────────────────────────────────────
ipcMain.handle('get-system-stats', async () => {
  const [cpu, mem, disk, fsStats] = await Promise.all([
    si.currentLoad(),
    si.mem(),
    si.fsStats(),
    si.disksIO().catch(() => ({ rIO_sec: 0, wIO_sec: 0 }))
  ]);
  return {
    cpu: Math.round(cpu.currentLoad),
    cpuHistory: null,
    memUsed: mem.used,
    memTotal: mem.total,
    memPct: Math.round((mem.used / mem.total) * 100),
    diskRead: disk.rx_sec || 0,
    diskWrite: disk.wx_sec || 0,
  };
});

// ── Recursive file list ────────────────────────────────────────────
async function listAllFiles(src) {
  const items = [];
  async function walk(p) {
    const stat = await fsp.stat(p);
    if (stat.isDirectory()) {
      const children = await fsp.readdir(p);
      for (const c of children) await walk(path.join(p, c));
    } else {
      items.push({ path: p, size: stat.size });
    }
  }
  await walk(src);
  return items;
}

// ── Core operation runner ─────────────────────────────────────────
async function runOperation(event, opId, sources, dest, mode, newName) {
  activeOperations.set(opId, { cancelled: false, paused: false });

  let totalFiles = 0, totalBytes = 0;
  let doneFiles = 0, doneBytes = 0;
  const allFiles = [];

  // Phase 1: scan
  event.sender.send('op-progress', { opId, phase: 'scanning', pct: 0, status: 'SCANNING' });
  for (const src of sources) {
    const files = await listAllFiles(src);
    for (const f of files) { allFiles.push({ ...f, src }); totalBytes += f.size; totalFiles++; }
  }
  event.sender.send('op-progress', { opId, phase: 'scanning', totalFiles, totalBytes, pct: 0, status: 'IN_PROGRESS' });

  const CHUNK = 64 * 1024; // 64KB chunks for low memory

  // Phase 2: process files
  for (const file of allFiles) {
    const op = activeOperations.get(opId);
    if (op.cancelled) {
      event.sender.send('op-progress', { opId, pct: Math.round((doneFiles / totalFiles) * 100), doneFiles, totalFiles, doneBytes, totalBytes, status: 'CANCELLED' });
      activeOperations.delete(opId);
      return;
    }

    // Pause loop
    while (activeOperations.get(opId)?.paused) {
      await new Promise(r => setTimeout(r, 200));
    }

    const relPath = path.relative(file.src, file.path);
    let destPath;

    if (mode === 'rename' && newName) {
      const ext = path.extname(file.path);
      const base = path.basename(newName, path.extname(newName));
      destPath = path.join(dest || path.dirname(file.path),
        totalFiles === 1 ? newName : `${base}_${doneFiles}${ext}`);
    } else {
      destPath = dest ? path.join(dest, relPath) : file.path;
    }

    try {
      if (mode === 'copy' || mode === 'move' || mode === 'rename') {
        if (dest || mode === 'rename') {
          await fsp.mkdir(path.dirname(destPath), { recursive: true });
          // Stream copy in chunks — low memory usage
          await new Promise((resolve, reject) => {
            const rs = fs.createReadStream(file.path, { highWaterMark: CHUNK });
            const ws = fs.createWriteStream(destPath);
            let written = 0;
            rs.on('data', chunk => {
              written += chunk.length;
              const filePct = Math.round((written / file.size) * 100);
              event.sender.send('op-progress', {
                opId, currentFile: path.basename(file.path),
                filePct, pct: Math.round(((doneBytes + written) / totalBytes) * 100),
                doneFiles, totalFiles, doneBytes: doneBytes + written, totalBytes,
                status: 'IN_PROGRESS'
              });
            });
            rs.on('error', reject);
            ws.on('error', reject);
            ws.on('close', resolve);
            rs.pipe(ws);
          });
          if (mode === 'move' || mode === 'rename') await fsp.unlink(file.path);
        }
      } else if (mode === 'delete') {
        await fsp.unlink(file.path);
      }
    } catch (err) {
      event.sender.send('op-log', { opId, level: 'error', msg: `Failed: ${file.path} — ${err.message}` });
    }

    doneFiles++;
    doneBytes += file.size;
    const pct = Math.round((doneBytes / totalBytes) * 100);
    event.sender.send('op-progress', { opId, pct, doneFiles, totalFiles, doneBytes, totalBytes, currentFile: path.basename(file.path), status: 'IN_PROGRESS' });
  }

  // Clean up empty dirs after move
  if (mode === 'move') {
    for (const src of sources) {
      try { await fsp.rm(src, { recursive: true, force: true }); } catch (_) {}
    }
  }

  event.sender.send('op-progress', { opId, pct: 100, doneFiles, totalFiles, doneBytes, totalBytes, status: 'COMPLETE' });
  activeOperations.delete(opId);
}

ipcMain.handle('start-operation', async (event, { opId, sources, dest, mode, newName }) => {
  runOperation(event, opId, sources, dest, mode, newName).catch(err => {
    event.sender.send('op-progress', { opId, status: 'ERROR', error: err.message });
  });
  return { started: true };
});

ipcMain.on('op-cancel', (_, opId) => {
  if (activeOperations.has(opId)) activeOperations.get(opId).cancelled = true;
});

ipcMain.on('op-pause', (_, opId) => {
  if (activeOperations.has(opId)) {
    const op = activeOperations.get(opId);
    op.paused = !op.paused;
    mainWindow.webContents.send('op-paused', { opId, paused: op.paused });
  }
});

ipcMain.handle('open-path', async (_, p) => { shell.showItemInFolder(p); });
