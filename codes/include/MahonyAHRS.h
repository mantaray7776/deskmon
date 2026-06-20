#pragma once
#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Mahony AHRS filter
//  Fuses gyro + accel + magnetometer into a stable quaternion.
//  Much cheaper than Madgwick for this MCU — runs comfortably at 100 Hz.
//
//  Usage:
//    MahonyAHRS ahrs;
//    ahrs.update(gx,gy,gz, ax,ay,az, mx,my,mz, dt);
//    float heading = ahrs.getHeading();   // 0–360°, tilt-compensated
// ─────────────────────────────────────────────────────────────────────────────

class MahonyAHRS {
public:
    float q0 = 1, q1 = 0, q2 = 0, q3 = 0; // quaternion
    float roll = 0, pitch = 0, yaw = 0;     // Euler (degrees)

    // Tuning — increase Kp for faster convergence, lower for smoother output
    float Kp = 2.0f;
    float Ki = 0.005f;

    MahonyAHRS() { _ix=_iy=_iz=0; }

    // Call at sample rate (dt in seconds)
    // Gyro in rad/s, accel in any consistent unit, mag in any consistent unit
    void update(float gx, float gy, float gz,
                float ax, float ay, float az,
                float mx, float my, float mz,
                float dt)
    {
        float recipNorm;
        float hx, hy, bx, bz;
        float vx, vy, vz, wx, wy, wz;
        float ex, ey, ez;

        // Normalise accelerometer
        float anorm = sqrtf(ax*ax + ay*ay + az*az);
        if (anorm == 0.0f) return;
        ax /= anorm; ay /= anorm; az /= anorm;

        // Normalise magnetometer
        float mnorm = sqrtf(mx*mx + my*my + mz*mz);
        if (mnorm == 0.0f) return;
        mx /= mnorm; my /= mnorm; mz /= mnorm;

        // Reference direction of Earth's magnetic field
        hx = 2.0f*(mx*(0.5f - q2*q2 - q3*q3) + my*(q1*q2 - q0*q3) + mz*(q1*q3 + q0*q2));
        hy = 2.0f*(mx*(q1*q2 + q0*q3) + my*(0.5f - q1*q1 - q3*q3) + mz*(q2*q3 - q0*q1));
        bx = sqrtf(hx*hx + hy*hy);
        bz = 2.0f*(mx*(q1*q3 - q0*q2) + my*(q2*q3 + q0*q1) + mz*(0.5f - q1*q1 - q2*q2));

        // Estimated direction of gravity and magnetic field
        vx = 2.0f*(q1*q3 - q0*q2);
        vy = 2.0f*(q0*q1 + q2*q3);
        vz = q0*q0 - q1*q1 - q2*q2 + q3*q3;
        wx = 2.0f*bx*(0.5f - q2*q2 - q3*q3) + 2.0f*bz*(q1*q3 - q0*q2);
        wy = 2.0f*bx*(q1*q2 - q0*q3)         + 2.0f*bz*(q0*q1 + q2*q3);
        wz = 2.0f*bx*(q0*q2 + q1*q3)         + 2.0f*bz*(0.5f - q1*q1 - q2*q2);

        // Error (cross product between estimated and measured direction)
        ex = (ay*vz - az*vy) + (my*wz - mz*wy);
        ey = (az*vx - ax*vz) + (mz*wx - mx*wz);
        ez = (ax*vy - ay*vx) + (mx*wy - my*wx);

        // Integral feedback
        _ix += Ki * ex * dt;
        _iy += Ki * ey * dt;
        _iz += Ki * ez * dt;

        // Apply feedback
        gx += Kp*ex + _ix;
        gy += Kp*ey + _iy;
        gz += Kp*ez + _iz;

        // Integrate quaternion rate
        float dq0 = 0.5f*(-q1*gx - q2*gy - q3*gz) * dt;
        float dq1 = 0.5f*( q0*gx + q2*gz - q3*gy) * dt;
        float dq2 = 0.5f*( q0*gy - q1*gz + q3*gx) * dt;
        float dq3 = 0.5f*( q0*gz + q1*gy - q2*gx) * dt;
        q0 += dq0; q1 += dq1; q2 += dq2; q3 += dq3;

        // Normalise quaternion
        recipNorm = 1.0f / sqrtf(q0*q0 + q1*q1 + q2*q2 + q3*q3);
        q0 *= recipNorm; q1 *= recipNorm; q2 *= recipNorm; q3 *= recipNorm;

        // Euler angles (degrees)
        roll  =  atan2f(2.0f*(q0*q1 + q2*q3), 1.0f - 2.0f*(q1*q1 + q2*q2)) * 57.2958f;
        pitch = -asinf( 2.0f*(q1*q3 - q0*q2))                                * 57.2958f;
        yaw   =  atan2f(2.0f*(q0*q3 + q1*q2), 1.0f - 2.0f*(q2*q2 + q3*q3)) * 57.2958f;
    }

    // Tilt-compensated heading (0–360°, 0 = North)
    float getHeading() const {
        float h = yaw;
        if (h < 0)   h += 360.0f;
        if (h > 360) h -= 360.0f;
        return h;
    }

private:
    float _ix, _iy, _iz; // integral error
};
