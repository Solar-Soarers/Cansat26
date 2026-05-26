import React, { useEffect, useMemo, useRef, useState } from 'react';
import { formatShortClock } from '../../lib/formatters.js';
import { useTelemetryStore } from '../../store/telemetryStore.js';

function formatLine(packet) {
  const clock = formatShortClock(packet.missionElapsedMs);
  const phase = packet.mission.phase;
  const severity = packet.link.packetLossPercent > 5 ? 'ERR' : packet.imu.drift.rateZ > 0.5 ? 'WARN' : 'PKT';
  return `[${clock}] ${severity}#${String(packet.packetId).padStart(4, '0')} ${phase} ALT:${packet.altitude.barometric.toFixed(1)} PITCH:${packet.imu.compensated.pitch.toFixed(1)} ROLL:${packet.imu.compensated.roll.toFixed(1)} HDG:${packet.imu.compensated.heading.toFixed(1)} BAT:${packet.power.batteryVoltage.toFixed(2)}V`;
}

export default function LogConsole() {
  const packetHistory = useTelemetryStore((state) => state.packetHistory);
  const [paused, setPaused] = useState(false);
  const scrollRef = useRef(null);

  const lines = useMemo(() => packetHistory.slice(-500).map((packet) => ({ packet, text: formatLine(packet) })), [packetHistory]);

  useEffect(() => {
    if (!paused && scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [lines, paused]);

  return (
    <div className="panel panel--fill scroll-panel panel-pad-0 log-panel">
      <div className="panel-header panel-header--tight">
        <h3 className="panel-title">TELEMETRY LOG</h3>
        <button type="button" onClick={() => setPaused((value) => !value)}>{paused ? 'RESUME SCROLL' : 'PAUSE SCROLL'}</button>
      </div>
      <div className="console" ref={scrollRef}>
        {lines.map(({ packet, text }) => (
          <div key={packet.packetId} className="console__line">
            <span className="console__time">[{formatShortClock(packet.missionElapsedMs)}]</span>{' '}
            <span className="console__pkt">PKT#{String(packet.packetId).padStart(4, '0')}</span>{' '}
            <span className="console__value">{packet.mission.phase}</span>{' '}
            <span className="console__value">ALT:{packet.altitude.barometric.toFixed(1)}</span>{' '}
            <span className={packet.link.packetLossPercent > 5 ? 'console__err' : packet.imu.drift.rateZ > 0.5 ? 'console__warn' : 'console__value'}>
              {text.replace(/^\[[^\]]+\]\s+[^\s]+\s+/, '')}
            </span>
          </div>
        ))}
      </div>
    </div>
  );
}
