import { useEffect, useMemo } from 'react';
import { telemetrySelectors, useTelemetryStore } from '../store/telemetryStore.js';

export function useTelemetry() {
  const handlePacket = useTelemetryStore((state) => state.handlePacket);
  const setConnectionStatus = useTelemetryStore((state) => state.setConnectionStatus);

  useEffect(() => {
    if (!window.electronAPI) {
      return undefined;
    }

    const unsubscribePacket = window.electronAPI.onTelemetry((packet) => {
      handlePacket(packet);
      setConnectionStatus('connected');
    });

    const unsubscribeStatus = window.electronAPI.onStatus((status) => {
      setConnectionStatus(status);
    });

    const unsubscribeConnection = window.electronAPI.onConnectionStatus((status) => {
      setConnectionStatus(status);
    });

    return () => {
      unsubscribePacket?.();
      unsubscribeStatus?.();
      unsubscribeConnection?.();
    };
  }, [handlePacket, setConnectionStatus]);
}

export function useTelemetryState(selector) {
  return useTelemetryStore(selector);
}

export function useTelemetryData() {
  const packetHistory = useTelemetryStore((state) => state.packetHistory);
  const latestPacket = useTelemetryStore((state) => state.latestPacket);
  const altitudeHistory = useMemo(() => telemetrySelectors.altitudeHistory({ packetHistory }), [packetHistory]);
  const gyroHistory = useMemo(() => telemetrySelectors.gyroHistory({ packetHistory }), [packetHistory]);
  const gpsTrail = useMemo(() => telemetrySelectors.gpsTrail({ packetHistory }), [packetHistory]);
  return { latestPacket, altitudeHistory, gyroHistory, gpsTrail };
}
