# INSpace CanSat GS - Simple Run Guide

This guide shows how to start the Electron ground-station app from scratch in a plain, non-technical way.

## What you need first
- A laptop with Node.js and Python installed
- The project folder opened in VS Code or Finder
- The Zigbee USB receiver and ESP32 telemetry transmitter if you want live hardware data

## Easiest way to run the app
If you only want to open the desktop app and see the interface, use this command in the project folder:

```bash
npm run electron:dev
```

That command starts two things together:
- the React app for the screen
- the Electron desktop shell that opens the window

When it works, you should see the app window with a green LIVE badge, packet count, and all the telemetry panels.

## First-time setup
Do this once after cloning or opening the project:

```bash
npm install
```

If you are running the Python backend yourself, also set up Python packages:

```bash
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

## How to run with live telemetry
Start the Python backend first:

```bash
python backend/main.py --port COM4 --baud 115200
```

Then start the Electron app in another terminal:

```bash
npm run electron:dev
```

## How to run without hardware
If you do not have the ESP32 or Zigbee dongle connected, use mock mode:

```bash
python backend/main.py --mock
```

Then run the Electron app:

```bash
npm run electron:dev
```

Mock mode generates fake telemetry so you can test the UI safely.

## What you should see
- Top bar with connection status
- Artificial horizon moving in the Orientation panel
- Altitude chart updating in the center
- GPS map and stats on the right
- Telemetry log scrolling at the bottom

## If something does not work
- If the app does not open, make sure `npm install` completed successfully
- If no data appears, check that the Python backend is running
- If the port is wrong, change the `--port` value to the correct COM or tty device
- If you want to test the UI only, use `--mock`

## Quick launch summary
1. Open a terminal in the project folder
2. Run `npm install`
3. Run `python backend/main.py --mock` for testing or `python backend/main.py --port COM4 --baud 115200` for live hardware
4. Run `npm run electron:dev`
5. Look for the LIVE badge and rising packet count
