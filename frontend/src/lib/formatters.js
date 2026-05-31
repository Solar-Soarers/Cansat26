export function formatSigned(value, digits = 1, suffix = '') {
  const numeric = Number.isFinite(value) ? value : 0;
  const prefix = numeric >= 0 ? '+' : '';
  return `${prefix}${numeric.toFixed(digits)}${suffix}`;
}

export function formatFixed(value, digits = 1, suffix = '') {
  return `${Number.isFinite(value) ? value : 0}${suffix}`;
}

export function formatDuration(ms = 0) {
  const safeMs = Math.max(0, Math.floor(ms));
  const totalSeconds = Math.floor(safeMs / 1000);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  const centiseconds = Math.floor((safeMs % 1000) / 10);

  if (hours > 0) {
    return `${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
  }

  return `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}.${String(centiseconds).padStart(2, '0')}`;
}

export function formatShortClock(ms = 0) {
  const safeMs = Math.max(0, Math.floor(ms));
  const totalSeconds = Math.floor(safeMs / 1000);
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  const centiseconds = Math.floor((safeMs % 1000) / 10);
  return `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}.${String(centiseconds).padStart(2, '0')}`;
}

export function formatHeading(value = 0) {
  return `${Math.round(((value % 360) + 360) % 360)}°`;
}

export function formatBytesLike(value, digits = 1, suffix = '') {
  const numeric = Number.isFinite(value) ? value : 0;
  return `${numeric.toFixed(digits)}${suffix}`;
}

export function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}
