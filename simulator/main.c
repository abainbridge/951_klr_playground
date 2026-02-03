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
    double sim_speed = 0.0001;
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
        double advance_time = (now - prev_now) * sim_speed;
        prev_now = now;
        if (advance_time > 0.03)
            advance_time = 0.03;
        advance_time += sim_speed;

        vc_advance(advance_time);
        
        BitmapClear(g_window->bmp, g_colourWhite);
        DrawTextRight(g_defaultFont, g_colourBlack, g_window->bmp,
            g_window->bmp->width, g_defaultFont->charHeight, "Sim Speed:%.6f ", sim_speed);
        vc_draw_state(0, 0);
        cpu_draw_state(0, 60);

        {
            int x = g_window->bmp->width * 0.1;
            int y = 250;
            int w = g_window->bmp->width * 0.8;
            int h = 40;
            graph_draw(TO_KLR_RESET, master_clk, CPU_CLOCK_RATE_HZ * 50e-3, x, y, w, h);
            y += h + 10;
            graph_draw(TO_KLR_IGNTION, master_clk, CPU_CLOCK_RATE_HZ * 50e-3, x, y, w, h);
        }

        UpdateWin(g_window);
        WaitVsync();
    }
}
