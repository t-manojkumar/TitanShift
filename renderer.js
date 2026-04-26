'use strict';

// ── State ─────────────────────────────────────────────────────────
const state = {
  mode: 'copy',
  sources: [],        // [{path, name, size}]
  dest: null,
  currentOpId: null,
  isPaused: false,
  isRunning: false,
  readHistory:  new Array(60).fill(0),
  writeHistory: new Array(60).fill(0),
};

// ── DOM refs ──────────────────────────────────────────────────────
const $ = id => document.getElementById(id);
const modeBtns    = document.querySelectorAll('.mode-btn');
const dropZone    = $('drop-zone-src');
const srcList     = $('src-list');
const destGroup   = $('dest-group');
const renameGroup = $('rename-group');
const destDisplay = $('dest-display');
const renameInput = $('rename-input');
const runBtn      = $('btn-run');
const runBtnText  = $('run-btn-text');
const logPanel    = $('log-panel');
const statusBadge = $('status-badge');
const progressMeta= $('progress-meta');
const currentFile = $('current-file');
const progressFill= $('progress-fill');
const progressPct = $('progress-pct');
const btnCancel   = $('btn-cancel');
const btnPause    = $('btn-pause');
const cpuVal      = $('cpu-val');
const cpuSub      = $('cpu-sub');
const memVal      = $('mem-val');
const memSub      = $('mem-sub');
const cpuRing     = $('cpu-ring');
const memRing     = $('mem-ring');
const diskReadBar = $('disk-read-bar');
const diskWriteBar= $('disk-write-bar');
const diskReadVal = $('disk-read-val');
const diskWriteVal= $('disk-write-val');
const canvas      = $('graph-canvas');
const ctx         = canvas.getContext('2d');

// ── Titlebar ──────────────────────────────────────────────────────
$('btn-minimize').onclick = () => window.titanAPI.minimize();
$('btn-maximize').onclick = () => window.titanAPI.maximize();
$('btn-close').onclick    = () => window.titanAPI.close();

// ── Mode selection ────────────────────────────────────────────────
modeBtns.forEach(btn => {
  btn.onclick = () => {
    modeBtns.forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    state.mode = btn.dataset.mode;
    updateModeUI();
  };
});

function updateModeUI() {
  const isDelete = state.mode === 'delete';
  const isRename = state.mode === 'rename';
  destGroup.classList.toggle('hidden', isDelete || isRename);
  renameGroup.classList.toggle('hidden', !isRename);
  runBtnText.textContent = {
    copy: 'RUN COPY', move: 'RUN MOVE',
    rename: 'RUN RENAME', delete: 'DELETE FILES'
  }[state.mode];
  runBtn.className = 'run-btn' + (isDelete ? ' danger-run' : '');
}

// ── Source files ──────────────────────────────────────────────────
dropZone.onclick = () => addFiles(false);

$('btn-add-files').onclick  = () => addFiles(false);
$('btn-add-folder').onclick = () => addFiles(true);
$('btn-clear-src').onclick  = () => { state.sources = []; renderSrcList(); };

async function addFiles(folders) {
  const paths = await window.titanAPI.selectFiles({ folders });
  for (const p of paths) {
    if (!state.sources.find(s => s.path === p)) {
      const name = p.split(/[/\\]/).pop();
      state.sources.push({ path: p, name, size: null });
    }
  }
  renderSrcList();
}

function renderSrcList() {
  srcList.innerHTML = '';
  if (!state.sources.length) { srcList.classList.remove('has-files'); return; }
  srcList.classList.add('has-files');
  state.sources.forEach((f, i) => {
    const el = document.createElement('div');
    el.className = 'file-item';
    el.innerHTML = `
      <span class="file-name" title="${f.path}">${f.name}</span>
      <button class="file-remove" data-i="${i}">✕</button>`;
    srcList.appendChild(el);
  });
  srcList.querySelectorAll('.file-remove').forEach(b => {
    b.onclick = () => { state.sources.splice(+b.dataset.i, 1); renderSrcList(); };
  });
}

// Drag-drop onto drop zone
dropZone.addEventListener('dragover', e => { e.preventDefault(); dropZone.classList.add('drag-over'); });
dropZone.addEventListener('dragleave', () => dropZone.classList.remove('drag-over'));
dropZone.addEventListener('drop', e => {
  e.preventDefault(); dropZone.classList.remove('drag-over');
  const files = [...e.dataTransfer.files];
  files.forEach(f => {
    if (!state.sources.find(s => s.path === f.path)) {
      state.sources.push({ path: f.path, name: f.name, size: f.size });
    }
  });
  renderSrcList();
});

// ── Destination ───────────────────────────────────────────────────
$('btn-pick-dest').onclick = async () => {
  const p = await window.titanAPI.selectDest();
  if (p) {
    state.dest = p;
    destDisplay.textContent = p;
    destDisplay.classList.add('set');
  }
};

// ── Run operation ─────────────────────────────────────────────────
runBtn.onclick = async () => {
  if (!state.sources.length) { addLog('No source files selected.', 'error'); return; }
  if ((state.mode === 'copy' || state.mode === 'move') && !state.dest) {
    addLog('No destination selected.', 'error'); return;
  }
  if (state.mode === 'rename' && !renameInput.value.trim()) {
    addLog('Enter a new name.', 'error'); return;
  }

  const opId = `op_${Date.now()}`;
  state.currentOpId = opId;
  state.isPaused = false;
  state.isRunning = true;

  setStatus('IN_PROGRESS');
  btnCancel.disabled = false;
  btnPause.disabled  = false;
  runBtn.disabled    = true;

  addLog(`Starting ${state.mode.toUpperCase()} of ${state.sources.length} item(s)...`, 'info');

  await window.titanAPI.startOperation({
    opId,
    sources: state.sources.map(s => s.path),
    dest: state.dest,
    mode: state.mode,
    newName: renameInput.value.trim() || null,
  });
};

// ── Cancel / Pause ────────────────────────────────────────────────
btnCancel.onclick = () => {
  if (state.currentOpId) window.titanAPI.cancelOp(state.currentOpId);
};
btnPause.onclick = () => {
  if (state.currentOpId) window.titanAPI.pauseOp(state.currentOpId);
};

window.titanAPI.onPaused(({ paused }) => {
  state.isPaused = paused;
  btnPause.textContent = paused ? 'RESUME' : 'PAUSE';
  setStatus(paused ? 'PAUSED' : 'IN_PROGRESS');
});

// ── Progress events ────────────────────────────────────────────────
window.titanAPI.onProgress(data => {
  const { pct, status, doneFiles, totalFiles, doneBytes, totalBytes, currentFile: cf, error } = data;

  progressFill.style.width = `${pct || 0}%`;
  progressPct.textContent  = `${pct || 0}%`;

  if (cf) currentFile.textContent = cf;

  if (doneFiles !== undefined && totalFiles !== undefined) {
    progressMeta.textContent = `${doneFiles}/${totalFiles} files · ${fmtBytes(doneBytes || 0)} / ${fmtBytes(totalBytes || 0)}`;
  }

  if (status === 'SCANNING') {
    setStatus('SCANNING');
    currentFile.textContent = 'Scanning files…';
  } else if (status === 'IN_PROGRESS') {
    setStatus('IN_PROGRESS');
  } else if (status === 'COMPLETE') {
    setStatus('COMPLETE');
    currentFile.textContent = 'Operation completed successfully';
    resetControls();
    addLog('✓ Operation completed.', 'success');
  } else if (status === 'CANCELLED') {
    setStatus('CANCELLED');
    resetControls();
    addLog('Operation cancelled.', 'error');
  } else if (status === 'ERROR') {
    setStatus('ERROR');
    resetControls();
    addLog(`Error: ${error}`, 'error');
  }
});

window.titanAPI.onLog(({ level, msg }) => addLog(msg, level));

function resetControls() {
  state.isRunning = false;
  btnCancel.disabled = true;
  btnPause.disabled  = true;
  runBtn.disabled    = false;
  btnPause.textContent = 'PAUSE';
}

// ── Status badge helper ───────────────────────────────────────────
function setStatus(s) {
  const map = {
    IDLE: ['IDLE', ''],
    SCANNING: ['SCANNING…', 'active'],
    IN_PROGRESS: ['IN PROGRESS', 'active'],
    COMPLETE: ['COMPLETE', 'complete'],
    ERROR: ['ERROR', 'error'],
    CANCELLED: ['CANCELLED', 'cancelled'],
    PAUSED: ['PAUSED', 'paused'],
  };
  const [label, cls] = map[s] || ['UNKNOWN', ''];
  statusBadge.textContent = label;
  statusBadge.className = 'status-badge' + (cls ? ` ${cls}` : '');
}

// ── Log helper ────────────────────────────────────────────────────
function addLog(msg, level = 'info') {
  const ts = new Date().toLocaleTimeString('en', { hour12: false });
  const el = document.createElement('div');
  el.className = `log-line ${level}`;
  el.textContent = `[${ts}] ${msg}`;
  logPanel.appendChild(el);
  logPanel.scrollTop = logPanel.scrollHeight;
  // Keep max 200 lines
  while (logPanel.children.length > 200) logPanel.removeChild(logPanel.firstChild);
}

// ── System metrics ────────────────────────────────────────────────
const CIRC = 2 * Math.PI * 24; // circumference of r=24

function setRing(el, pct) {
  const filled = (pct / 100) * CIRC;
  el.style.strokeDasharray = `${filled} ${CIRC - filled}`;
}

async function pollMetrics() {
  try {
    const s = await window.titanAPI.getSystemStats();

    // CPU
    cpuVal.textContent = `${s.cpu}%`;
    cpuSub.textContent = `CPU LOAD`;
    setRing(cpuRing, s.cpu);

    // Memory
    memVal.textContent = `${s.memPct}%`;
    memSub.textContent = `${fmtBytes(s.memUsed)} / ${fmtBytes(s.memTotal)}`;
    setRing(memRing, s.memPct);

    // Disk
    const readMB  = s.diskRead  / 1e6;
    const writeMB = s.diskWrite / 1e6;
    const maxMB   = 500; // scale max
    diskReadBar.style.width  = `${Math.min(100, (readMB  / maxMB) * 100)}%`;
    diskWriteBar.style.width = `${Math.min(100, (writeMB / maxMB) * 100)}%`;
    diskReadVal.textContent  = readMB.toFixed(1);
    diskWriteVal.textContent = writeMB.toFixed(1);

    // Graph history
    state.readHistory.push(readMB);
    state.writeHistory.push(writeMB);
    state.readHistory.shift();
    state.writeHistory.shift();

    drawGraph();
  } catch (_) {}
}

// ── Canvas graph (Win11 Task Manager style) ───────────────────────
function drawGraph() {
  const W = canvas.offsetWidth;
  const H = canvas.offsetHeight;
  canvas.width  = W * devicePixelRatio;
  canvas.height = H * devicePixelRatio;
  ctx.scale(devicePixelRatio, devicePixelRatio);

  const bg = '#111111';
  const grid = '#1e1e1e';

  // Background
  ctx.fillStyle = bg;
  ctx.fillRect(0, 0, W, H);

  // Grid lines
  ctx.strokeStyle = grid;
  ctx.lineWidth = 0.5;
  const cols = 12, rows = 5;
  for (let i = 0; i <= cols; i++) {
    const x = (i / cols) * W;
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
  }
  for (let i = 0; i <= rows; i++) {
    const y = (i / rows) * H;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
  }

  const maxVal = Math.max(10, ...state.readHistory, ...state.writeHistory);
  const points = 60;

  function drawLine(history, color, fill) {
    const pts = history.slice(-points);
    ctx.beginPath();
    pts.forEach((v, i) => {
      const x = (i / (points - 1)) * W;
      const y = H - (v / maxVal) * (H - 8) - 4;
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.5;
    ctx.stroke();

    // Fill under
    ctx.lineTo(W, H); ctx.lineTo(0, H); ctx.closePath();
    ctx.fillStyle = fill;
    ctx.fill();
  }

  drawLine(state.readHistory,  '#47c8ff', 'rgba(71,200,255,0.06)');
  drawLine(state.writeHistory, '#e8ff47', 'rgba(232,255,71,0.08)');

  // Scale label
  ctx.fillStyle = '#444';
  ctx.font = `${9 * devicePixelRatio / devicePixelRatio}px Courier New`;
  ctx.fillText(`${maxVal.toFixed(0)} MB/s`, 4, 12);
}

// ── Format bytes ──────────────────────────────────────────────────
function fmtBytes(b) {
  if (b === 0) return '0 B';
  const k = 1024, sizes = ['B','KB','MB','GB','TB'];
  const i = Math.floor(Math.log(b) / Math.log(k));
  return `${(b / Math.pow(k, i)).toFixed(1)} ${sizes[i]}`;
}

// ── Init ──────────────────────────────────────────────────────────
updateModeUI();
addLog('TitanShift ready. Select files and run an operation.', 'info');
setStatus('IDLE');
pollMetrics();
setInterval(pollMetrics, 1500);
