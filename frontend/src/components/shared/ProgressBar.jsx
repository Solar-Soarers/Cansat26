import React from 'react';

export default function ProgressBar({ value = 0, color = 'var(--cyan)', label, className = '' }) {
  const clamped = Math.max(0, Math.min(100, value));
  return (
    <div className={`stack ${className}`.trim()}>
      {label ? <div className="row row--spread"><span className="label">{label}</span><span className="value">{clamped.toFixed(0)}%</span></div> : null}
      <div className="progress" aria-label={label || 'progress'}>
        <div className="progress__bar" style={{ width: `${clamped}%`, background: color }} />
      </div>
    </div>
  );
}
