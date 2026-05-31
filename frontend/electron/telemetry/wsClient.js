import WebSocket from 'ws';
import { createMockPacket } from './mockDataEmitter.js';

let lastPacket = null;
const RECONNECT_INTERVAL_MS = 3000;
const MAX_RECONNECT_ATTEMPTS = 20;

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function snakeToCamel(input) {
  if (Array.isArray(input)) {
    return input.map((item) => snakeToCamel(item));
  }
  if (!input || typeof input !== 'object') {
    return input;
  }

  return Object.entries(input).reduce((result, [key, value]) => {
    const camelKey = key.replace(/_([a-z])/g, (_, letter) => letter.toUpperCase());
    result[camelKey] = snakeToCamel(value);
    return result;
  }, {});
}

function safeNumber(value, fallback = 0) {
  return Number.isFinite(Number(value)) ? Number(value) : fallback;
}

function ensureObject(value) {
  return value && typeof value === 'object' ? value : {};
}

export function normalisePacket(raw) {
  const packet = snakeToCamel(raw);
  const packetId = safeNumber(packet?.packetId, 0);
  const defaults = {
    packetId,
    timestamp: 0,
    missionElapsedMs: 0,
    imu: {
      raw: {
        pitch: 0,
        roll: 0,
        heading: 0,
        gyroX: 0,
        gyroY: 0,
        gyroZ: 0,
        accelX: 0,
        accelY: 0,
        accelZ: 0,
        magX: 0,
        magY: 0,
        magZ: 0,
      },
      compensated: { pitch: 0, roll: 0, heading: 0 },
      drift: {
        rateX: 0,
        rateY: 0,
        rateZ: 0,
        accumulatedX: 0,
        accumulatedY: 0,
        accumulatedZ: 0,
        filterAlpha: 0,
        lastCalibrationMs: 0,
      },
    },
    altitude: { barometric: 0, gps: 0, seaLevel: 0, apogee: 0, descentRate: 0 },
    gps: { latitude: 0, longitude: 0, altitudeGPS: 0, speedKmh: 0, courseDeg: 0, satellites: 0, hdop: 0, fixType: 'UNKNOWN', distanceFromPad: 0 },
    env: { temperatureExternal: 0, temperatureInternal: 0, pressure: 0, humidity: 0, uvIndex: 0 },
    autogyro: { rotorRpm: 0, servoAlpha: 0, servoBeta: 0, ikInputPitch: 0, ikInputRoll: 0, stabilityIndex: 0, state: 'UNKNOWN', correctionXDeg: 0, correctionYDeg: 0 },
    power: { batteryVoltage: 0, batteryPercent: 0, current: 0 },
    link: { rssi: 0, snr: 0, packetLossPercent: 0, port: 'UNKNOWN', baudRate: 0 },
    mission: { phase: 'UNKNOWN', parachuteDeployed: false, autogyroArmed: false, buzzerActive: false },
  };

  const result = {
    ...defaults,
    ...packet,
    imu: {
      ...defaults.imu,
      ...ensureObject(packet.imu),
      raw: { ...defaults.imu.raw, ...ensureObject(packet.imu?.raw) },
      compensated: { ...defaults.imu.compensated, ...ensureObject(packet.imu?.compensated) },
      drift: { ...defaults.imu.drift, ...ensureObject(packet.imu?.drift) },
    },
    altitude: { ...defaults.altitude, ...ensureObject(packet.altitude) },
    gps: { ...defaults.gps, ...ensureObject(packet.gps) },
    env: { ...defaults.env, ...ensureObject(packet.env) },
    autogyro: { ...defaults.autogyro, ...ensureObject(packet.autogyro) },
    power: { ...defaults.power, ...ensureObject(packet.power) },
    link: { ...defaults.link, ...ensureObject(packet.link) },
    mission: { ...defaults.mission, ...ensureObject(packet.mission) },
  };

  const requiredTopLevel = ['packetId', 'timestamp', 'missionElapsedMs', 'imu', 'altitude', 'gps', 'env', 'autogyro', 'power', 'link', 'mission'];
  for (const key of requiredTopLevel) {
    if (!(key in packet)) {
      console.warn(`Missing telemetry key ${key} on packet ${packetId}`);
    }
  }

  result.packetId = packetId;
  result.timestamp = safeNumber(result.timestamp, 0);
  result.missionElapsedMs = safeNumber(result.missionElapsedMs, 0);

  result.imu.raw.pitch = clamp(safeNumber(result.imu.raw.pitch), -180, 180);
  result.imu.raw.roll = clamp(safeNumber(result.imu.raw.roll), -180, 180);
  result.imu.raw.heading = clamp(safeNumber(result.imu.raw.heading), 0, 360);
  result.altitude.barometric = clamp(safeNumber(result.altitude.barometric), -500, 50000);
  result.env.pressure = clamp(safeNumber(result.env.pressure), 0, 1200);
  result.power.batteryVoltage = clamp(safeNumber(result.power.batteryVoltage), 0, 5);
  result.link.rssi = clamp(safeNumber(result.link.rssi), -120, 0);
  result.autogyro.stabilityIndex = clamp(safeNumber(result.autogyro.stabilityIndex), 0, 100);

  return result;
}

export function getLastPacket() {
  return lastPacket;
}

export function createWsClient({ url = 'ws://localhost:8765', onPacket, onStatus, onError, useMock = false }) {
  let socket = null;
  let reconnectTimer = null;
  let closedManually = false;
  let mockTimer = null;
  let reconnectAttempts = 0;

  const emitStatus = (status) => {
    onStatus?.(status);
  };

  const clearReconnect = () => {
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
  };

  const scheduleReconnect = () => {
    if (closedManually || useMock) {
      return;
    }
    clearReconnect();
    if (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
      reconnectAttempts += 1;
      emitStatus({ state: 'connecting', attempts: reconnectAttempts, maxAttempts: MAX_RECONNECT_ATTEMPTS });
      console.log(`[WS] Reconnecting attempt ${reconnectAttempts}/${MAX_RECONNECT_ATTEMPTS} in ${RECONNECT_INTERVAL_MS}ms`);
      reconnectTimer = setTimeout(() => connect(), RECONNECT_INTERVAL_MS);
      return;
    }
    console.error('[WS] Max reconnect attempts reached - check Python backend');
    emitStatus({ state: 'failed', attempts: reconnectAttempts, maxAttempts: MAX_RECONNECT_ATTEMPTS });
  };

  const startMock = () => {
    if (mockTimer) {
      return;
    }
    emitStatus({ state: 'connected', attempts: 0, maxAttempts: MAX_RECONNECT_ATTEMPTS });
    mockTimer = setInterval(() => {
      const normalisedPacket = normalisePacket(createMockPacket());
      lastPacket = normalisedPacket;
      onPacket?.(normalisedPacket);
    }, 100);
  };

  const stopMock = () => {
    if (mockTimer) {
      clearInterval(mockTimer);
      mockTimer = null;
    }
  };

  const connect = () => {
    if (useMock) {
      startMock();
      return;
    }
    clearReconnect();
    emitStatus({ state: 'connecting', attempts: reconnectAttempts, maxAttempts: MAX_RECONNECT_ATTEMPTS });
    socket = new WebSocket(url);

    socket.on('open', () => {
      reconnectAttempts = 0;
      clearReconnect();
      emitStatus({ state: 'connected', attempts: 0, maxAttempts: MAX_RECONNECT_ATTEMPTS });
    });

    socket.on('message', (message) => {
      try {
        const packet = JSON.parse(message.toString());
        const normalisedPacket = normalisePacket(packet);
        lastPacket = normalisedPacket;
        onPacket?.(normalisedPacket);
      } catch (error) {
        onError?.(error);
      }
    });

    socket.on('close', () => {
      emitStatus({ state: 'disconnected', attempts: reconnectAttempts, maxAttempts: MAX_RECONNECT_ATTEMPTS });
      if (!closedManually) {
        scheduleReconnect();
      }
    });

    socket.on('error', (error) => {
      onError?.(error);
      emitStatus({ state: 'disconnected', attempts: reconnectAttempts, maxAttempts: MAX_RECONNECT_ATTEMPTS });
      if (!closedManually && (socket?.readyState === WebSocket.CLOSED || socket?.readyState === WebSocket.CLOSING)) {
        scheduleReconnect();
      }
    });
  };

  const sendCommand = (command) => {
    if (useMock) {
      return true;
    }
    if (socket && socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify(command));
      return true;
    }
    return false;
  };

  const close = () => {
    closedManually = true;
    clearReconnect();
    stopMock();
    if (socket) {
      socket.close();
      socket = null;
    }
  };

  connect();

  return { sendCommand, close, reconnect: connect };
}
