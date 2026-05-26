# INSpace CanSat GS

Desktop ground-station app for the INSpace CanSat workflow.

## Start here
- [Simple run guide](docs/USER_RUN_GUIDE.md)
- [Architecture and implementation guide](docs/ARCHITECTURE_AND_IMPLEMENTATION.md)
- [Hardware integration guide](docs/HARDWARE_INTEGRATION.md)
- [Competition checklist](docs/COMPETITION_CHECKLIST.md)

## Quick start
1. Install dependencies:

```bash
cd frontend
npm install
```

2. Start the Electron app in development mode:

```bash
cd frontend
npm run electron:dev
```

3. If you want live telemetry, start the Python backend from the repository root in another terminal.

## Useful scripts
- `cd frontend && npm run dev` starts only the Vite renderer
- `cd frontend && npm run electron:dev` starts Vite and Electron together
- `cd frontend && npm run build` builds the renderer for production
- `cd frontend && npm run electron:build` builds the app and packages it with Electron Builder
- `cd frontend && npm run dist` creates a distributable build

## Project layout
- `frontend/` contains the Electron main process, Vite app, and React dashboard
- `backend/` contains the Python telemetry backend
- `docs/` contains user-facing guides and system documentation

## Notes
- Use mock mode in the backend if hardware is not connected
- The Electron app expects telemetry through the Python backend during normal operation
