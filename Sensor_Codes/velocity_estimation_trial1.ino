struct KalmanVertical
{
    float h;    // altitude
    float v;    // vertical velocity

    float P00 = 1;
    float P01 = 0;
    float P10 = 0;
    float P11 = 1;

    float Qh = 0.01;   // process noise altitude
    float Qv = 0.10;   // process noise velocity

    float R = 1.0;     // barometer noise

    void predict(float accel, float dt)
    {
        // State prediction

        h = h + v * dt + 0.5f * accel * dt * dt;
        v = v + accel * dt;

        // Covariance prediction

        float P00_new =
            P00 + dt * (P10 + P01)
            + dt * dt * P11
            + Qh;

        float P01_new =
            P01 + dt * P11;

        float P10_new =
            P10 + dt * P11;

        float P11_new =
            P11 + Qv;

        P00 = P00_new;
        P01 = P01_new;
        P10 = P10_new;
        P11 = P11_new;
    }

    void update(float altitudeMeasured)
    {
        float y =
            altitudeMeasured - h;

        float S =
            P00 + R;

        float K0 =
            P00 / S;

        float K1 =
            P10 / S;

        h += K0 * y;
        v += K1 * y;

        float P00_old = P00;
        float P01_old = P01;

        P00 -= K0 * P00_old;
        P01 -= K0 * P01_old;
        P10 -= K1 * P00_old;
        P11 -= K1 * P01_old;
    }
};

KalmanVertical kf;
float pressureToAltitude(float pressurePa)
{
    const float P0 = 101325.0; // launch pressure

    return 44330.0 *
           (1.0 -
           pow(pressurePa / P0, 0.1903));
}
float getVerticalAccel(float ax,
                       float ay,
                       float az,
                       float roll,
                       float pitch)
{
    float sr = sin(roll);
    float cr = cos(roll);

    float sp = sin(pitch);
    float cp = cos(pitch);

    float az_world =
        -sp * ax
        + cp * sr * ay
        + cp * cr * az;

    // remove gravity

    return az_world - 9.81;
}
unsigned long lastTime;

void loop()
{
    unsigned long now = millis();

    float dt =
        (now - lastTime) / 1000.0f;

    lastTime = now;

    //-----------------------------------
    // Read MPU9250
    //-----------------------------------

    float ax = readAccelX();
    float ay = readAccelY();
    float az = readAccelZ();

    //-----------------------------------
    // Read attitude
    //-----------------------------------

    float roll  = getRollRadians();
    float pitch = getPitchRadians();

    //-----------------------------------
    // Vertical acceleration
    //-----------------------------------

    float aVert =
        getVerticalAccel(
            ax,
            ay,
            az,
            roll,
            pitch
        );

    //-----------------------------------
    // Predict
    //-----------------------------------

    kf.predict(aVert, dt);

    //-----------------------------------
    // Read BME680
    //-----------------------------------

    float pressure =
        readPressurePa();

    float altitude =
        pressureToAltitude(
            pressure
        );

    //-----------------------------------
    // Update
    //-----------------------------------

    kf.update(altitude);

    //-----------------------------------
    // Results
    //-----------------------------------

    Serial.print("Altitude: ");
    Serial.print(kf.h);

    Serial.print(" m  ");

    Serial.print("Vertical Velocity: ");
    Serial.print(kf.v);

    Serial.println(" m/s");
}
