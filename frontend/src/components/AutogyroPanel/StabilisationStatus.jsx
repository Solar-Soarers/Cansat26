import React from 'react';
import ProgressBar from '../shared/ProgressBar.jsx';
import StatusBadge from '../shared/StatusBadge.jsx';
import { useTelemetryStore } from '../../store/telemetryStore.js';

export default function StabilisationStatus() {
  const latestPacket = useTelemetryStore((state) => state.latestPacket);
  const state = latestPacket?.autogyro?.state || 'IDLE';
  const stabilityIndex = latestPacket?.autogyro?.stabilityIndex ?? 0;
  const rotorRpm = latestPacket?.autogyro?.rotorRpm ?? 0;
  const history = useTelemetryStore((store) => store.packetHistory.slice(-20).map((packet) => packet.autogyro.rotorRpm));
  const sparkMax = Math.max(1, ...history);

  return (
    <div className="stack">
      <div className="panel-header">
        <h3 className="panel-title">AUTOGYRO / STABILISATION</h3>
        <StatusBadge variant={state === 'ACTIVE' ? 'connected' : state === 'FAULT' ? 'disconnected' : 'idle'}>{state}</StatusBadge>
      </div>
      <div className="metric-card">
        <div className="metric-card__label">Rotor RPM</div>
        <div className="metric-card__value value--purple">{rotorRpm.toFixed(0)}</div>
        <svg width="100%" height="36" viewBox="0 0 180 36" preserveAspectRatio="none">
          {history.map((value, index) => {
            const height = (value / sparkMax) * 28;
            return <rect key={index} x={index * 8 + 2} y={32 - height} width="5" height={height} fill="var(--cyan)" />;
          })}
        </svg>
      </div>
      <ProgressBar label="Stability Index" value={stabilityIndex} color={stabilityIndex > 70 ? 'var(--green)' : stabilityIndex > 40 ? 'var(--amber)' : 'var(--red)'} />
    </div>
  );
}
