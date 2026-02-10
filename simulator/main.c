// This project's headers
#include "cpu.h"
#include "graph.h"
#include "virtual_car.h"

// Deadfrog headers
#include "df_font.h"
#include "df_message_dialog.h"
#include "df_time.h"
#include "df_window.h"
#include "fonts/df_mono.h"

// Standard headers
#include <stdio.h>


static void show_help_dialog(void) {
    MessageDialog("951 KLR Simulator Help",
        "Keyboard shortcuts:\n\n"
        "  Esc - Quit\n"
        "  -/+ keys (next to backspace) - Slow down/speed up the simulation\n"
        "  1-9 - Set throttle position. 1=idle 9=wide open",
        MsgDlgTypeOk);
}

static int draw_signals_from_dme(int y) {
    cpu_t *cpu = cpu_get();
    int x = g_defaultFont->maxCharWidth;
    int w = g_window->bmp->width * 0.8;
    int h = g_defaultFont->charHeight * 2;
    int text_y_offset = g_defaultFont->charHeight * 0.6;
    double time_range_to_display = CPU_CLOCK_RATE_HZ * 50e-3;
    DrawTextLeft(g_defaultFont, g_colourBlack, g_window->bmp, x, y, "Signals from DME to KLR");
    DrawTextLeft(g_defaultFont, g_colourBlack, g_window->bmp, x + 1, y, "Signals from DME to KLR");
    x = g_window->bmp->width * 0.15;
    y += g_defaultFont->charHeight * 1.2;
    int text_x = x - g_defaultFont->maxCharWidth;
    DrawTextRight(g_defaultFont, g_colourBlack, g_window->bmp, text_x, y + text_y_offset, "Reset");
    graph_draw(TO_KLR_RESET, cpu->master_clk, time_range_to_display, x, y, w, h);
    y += h + 10;
    DrawTextRight(g_defaultFont, g_colourBlack, g_window->bmp, text_x, y + text_y_offset, "Ignition");
    graph_draw(TO_KLR_IGNTION, cpu->master_clk, time_range_to_display, x, y, w, h);
    y += h + g_defaultFont->charHeight;
    HLine(g_window->bmp, 0, y, g_window->bmp->width, g_colourBlack);
    return y;
}

static int draw_signals_from_klr(int y) {
    cpu_t *cpu = cpu_get();
    int x = g_defaultFont->maxCharWidth;
    int w = g_window->bmp->width * 0.8;
    int h = g_defaultFont->charHeight * 2;
    int text_y_offset = g_defaultFont->charHeight * 0.6;
    double time_range_to_display = CPU_CLOCK_RATE_HZ * 0.1;
    DrawTextLeft(g_defaultFont, g_colourBlack, g_window->bmp, x, y, "Signals from KLR to DME");
    DrawTextLeft(g_defaultFont, g_colourBlack, g_window->bmp, x + 1, y, "Signals from KLR to DME");
    x = g_window->bmp->width * 0.15;
    y += g_defaultFont->charHeight * 1.2;
    int text_x = x - g_defaultFont->maxCharWidth;

    DrawTextRight(g_defaultFont, g_colourBlack, g_window->bmp, text_x, y + text_y_offset, "Ignition");
    graph_draw(FROM_KLR_IGNITION, cpu->master_clk, time_range_to_display, x, y, w, h);
    y += h + 10;

    DrawTextRight(g_defaultFont, g_colourBlack, g_window->bmp, text_x, y + text_y_offset, "Cyc valve");
    graph_draw(FROM_KLR_CYCLING_VALVE_PWM, cpu->master_clk, time_range_to_display, x, y, w, h);
    y += h + 10;

    DrawTextRight(g_defaultFont, g_colourBlack, g_window->bmp, text_x, y + text_y_offset, "Full load");
    graph_draw(FROM_KLR_FULL_LOAD_SIGNAL, cpu->master_clk, time_range_to_display, x, y, w, h);
    y += h + g_defaultFont->charHeight;

//    time_range_to_display = CPU_CLOCK_RATE_HZ * 0.5;
    DrawTextRight(g_defaultFont, g_colourBlack, g_window->bmp, text_x, y + text_y_offset, "Blink code");
    graph_draw(FROM_KLR_BLINK_CODE, cpu->master_clk, time_range_to_display, x, y, w, h);
    y += h + g_defaultFont->charHeight;

    HLine(g_window->bmp, 0, y, g_window->bmp->width, g_colourBlack);
    return y;
}

int main() {
//void __stdcall WinMain(void *instance, void *prev_instance, char *cmd_line, int show_cmd) {
    g_window = CreateWin(700, 800, WT_WINDOWED_FIXED, "951 KLR Simulator");
    g_defaultFont = LoadFontFromMemory(df_mono_8x15, sizeof(df_mono_8x15));

    vc_init();
    cpu_reset();

    FILE *rom_file = fopen("rom.bin", "rb");
    if (!rom_file) 
        rom_file = fopen("C:/Coding/951_klr_playground/rom.bin", "rb");
    if (!rom_file) return 0;
    cpu_t *cpu = cpu_get();
    fread(cpu->rom, 1, sizeof(cpu->rom), rom_file);

    double prev_now = GetRealTime();
    double sim_speed = 0.004;
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
        if (g_window->input.keyDowns[KEY_H])
            show_help_dialog();

        double now = GetRealTime();
        double advance_time = now - prev_now;
        prev_now = now;
        if (advance_time > 0.03)
            advance_time = 0.03;
        advance_time = 0.016; // Temporary hack to force the system to be deterministic for the
							  // interrupt timings not to be effected by the application's frame 
							  // rate.
        advance_time *= sim_speed;

        vc_advance(advance_time);
        
        BitmapClear(g_window->bmp, g_colourWhite);
        DrawTextRight(g_defaultFont, g_colourBlack, g_window->bmp,
            g_window->bmp->width, g_defaultFont->charHeight * 1.65, "Sim Speed:%.5f ", sim_speed);

        int y = 0;
        vc_draw_state(0, y);
        y += g_defaultFont->charHeight * 4.5;

        cpu_draw_state(0, y);
        y += g_defaultFont->charHeight * 15.0;

        y = draw_signals_from_dme(y) + g_defaultFont->charHeight;
        y = draw_signals_from_klr(y);

        DrawTextCentre(g_defaultFont, g_colourBlack,
            g_window->bmp, g_window->bmp->width / 2, g_window->bmp->height - g_defaultFont->charHeight,
            "For help press 'h'");

        UpdateWin(g_window);
        WaitVsync();
    }
}
