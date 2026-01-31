// This project's headers
#include "cpu.h"
#include "virtual_car.h"

// Deadfrog headers
#include "df_font.h"
#include "df_window.h"
#include "fonts/df_mono.h"

// Standard headers
#include <stdio.h>


Byte read_PB(Byte p) {
    return 0;
}

Byte read_t1(void) {
    return 0;
}

void write_p1(Byte d) {
}

void write_PB(Byte p, Byte val) {
}

//int main() {
void __stdcall WinMain(void *instance, void *prev_instance, char *cmd_line, int show_cmd) {
    int desk_width, desk_height;
    GetDesktopRes(&desk_width, &desk_height);
    g_window = CreateWin(desk_width / 4, desk_height / 4, WT_WINDOWED_FIXED, "951 KLR Simulator");
    g_defaultFont = LoadFontFromMemory(df_mono_7x13, sizeof(df_mono_7x13));

    vc_init();
    cpu_init();

    FILE *rom_file = fopen("C:/Coding/951_klr_playground/out.bin", "rb");
    if (!rom_file) return;
    fread(rom, 1, sizeof(rom), rom_file);

    while (!g_window->windowClosed && !g_window->input.keys[KEY_ESC]) {
        InputPoll(g_window);
        vc_advance(0.016);
//        cpu_exec(0.0016);
        
        BitmapClear(g_window->bmp, g_colourWhite);
        vc_draw_state(0, 0);
        cpu_draw_state(0, 100);

        UpdateWin(g_window);
        WaitVsync();
    }
}
