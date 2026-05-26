import React, { useEffect, useMemo } from 'react';
import L from 'leaflet';
import { MapContainer, Marker, Polyline, Popup, TileLayer, useMap } from 'react-leaflet';
import { useTelemetryStore } from '../../store/telemetryStore.js';

function isValidCoord(lat, lon) {
  return Number.isFinite(lat) && Number.isFinite(lon) && Math.abs(lat) <= 90 && Math.abs(lon) <= 180;
}

function MapAutoCenter({ position }) {
  const map = useMap();
  useEffect(() => {
    if (position?.fixType === '3D' && isValidCoord(position.latitude, position.longitude)) {
      map.panTo([position.latitude, position.longitude], { animate: true, duration: 0.5 });
    }
  }, [map, position?.fixType, position?.latitude, position?.longitude]);
  return null;
}

const padIcon = L.divIcon({
  html: '<div style="width:10px;height:10px;background:#22C55E;border-radius:50%;border:2px solid #0A0C0F;"></div>',
  className: '',
  iconAnchor: [5, 5],
});

const canSatIcon = L.divIcon({
  html: '<div style="width:10px;height:10px;background:#45F3FF;border-radius:50%;border:2px solid #0A0C0F;box-shadow:0 0 6px #45F3FF;"></div>',
  className: '',
  iconAnchor: [5, 5],
});

function hdopClass(hdop) {
  if (!Number.isFinite(hdop)) {
    return '';
  }
  if (hdop < 1.5) {
    return 'value--green';
  }
  if (hdop < 2.5) {
    return 'value--amber';
  }
  return 'value--red';
}

function fixClass(fixType) {
  if (fixType === '3D') {
    return 'value--green';
  }
  if (fixType === '2D') {
    return 'value--amber';
  }
  return 'value--red';
}

export default function GPSMap() {
  const latestGPS = useTelemetryStore((state) => state.latestPacket?.gps);
  const history = useTelemetryStore((state) => state.packetHistory);
  const launchPad = useTelemetryStore((state) => state.launchPadCoords);

  const trail = useMemo(
    () => history
      .filter((packet) => packet.gps?.fixType === '3D' && isValidCoord(packet.gps?.latitude, packet.gps?.longitude))
      .slice(-200)
      .map((packet) => [packet.gps.latitude, packet.gps.longitude]),
    [history],
  );

  const center = useMemo(() => {
    if (latestGPS?.fixType === '3D' && isValidCoord(latestGPS.latitude, latestGPS.longitude)) {
      return [latestGPS.latitude, latestGPS.longitude];
    }
    return [28.6139, 77.209];
  }, [latestGPS]);

  return (
    <div className="stack">
      <style>{'.leaflet-tile { filter: brightness(0.4) saturate(0.6) hue-rotate(180deg); } .leaflet-container { background: #0A0C0F; }'}</style>
      <div className="map-shell">
        <MapContainer center={center} zoom={16} className="map-container" zoomControl={false} attributionControl={false}>
          <TileLayer url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png" />

          {launchPad ? (
            <Marker position={[launchPad.lat, launchPad.lng]} icon={padIcon}>
              <Popup>Launch Pad</Popup>
            </Marker>
          ) : null}

          {latestGPS?.fixType === '3D' && isValidCoord(latestGPS.latitude, latestGPS.longitude) ? (
            <Marker position={[latestGPS.latitude, latestGPS.longitude]} icon={canSatIcon}>
              <Popup>CanSat - Alt: {latestGPS.altitudeGPS.toFixed(1)}m</Popup>
            </Marker>
          ) : null}

          {trail.length > 1 ? (
            <Polyline positions={trail} pathOptions={{ color: '#45F3FF', weight: 1.5, opacity: 0.8 }} />
          ) : null}

          <MapAutoCenter position={latestGPS} />
        </MapContainer>
      </div>

      <div className="grid grid--stats-2x4">
        <div className="metric-card"><div className="metric-card__label">LAT</div><div className="metric-card__value">{latestGPS?.latitude?.toFixed(6) ?? '--'}</div></div>
        <div className="metric-card"><div className="metric-card__label">LON</div><div className="metric-card__value">{latestGPS?.longitude?.toFixed(6) ?? '--'}</div></div>
        <div className="metric-card"><div className="metric-card__label">SPD</div><div className="metric-card__value">{latestGPS?.speedKmh?.toFixed(1) ?? '--'} km/h</div></div>
        <div className="metric-card"><div className="metric-card__label">CRS</div><div className="metric-card__value">{latestGPS?.courseDeg?.toFixed(1) ?? '--'}°</div></div>
        <div className="metric-card"><div className="metric-card__label">SAT</div><div className="metric-card__value">{latestGPS?.satellites ?? '--'}</div></div>
        <div className="metric-card"><div className="metric-card__label">HDOP</div><div className={`metric-card__value ${hdopClass(latestGPS?.hdop)}`}>{latestGPS?.hdop?.toFixed(2) ?? '--'}</div></div>
        <div className="metric-card"><div className="metric-card__label">FIX</div><div className={`metric-card__value ${fixClass(latestGPS?.fixType)}`}>{latestGPS?.fixType ?? 'NO_FIX'}</div></div>
        <div className="metric-card"><div className="metric-card__label">PAD Δ</div><div className="metric-card__value">{latestGPS?.distanceFromPad?.toFixed(1) ?? '--'} m</div></div>
      </div>
    </div>
  );
}
