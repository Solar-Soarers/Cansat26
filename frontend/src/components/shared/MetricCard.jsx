import React from 'react';

export default function MetricCard({ label, value, subvalue, valueClassName = '' }) {
  return (
    <div className="metric-card">
      <div className="metric-card__label">{label}</div>
      <div className={`metric-card__value ${valueClassName}`.trim()}>{value}</div>
      {subvalue ? <div className="metric-card__subvalue">{subvalue}</div> : null}
    </div>
  );
}
