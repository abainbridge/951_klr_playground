#include "cpu.h"
#include "df_font.h"
#include "df_window.h"
#include "fonts/df_mono.h"
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

#include <windows.h>

void enableANSI() {
    DWORD dwMode = 0;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    if (!GetConsoleMode(hOut, &dwMode)) return;

    // Enable the virtual terminal processing flag
    dwMode |= 4; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
    SetConsoleMode(hOut, dwMode);
}

int main() {
    enableANSI();

    int desk_width, desk_height;
    GetDesktopRes(&desk_width, &desk_height);
    g_window = CreateWin(desk_width / 4, desk_height / 4, WT_WINDOWED_FIXED, "951 KLR Simulator");
    g_defaultFont = LoadFontFromMemory(df_mono_7x13, sizeof(df_mono_7x13));

    FILE *rom_file = fopen("C:/Coding/951_klr_playground/out.bin", "rb");
    if (!rom_file) return;
    fread(rom, 1, sizeof(rom), rom_file);

    init_cpu();
    cpu_exec();
}
