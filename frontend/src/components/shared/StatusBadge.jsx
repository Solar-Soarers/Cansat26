import React from 'react';

const variants = {
  connected: 'badge badge--connected',
  disconnected: 'badge badge--disconnected',
  connecting: 'badge badge--connecting',
  active: 'badge badge--connected',
  idle: 'badge',
  fault: 'badge badge--disconnected',
  warning: 'badge badge--connecting',
};

export default function StatusBadge({ children, variant = 'idle' }) {
  return <span className={variants[variant] || variants.idle}>{children}</span>;
}
