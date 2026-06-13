import React, { useCallback } from 'react';
import { formatDuration, formatSigned } from '../../lib/formatters.js';
import { useTelemetryStore } from '../../store/telemetryStore.js';

function Row({ label, rawValue, compValue, driftValue, unit }) {
  return (
    <div className="kv-row">
      <span className="key">{label}</span>
      <span className="val">RAW {rawValue} {unit} → COMP {compValue} {unit} Δ {driftValue}°/s</span>
    </div>
  );
}

export default function IMUReadings() {
  const latestPacket = useTelemetryStore((state) => state.latestPacket);
  const updateCommandSetting = useTelemetryStore((state) => state.updateCommandSetting);
  const filterAlpha = useTelemetryStore((state) => state.commands.filterAlpha);

  const sendCommand = useCallback((command) => {
    window.electronAPI?.sendCommand?.(command);
  }, []);

  const drift = latestPacket?.imu?.drift;
  const raw = latestPacket?.imu?.raw;
  const comp = latestPacket?.imu?.compensated;
  const calibrationAge = drift?.lastCalibrationMs ?? 0;
  const maxDrift = Math.max(Math.abs(drift?.rateX ?? 0), Math.abs(drift?.rateY ?? 0), Math.abs(drift?.rateZ ?? 0));

  const handleAlphaChange = (event) => {
    const value = Number(event.target.value);
    updateCommandSetting('filterAlpha', value);
  };

  const handleRecalibrate = () => sendCommand({ type: 'RECALIBRATE_IMU' });
  const handleApplyAlpha = () => sendCommand({ type: 'SET_FILTER_ALPHA', value: filterAlpha });

  return (
    <div className="stack">
      <Row label="PITCH" rawValue={raw?.pitch?.toFixed(1) ?? '0.0'} compValue={comp?.pitch?.toFixed(1) ?? '0.0'} driftValue={formatSigned(drift?.rateX ?? 0, 2)} unit="°" />
      <Row label="ROLL" rawValue={raw?.roll?.toFixed(1) ?? '0.0'} compValue={comp?.roll?.toFixed(1) ?? '0.0'} driftValue={formatSigned(drift?.rateY ?? 0, 2)} unit="°" />
      <Row label="HEADING" rawValue={raw?.heading?.toFixed(1) ?? '0.0'} compValue={comp?.heading?.toFixed(1) ?? '0.0'} driftValue={formatSigned(drift?.rateZ ?? 0, 2)} unit="°" />

      <div className="panel" style={{ padding: 8 }}>
        <div className="panel-header">
          <h3 className="panel-title">DRIFT ACCUMULATION</h3>
          <span className={`badge ${maxDrift > 0.5 ? 'badge--connecting' : 'badge--connected'}`}>{maxDrift > 0.5 ? 'DRIFT WARNING' : 'STABLE'}</span>
        </div>
        <div className="kv-list">
          <div className="kv-row"><span className="key">X rate</span><span className="val">{formatSigned(drift?.rateX ?? 0, 2)}°/s · {formatSigned(drift?.accumulatedX ?? 0, 2)}°</span></div>
          <div className="kv-row"><span className="key">Y rate</span><span className="val">{formatSigned(drift?.rateY ?? 0, 2)}°/s · {formatSigned(drift?.accumulatedY ?? 0, 2)}°</span></div>
          <div className="kv-row"><span className="key">Z rate</span><span className="val">{formatSigned(drift?.rateZ ?? 0, 2)}°/s · {formatSigned(drift?.accumulatedZ ?? 0, 2)}°</span></div>
          <div className="kv-row"><span className="key">Calibration age</span><span className="val">{formatDuration(calibrationAge)}</span></div>
          <div className="kv-row"><span className="key">Filter alpha</span><span className="val">{Number(filterAlpha ?? drift?.filterAlpha ?? 0.97).toFixed(2)}</span></div>
        </div>
        <div className="row" style={{ marginTop: 8 }}>
          <input type="number" min="0.90" max="0.99" step="0.01" value={filterAlpha ?? drift?.filterAlpha ?? 0.97} onChange={handleAlphaChange} />
          <button type="button" onClick={handleApplyAlpha}>APPLY α</button>
          <button type="button" onClick={handleRecalibrate}>RECALIBRATE</button>
        </div>
      </div>
    </div>
  );
}
