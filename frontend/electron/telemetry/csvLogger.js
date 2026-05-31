import fs from 'node:fs';
import path from 'node:path';
import { app } from 'electron';

function getLogsDir() {
  const dir = path.join(app.getPath('userData'), 'logs');
  fs.mkdirSync(dir, { recursive: true });
  return dir;
}

function escapeCsv(value) {
  if (value === null || value === undefined) {
    return '';
  }
  const stringValue = typeof value === 'object' ? JSON.stringify(value) : String(value);
  return /[",\n]/.test(stringValue) ? `"${stringValue.replaceAll('"', '""')}"` : stringValue;
}

export function logPacket(packet) {
  const fileName = new Date(packet.timestamp || Date.now()).toISOString().slice(0, 10) + '.csv';
  const filePath = path.join(getLogsDir(), fileName);
  const isNew = !fs.existsSync(filePath);
  const headers = [
    'packetId', 'timestamp', 'missionElapsedMs', 'phase', 'barometric', 'gpsAlt', 'lat', 'lng', 'pitch', 'roll', 'heading', 'gyroZ', 'batteryPercent', 'rssi', 'packetLossPercent',
  ];
  const row = [
    packet.packetId,
    packet.timestamp,
    packet.missionElapsedMs,
    packet.mission?.phase,
    packet.altitude?.barometric,
    packet.altitude?.gps,
    packet.gps?.latitude,
    packet.gps?.longitude,
    packet.imu?.compensated?.pitch,
    packet.imu?.compensated?.roll,
    packet.imu?.compensated?.heading,
    packet.imu?.raw?.gyroZ,
    packet.power?.batteryPercent,
    packet.link?.rssi,
    packet.link?.packetLossPercent,
  ].map(escapeCsv);

  const content = `${isNew ? `${headers.map(escapeCsv).join(',')}\n` : ''}${row.join(',')}\n`;
  fs.appendFileSync(filePath, content, 'utf8');
  return filePath;
}
