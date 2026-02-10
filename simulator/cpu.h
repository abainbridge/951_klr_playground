// license:BSD-3-Clause
// copyright-holders:Andrew Bainbridge, Dan Boris, Mirko Buffoni, Aaron Giles, 
// Couriersud
// Based on the original work Copyright Mirko Buffoni, Dan Boris

// Intel MCS-48 Emulator. Specifically the 8048-like processor in the Porsche 
// 944 Turbo KLR Module.

#pragma once

#include "types.h"


enum { CPU_CLOCK_RATE_HZ = 733333 };
#define CPU_CLOCK_PERIOD (1.0 / CPU_CLOCK_RATE_HZ)


typedef struct {
    u16 prev_pc;
    u16 pc;               // Program Counter

    u8 acc;               // Accumulator
    u8 *reg_ptr;          // Pointer to r0-r7
    u8 psw;               // Program Status Word
    bool f1;              // F1 flag (F0 is in PSW)
    u16 a11;              // 11th address bit, either 0x000 or 0x800
    u8 p1;                // Latched port 1
    u8 p2;                // Latched port 2
    u8 timer_counter;     // Is incremented every 32 cycles. Generates interrupt when overflows.
    u8 prescaler;         // 5-bit timer prescaler
    u8 t1_history;        // 8-bit history of the T1 input

    bool irq_state;       // true if the IRQ line is active
    bool irq_polled;      // true if last instruction was JNI (and not taken)
    bool irq_in_progress; // true if an IRQ is in progress
    bool timer_overflow;  // true on a timer overflow; cleared by taking interrupt
    bool timer_flag;      // true on a timer overflow; cleared on JTF
    bool tirq_enabled;    // true if the timer IRQ is enabled
    bool xirq_enabled;    // true if the external IRQ is enabled
    u8 timecount_enabled; // bitmask of timer/counter enabled

    int icount;           // Number of cycles to execute. Can be -1 when cpu_execute() returns.
    int master_clk;       // Total number of cycles executed.

    u8 rom[4096];
    u8 ram[128];
} cpu_t;


// Implement these call-back functions to handle access by the CPU core into the
// rest of the simulated system.
u8 cpu_t0_read(void);
u8 cpu_t1_read(void);
void cpu_port1_write(cpu_t *cpu, u8 val);
void cpu_port2_write(cpu_t *cpu, u8 val);
u8 cpu_external_mem_read(cpu_t *cpu, u8 addr);

cpu_t *cpu_get(void);
void cpu_reset();
void cpu_execute(int num_cycles);
void cpu_draw_state(int x, int y);
