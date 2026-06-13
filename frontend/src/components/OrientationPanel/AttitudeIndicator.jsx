import React, { useEffect, useRef } from 'react';
import { useTelemetryStore } from '../../store/telemetryStore.js';
import styles from './AttitudeIndicator.module.css';

export default function AttitudeIndicator() {
  const pitch = useTelemetryStore((state) => state.latestPacket?.imu?.compensated?.pitch ?? 0);
  const roll = useTelemetryStore((state) => state.latestPacket?.imu?.compensated?.roll ?? 0);
  const rawPitch = useTelemetryStore((state) => state.latestPacket?.imu?.raw?.pitch ?? 0);
  const rawRoll = useTelemetryStore((state) => state.latestPacket?.imu?.raw?.roll ?? 0);

  const canvasRef = useRef(null);
  const rafRef = useRef(null);
  const stateRef = useRef({ pitch: 0, roll: 0 });

  useEffect(() => {
    stateRef.current = { pitch, roll };
  }, [pitch, roll]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) {
      return undefined;
    }

    const ctx = canvas.getContext('2d');
    const W = canvas.width;
    const H = canvas.height;
    const CX = W / 2;
    const CY = H / 2;
    const R = 72;

    function draw() {
      const live = stateRef.current;
      const currentPitch = live.pitch;
      const currentRoll = live.roll;

      ctx.clearRect(0, 0, W, H);

      ctx.save();
      ctx.translate(CX, CY);
      ctx.rotate((currentRoll * Math.PI) / 180);

      ctx.beginPath();
      ctx.arc(0, 0, R, 0, Math.PI * 2);
      ctx.clip();

      const horizonY = (currentPitch / 90) * R;

      ctx.fillStyle = '#0D2137';
      ctx.fillRect(-R, -R, R * 2, R + horizonY);

      ctx.fillStyle = '#2D1B0E';
      ctx.fillRect(-R, horizonY, R * 2, R);

      ctx.strokeStyle = 'rgba(226,232,240,0.9)';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(-R, horizonY);
      ctx.lineTo(R, horizonY);
      ctx.stroke();

      const pitchIntervals = [5, 10, 15, 20];
      pitchIntervals.forEach((deg) => {
        const lineYPos = horizonY + (deg / 90) * R;
        const lineYNeg = horizonY - (deg / 90) * R;
        const halfLen = deg === 10 || deg === 20 ? 28 : 18;

        [lineYPos, lineYNeg].forEach((ly, i) => {
          if (Math.abs(ly) > R) {
            return;
          }
          ctx.strokeStyle = 'rgba(226,232,240,0.45)';
          ctx.lineWidth = 0.8;
          ctx.beginPath();
          ctx.moveTo(-halfLen, ly);
          ctx.lineTo(halfLen, ly);
          ctx.stroke();
          ctx.fillStyle = 'rgba(148,163,184,0.7)';
          ctx.font = '8px JetBrains Mono, monospace';
          ctx.fillText(`${i === 0 ? '-' : '+'}${deg}`, halfLen + 3, ly + 3);
        });
      });

      ctx.restore();

      ctx.strokeStyle = '#FBBF24';
      ctx.lineWidth = 2;
      ctx.lineCap = 'round';

      ctx.beginPath();
      ctx.moveTo(CX - 30, CY);
      ctx.lineTo(CX - 10, CY);
      ctx.stroke();

      ctx.beginPath();
      ctx.moveTo(CX + 10, CY);
      ctx.lineTo(CX + 30, CY);
      ctx.stroke();

      ctx.fillStyle = '#FBBF24';
      ctx.beginPath();
      ctx.arc(CX, CY, 3, 0, Math.PI * 2);
      ctx.fill();

      ctx.strokeStyle = '#1E2530';
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.arc(CX, CY, R + 1, 0, Math.PI * 2);
      ctx.stroke();

      ctx.save();
      ctx.translate(CX, CY);
      ctx.strokeStyle = 'rgba(69,243,255,0.4)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.arc(0, 0, R - 4, -Math.PI * 0.75, -Math.PI * 0.25);
      ctx.stroke();
      ctx.rotate((currentRoll * Math.PI) / 180);
      ctx.fillStyle = '#45F3FF';
      ctx.beginPath();
      ctx.moveTo(0, -(R - 4));
      ctx.lineTo(-4, -(R - 12));
      ctx.lineTo(4, -(R - 12));
      ctx.closePath();
      ctx.fill();
      ctx.restore();

      rafRef.current = requestAnimationFrame(draw);
    }

    rafRef.current = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(rafRef.current);
  }, []);

  return (
    <div className={styles.wrapper}>
      <canvas ref={canvasRef} width={160} height={160} className={styles.canvas} aria-label="Attitude indicator" />
      <div className={styles.rawRow}>
        <span className={styles.rawLabel}>RAW</span>
        <span className={styles.rawVal}>P {rawPitch.toFixed(1)}°</span>
        <span className={styles.rawVal}>R {rawRoll.toFixed(1)}°</span>
        <span className={styles.compLabel}>COMP</span>
        <span className={styles.compVal}>P {pitch.toFixed(1)}°</span>
        <span className={styles.compVal}>R {roll.toFixed(1)}°</span>
      </div>
    </div>
  );
}
