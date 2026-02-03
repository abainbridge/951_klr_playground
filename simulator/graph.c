// Own header
#include "graph.h"

// Deadfrog headers
#include "df_bitmap.h"
#include "df_window.h"


typedef struct {
    unsigned time;
    uint8_t val;
} graph_point_t;


enum { MAX_POINTS = 64 };

typedef struct {
    graph_point_t points[MAX_POINTS]; // In time order. Oldest first.
    int first_idx;
    int last_idx;   // if first_idx == last_idx, there are no points.
} graph_t;


static graph_t g_graphs[NUM_GRAPHS];

void graph_add_point(graph_id_t id, unsigned time, uint8_t val) {
    if (id >= NUM_GRAPHS) return;
    graph_t *g = &g_graphs[id];

    // TODO: check that time is greater than last point.

    int num_points = (g->last_idx - g->first_idx + MAX_POINTS) % MAX_POINTS;
    if (num_points == (MAX_POINTS - 1)) {
        // Already full. Remove oldest point.
        g->first_idx++;
        g->first_idx %= MAX_POINTS;
    }

    g->last_idx++;
    g->last_idx %= MAX_POINTS;
    g->points[g->last_idx].time = time;
    g->points[g->last_idx].val = val;
}

void graph_draw(graph_id_t id, unsigned time_now, unsigned time_range_to_display,
        int x, int y, int w, int h) {
    if (id >= NUM_GRAPHS) return;
    graph_t *g = &g_graphs[id];

    DfColour gray = Colour(220, 220, 220, 255);
    RectFill(g_window->bmp, x, y, w, h, gray);

    if (g->first_idx == g->last_idx)
        return; // No data to plot

    double start_time = (double)time_now - (double)time_range_to_display;
    double scale_x = (double)w / (double)time_range_to_display;
    double scale_y = -(double)(h - 1) / 255.0;
    y += h - 1;

    double x2;
    double y2;
    if (g->points[g->last_idx].time < start_time) {
        HLine(g_window->bmp, x, y + g->points[g->last_idx].val * scale_y, w, g_colourBlack);
        return;
    }

    for (int i = g->first_idx; i != g->last_idx; i = (i + 1) % MAX_POINTS) {
        graph_point_t *p1 = &g->points[i];
        graph_point_t *p2 = &g->points[(i + 1) % MAX_POINTS];
        if (p2->time < start_time)
            continue;
        double x1 = x + (p1->time - start_time) * scale_x;
        double y1 = y + p1->val * scale_y;
        x2 = x + (p2->time - start_time) * scale_x;
        y2 = y + p2->val * scale_y;
        if (x1 < x) x1 = x;
        DrawLine(g_window->bmp, x1, y1, x2, y2, g_colourBlack);
    }

    int len = (w + x);
    len -= x2;
    HLine(g_window->bmp, x2, y2, len, g_colourBlack);
}

void graph_draw_y_axis(unsigned time_now, unsigned time_range_to_display) {
}
