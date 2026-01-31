#pragma once

typedef struct {
    double throttle_pos;
    double engine_rpm;
    double turbo_rpm;

    double manifold_pressure;   // In bar
    double engine_power;        // In BHP
    double advance_period_residual; // In seconds
} VirtualCar;

extern VirtualCar g_virtual_car;

void vc_init();
void vc_draw_state(int _x, int _y);
void vc_advance(double advance_period_in_seconds);
