import React from 'react';
import { formatHeading } from '../../lib/formatters.js';
import { useTelemetryStore } from '../../store/telemetryStore.js';

export default function CompassRose() {
  const latestPacket = useTelemetryStore((state) => state.latestPacket);
  const heading = latestPacket?.imu?.compensated?.heading ?? 0;
  const rawHeading = latestPacket?.imu?.raw?.heading ?? 0;
  const declination = rawHeading - heading;
  const normalizedDeclination = ((declination + 540) % 360) - 180;

  return (
    <div className="stack center-stack">
      <svg width="100" height="100" viewBox="0 0 100 100" aria-label="Compass rose">
        <circle cx="50" cy="50" r="46" fill="var(--bg-base)" stroke="var(--bg-border)" strokeWidth="2" />
        {Array.from({ length: 12 }).map((_, index) => {
          const angle = (index * 30 - 90) * (Math.PI / 180);
          const x1 = 50 + Math.cos(angle) * 38;
          const y1 = 50 + Math.sin(angle) * 38;
          const x2 = 50 + Math.cos(angle) * 44;
          const y2 = 50 + Math.sin(angle) * 44;
          return <line key={index} x1={x1} y1={y1} x2={x2} y2={y2} stroke="var(--text-secondary)" strokeWidth="1" />;
        })}
        <text x="50" y="12" textAnchor="middle" className="mono" fill="var(--text-primary)" fontSize="10">N</text>
        <text x="88" y="54" textAnchor="middle" className="mono" fill="var(--text-primary)" fontSize="10">E</text>
        <text x="50" y="95" textAnchor="middle" className="mono" fill="var(--text-primary)" fontSize="10">S</text>
        <text x="12" y="54" textAnchor="middle" className="mono" fill="var(--text-primary)" fontSize="10">W</text>
        <g transform={`rotate(${heading} 50 50)`}>
          <line x1="50" y1="50" x2="50" y2="16" stroke="var(--cyan)" strokeWidth="2" />
          <polygon points="50,10 45,22 55,22" fill="var(--cyan)" />
        </g>
        <g transform={`rotate(${normalizedDeclination} 50 50)`} opacity="0.35">
          <line x1="50" y1="50" x2="50" y2="18" stroke="var(--amber)" strokeWidth="2" />
          <polygon points="50,12 46,22 54,22" fill="var(--amber)" />
        </g>
        <circle cx="50" cy="50" r="4" fill="var(--text-primary)" />
      </svg>
      <div className="value" style={{ fontSize: 18 }}>{formatHeading(heading)}</div>
      <div className="small-muted">mag declination {normalizedDeclination >= 0 ? '+' : ''}{normalizedDeclination.toFixed(1)}°</div>
    </div>
  );
}
