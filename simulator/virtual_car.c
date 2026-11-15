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

// Crank angles, in degrees after TDC, of the two fixed events in each
// cylinder's cycle.
static double const RESET_ANGLE = -80.0;    // DME pulses the KLR's reset line (the "trigger" signal).
static double const SPARK_ANGLE = -30.0;    // DME ends the dwell period. Should really vary with RPM and load.


// The DME charges the coil for a roughly constant time before each spark. Its
// real dwell map (map 61 in the DME EPROM) also depends on battery voltage and
// is longer below 2000 RPM, but at 14V it is about 3.7ms from 2000 RPM up, so
// we just use that everywhere. The DME never uses more than 144.5 degrees of
// crank rotation for dwell, which limits the time above about 6500 RPM.
static double const DWELL_SECONDS = 0.0037;
static double const MAX_DWELL_DEGREES = 144.5;

// Works out the crank angle at which the DME will start the dwell period for
// the next spark, wrapped into the -90 to 90 range that crank_angle uses.
// At high RPM the dwell is longer than the 60 degrees between -90 and the
// spark, so it starts during the previous cylinder's cycle. In that case the
// angle returned is after that cylinder's spark.
static double get_next_dwell_start_angle(double rpm) {
    double degrees_per_second = (rpm / 60.0) * 360.0;
    double dwell_degrees = degrees_per_second * DWELL_SECONDS;
    if (dwell_degrees > MAX_DWELL_DEGREES)
        dwell_degrees = MAX_DWELL_DEGREES;

    double angle = SPARK_ANGLE - dwell_degrees;
    if (angle < -90.0)
        angle += 180.0;
    return angle;
}


unsigned g_adc_latched_address;
unsigned g_adc_latch_cycle;
VirtualCar g_virtual_car;
static bool g_t1;


u8 cpu_t0_read(void) {
    return 0;
}

u8 cpu_t1_read(void) {
    return g_t1;
}

static u8 get_graph_val_from_bit_n(u8 val, int n) {
    val >>= n;
    return (val & 1) * 255;
}

void cpu_port1_write(cpu_t *cpu, u8 val) {
    u8 changes = cpu->p1 ^ val;

    if ((changes & 0x08) && (val & 0x08)) {
        // ADC ALE
        g_adc_latched_address = val & 7;
        g_adc_latch_cycle = cpu->master_clk;
    }
    if (changes & 0x10) {
        // Cycling valve changed
        graph_add_point(FROM_KLR_CYCLING_VALVE_PWM, cpu->master_clk, get_graph_val_from_bit_n(cpu->p1, 4));
        graph_add_point(FROM_KLR_CYCLING_VALVE_PWM, cpu->master_clk, get_graph_val_from_bit_n(val, 4));
    }
    if (changes & 0x20) {
        // Full load signal changed
        graph_add_point(FROM_KLR_FULL_LOAD_SIGNAL, cpu->master_clk, get_graph_val_from_bit_n(cpu->p1, 5));
        graph_add_point(FROM_KLR_FULL_LOAD_SIGNAL, cpu->master_clk, get_graph_val_from_bit_n(val, 5));
    }
}

void cpu_port2_write(cpu_t *cpu, u8 val) {
    u8 changes = cpu->p2 ^ val;

    if (changes & 0x10) {
        // Blink code changed
        graph_add_point(FROM_KLR_BLINK_CODE, cpu->master_clk, get_graph_val_from_bit_n(cpu->p2, 4));
        graph_add_point(FROM_KLR_BLINK_CODE, cpu->master_clk, get_graph_val_from_bit_n(val, 4));
    }
    if (changes & 0x80) {
        // Ignition output changed
        graph_add_point(FROM_KLR_IGNITION, cpu->master_clk, get_graph_val_from_bit_n(cpu->p2, 7));
        graph_add_point(FROM_KLR_IGNITION, cpu->master_clk, get_graph_val_from_bit_n(val, 7));
    }
}

u8 cpu_external_mem_read(cpu_t *cpu, u8 addr) {
    if (cpu->pc == 0x44d)
        cpu->master_clk = cpu->master_clk;

    double conversion_time = 14e-6; // In seconds. From datasheet.
    unsigned data_ready_time = g_adc_latch_cycle + CPU_CLOCK_RATE_HZ * conversion_time;
    if ((unsigned)cpu->master_clk < data_ready_time)
        data_ready_time = data_ready_time; // Error. Output accessed before it was ready.

    switch (g_adc_latched_address) {
    case 0: // Knock sensor noise level
        return 60;
    case 1: // Battery voltage
        return 200;
    case 2: // ?
        return 0;
    case 3: // Throttle position sensor supply voltage
        return 200;
    case 4: // Manifold air pressure
        return g_virtual_car.manifold_pressure * 100;
    case 5: // Knock sensor integrator
        return 2;
    case 6: // 6 kOhm to ground
        return 0;
    case 7: // Throttle position sensor angle
        return 20 + g_virtual_car.throttle_pos * 175;
    }

    return 0;
}

// On the KLR's circuit board, the T1 and INT pins of the microcontroller are
// connected together. Both are fed by the DME's ignition signal, via an
// inverter (see "Ignition input circuit" in the README). INT is active low, so
// the external interrupt is requested for as long as T1 is low, and stops
// being requested when T1 goes high.
static void set_t1_and_int(bool t1_level) {
    cpu_t *cpu = cpu_get();
    g_t1 = t1_level;
    cpu->irq_state = !t1_level; // Dodgy. There should be an interface function in the cpu module that we can call to do this.
}

void vc_init() {
    g_virtual_car.throttle_pos = 0;

    g_virtual_car.engine_rpm = 2500;
    g_virtual_car.crank_angle = 0;
    g_virtual_car.turbo_rpm = 0;
    g_virtual_car.manifold_pressure = 0;
    g_virtual_car.engine_power = 0;
    g_virtual_car.dwell_start_angle = get_next_dwell_start_angle(g_virtual_car.engine_rpm);

    g_virtual_car.advance_period_residual = 0;
    g_virtual_car.crank_angle_residual = 0;

    // The engine starts outside the dwell period, so T1 is low.
    set_t1_and_int(0);
}

#define DRAW_TEXT(x, y, msg, ...) \
    DrawTextLeft(g_defaultFont, g_colourBlack, g_window->bmp, x, y, msg, ##__VA_ARGS__)

void vc_draw_state(int _x, int _y) {
    cpu_t *cpu = cpu_get();

    int x = _x + g_defaultFont->maxCharWidth;
    int y = _y + g_defaultFont->charHeight / 2;
    DRAW_TEXT(x, y, "Virtual Car Simulation Parameters");
    DRAW_TEXT(x+1, y, "Virtual Car Simulation Parameters");
    x += g_defaultFont->maxCharWidth;
    y += g_defaultFont->charHeight * 1.2;
    x += DRAW_TEXT(x, y, "Engine RPM:%.0f  ", g_virtual_car.engine_rpm);
    x += DRAW_TEXT(x, y, "Throttle Pos:%d%%  ", (int)(g_virtual_car.throttle_pos*100.0));
    x += DRAW_TEXT(x, y, "Turbo KRPM:%.0f  ", g_virtual_car.turbo_rpm / 1e3);
    x += DRAW_TEXT(x, y, "Crank angle:%.2f  ", g_virtual_car.crank_angle);

    x = _x + g_defaultFont->maxCharWidth * 2;
    y += g_defaultFont->charHeight * 1.2;
    x += DRAW_TEXT(x, y, "Manifold pressure:%.2f bar  ", g_virtual_car.manifold_pressure);
    x += DRAW_TEXT(x, y, "Engine power:%.0f BHP  ", g_virtual_car.engine_power);
    x += DRAW_TEXT(x, y, "RealTime:%4.1fms  ", cpu->master_clk * CPU_CLOCK_PERIOD * 1e3);
    y += g_defaultFont->charHeight * 1.7;
    HLine(g_window->bmp, 0, y, g_window->bmp->width, g_colourBlack);
}

void signal_reset() {
    cpu_t *cpu = cpu_get();
    cpu_reset();
    graph_add_point(TO_KLR_RESET, cpu->master_clk, 0);
    graph_add_point(TO_KLR_RESET, cpu->master_clk, 255);
    graph_add_point(TO_KLR_RESET, cpu->master_clk + 1, 255);
    graph_add_point(TO_KLR_RESET, cpu->master_clk + 1, 0);
}

void signal_dwell_start() {
    cpu_t *cpu = cpu_get();
    set_t1_and_int(1);
    graph_add_point(TO_KLR_IGNTION, cpu->master_clk, 0);
    graph_add_point(TO_KLR_IGNTION, cpu->master_clk, 255);
}

void signal_dwell_end() {
    cpu_t *cpu = cpu_get();
    set_t1_and_int(0);
    graph_add_point(TO_KLR_IGNTION, cpu->master_clk, 255);
    graph_add_point(TO_KLR_IGNTION, cpu->master_clk, 0);
}

void vc_advance(double advance_period_requested) {
    VirtualCar *car = &g_virtual_car;

    // 
    for (int i = 0; i < g_window->input.numKeysTyped; i++) {
        char key = g_window->input.keysTyped[i];
        if (key >= KEY_1 && key <= KEY_9) {
            car->throttle_pos = (key - KEY_1) / 8.0f;
        }
    }


    //
    // Run car + engine physics (but don't increment crank angle - that will be done below)

    double step_period = 0.01;
    unsigned num_time_steps = (advance_period_requested + car->advance_period_residual) / step_period;
    for (unsigned i = 0; i < num_time_steps; i++) {
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

    double advance_period = num_time_steps * step_period;
    car->advance_period_residual += advance_period_requested - advance_period;


    //
    // Simulate CPU cycles and crank rotation

    // This function is passed different values of 
    // advance_period_seconds, depending on whether the simulation
    // is running in real time, or slowed down. In real time,
    // advance_period_seconds will be about 16ms (assuming the program's
    // window is on a monitor with a 60 Hz refresh rate). If we're
    // running slowed down by a factor of a million, then 
    // advance_period_seconds will be 16ns. In this case, we only
    // execute a single instruction once every ~50 calls. We need
    // to cope with these extremes.
    //
    // We will keep car->crank_angle in the range -90 to +90 degrees, since
    // this is the convenient range for all the events concerning a single
    // cylinder ignition.
    //
    // Each time through the loop below, wSime simulate as many CPU cycles as possible
    // before an event happens (reset, or dwell start/end, or if we've got to +90
    // of crank angle).
    // 
    // At 850 RPM, the time between ignition events is 35ms.
    // At 6500 RPM, the time between ignition events is 4.6ms.

    double engine_speed_degrees_per_second = (car->engine_rpm / 60.0) * 360.0;
    double engine_speed_degrees_per_cycle = engine_speed_degrees_per_second * CPU_CLOCK_PERIOD;
    double target_crank_angle = car->crank_angle + engine_speed_degrees_per_second * advance_period_requested + 
        car->crank_angle_residual;

    if (target_crank_angle < car->crank_angle)
        target_crank_angle = target_crank_angle;

    // The loop runs until the crank has reached target_crank_angle, to within
    // one CPU cycle's worth of rotation. Anything left over is carried to the
    // next call in crank_angle_residual. That also covers the case where this
    // call is asked to advance by less than one CPU cycle: the residual builds
    // up over several calls until there is enough for one cycle.
    for (;;) {
        // There are four crank angles that require us to end a block of CPU execution:
        // * RESET_ANGLE:       Reset the CPU
        // * dwell_start_angle: Start of the DME's dwell period (T1 goes high)
        // * SPARK_ANGLE:       End of the DME's dwell period (T1 goes low)
        // * +90 degrees:       End of cycle for this cylinder. Move to next cylinder.
        //
        // The dwell start moves around with RPM, because the dwell is a fixed
        // time. At low RPM it comes between the reset and the
        // spark. As RPM rises it moves earlier, to before the reset, and then
        // further back still into the previous cylinder's cycle, where it
        // shows up here as an angle after that cylinder's spark. So we can't
        // assume the events come in any fixed order. Instead, find whichever
        // one we will reach first.
        //
        // Inputs from DME that we model slightly wrongly:
        // 1. The spark angle should vary depending on RPM and load.

        enum { EVENT_NONE, EVENT_RESET, EVENT_DWELL_START, EVENT_DWELL_END, EVENT_NEXT_CYLINDER };
        int event = EVENT_NONE;
        double event_angle = target_crank_angle;
        if (target_crank_angle > 90.0) {
            event = EVENT_NEXT_CYLINDER;
            event_angle = 90.0;
        }
        if (car->crank_angle < RESET_ANGLE && event_angle > RESET_ANGLE) {
            event = EVENT_RESET;
            event_angle = RESET_ANGLE;
        }
        if (car->crank_angle < car->dwell_start_angle && event_angle > car->dwell_start_angle) {
            event = EVENT_DWELL_START;
            event_angle = car->dwell_start_angle;
        }
        if (car->crank_angle < SPARK_ANGLE && event_angle > SPARK_ANGLE) {
            event = EVENT_DWELL_END;
            event_angle = SPARK_ANGLE;
        }

        if (event == EVENT_NONE) {
            // No events needed - do all the requested cycles.
            double degrees_until_end = target_crank_angle - car->crank_angle;
            // Not using ceil() here because rounding up could be bad. For example, imagine
            // target_crank_angle was just short of an event angle. Rounding up could carry
            // crank_angle past that angle, and then we would skip the event on the next call.
            int cycles_until_end = degrees_until_end / engine_speed_degrees_per_cycle;
            // cycles_until_end can be zero, or negative if the ceil() at an
            // event below carried the crank slightly past the target. Either
            // way there is nothing more to do in this call.
            if (cycles_until_end > 0) {
                cpu_execute(cycles_until_end);
                car->crank_angle += cycles_until_end * engine_speed_degrees_per_cycle;
            }
            break;
        }

        // Run the CPU up to the event.
        // Note: used ceil() here to guarantee that crank_angle will be incremented to
        // more than the event angle. If it was just truncated, we'd increment it to some
        // tiny amount less, meaning that next time around the for loop we'd come here
        // again and wrongly generate the same event twice.
        double degrees_until_event = event_angle - car->crank_angle;
        int cycles_until_event = ceil(degrees_until_event / engine_speed_degrees_per_cycle);
        cpu_execute(cycles_until_event);
        car->crank_angle += cycles_until_event * engine_speed_degrees_per_cycle;

        switch (event) {
        case EVENT_RESET:
            signal_reset();
            break;
        case EVENT_DWELL_START:
            signal_dwell_start();
            break;
        case EVENT_DWELL_END:
            signal_dwell_end();
            // Now that this spark is done, decide where the dwell for the next
            // one starts. Doing it here, once per cylinder, means the angle
            // can't move while we are part way towards it.
            car->dwell_start_angle = get_next_dwell_start_angle(car->engine_rpm);
            break;
        case EVENT_NEXT_CYLINDER:
            // Wrap crank angle back to -90
            car->crank_angle -= 180.0;
            target_crank_angle -= 180.0;
            break;
        }
    }

    car->crank_angle_residual = target_crank_angle - car->crank_angle;
}
