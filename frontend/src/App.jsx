import React from 'react';
import { useTelemetry } from './hooks/useTelemetry.js';
import { useTelemetryStore } from './store/telemetryStore.js';
import ErrorBoundary from './components/shared/ErrorBoundary.jsx';
import TopBar from './components/TopBar/index.jsx';
import AttitudeIndicator from './components/OrientationPanel/AttitudeIndicator.jsx';
import CanSat3DView from './components/OrientationPanel/CanSat3DView.jsx';
import CompassRose from './components/OrientationPanel/CompassRose.jsx';
import IMUReadings from './components/OrientationPanel/IMUReadings.jsx';
import StabilisationStatus from './components/AutogyroPanel/StabilisationStatus.jsx';
import ServoPositions from './components/AutogyroPanel/ServoPositions.jsx';
import IKOutputDisplay from './components/AutogyroPanel/IKOutputDisplay.jsx';
import AltitudeBigNumber from './components/AltitudePanel/AltitudeBigNumber.jsx';
import AltitudeChart from './components/AltitudePanel/AltitudeChart.jsx';
import GPSMap from './components/GPSPanel/GPSMap.jsx';
import GPSStats from './components/GPSPanel/GPSStats.jsx';
import SensorCards from './components/EnvironmentalPanel/SensorCards.jsx';
import HealthPanel from './components/SystemHealth/HealthPanel.jsx';
import LogConsole from './components/TelemetryLog/LogConsole.jsx';

function Panel({ title, children }) {
  return (
    <section className="panel panel--fill">
      <div className="panel-header">
        <h2 className="panel-title">{title}</h2>
      </div>
      {children}
    </section>
  );
}

function OrientationPanel() {
  const latestPacket = useTelemetryStore((state) => state.latestPacket);

  return (
    <Panel title="ORIENTATION">
      <div className="orientation-main">
        <div className="orientation-stack">
          <AttitudeIndicator />
          <CompassRose />
        </div>
        <div className="orientation-3d">
          <CanSat3DView />
        </div>
      </div>
      <IMUReadings packet={latestPacket} />
    </Panel>
  );
}

function AutogyroPanel() {
  return (
    <Panel title="AUTOGYRO PANEL">
      <div className="stack">
        <StabilisationStatus />
        <ServoPositions />
        <IKOutputDisplay />
      </div>
    </Panel>
  );
}

function AltitudePanel() {
  return (
    <Panel title="ALTITUDE">
      <AltitudeBigNumber />
      <div className="panel-stack-gap"><AltitudeChart /></div>
    </Panel>
  );
}

function GPSPanel() {
  return (
    <Panel title="GPS POSITION">
      <GPSMap />
      <div className="panel-inline-gap">
        <GPSStats />
      </div>
    </Panel>
  );
}

function App() {
  useTelemetry();
  const connectionStatus = useTelemetryStore((state) => state.connectionStatus);

  return (
    <div className="app-layout">
      <TopBar />
      <div className="left-column">
        <ErrorBoundary>
          <OrientationPanel />
        </ErrorBoundary>
        <ErrorBoundary>
          <AutogyroPanel />
        </ErrorBoundary>
      </div>
      <div className="center-column">
        <ErrorBoundary>
          <AltitudePanel />
        </ErrorBoundary>
        <ErrorBoundary>
          <LogConsole />
        </ErrorBoundary>
      </div>
      <div className="right-column">
        <ErrorBoundary>
          <GPSPanel />
        </ErrorBoundary>
        <ErrorBoundary>
          <SensorCards />
        </ErrorBoundary>
        <ErrorBoundary>
          <HealthPanel />
        </ErrorBoundary>
      </div>
      <div hidden data-connection-status={connectionStatus} />
    </div>
  );
}

export default App;