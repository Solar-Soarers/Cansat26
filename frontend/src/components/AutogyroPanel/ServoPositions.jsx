import React from 'react';
import { clamp } from '../../lib/formatters.js';
import { useTelemetryStore } from '../../store/telemetryStore.js';

function Gauge({ label, value }) {
  const angle = clamp((value + 45) / 90, 0, 1) * 180;
  return (
    <div className="metric-card">
      <div className="metric-card__label">{label}</div>
      <div className="metric-card__value">{value.toFixed(1)}°</div>
      <svg width="100%" height="64" viewBox="0 0 140 64" preserveAspectRatio="none">
        <path d="M20 48 A50 50 0 0 1 120 48" fill="none" stroke="var(--bg-border)" strokeWidth="8" />
        <path d="M20 48 A50 50 0 0 1 120 48" fill="none" stroke="var(--cyan)" strokeWidth="8" strokeDasharray={`${angle} ${180 - angle}`} />
        <line x1="70" y1="48" x2={70 + Math.cos(((angle - 90) * Math.PI) / 180) * 36} y2={48 - Math.sin(((angle - 90) * Math.PI) / 180) * 36} stroke="var(--amber)" strokeWidth="3" />
      </svg>
    </div>
  );
}

export default function ServoPositions() {
  const latestPacket = useTelemetryStore((state) => state.latestPacket);
  const servoAlpha = latestPacket?.autogyro?.servoAlpha ?? 0;
  const servoBeta = latestPacket?.autogyro?.servoBeta ?? 0;

  return (
    <div className="grid grid--2x2">
      <Gauge label="Servo α" value={servoAlpha} />
      <Gauge label="Servo β" value={servoBeta} />
    </div>
  );
}
