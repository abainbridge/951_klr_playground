#pragma once

typedef struct {
    // Input to the physics sim
    double throttle_pos;

    // Outputs from the physics sim
    double engine_rpm;
    double crank_angle;         // In degrees after TDC. Range is -90 to 90. Gets reset for every cylinder firing.
    double turbo_rpm;
    double manifold_pressure;   // In bar
    double engine_power;        // In BHP
    double dwell_start_angle;   // In degrees after TDC. Range is -90 to 90. Gets reset for every cylinder firing.

    double advance_period_residual; // In seconds. Accumulated error in physics modeling time.
    double crank_angle_residual; // In degrees. Accumulated error in crank angle advance.
} VirtualCar;

extern VirtualCar g_virtual_car;

void vc_init();
void vc_draw_state(int _x, int _y);
void vc_advance(double advance_period_in_seconds);
