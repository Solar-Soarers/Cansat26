import React from 'react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, ReferenceLine } from 'recharts';
import { useTelemetryStore } from '../../store/telemetryStore.js';
import { telemetrySelectors } from '../../store/telemetryStore.js';

function formatTick(value) {
  return `${Number(value).toFixed(0)}`;
}

export default function AltitudeChart() {
  const packetHistory = useTelemetryStore((state) => state.packetHistory);
  const data = telemetrySelectors.altitudeHistory({ packetHistory });
  const apogee = useTelemetryStore((state) => state.apogeeAltitude);

  return (
    <div className="chart-shell">
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={data} margin={{ top: 10, right: 18, left: 0, bottom: 8 }}>
          <CartesianGrid stroke="var(--bg-border)" strokeDasharray="3 3" />
          <XAxis dataKey="t" tickFormatter={formatTick} stroke="var(--text-muted)" />
          <YAxis stroke="var(--text-muted)" />
          <Tooltip
            contentStyle={{ background: 'var(--bg-surface)', border: '1px solid var(--bg-border)' }}
            formatter={(value) => [`${Number(value).toFixed(1)} m`, '']}
            labelFormatter={(value) => `${Number(value).toFixed(1)} s`}
          />
          <ReferenceLine y={apogee} stroke="var(--amber)" strokeDasharray="4 4" label="APOGEE" />
          <Line type="monotone" dataKey="baro" stroke="var(--cyan)" dot={false} strokeWidth={2} isAnimationActive={false} />
          <Line type="monotone" dataKey="gps" stroke="var(--purple)" dot={false} strokeWidth={2} isAnimationActive={false} />
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}
