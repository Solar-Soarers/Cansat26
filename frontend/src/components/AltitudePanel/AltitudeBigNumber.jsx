import React from 'react';
import { useTelemetryStore } from '../../store/telemetryStore.js';

export default function AltitudeBigNumber() {
  const latestPacket = useTelemetryStore((state) => state.latestPacket);
  const altitude = latestPacket?.altitude?.barometric ?? 0;
  const descentRate = latestPacket?.altitude?.descentRate ?? 0;
  const gpsAltitude = latestPacket?.altitude?.gps ?? 0;

  return (
    <div className="stack center-stack">
      <div className="panel-title">ALTITUDE & DESCENT PROFILE</div>
      <div className="altitude-readout value value--cyan">{altitude.toFixed(1)} <span className="altitude-unit">m</span></div>
      <div className="small-muted">↓ {descentRate.toFixed(1)} m/s · GPS: {gpsAltitude.toFixed(1)} m · BARO: {altitude.toFixed(1)} m</div>
    </div>
  );
}
