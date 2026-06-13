/**
 * @typedef {Object} TelemetryPacket
 * @property {number} packetId
 * @property {number} timestamp
 * @property {number} missionElapsedMs
 * @property {{
 *   raw: {pitch:number, roll:number, heading:number, gyroX:number, gyroY:number, gyroZ:number, accelX:number, accelY:number, accelZ:number, magX:number, magY:number, magZ:number},
 *   compensated: {pitch:number, roll:number, heading:number},
 *   drift: {rateX:number, rateY:number, rateZ:number, accumulatedX:number, accumulatedY:number, accumulatedZ:number, filterAlpha:number, lastCalibrationMs:number}
 * }} imu
 * @property {{barometric:number, gps:number, seaLevel:number, apogee:number, descentRate:number}} altitude
 * @property {{latitude:number, longitude:number, altitudeGPS:number, speedKmh:number, courseDeg:number, satellites:number, hdop:number, fixType:string, distanceFromPad:number}} gps
 * @property {{temperatureExternal:number, temperatureInternal:number, pressure:number, humidity:number, uvIndex:number}} env
 * @property {{rotorRpm:number, servoAlpha:number, servoBeta:number, ikInputPitch:number, ikInputRoll:number, stabilityIndex:number, state:string, correctionXDeg:number, correctionYDeg:number}} autogyro
 * @property {{batteryVoltage:number, batteryPercent:number, current:number}} power
 * @property {{rssi:number, snr:number, packetLossPercent:number, port:string, baudRate:number}} link
 * @property {{phase:string, parachuteDeployed:boolean, autogyroArmed:boolean, buzzerActive:boolean}} mission
 */

export {};
