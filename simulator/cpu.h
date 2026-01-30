#pragma once

#include "types.h"


void init_cpu(void);
void cpu_exec(void);

extern Byte acc;   // Accumulator
extern ADDRESS pc; // Program counter
extern long clk;   // clock

extern Byte timer_counter;   // Internal timer
extern Byte reg_pnt;  // pointer to register bank
extern Byte timer_on; // 0=timer off/1=timer on
extern Byte count_on; // 0=count off/1=count on
extern Byte psw;      // Processor status word
extern Byte sp;       // Stack pointer (part of psw)

extern Byte p1;        // I/O port 1
extern Byte p2;        // I/O port 2
extern Byte xirq_pend; // external IRQ pending
extern Byte tirq_pend; // timer IRQ pending
extern Byte t_flag;    // Timer flag

extern ADDRESS lastpc;
extern ADDRESS A11; // PC bit 11
extern ADDRESS A11ff;
extern Byte reg_bank;      // Register Bank (part of psw)
extern Byte f0;      // Flag Bit (part of psw)
extern Byte f1;      // Flag Bit 1
extern Byte ac;      // Aux Carry (part of psw)
extern Byte carry;      // Carry flag (part of psw)
extern Byte xirq_en; // external IRQ's enabled
extern Byte tirq_en; // Timer IRQ enabled
extern Byte irq_ex;  // IRQ executing

extern int master_count;

extern Byte ram[128];
extern Byte rom[4096];
