import { app, BrowserWindow, dialog, ipcMain } from 'electron';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import Store from 'electron-store';
import { createWsClient } from './telemetry/wsClient.js';
import { createSerialClient, listPorts } from './telemetry/serialClient.js';
import { logPacket } from './telemetry/csvLogger.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const store = new Store({
  defaults: {
    comPort: 'COM4',
    baudRate: 115200,
    filterAlpha: 0.97,
    kp: 1.2,
    kd: 0.05,
  },
});

let mainWindow = null;
let wsClient = null;
let serialClient = null;

function statusState(status) {
  return typeof status === 'string' ? status : status?.state;
}

async function startSerialFallback(portPath = null) {
  if (serialClient) {
    return;
  }
  const ports = await listPorts();
  const targetPort = portPath || store.get('comPort') || ports[0]?.path;
  if (!targetPort) {
    return;
  }
  serialClient = createSerialClient({
    portPath: targetPort,
    baudRate: store.get('baudRate'),
    onPacket: forwardPacket,
    onStatus: (status) => {
      forwardStatus(status);
      if (status === 'disconnected') {
        console.warn('[SERIAL] Port closed unexpectedly');
        serialClient = null;
        setTimeout(() => {
          startSerialFallback(targetPort).catch((error) => {
            console.error(`[SERIAL] Reopen failed: ${error.message}`);
          });
        }, 5000);
      }
    },
    onError: (error) => {
      console.error(`[SERIAL] Port error: ${error.message}`);
      forwardStatus('disconnected');
      serialClient = null;
      setTimeout(() => {
        startSerialFallback(targetPort).catch((error2) => {
          console.error(`[SERIAL] Reopen failed: ${error2.message}`);
        });
      }, 5000);
    },
  });
}

function stopSerialFallback() {
  serialClient?.close();
  serialClient = null;
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1500,
    height: 900,
    backgroundColor: '#0A0C0F',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
    },
  });

  const devServerUrl = process.env.VITE_DEV_SERVER_URL || (!app.isPackaged ? 'http://localhost:5173' : null);

  if (devServerUrl) {
    let loadAttempts = 0;
    const loadDevServer = () => {
      if (!mainWindow || mainWindow.isDestroyed()) {
        return;
      }

      loadAttempts += 1;
      mainWindow.loadURL(devServerUrl).catch((error) => {
        console.error(`[ELECTRON] Dev server load failed (${loadAttempts}): ${error.message}`);
      });
    };

    mainWindow.webContents.on('did-fail-load', (_event, errorCode, errorDescription, validatedURL) => {
      if (validatedURL === devServerUrl && loadAttempts < 10) {
        setTimeout(loadDevServer, 1000);
      }
    });

    loadDevServer();
    return;
  }

  mainWindow.loadFile(path.join(app.getAppPath(), 'dist', 'index.html'));
}

function forwardPacket(packet) {
  if (!mainWindow || mainWindow.isDestroyed()) {
    return;
  }
  logPacket(packet);
  mainWindow.webContents.send('telemetry-packet', packet);
}

function forwardStatus(status) {
  if (!mainWindow || mainWindow.isDestroyed()) {
    return;
  }
  mainWindow.webContents.send('telemetry-status', status);
  mainWindow.webContents.send('connection-status', status);
}

function startTelemetry() {
  if (wsClient) {
    wsClient.close();
  }
  wsClient = createWsClient({
    url: 'ws://localhost:8765',
    useMock: Boolean(process.env.INSPACE_GS_MOCK),
    onPacket: forwardPacket,
    onStatus: (status) => {
      forwardStatus(status);
      const state = statusState(status);
      if (state === 'connected') {
        stopSerialFallback();
      }
      if (state === 'disconnected') {
        startSerialFallback().catch((error) => console.error('Serial fallback start failed:', error));
      }
    },
    onError: (error) => {
      console.error('Telemetry error:', error);
      forwardStatus('disconnected');
    },
  });
}

app.whenReady().then(() => {
  createWindow();
  startTelemetry();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('before-quit', () => {
  wsClient?.close();
  stopSerialFallback();
});

ipcMain.handle('export-csv', async () => {
  const result = await dialog.showOpenDialog(mainWindow, {
    title: 'Choose CSV export folder',
    properties: ['openDirectory', 'createDirectory'],
  });
  if (result.canceled || result.filePaths.length === 0) {
    return { canceled: true };
  }
  store.set('csvExportPath', result.filePaths[0]);
  return { canceled: false, path: result.filePaths[0] };
});

ipcMain.handle('get-port-list', async () => listPorts());

ipcMain.handle('connect-port', async (_event, { port, baud }) => {
  store.set('comPort', port);
  store.set('baudRate', baud);
  stopSerialFallback();
  serialClient = createSerialClient({
    portPath: port,
    baudRate: baud,
    onPacket: forwardPacket,
    onStatus: (status) => {
      forwardStatus(status);
      if (status === 'disconnected') {
        console.warn('[SERIAL] Port closed unexpectedly');
        serialClient = null;
        setTimeout(() => {
          startSerialFallback(port).catch((error) => {
            console.error(`[SERIAL] Reopen failed: ${error.message}`);
          });
        }, 5000);
      }
    },
    onError: (error) => {
      console.error(`[SERIAL] Port error: ${error.message}`);
      forwardStatus('disconnected');
      serialClient = null;
      setTimeout(() => {
        startSerialFallback(port).catch((error2) => {
          console.error(`[SERIAL] Reopen failed: ${error2.message}`);
        });
      }, 5000);
    },
  });
  return { ok: true };
});

ipcMain.on('send-command', (_event, cmd) => {
  if (!wsClient) {
    return;
  }
  wsClient.sendCommand(cmd);
});
