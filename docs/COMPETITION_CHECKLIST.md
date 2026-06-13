# INSpace CanSat - Competition Day Checklist

## T-60 min (Setup)
- [ ] Laptop charged to 100%, charger packed
- [ ] Python venv confirmed working: `python backend/main.py --mock`
- [ ] Electron app opens cleanly in mock mode, all panels live
- [ ] Zigbee USB dongle in bag
- [ ] Backup USB dongle in bag (driver pre-installed)
- [ ] CSV log folder exists: `/logs/` writable
- [ ] All team members know the port name for their OS

## T-30 min (Hardware)
- [ ] ESP32 powered and transmitting (LED pattern confirms TX)
- [ ] Zigbee receiver plugged in
- [ ] `list_ports` confirms dongle visible
- [ ] Backend started, first packet received
- [ ] Electron app shows green LIVE badge
- [ ] GPS acquiring fix - wait for 3D / HDOP < 2.0
- [ ] AttitudeIndicator horizon level when CanSat flat on table
- [ ] Drift badge shows green (< 0.5deg/s at rest)
- [ ] IK panel: Reported vs Predicted delta < 1deg at rest

## T-10 min (Pre-launch)
- [ ] Mission phase shows PRE_LAUNCH
- [ ] CSV log open in separate terminal to confirm writes: `tail -f logs/YYYY-MM-DD.csv`
- [ ] Autogyro state shows IDLE or SPINNING_UP depending on launch config
- [ ] Screenshot current baseline readings for post-flight comparison

## During flight
- [ ] One team member watches AltitudeChart for apogee
- [ ] One team member watches AutogyroPanel stability index
- [ ] Announce apogee over team comms when phase changes to APOGEE
- [ ] Watch drift.accumulatedZ - call recalibrate if > 5deg mid-flight (button in IMUReadings)
- [ ] PKT LOSS % visible in top bar - < 5% is acceptable

## Post-landing
- [ ] Mission phase shows LANDED
- [ ] Export CSV via top bar button before closing app
- [ ] Copy logs/ folder to USB backup immediately
- [ ] Note final apogee altitude, max descent rate, GPS landing coords for report
