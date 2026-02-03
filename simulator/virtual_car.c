// Own header
#include "virtual_car.h"

// This project's headers
#include "cpu.h"
#include "graph.h"

// Deadfrog headers
#include "df_font.h"
#include "df_window.h"

// Standard headers
#include <math.h>


static double const MIN_ENGINE_RPM = 850;
static double const MAX_ENGINE_RPM = 6500;
static double const MAX_TURBO_RPM = 200000;


VirtualCar g_virtual_car;
static bool g_t1;


Byte read_PB(Byte p) {
    return 0;
}

Byte read_t1(void) {
    return g_t1;
}

void write_p1(Byte d) {
    d = d;
}

void write_PB(Byte p, Byte val) {
    p = p;
}

void vc_init() {
    g_virtual_car.throttle_pos = 0;

    g_virtual_car.engine_rpm = 2500;
    g_virtual_car.crank_angle = 0;
    g_virtual_car.turbo_rpm = 0;
    g_virtual_car.manifold_pressure = 0;
    g_virtual_car.engine_power = 0;

    g_virtual_car.advance_period_residual = 0;
}

#define DRAW_TEXT(x, y, msg, ...) \
    DrawTextLeft(g_defaultFont, g_colourBlack, g_window->bmp, x, y, msg, ##__VA_ARGS__)

void vc_draw_state(int _x, int _y) {
    int x = _x + g_defaultFont->maxCharWidth;
    int y = _y + g_defaultFont->charHeight;
    x += DRAW_TEXT(x, y, "Engine RPM:%.0f  ", g_virtual_car.engine_rpm);
    x += DRAW_TEXT(x, y, "Throttle Pos:%d%%  ", (int)(g_virtual_car.throttle_pos*100.0));
    x += DRAW_TEXT(x, y, "Turbo KRPM:%.0f  ", g_virtual_car.turbo_rpm / 1e3);
    x += DRAW_TEXT(x, y, "Crank angle:%.2f  ", g_virtual_car.crank_angle);

    x = _x + g_defaultFont->maxCharWidth;
    y += g_defaultFont->charHeight * 1.2;
    x += DRAW_TEXT(x, y, "Manifold pressure:%.2f bar  ", g_virtual_car.manifold_pressure);
    x += DRAW_TEXT(x, y, "Engine power:%.0f BHP ", g_virtual_car.engine_power);
}

void signal_reset() {
    cpu_reset();
    graph_add_point(TO_KLR_RESET, master_clk, 0);
    graph_add_point(TO_KLR_RESET, master_clk, 255);
    graph_add_point(TO_KLR_RESET, master_clk + 1, 255);
    graph_add_point(TO_KLR_RESET, master_clk + 1, 0);
}

void signal_dwell_start() {
    g_t1 = 1;
    graph_add_point(TO_KLR_IGNTION, master_clk, 0);
    graph_add_point(TO_KLR_IGNTION, master_clk, 255);
}

void signal_dwell_end() {
    g_t1 = 0;
    xirq_pend = 1;
    graph_add_point(TO_KLR_IGNTION, master_clk, 255);
    graph_add_point(TO_KLR_IGNTION, master_clk, 0);
}

void vc_advance(double advance_period_seconds) {
    VirtualCar *car = &g_virtual_car;

    // 
    for (int i = 0; i < g_window->input.numKeysTyped; i++) {
        char key = g_window->input.keysTyped[i];
        if (key >= KEY_1 && key <= KEY_9) {
            car->throttle_pos = (key - KEY_1) / 8.0f;
        }
    }

    // Run car+engine physics
    double advance_period = advance_period_seconds + car->advance_period_residual;
    double step_period = 0.01;
    for (; advance_period > step_period; advance_period -= step_period) {
        double pressure_from_turbo = car->turbo_rpm * car->turbo_rpm;
        pressure_from_turbo /= (MAX_TURBO_RPM * MAX_TURBO_RPM);
        pressure_from_turbo += 1.0;
        if (pressure_from_turbo > 2.0)
            pressure_from_turbo = 2.0;

        car->manifold_pressure = pressure_from_turbo * sqrtf(car->throttle_pos);
        car->engine_power = (car->engine_rpm / MAX_ENGINE_RPM) * 
                            car->manifold_pressure * 110.0;

        double turbo_increment = car->engine_power * 5000.0;
        car->turbo_rpm += turbo_increment * step_period;
        car->turbo_rpm *= 1.0 - (step_period * 4.0);

        car->engine_rpm += car->engine_power * 10 * step_period;
        car->engine_rpm *= 1.0 - (step_period * 0.05);
        if (car->engine_rpm < MIN_ENGINE_RPM) {
            car->engine_rpm = MIN_ENGINE_RPM;
        }
        if (car->engine_rpm > MAX_ENGINE_RPM) {
            car->engine_rpm = MAX_ENGINE_RPM;
        }
    }
    car->advance_period_residual = advance_period;

    // This function, vc_advance() is passed different values of 
    // advance_period_seconds, depending on whether the simulation
    // is running in real time, or slowed down. In real time,
    // advance_period_seconds will be about 16ms (assuming the program's
    // window is on a monitor with a 60 Hz refresh rate). If we're
    // running slowed down by a factor of a million, then 
    // advance_period_seconds will be 16ns. In this case, we only
    // execute a single instruction once every ~50 calls. We need
    // to cope with this extremes.

    // If a reset or ignition interrupt signal that we generate occurs
    // during this advance period. If so, we should execute instructions
    // until it is time to send the signal, send it, then resume executing
    // instructions. That way the CPU simulation will see our signals at
    // exactly the right time.
    // 
    // At 850 RPM, the time between ignition events is 35ms.
    // At 6500 RPM, the time between ignition events is 4.6ms.

    double engine_speed_degrees_per_second = (car->engine_rpm / 60.0) * 360.0;
    double advance_period_cycles = advance_period_seconds * CPU_CLOCK_RATE_HZ;
    double target_crank_angle = car->crank_angle + engine_speed_degrees_per_second * advance_period_seconds;
    for (int cycle = 0; cycle < advance_period_cycles;) {
        // There are four crank angles that require us to end a block of CPU execution:
        // * -80 degrees: Reset the CPU
        // * -33 degrees: Start of ignition dwell period (coil starts charging up)
        // * -30 degrees: End of ignition dwell period (spark fires)
        // * +90 degrees: End of cycle for this cylinder. Move to next cylinder.
        //
        // Notes:
        // 1. The start of the dwell period varies by +/- 10 degrees depending on RPM and load.
        // 2. The dwell period should be 3ms, not a constant amount of crank rotation.
        // 3. There's no requirement to end the block of CPU execution when we reach +80
        //    degrees, but it makes the logic simpler.

        if (car->crank_angle < -80.0 && target_crank_angle > -80.0) {
            // Need to generate a reset
            double degrees_until_reset = -80.0 - car->crank_angle;
            int cycles_until_reset = CPU_CLOCK_RATE_HZ * degrees_until_reset /
                engine_speed_degrees_per_second;
            cpu_exec(cycles_until_reset);
            cycle += cycles_until_reset;
            car->crank_angle = -80.0;
            signal_reset();
        }
        else if (car->crank_angle < -33.0 && target_crank_angle > -33.0) {
            // Need to signal start of dwell period
            double degrees_until_dwell_start = -33.0 - car->crank_angle;
            int cycles_until_dwell_start = CPU_CLOCK_RATE_HZ * degrees_until_dwell_start /
                engine_speed_degrees_per_second;
            cpu_exec(cycles_until_dwell_start);
            cycle += cycles_until_dwell_start;
            car->crank_angle = -33.0;
            signal_dwell_start();
        }
        else if (car->crank_angle < -30.0 && target_crank_angle > -30.0) {
            // Need to signal end of dwell period
            double degrees_until_dwell_end = -30.0 - car->crank_angle;
            int cycles_until_dwell_end = CPU_CLOCK_RATE_HZ * degrees_until_dwell_end /
                engine_speed_degrees_per_second;
            cpu_exec(cycles_until_dwell_end);
            cycle += cycles_until_dwell_end;
            car->crank_angle = -30.0;
            signal_dwell_end();
        }
        else if (car->crank_angle < 90.0 && target_crank_angle > 90.0) {
            double degrees_until_90 = 90.0 - car->crank_angle;
            int cycles_until_80 = CPU_CLOCK_RATE_HZ * degrees_until_90 / engine_speed_degrees_per_second;
            cpu_exec(cycles_until_80);
            cycle += cycles_until_80;
            car->crank_angle = -90.0;
            target_crank_angle -= degrees_until_90 + 180.0;
        }
        else {
            cpu_exec(advance_period_cycles);
            car->crank_angle = target_crank_angle;
            break;
        }
    }
}
