import React, { useCallback } from 'react';
import { formatDuration } from '../../lib/formatters.js';
import { useTelemetryStore } from '../../store/telemetryStore.js';
import StatusBadge from '../shared/StatusBadge.jsx';

function signalBars(rssi) {
  if (rssi >= -55) return 5;
  if (rssi >= -65) return 4;
  if (rssi >= -75) return 3;
  if (rssi >= -85) return 2;
  return 1;
}

function packetLossVariant(loss) {
  if (loss > 5) return 'disconnected';
  if (loss > 1) return 'connecting';
  return 'connected';
}

export default function TopBar() {
  const latestPacket = useTelemetryStore((state) => state.latestPacket);
  const connectionStatus = useTelemetryStore((state) => state.connectionStatus);
  const totalPacketsRx = useTelemetryStore((state) => state.totalPacketsRx);
  const packetLossPercent = useTelemetryStore((state) => state.packetLossPercent);
  const missionStartMs = useTelemetryStore((state) => state.missionStartMs);
  const reconnectAttempts = useTelemetryStore((state) => state.reconnectAttempts);
  const reconnectMaxAttempts = useTelemetryStore((state) => state.reconnectMaxAttempts);

  const handleExport = useCallback(() => {
    window.electronAPI?.exportCSV?.();
  }, []);

  const met = missionStartMs ? Date.now() - missionStartMs : latestPacket?.missionElapsedMs || 0;
  const rssi = latestPacket?.link.rssi ?? -120;
  const bars = signalBars(rssi);
  const statusLabel = connectionStatus === 'connected'
    ? 'LIVE'
    : connectionStatus === 'connecting'
      ? `RECONNECTING (${reconnectAttempts}/${reconnectMaxAttempts})`
      : connectionStatus === 'failed'
        ? 'DISC'
        : 'DISC';
  const statusVariant = connectionStatus === 'failed' ? 'disconnected' : connectionStatus;

  return (
    <header className="topbar">
      <div className="badge">◈ INSPACE · CANSAT · GS</div>
      <StatusBadge variant={statusVariant}>{statusLabel}</StatusBadge>
      <div className="separator" />
      <div className="badge">MET {formatDuration(met)}</div>
      <div className="badge">PKT RX {totalPacketsRx.toLocaleString('en-US')}</div>
      <StatusBadge variant={packetLossVariant(packetLossPercent)}>PKT LOSS {packetLossPercent.toFixed(1)}%</StatusBadge>
      <div className="separator" />
      <div className="badge">
        RSSI {rssi.toFixed(0)} dBm
        <span aria-hidden="true" style={{ display: 'inline-flex', gap: 2, marginLeft: 4 }}>
          {Array.from({ length: 5 }).map((_, index) => (
            <span
              key={index}
              style={{
                width: 3,
                height: 8 + index * 3,
                background: index < bars ? 'var(--green)' : 'var(--bg-border)',
                display: 'inline-block',
              }}
            />
          ))}
        </span>
      </div>
      <div className="badge">{latestPacket?.link.port || 'COM?'} · {latestPacket?.link.baudRate?.toLocaleString('en-US') || '115200'}</div>
      <button type="button" onClick={handleExport}>EXPORT CSV</button>
    </header>
  );
}
