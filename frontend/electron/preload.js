import { contextBridge, ipcRenderer } from 'electron';

contextBridge.exposeInMainWorld('electronAPI', {
  onTelemetry: (callback) => {
    const listener = (_, data) => callback(data);
    ipcRenderer.on('telemetry-packet', listener);
    return () => ipcRenderer.removeListener('telemetry-packet', listener);
  },
  onStatus: (callback) => {
    const listener = (_, data) => callback(data);
    ipcRenderer.on('telemetry-status', listener);
    return () => ipcRenderer.removeListener('telemetry-status', listener);
  },
  onConnectionStatus: (callback) => {
    const listener = (_, data) => callback(data);
    ipcRenderer.on('connection-status', listener);
    return () => ipcRenderer.removeListener('connection-status', listener);
  },
  sendCommand: (cmd) => ipcRenderer.send('send-command', cmd),
  exportCSV: () => ipcRenderer.invoke('export-csv'),
  getPortList: () => ipcRenderer.invoke('get-port-list'),
  connectPort: (port, baud) => ipcRenderer.invoke('connect-port', { port, baud }),
});
