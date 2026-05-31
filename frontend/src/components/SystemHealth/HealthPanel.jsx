import React from 'react';
import ProgressBar from '../shared/ProgressBar.jsx';
import StatusBadge from '../shared/StatusBadge.jsx';
import { formatDuration } from '../../lib/formatters.js';
import { useTelemetryStore } from '../../store/telemetryStore.js';

function phaseVariant(phase) {
  if (phase === 'DESCENT') return 'warning';
  if (phase === 'APOGEE') return 'connecting';
  if (phase === 'LANDED') return 'idle';
  return 'connected';
}

export default function HealthPanel() {
  const latestPacket = useTelemetryStore((state) => state.latestPacket);
  const packetLossPercent = useTelemetryStore((state) => state.packetLossPercent);
  const batteryPercent = latestPacket?.power?.batteryPercent ?? 0;
  const rssi = latestPacket?.link?.rssi ?? -120;
  const hdop = latestPacket?.gps?.hdop ?? 10;
  const phase = latestPacket?.mission?.phase ?? 'PRE_LAUNCH';
  const drift = latestPacket?.imu?.drift;
  const alpha = drift?.filterAlpha ?? 0.97;
  const isFilterHealthy = alpha >= 0.95 && alpha <= 0.98;
  const calibrationAge = drift?.lastCalibrationMs ?? 0;

  const signalQuality = Math.max(0, Math.min(100, ((rssi + 120) / 60) * 100));
  const gpsQuality = Math.max(0, 100 - hdop * 20);

  return (
    <div className="stack">
      <div className="panel-header">
        <h3 className="panel-title">SYSTEM HEALTH</h3>
      </div>
      <ProgressBar label="Battery %" value={batteryPercent} color="var(--green)" />
      <ProgressBar label="Signal Strength" value={signalQuality} color="var(--cyan)" />
      <ProgressBar label="GPS Fix Quality" value={gpsQuality} color="var(--blue)" />
      <div className="metric-card">
        <div className="metric-card__label">Mission Phase</div>
        <StatusBadge variant={phaseVariant(phase)}>{phase}</StatusBadge>
      </div>
      <div className="metric-card">
        <div className="metric-card__label">Drift Status</div>
        <div className="kv-list">
          <div className="kv-row"><span className="key">Gyro Z drift</span><span className="val">{(drift?.rateZ ?? 0).toFixed(2)}°/s</span></div>
          <div className="kv-row"><span className="key">Filter</span><span className="val">{isFilterHealthy ? 'Active' : 'Fault'}</span></div>
          <div className="kv-row"><span className="key">Calibration timer</span><span className="val">{formatDuration(Math.max(0, 60000 - calibrationAge))}</span></div>
          <div className="kv-row"><span className="key">Last calibration</span><span className="val">{calibrationAge ? `${formatDuration(calibrationAge)} ago` : 'n/a'}</span></div>
        </div>
        <div className="small-muted">PKT LOSS {packetLossPercent.toFixed(1)}%</div>
      </div>
    </div>
  );
}
