# INSpace CanSat GS - Hardware Integration Guide

## 1. Pre-connection checks
- [ ] Python venv is activated: `source venv/bin/activate` (required - Homebrew Python is externally managed)
- [ ] `python -m serial.tools.list_ports` lists the ESP32 dongle
- [ ] Note the port name: COM4 on Windows / /dev/ttyUSB0 or /dev/tty.usbserial-* on Linux/Mac
- [ ] Zigbee receiver is plugged in before running the backend

## 2. Starting the backend
```bash
python backend/main.py --port COM4 --baud 115200
```

## 3. Starting the Electron app
```bash
npm run electron:dev
```
Top bar should show: green LIVE badge + COM port name + rising PKT RX counter

## 4. First-packet failure modes and fixes

| Symptom | Likely cause | Fix |
|---|---|---|
| MALFORMED PKT - got N fields (N != 42) | ESP32 firmware field count changed | Update FIELDS_EXPECTED in config.py |
| All panels show 0 / null | snakeToCamel mismatch | console.log raw WS message in wsClient.js, compare key names to schema |
| Backend starts but no packets | Wrong COM port | Re-run list_ports, update --port arg |
| Packets arrive, AttitudeIndicator frozen | rAF loop not starting | Check browser console for canvas errors |
| Backend exits immediately | pyserial permission denied | `sudo chmod a+rw /dev/ttyUSB0` (Linux) |
| Pressure reads ~97000 instead of ~970 | Pa not converted to hPa | Verify field index 16 division by 100 in packet_builder.py |
| Battery reads ~3840 instead of ~3.84 | mV not converted to V | Verify field index 32 division by 1000 |
| Gyro drift not showing | lastCalibrationMs is 0 | IMUProcessor.last_calib_time not initialised - set to time.time() in __init__ |
| GPS panel blank | No 3D fix yet | Normal at ground - wait for satellite lock (hdop < 2.0, satellites > 6) |

## 5. Verifying complementary filter output
With the CanSat sitting still on the bench:
- comp_pitch should read ~= 0deg (+/-1deg)
- comp_roll should read ~= 0deg (+/-1deg)
- drift.rateZ should be < 0.05deg/s at rest
- drift.accumulatedZ should be near 0 after recalibrate command

## 6. Verifying IK display
- Tilt the CanSat by hand ~15deg in pitch
- IKOutputDisplay should show servo_β changing
- Predicted vs Reported delta should be < 1deg if ESP32 Kp/Kd match the GS defaults (1.20, 0.05)
- If delta > 3deg (red), ESP32 firmware is using different gain values - adjust GS sliders until delta < 1deg

## 7. Line-ending check
If every packet is rejected with wrong field count:
In serial_reader.py, change:
`line = raw.decode('utf-8').strip()`
to:
`line = raw.decode('utf-8').rstrip('\r\n')`
Some ESP32 firmware sends \n only.
