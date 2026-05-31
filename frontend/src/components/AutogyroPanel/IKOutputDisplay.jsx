import React, { useCallback, useMemo, useState } from 'react';
import { useTelemetryStore } from '../../store/telemetryStore.js';
import styles from './IKOutputDisplay.module.css';

const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

function deltaClassName(delta) {
  const absolute = Math.abs(delta);
  if (absolute < 1) {
    return 'value--green';
  }
  if (absolute < 3) {
    return 'value--amber';
  }
  return 'value--red';
}

function stateVariant(state) {
  if (state === 'ACTIVE') {
    return 'badge--connected';
  }
  if (state === 'SPINNING_UP') {
    return 'badge--connecting';
  }
  if (state === 'FAULT') {
    return 'badge--disconnected';
  }
  return '';
}

export default function IKOutputDisplay() {
  const compPitch = useTelemetryStore((state) => state.latestPacket?.imu?.compensated?.pitch ?? 0);
  const compRoll = useTelemetryStore((state) => state.latestPacket?.imu?.compensated?.roll ?? 0);
  const gyroX = useTelemetryStore((state) => state.latestPacket?.imu?.raw?.gyroX ?? 0);
  const gyroY = useTelemetryStore((state) => state.latestPacket?.imu?.raw?.gyroY ?? 0);
  const servoAlpha = useTelemetryStore((state) => state.latestPacket?.autogyro?.servoAlpha ?? 0);
  const servoBeta = useTelemetryStore((state) => state.latestPacket?.autogyro?.servoBeta ?? 0);
  const ikState = useTelemetryStore((state) => state.latestPacket?.autogyro?.state ?? 'IDLE');

  const [kp, setKp] = useState(1.20);
  const [kd, setKd] = useState(0.05);

  const adjustGain = useCallback((setter, step, min, max) => (event) => {
    event.preventDefault();
    const direction = event.deltaY > 0 ? -1 : 1;
    setter((value) => clamp(Number((value + direction * step).toFixed(3)), min, max));
  }, []);

  const predictedAlpha = clamp(kp * compRoll + kd * gyroY, -45, 45);
  const predictedBeta = clamp(kp * compPitch + kd * gyroX, -45, 45);

  const alphaTermOne = kp * compRoll;
  const alphaTermTwo = kd * gyroY;
  const betaTermOne = kp * compPitch;
  const betaTermTwo = kd * gyroX;

  const deltaAlpha = servoAlpha - predictedAlpha;
  const deltaBeta = servoBeta - predictedBeta;

  const sendGains = () => {
    window.electronAPI?.sendCommand?.({
      type: 'SET_IK_GAINS',
      kp: parseFloat(kp.toFixed(3)),
      kd: parseFloat(kd.toFixed(3)),
    });
  };

  const stateBadgeClass = useMemo(() => `badge ${stateVariant(ikState)}`.trim(), [ikState]);

  return (
    <div className="stack">
      <div className="panel-header">
        <h3 className="panel-title">IK EQUATIONS</h3>
        <span className={stateBadgeClass}>{ikState}</span>
      </div>

      <div className="stack">
        <div className={styles.eqRow}>
          <span className={styles.eqVar}>servo_α</span>
          <span className={styles.eqOp}>=</span>
          <span className={styles.eqClamp}>clamp(</span>
          <span className={styles.eqGain}>{kp.toFixed(2)}</span>
          <span className={styles.eqOp}>x</span>
          <span className={styles.eqInput}>({compRoll >= 0 ? '+' : ''}{compRoll.toFixed(2)}°)</span>
          <span className={styles.eqOp}>+</span>
          <span className={styles.eqGain}>{kd.toFixed(2)}</span>
          <span className={styles.eqOp}>x</span>
          <span className={styles.eqInput}>({gyroY >= 0 ? '+' : ''}{gyroY.toFixed(2)})</span>
          <span className={styles.eqClamp}>, -45°, +45° )</span>
          <span className={styles.eqOp}>=</span>
          <span className={styles.eqResult}>{predictedAlpha >= 0 ? '+' : ''}{predictedAlpha.toFixed(2)}°</span>
        </div>
        <div className={styles.eqRow}>
          <span className={styles.eqVar}>servo_α</span>
          <span className={styles.eqOp}>=</span>
          <span className={styles.eqClamp}>clamp(</span>
          <span className={styles.eqResult}>{alphaTermOne.toFixed(3)}</span>
          <span className={styles.eqOp}>+</span>
          <span className={styles.eqResult}>{alphaTermTwo.toFixed(3)}</span>
          <span className={styles.eqClamp}>, -45°, +45° )</span>
        </div>
        <div className={styles.eqRow}>
          <span className={styles.eqVar}>servo_β</span>
          <span className={styles.eqOp}>=</span>
          <span className={styles.eqClamp}>clamp(</span>
          <span className={styles.eqGain}>{kp.toFixed(2)}</span>
          <span className={styles.eqOp}>x</span>
          <span className={styles.eqInput}>({compPitch >= 0 ? '+' : ''}{compPitch.toFixed(2)}°)</span>
          <span className={styles.eqOp}>+</span>
          <span className={styles.eqGain}>{kd.toFixed(2)}</span>
          <span className={styles.eqOp}>x</span>
          <span className={styles.eqInput}>({gyroX >= 0 ? '+' : ''}{gyroX.toFixed(2)})</span>
          <span className={styles.eqClamp}>, -45°, +45° )</span>
          <span className={styles.eqOp}>=</span>
          <span className={styles.eqResult}>{predictedBeta >= 0 ? '+' : ''}{predictedBeta.toFixed(2)}°</span>
        </div>
        <div className={styles.eqRow}>
          <span className={styles.eqVar}>servo_β</span>
          <span className={styles.eqOp}>=</span>
          <span className={styles.eqClamp}>clamp(</span>
          <span className={styles.eqResult}>{betaTermOne.toFixed(3)}</span>
          <span className={styles.eqOp}>+</span>
          <span className={styles.eqResult}>{betaTermTwo.toFixed(3)}</span>
          <span className={styles.eqClamp}>, -45°, +45° )</span>
        </div>
      </div>

      <div className={styles.sectionDivider} />

      <div className={styles.compRow}>
        <div className={styles.compCard}>
          <div className={styles.compTitle}>ESP32 REPORTED</div>
          <div className={styles.compVal}>servo_α: {servoAlpha.toFixed(2)}°</div>
          <div className={styles.compVal}>servo_β: {servoBeta.toFixed(2)}°</div>
        </div>
        <div className={styles.compCard}>
          <div className={styles.compTitle}>GS PREDICTED</div>
          <div className={styles.compVal}>servo_α: {predictedAlpha.toFixed(2)}°</div>
          <div className={styles.compVal}>servo_β: {predictedBeta.toFixed(2)}°</div>
          <div className={styles.deltaRow}>
            <span className={deltaClassName(deltaAlpha)}>Δα {deltaAlpha.toFixed(2)}°</span>
            {' | '}
            <span className={deltaClassName(deltaBeta)}>Δβ {deltaBeta.toFixed(2)}°</span>
          </div>
        </div>
      </div>

      <div className={styles.sectionDivider} />

      <div className={styles.gainPanel}>
        <button
          type="button"
          className={styles.gainCard}
          onWheel={adjustGain(setKp, 0.01, 0.50, 3.00)}
          aria-label="Adjust K P with mouse wheel"
        >
          <span className={styles.gainLabel}>Kp</span>
          <span className={styles.gainValue}>{kp.toFixed(3)}</span>
          <span className={styles.gainHint}>Wheel or trackpad scroll</span>
        </button>
        <button
          type="button"
          className={styles.gainCard}
          onWheel={adjustGain(setKd, 0.005, 0.00, 0.50)}
          aria-label="Adjust K D with mouse wheel"
        >
          <span className={styles.gainLabel}>Kd</span>
          <span className={styles.gainValue}>{kd.toFixed(3)}</span>
          <span className={styles.gainHint}>Wheel or trackpad scroll</span>
        </button>
        <div className={styles.gainNote}>Scroll on a gain card to change its value. Click send after tuning.</div>
        <button type="button" className={styles.sendBtn} onClick={sendGains}>SEND GAINS TO ESP32 ↗</button>
      </div>
    </div>
  );
}
