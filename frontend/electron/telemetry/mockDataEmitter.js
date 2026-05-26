const state = {
  packetId: 1,
  missionElapsedMs: 0,
  altitude: 360,
  descentRate: -4.2,
  lat: 28.6139,
  lng: 77.209,
  heading: 127,
  pitch: 12.4,
  roll: -3.8,
  gyroDriftZ: 0,
  battery: 84,
  rotorRpm: 1240,
  filterAlpha: 0.97,
  apogee: 360,
};

const emitterStartTime = Date.now();
let mockGyroZDrift = 0.0;
let mockAccumZ = 0.0;
let lastCalibMs = Date.now();
let driftWarningFired = false;

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function rand(min, max) {
  return min + Math.random() * (max - min);
}

export function createMockPacket() {
  const now = Date.now();
  const elapsedMs = now - emitterStartTime;
  const elapsedSec = elapsedMs / 1000;

  state.missionElapsedMs += 100;

  let phase = 'LANDED';
  if (elapsedSec < 5) {
    phase = 'PRE_LAUNCH';
  } else if (elapsedSec < 15) {
    phase = 'ASCENT';
  } else if (elapsedSec < 20) {
    phase = 'APOGEE';
  } else if (elapsedSec < 50) {
    phase = 'DESCENT';
  }

  if (phase === 'PRE_LAUNCH') {
    state.descentRate = 0;
  } else if (phase === 'ASCENT') {
    state.descentRate = clamp(Math.abs(state.descentRate) + rand(0.2, 0.45), 1.2, 8.0);
  } else if (phase === 'APOGEE') {
    state.descentRate = clamp(rand(-0.15, 0.15), -0.2, 0.2);
  } else if (phase === 'DESCENT') {
    state.descentRate = clamp(-Math.abs(state.descentRate) + rand(-0.4, 0.2), -8.0, -1.2);
  } else {
    state.descentRate = 0;
  }

  state.altitude = Math.max(0, state.altitude + state.descentRate * 0.1 + rand(-0.15, 0.15));

  mockGyroZDrift += 0.003;
  mockAccumZ += mockGyroZDrift * 0.1;

  const msSinceCalib = now - lastCalibMs;
  if (msSinceCalib >= 30000) {
    mockAccumZ = 0.0;
    mockGyroZDrift = 0.0;
    lastCalibMs = now;
    driftWarningFired = false;
    console.log('[MOCK] IMU auto-recalibrated - drift reset');
  }

  if (mockGyroZDrift > 0.5 && !driftWarningFired) {
    console.warn('[MOCK] DRIFT_WARN - gyroZ drift rate exceeded 0.5 deg/s');
    driftWarningFired = true;
  }

  state.lat += rand(-0.00003, 0.00003);
  state.lng += rand(-0.00003, 0.00003);
  state.heading = (state.heading + rand(-1.4, 1.4) + 360) % 360;
  state.pitch = clamp(state.pitch + rand(-0.2, 0.2), -30, 30);
  state.roll = clamp(state.roll + rand(-0.15, 0.15), -30, 30);
  state.gyroDriftZ = mockGyroZDrift;
  state.rotorRpm = clamp(state.rotorRpm + rand(-18, 24), 800, 2600);
  state.battery = clamp(state.battery - 0.002, 10, 100);

  const packet = {
    packetId: state.packetId++,
    timestamp: now,
    missionElapsedMs: state.missionElapsedMs,
    imu: {
      raw: {
        pitch: state.pitch + rand(-0.8, 0.8),
        roll: state.roll + rand(-0.8, 0.8),
        heading: state.heading,
        gyroX: rand(-0.2, 0.2),
        gyroY: rand(-0.2, 0.2),
        gyroZ: mockGyroZDrift,
        accelX: rand(-0.1, 0.1),
        accelY: rand(-0.1, 0.1),
        accelZ: 9.81 + rand(-0.05, 0.05),
        magX: rand(-35, 35),
        magY: rand(-35, 35),
        magZ: rand(-35, 35),
      },
      compensated: {
        pitch: state.pitch,
        roll: state.roll,
        heading: state.heading,
      },
      drift: {
        rateX: 0.01,
        rateY: 0.005,
        rateZ: parseFloat(mockGyroZDrift.toFixed(4)),
        accumulatedX: 0.05,
        accumulatedY: 0.02,
        accumulatedZ: parseFloat(mockAccumZ.toFixed(3)),
        filterAlpha: 0.96,
        lastCalibrationMs: msSinceCalib,
      },
    },
    altitude: {
      barometric: state.altitude,
      gps: state.altitude + rand(-1.2, 1.2),
      seaLevel: 970.1,
      apogee: state.apogee = Math.max(state.apogee, state.altitude),
      descentRate: state.descentRate,
    },
    gps: {
      latitude: state.lat,
      longitude: state.lng,
      altitudeGPS: state.altitude + rand(-1.1, 1.1),
      speedKmh: Math.max(0, rand(7, 18)),
      courseDeg: state.heading,
      satellites: 9,
      hdop: 0.9,
      fixType: '3D',
      distanceFromPad: Math.max(0, rand(0, 12)),
    },
    env: {
      temperatureExternal: 24.3 + rand(-0.2, 0.3),
      temperatureInternal: 29.8 + rand(-0.4, 0.4),
      pressure: 970.1 + rand(-0.4, 0.4),
      humidity: 62 + rand(-0.4, 0.5),
      uvIndex: rand(0.8, 1.6),
    },
    autogyro: {
      rotorRpm: state.rotorRpm,
      servoAlpha: clamp(state.roll * 0.9, -45, 45),
      servoBeta: clamp(state.pitch * 0.85, -45, 45),
      ikInputPitch: state.pitch,
      ikInputRoll: state.roll,
      stabilityIndex: clamp(87 + rand(-4, 3), 0, 100),
      state: 'ACTIVE',
      correctionXDeg: 14.2 + rand(-0.8, 0.8),
      correctionYDeg: -6.7 + rand(-0.7, 0.7),
    },
    power: {
      batteryVoltage: 3.84 - (100 - state.battery) * 0.004,
      batteryPercent: state.battery,
      current: rand(180, 320),
    },
    link: {
      rssi: -68 + rand(-2, 2),
      snr: 11.8 + rand(-0.9, 0.9),
      packetLossPercent: rand(0.1, 0.8),
      port: 'COM4',
      baudRate: 115200,
    },
    mission: {
      phase,
      parachuteDeployed: phase === 'DESCENT' || phase === 'LANDED',
      autogyroArmed: phase !== 'PRE_LAUNCH',
      buzzerActive: phase === 'LANDED',
    },
  };

  return packet;
}
