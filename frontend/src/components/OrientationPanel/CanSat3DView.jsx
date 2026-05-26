import React, { useMemo } from 'react';
import { useTelemetryStore } from '../../store/telemetryStore.js';
import styles from './CanSat3DView.module.css';

function normalizeAngle(value) {
  return ((value % 360) + 360) % 360;
}

export default function CanSat3DView() {
  const latestPacket = useTelemetryStore((state) => state.latestPacket);

  const pitch = latestPacket?.imu?.compensated?.pitch ?? 0;
  const roll = latestPacket?.imu?.compensated?.roll ?? 0;
  const heading = latestPacket?.imu?.compensated?.heading ?? 0;
  const rawPitch = latestPacket?.imu?.raw?.pitch ?? 0;
  const rawRoll = latestPacket?.imu?.raw?.roll ?? 0;
  const correctionX = latestPacket?.autogyro?.correctionXDeg ?? 0;
  const correctionY = latestPacket?.autogyro?.correctionYDeg ?? 0;
  const rotorRpm = latestPacket?.autogyro?.rotorRpm ?? 0;
  const state = latestPacket?.autogyro?.state ?? 'IDLE';

  const sceneStyle = useMemo(() => ({
    '--pitch': `${pitch}deg`,
    '--roll': `${roll}deg`,
    '--yaw': `${normalizeAngle(heading)}deg`,
    '--correction-x': `${correctionX}deg`,
    '--correction-y': `${correctionY}deg`,
  }), [pitch, roll, heading, correctionX, correctionY]);

  return (
    <div className={styles.wrapper}>
      <div className={styles.header}>
        <div>
          <div className={styles.title}>3D CANSAT VIEW</div>
          <div className={styles.subtitle}>Autogyro-guided attitude in 3D space</div>
        </div>
        <div className={styles.stateBadge}>{state}</div>
      </div>

      <div className={styles.scene} style={sceneStyle} aria-label="3D CanSat orientation view">
        <div className={styles.grid} />
        <div className={styles.axes}>
          <span className={`${styles.axis} ${styles.axisX}`}>X</span>
          <span className={`${styles.axis} ${styles.axisY}`}>Y</span>
          <span className={`${styles.axis} ${styles.axisZ}`}>Z</span>
        </div>
        <div className={styles.shadow} />
        <div className={styles.satellite}>
          <div className={styles.halo} />
          <div className={`${styles.panel} ${styles.panelLeft}`} />
          <div className={`${styles.panel} ${styles.panelRight}`} />
          <div className={styles.body}>
            <div className={styles.bodyCap} />
            <div className={styles.bodyWindow} />
            <div className={styles.antenna} />
          </div>
          <div className={`${styles.thruster} ${styles.thrusterTop}`} />
          <div className={`${styles.thruster} ${styles.thrusterBottom}`} />
        </div>
      </div>

      <div className={styles.readoutGrid}>
        <div className={styles.readoutCard}>
          <div className={styles.readoutLabel}>Pitch / Roll / Yaw</div>
          <div className={styles.readoutValue}>{pitch.toFixed(1)}° / {roll.toFixed(1)}° / {heading.toFixed(1)}°</div>
        </div>
        <div className={styles.readoutCard}>
          <div className={styles.readoutLabel}>Autogyro Correction</div>
          <div className={styles.readoutValue}>{correctionX.toFixed(1)}° / {correctionY.toFixed(1)}°</div>
        </div>
      </div>

      <div className={styles.footer}>
        <span>Raw P {rawPitch.toFixed(1)}°</span>
        <span>Raw R {rawRoll.toFixed(1)}°</span>
        <span>Rotor {rotorRpm.toFixed(0)} RPM</span>
      </div>
    </div>
  );
}
