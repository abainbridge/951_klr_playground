// This project's headers
#include "cpu.h"
#include "graph.h"
#include "virtual_car.h"

// Deadfrog headers
#include "df_font.h"
#include "df_time.h"
#include "df_window.h"
#include "fonts/df_mono.h"

// Standard headers
#include <stdio.h>


//int main() {
void __stdcall WinMain(void *instance, void *prev_instance, char *cmd_line, int show_cmd) {
    int desk_width, desk_height;
    GetDesktopRes(&desk_width, &desk_height);
    g_window = CreateWin(desk_width / 4, desk_height / 3, WT_WINDOWED_FIXED, "951 KLR Simulator");
    g_defaultFont = LoadFontFromMemory(df_mono_7x13, sizeof(df_mono_7x13));

    vc_init();
    cpu_reset();

    FILE *rom_file = fopen("C:/Coding/951_klr_playground/out.bin", "rb");
    if (!rom_file) return;
    fread(rom, 1, sizeof(rom), rom_file);

    double prev_now = GetRealTime();
    double sim_speed = 0.001;
    while (!g_window->windowClosed && !g_window->input.keys[KEY_ESC]) {
        InputPoll(g_window);
        for (int i = 0; i < g_window->input.numKeysTyped; i++) {
            if (g_window->input.keysTyped[i] == '=') {
                sim_speed *= 2.0;
            }
            if (g_window->input.keysTyped[i] == '-') {
                sim_speed /= 2.0;
            }
        }

        double now = GetRealTime();
        double advance_time = now - prev_now;
        prev_now = now;
        if (advance_time > 0.03)
            advance_time = 0.03;
        advance_time *= sim_speed;

        vc_advance(advance_time);
        
        BitmapClear(g_window->bmp, g_colourWhite);
        DrawTextRight(g_defaultFont, g_colourBlack, g_window->bmp,
            g_window->bmp->width, g_defaultFont->charHeight, "Sim Speed:%.5f ", sim_speed);

        int y = 0;
        vc_draw_state(0, y);
        y += g_defaultFont->charHeight * 4.5;

        cpu_draw_state(0, y);
        y += g_defaultFont->charHeight * 14.5;

        {
            int x = g_window->bmp->width * 0.15;
            int w = g_window->bmp->width * 0.8;
            int h = g_defaultFont->charHeight * 3;
            int text_x = x - g_defaultFont->maxCharWidth;
            int text_y_offset = g_defaultFont->charHeight * 1.1;
            double time_range_to_display = CPU_CLOCK_RATE_HZ * 50e-3;
            double x_tick_mark_spacing = CPU_CLOCK_RATE_HZ * 20e-3;
            DrawTextRight(g_defaultFont, g_colourBlack, g_window->bmp, text_x, y + text_y_offset, "Reset");
            graph_draw(TO_KLR_RESET, master_clk, time_range_to_display, x, y, w, h);
            y += h + 10;
            DrawTextRight(g_defaultFont, g_colourBlack, g_window->bmp, text_x, y + text_y_offset, "Ignition");
            graph_draw(TO_KLR_IGNTION, master_clk, time_range_to_display, x, y, w, h);
        }

        UpdateWin(g_window);
        WaitVsync();
    }
}
