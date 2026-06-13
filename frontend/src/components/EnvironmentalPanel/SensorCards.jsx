import React from 'react';
import MetricCard from '../shared/MetricCard.jsx';
import ProgressBar from '../shared/ProgressBar.jsx';
import { useTelemetryStore } from '../../store/telemetryStore.js';

export default function SensorCards() {
  const env = useTelemetryStore((state) => state.latestPacket?.env);
  const power = useTelemetryStore((state) => state.latestPacket?.power);

  return (
    <div className="stack">
      <div className="panel-header">
        <h3 className="panel-title">ENVIRONMENTAL</h3>
      </div>
      <div className="grid grid--2x2">
        <MetricCard label="Temperature" value={`${(env?.temperatureExternal ?? 0).toFixed(1)}°C`} valueClassName="value--red" />
        <MetricCard label="Pressure" value={`${(env?.pressure ?? 0).toFixed(1)} hPa`} valueClassName="value--blue" />
        <MetricCard label="Humidity" value={`${(env?.humidity ?? 0).toFixed(1)}%`} valueClassName="value--green" />
        <MetricCard label="Battery Voltage" value={`${(power?.batteryVoltage ?? 0).toFixed(2)} V`} valueClassName="value--amber" />
      </div>
      <div className="grid grid--2x2">
        <MetricCard label="Internal Board Temp" value={`${(env?.temperatureInternal ?? 0).toFixed(1)}°C`} />
        <MetricCard label="UV Index" value={(env?.uvIndex ?? 0).toFixed(1)} />
      </div>
      <ProgressBar label="Battery" value={power?.batteryPercent ?? 0} color="var(--amber)" />
    </div>
  );
}
