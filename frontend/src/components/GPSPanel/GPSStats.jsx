import React from 'react';
import { useTelemetryStore } from '../../store/telemetryStore.js';

function Stat({ label, value }) {
  return (
    <div className="metric-card">
      <div className="metric-card__label">{label}</div>
      <div className="metric-card__value">{value}</div>
    </div>
  );
}

export default function GPSStats() {
  const gps = useTelemetryStore((state) => state.latestPacket?.gps);
  const lat = gps?.latitude ?? 0;
  const lng = gps?.longitude ?? 0;

  return (
    <div className="grid grid--stats-2x4">
      <Stat label="LAT" value={`${lat.toFixed(4)}°N`} />
      <Stat label="LON" value={`${lng.toFixed(4)}°E`} />
      <Stat label="GPS ALT" value={`${(gps?.altitudeGPS ?? 0).toFixed(1)} m`} />
      <Stat label="SPEED" value={`${(gps?.speedKmh ?? 0).toFixed(1)} km/h`} />
      <Stat label="COURSE" value={`${(gps?.courseDeg ?? 0).toFixed(1)}°`} />
      <Stat label="SATS" value={`${gps?.satellites ?? 0}`} />
      <Stat label="HDOP" value={`${(gps?.hdop ?? 0).toFixed(1)}`} />
      <Stat label="DIST FROM PAD" value={`${(gps?.distanceFromPad ?? 0).toFixed(1)} m`} />
    </div>
  );
}
