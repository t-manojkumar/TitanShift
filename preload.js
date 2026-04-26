const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('titanAPI', {
  // Window
  minimize: () => ipcRenderer.send('window-minimize'),
  maximize: () => ipcRenderer.send('window-maximize'),
  close: () => ipcRenderer.send('window-close'),

  // Dialogs
  selectFiles: (opts) => ipcRenderer.invoke('dialog-select-files', opts),
  selectDest: () => ipcRenderer.invoke('dialog-select-dest'),

  // System stats
  getSystemStats: () => ipcRenderer.invoke('get-system-stats'),

  // Operations
  startOperation: (args) => ipcRenderer.invoke('start-operation', args),
  cancelOp: (opId) => ipcRenderer.send('op-cancel', opId),
  pauseOp: (opId) => ipcRenderer.send('op-pause', opId),
  openPath: (p) => ipcRenderer.invoke('open-path', p),

  // Events
  onProgress: (cb) => ipcRenderer.on('op-progress', (_, data) => cb(data)),
  onPaused: (cb) => ipcRenderer.on('op-paused', (_, data) => cb(data)),
  onLog: (cb) => ipcRenderer.on('op-log', (_, data) => cb(data)),
  removeAllListeners: (ch) => ipcRenderer.removeAllListeners(ch),
});
