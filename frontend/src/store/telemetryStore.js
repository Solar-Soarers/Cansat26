import { create } from 'zustand';

const emptyPacket = null;

function getPacketLoss(totalPacketsExpected, totalPacketsRx) {
  if (!totalPacketsExpected) {
    return 0;
  }
  return Math.max(0, ((totalPacketsExpected - totalPacketsRx) / totalPacketsExpected) * 100);
}

/**
 * @typedef {import('../types/telemetry.js').TelemetryPacket} TelemetryPacket
 */

export const useTelemetryStore = create((set, get) => ({
  latestPacket: emptyPacket,
  packetHistory: [],
  connectionStatus: 'connecting',
  reconnectAttempts: 0,
  reconnectMaxAttempts: 20,
  missionStartMs: null,
  launchPadCoords: null,
  apogeeAltitude: 0,
  totalPacketsRx: 0,
  totalPacketsExpected: 0,
  packetLossPercent: 0,
  commands: {
    filterAlpha: 0.97,
    kp: 1.2,
    kd: 0.05,
  },
  handlePacket: (packet) => {
    const state = get();
    const packetHistory = [...state.packetHistory, packet].slice(-500);
    const firstPacket = state.packetHistory[0] || packet;
    const totalPacketsExpected = Math.max(
      state.totalPacketsExpected,
      packet.packetId - (firstPacket.packetId || packet.packetId) + 1,
    );
    const totalPacketsRx = state.totalPacketsRx + 1;
    const packetLossPercent = getPacketLoss(totalPacketsExpected, totalPacketsRx);

    set({
      latestPacket: packet,
      packetHistory,
      missionStartMs: state.missionStartMs ?? (packet.mission?.phase === 'ASCENT' ? packet.timestamp : null),
      launchPadCoords: state.launchPadCoords ?? { lat: packet.gps.latitude, lng: packet.gps.longitude },
      apogeeAltitude: Math.max(state.apogeeAltitude, packet.altitude.apogee, packet.altitude.barometric),
      totalPacketsRx,
      totalPacketsExpected,
      packetLossPercent,
    });
  },
  setConnectionStatus: (status) => {
    if (typeof status === 'string') {
      set({ connectionStatus: status });
      return;
    }
    set({
      connectionStatus: status?.state || 'disconnected',
      reconnectAttempts: status?.attempts ?? 0,
      reconnectMaxAttempts: status?.maxAttempts ?? 20,
    });
  },
  resetMission: () => set({
    latestPacket: emptyPacket,
    packetHistory: [],
    missionStartMs: null,
    launchPadCoords: null,
    apogeeAltitude: 0,
    totalPacketsRx: 0,
    totalPacketsExpected: 0,
    packetLossPercent: 0,
    reconnectAttempts: 0,
    reconnectMaxAttempts: 20,
  }),
  updateCommandSetting: (key, value) => set((state) => ({
    commands: {
      ...state.commands,
      [key]: value,
    },
  })),
}));

export const telemetrySelectors = {
  altitudeHistory: (state) => state.packetHistory.map((packet) => ({
    t: packet.missionElapsedMs / 1000,
    baro: packet.altitude.barometric,
    gps: packet.altitude.gps,
    apogee: packet.altitude.apogee,
    timestamp: packet.timestamp,
  })),
  gyroHistory: (state) => state.packetHistory.map((packet) => ({
    t: packet.missionElapsedMs / 1000,
    gyroX: packet.imu.raw.gyroX,
    gyroY: packet.imu.raw.gyroY,
    gyroZ: packet.imu.raw.gyroZ,
    pitch: packet.imu.compensated.pitch,
    roll: packet.imu.compensated.roll,
  })),
  gpsTrail: (state) => state.packetHistory.slice(-200).map((packet) => [packet.gps.latitude, packet.gps.longitude]),
};
