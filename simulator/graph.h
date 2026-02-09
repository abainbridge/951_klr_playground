#pragma once

#include <stdint.h>


// Signals to graph are:
typedef enum {
    TO_KLR_RESET,
    TO_KLR_IGNTION,
    FROM_KLR_IGNITION,
    FROM_KLR_CYCLING_VALVE_PWM,
    FROM_KLR_FULL_LOAD_SIGNAL,
    FROM_KLR_BLINK_CODE,
    NUM_GRAPHS
} graph_id_t;


void graph_add_point(graph_id_t id, unsigned time, uint8_t val);

// Draws a chart for the specified graph. The displayed range is from
// time_now - time_range_to_display, to time_now.
void graph_draw(graph_id_t id, unsigned time_now, unsigned time_range_to_display,
    int x, int y, int w, int h);
