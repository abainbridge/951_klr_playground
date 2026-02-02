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

    double advance_period_residual; // In seconds
} VirtualCar;

extern VirtualCar g_virtual_car;

void vc_init();
void vc_draw_state(int _x, int _y);
void vc_advance(double advance_period_in_seconds);
