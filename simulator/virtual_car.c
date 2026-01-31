#include "virtual_car.h"

#include "df_font.h"
#include "df_window.h"

#include <math.h>


static double const MIN_ENGINE_RPM = 850;
static double const MAX_ENGINE_RPM = 6500;
static double const MAX_TURBO_RPM = 200000;


VirtualCar g_virtual_car;


void vc_init() {
    g_virtual_car.engine_rpm = 2500;
    g_virtual_car.throttle_pos = 0;
    g_virtual_car.turbo_rpm = 0;

    g_virtual_car.advance_period_residual = 0;
    g_virtual_car.manifold_pressure = 0;
    g_virtual_car.engine_power = 0;
}

#define DRAW_TEXT(x, y, msg, ...) \
    DrawTextLeft(g_defaultFont, g_colourBlack, g_window->bmp, x, y, msg, ##__VA_ARGS__)

void vc_draw_state(int _x, int _y) {
    int x = _x + g_defaultFont->maxCharWidth;
    int y = _y + g_defaultFont->charHeight;
    x += DRAW_TEXT(x, y, "Engine RPM:%.0f  ", g_virtual_car.engine_rpm);
    x += DRAW_TEXT(x, y, "Throttle Pos:%d%%  ", (int)(g_virtual_car.throttle_pos*100.0));
    x += DRAW_TEXT(x, y, "Turbo KRPM:%.0f  ", g_virtual_car.turbo_rpm / 1e3);
    
    x = _x + g_defaultFont->maxCharWidth;
    y += g_defaultFont->charHeight * 1.2;
    x += DRAW_TEXT(x, y, "Manifold pressure:%.2f bar  ", g_virtual_car.manifold_pressure);
    x += DRAW_TEXT(x, y, "Engine power:%.0f BHP ", g_virtual_car.engine_power);
}

void vc_advance(double advance_period_seconds) {
    VirtualCar *car = &g_virtual_car;

    for (int i = 0; i < g_window->input.numKeysTyped; i++) {
        char key = g_window->input.keysTyped[i];
        if (key >= KEY_1 && key <= KEY_9) {
            car->throttle_pos = (key - KEY_1) / 8.0f;
        }
    }

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

        double turbo_increment = car->engine_power * 25000.0;
        car->turbo_rpm += turbo_increment * step_period;
        car->turbo_rpm *= 1.0 - (step_period * 12.0);

        car->engine_rpm += car->engine_power * 50 * step_period;
        car->engine_rpm *= 1.0 - (step_period * 0.3);
        if (car->engine_rpm < MIN_ENGINE_RPM) {
            car->engine_rpm = MIN_ENGINE_RPM;
        }
        if (car->engine_rpm > MAX_ENGINE_RPM) {
            car->engine_rpm = MAX_ENGINE_RPM;
        }
    }

    car->advance_period_residual = advance_period;
}
