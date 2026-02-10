// license:BSD-3-Clause
// copyright-holders:Andrew Bainbridge, Dan Boris, Mirko Buffoni, Aaron Giles, 
// Couriersud
// Based on the original work Copyright Mirko Buffoni, Dan Boris
// 
// This code is mostly copied from MAME release from 29 Jan 2026. It came with 
// this TODO:
// - IRQ and/or timer increment timing is wrong? See test below. After IRQ,
//   A = 0x20 on MAME, A = 0x22 on the real 8048 as tested by bataais.
// 
//   stop tcnt
//   mov a,0xff
//   mov t,a
//   inc a
// 
//   en tcnti
//   strt t
// 
//   inc a
//   inc a
//   inc a
//   (etc.)
// 
//   With the following test, on MAME, A = 0xff after 30 NOPs, A = 0 after 31
//   NOPs. On the real 8048, A = 0xff after 31 NOPs, A = 0 after 32 NOPs.
//   It can mean that STRT T has a 1 cycle delay, or simply that MOV A,T gets
//   the timer value pre-increment.
// 
//   stop tcnt
//   mov a,0xff
//   mov t,a
//   strt t
// 
//   nop
//   nop
//   nop
//   (etc.)
//   mov a,t
// 
// - IRQ timing is hacked due to WY-100 needing to take JNI branch before
//   servicing interrupt (see m.irq_polled), probably related to note above?

// Own header
#include "cpu.h"

// Deadfrog headers
#include "df_bitmap.h"
#include "df_font.h"
#include "df_window.h"

// Standard headers
#include <assert.h>
#include <stdio.h>


// ****************************************************************************
// Defines
// ****************************************************************************

enum timer_bits {
    TIMER_ENABLED = 0x01,
    COUNTER_ENABLED = 0x02
};

enum flag_bits {
    C_FLAG = 0x80,
    A_FLAG = 0x40,
    F_FLAG = 0x20,
    B_FLAG = 0x10
};

// r0-r7 map to memory via reg_ptr
#define R0 m.reg_ptr[0]
#define R1 m.reg_ptr[1]
#define R2 m.reg_ptr[2]
#define R3 m.reg_ptr[3]
#define R4 m.reg_ptr[4]
#define R5 m.reg_ptr[5]
#define R6 m.reg_ptr[6]
#define R7 m.reg_ptr[7]


// ****************************************************************************
// Global variables
// ****************************************************************************

static cpu_t m;


// ****************************************************************************
// Static functions
// ****************************************************************************

static u8 rom_read(u16 a) { return m.rom[a]; }
static u8 ram_read(u16 a) { return m.ram[a]; }
static void ram_write(u16 a, u8 v) { m.ram[a] = v; }
static u8 ext_mem_read(u8 a) { return cpu_external_mem_read(&m, a); }
static void ext_mem_write(u16 a, u8 v) { assert(0); }
static void port1_write(u8 v) { cpu_port1_write(&m, v); m.p1 = v; }
static void port2_write(u8 v) { cpu_port2_write(&m, v); m.p2 = v; }
static int t0_read(void) { return cpu_t0_read(); }
static int t1_read(void) { return cpu_t1_read(); }

// fetch an opcode byte
static u8 opcode_fetch(void) {
    u16 address = m.pc;
    m.pc = ((m.pc + 1) & 0x7ff) | (m.pc & 0x800);
    return m.rom[address];
}

// fetch an opcode argument byte
static u8 argument_fetch(void) {
    u16 address = m.pc;
    m.pc = ((m.pc + 1) & 0x7ff) | (m.pc & 0x800);
    return m.rom[address];
}

// update reg_ptr to point to the appropriate register bank
static void update_reg_ptr(void) {
    m.reg_ptr = &m.ram[(m.psw & B_FLAG) ? 24 : 0];
}

// push the PC and PSW values onto the stack
static void push_pc_psw(void) {
    u8 sp = m.psw & 0x07;
    ram_write(8 + 2*sp, m.pc);
    ram_write(9 + 2*sp, ((m.pc >> 8) & 0x0f) | (m.psw & 0xf0));
    m.psw = (m.psw & 0xf0) | ((sp + 1) & 0x07);
}

// pull the PC and PSW values from the stack
static void pull_pc_psw(void) {
    u8 sp = (m.psw - 1) & 0x07;
    m.pc = ram_read(8 + 2*sp);
    m.pc |= ram_read(9 + 2*sp) << 8;
    m.psw = ((m.pc >> 8) & 0xf0) | sp;
    m.pc &= (m.irq_in_progress) ? 0x7ff : 0xfff;
    update_reg_ptr();
}

// pull the PC value from the stack, leaving the upper part of PSW intact
static void pull_pc(void) {
    u8 sp = (m.psw - 1) & 0x07;
    m.pc = ram_read(8 + 2*sp);
    m.pc |= ram_read(9 + 2*sp) << 8;
    m.pc &= (m.irq_in_progress) ? 0x7ff : 0xfff;
    m.psw = (m.psw & 0xf0) | sp;
}

static void execute_add(u8 dat) {
    u16 temp = m.acc + dat;
    u16 temp4 = (m.acc & 0x0f) + (dat & 0x0f);

    m.psw &= ~(C_FLAG | A_FLAG);
    m.psw |= (temp4 << 2) & A_FLAG;
    m.psw |= (temp >> 1) & C_FLAG;
    m.acc = temp;
}

static void execute_addc(u8 dat) {
    u8 carryin = (m.psw & C_FLAG) >> 7;
    u16 temp = m.acc + dat + carryin;
    u16 temp4 = (m.acc & 0x0f) + (dat & 0x0f) + carryin;

    m.psw &= ~(C_FLAG | A_FLAG);
    m.psw |= (temp4 << 2) & A_FLAG;
    m.psw |= (temp >> 1) & C_FLAG;
    m.acc = temp;
}

static void execute_jmp(u16 address) {
    u16 a11 = (m.irq_in_progress) ? 0 : m.a11;
    m.pc = address | a11;
}

static void execute_call(u16 address) {
    push_pc_psw();
    execute_jmp(address);
}

// perform the logic of a conditional jump instruction
static void execute_jcc(bool result) {
    u16 pch = m.pc & 0xf00;
    u8 offset = argument_fetch();
    if (result)
        m.pc = pch | offset;
}

// processing timers and counters
static void burn_cycles(int count) {
    if (m.timecount_enabled) {
        bool timer_over = false;

        // if the timer is enabled, accumulate prescaler cycles
        if (m.timecount_enabled & TIMER_ENABLED) {
            u8 old_timer = m.timer_counter;
            m.prescaler += count;
            m.timer_counter += m.prescaler >> 5;
            m.prescaler &= 0x1f;
            timer_over = m.timer_counter < old_timer;
        }

        // if the counter is enabled, poll the T1 test input once for each cycle
        else if (m.timecount_enabled & COUNTER_ENABLED) {
            for (; count > 0; count--, m.icount--, m.master_clk++) {
                m.t1_history = (m.t1_history << 1) | (t1_read() & 1);
                if ((m.t1_history & 3) == 2) {
                    if (++m.timer_counter == 0)
                        timer_over = true;
                }
            }
        }

        // if either source caused a timer overflow, set the flags
        if (timer_over) {
            m.timer_flag = true;

            // according to the docs, if an overflow occurs with interrupts disabled, the overflow is not stored
            if (m.tirq_enabled)
                m.timer_overflow = true;
        }
    }

    // (note: if timer counter is enabled, count was already reduced to 0)
    m.icount -= count;
    m.master_clk += count;
}

// check for and process IRQs
static void check_irqs() {
    // if something is in progress, we do nothing
    if (m.irq_in_progress)
        return;

    // external interrupts take priority
    else if (m.irq_state && m.xirq_enabled) {
        // indicate we took the external IRQ
        //        standard_irq_callback(0, m.pc);

        burn_cycles(2);
        m.irq_in_progress = true;

        // force JNI to be taken (hack)
        if (m.irq_polled) {
            m.pc = ((m.prev_pc + 1) & 0x7ff) | (m.prev_pc & 0x800);
            execute_jcc(true);
        }

        // transfer to location 0x03
        execute_call(0x03);
    }

    // timer overflow interrupts follow
    else if (m.timer_overflow && m.tirq_enabled) {
        //        standard_irq_callback(1, m.pc);

        burn_cycles(2);
        m.irq_in_progress = true;

        // transfer to location 0x07
        execute_call(0x07);

        // timer overflow flip-flop is reset once taken
        m.timer_overflow = false;
    }
}

// The mask of bits that the code can directly affect
enum { P2_MASK = 0xff };

#define OPHANDLER(_name) static void _name(void)

OPHANDLER( illegal ) {
    burn_cycles(1);
    printf("Illegal opcode = %02x @ %04X\n", rom_read(m.prev_pc), m.prev_pc);
}

OPHANDLER( add_a_r0 )       { burn_cycles(1); execute_add(R0); }
OPHANDLER( add_a_r1 )       { burn_cycles(1); execute_add(R1); }
OPHANDLER( add_a_r2 )       { burn_cycles(1); execute_add(R2); }
OPHANDLER( add_a_r3 )       { burn_cycles(1); execute_add(R3); }
OPHANDLER( add_a_r4 )       { burn_cycles(1); execute_add(R4); }
OPHANDLER( add_a_r5 )       { burn_cycles(1); execute_add(R5); }
OPHANDLER( add_a_r6 )       { burn_cycles(1); execute_add(R6); }
OPHANDLER( add_a_r7 )       { burn_cycles(1); execute_add(R7); }
OPHANDLER( add_a_xr0 )      { burn_cycles(1); execute_add(ram_read(R0)); }
OPHANDLER( add_a_xr1 )      { burn_cycles(1); execute_add(ram_read(R1)); }
OPHANDLER( add_a_n )        { burn_cycles(2); execute_add(argument_fetch()); }

OPHANDLER( adc_a_r0 )       { burn_cycles(1); execute_addc(R0); }
OPHANDLER( adc_a_r1 )       { burn_cycles(1); execute_addc(R1); }
OPHANDLER( adc_a_r2 )       { burn_cycles(1); execute_addc(R2); }
OPHANDLER( adc_a_r3 )       { burn_cycles(1); execute_addc(R3); }
OPHANDLER( adc_a_r4 )       { burn_cycles(1); execute_addc(R4); }
OPHANDLER( adc_a_r5 )       { burn_cycles(1); execute_addc(R5); }
OPHANDLER( adc_a_r6 )       { burn_cycles(1); execute_addc(R6); }
OPHANDLER( adc_a_r7 )       { burn_cycles(1); execute_addc(R7); }
OPHANDLER( adc_a_xr0 )      { burn_cycles(1); execute_addc(ram_read(R0)); }
OPHANDLER( adc_a_xr1 )      { burn_cycles(1); execute_addc(ram_read(R1)); }
OPHANDLER( adc_a_n )        { burn_cycles(2); execute_addc(argument_fetch()); }

OPHANDLER( anl_a_r0 )       { burn_cycles(1); m.acc &= R0; }
OPHANDLER( anl_a_r1 )       { burn_cycles(1); m.acc &= R1; }
OPHANDLER( anl_a_r2 )       { burn_cycles(1); m.acc &= R2; }
OPHANDLER( anl_a_r3 )       { burn_cycles(1); m.acc &= R3; }
OPHANDLER( anl_a_r4 )       { burn_cycles(1); m.acc &= R4; }
OPHANDLER( anl_a_r5 )       { burn_cycles(1); m.acc &= R5; }
OPHANDLER( anl_a_r6 )       { burn_cycles(1); m.acc &= R6; }
OPHANDLER( anl_a_r7 )       { burn_cycles(1); m.acc &= R7; }
OPHANDLER( anl_a_xr0 )      { burn_cycles(1); m.acc &= ram_read(R0); }
OPHANDLER( anl_a_xr1 )      { burn_cycles(1); m.acc &= ram_read(R1); }
OPHANDLER( anl_a_n )        { burn_cycles(2); m.acc &= argument_fetch(); }

OPHANDLER( anl_p1_n )       { burn_cycles(2); port1_write(m.p1 & argument_fetch()); }
OPHANDLER( anl_p2_n )       { burn_cycles(2); port2_write((m.p2 & argument_fetch()) | ~P2_MASK); }

OPHANDLER( call_0 )         { burn_cycles(2); execute_call(argument_fetch() | 0x000); }
OPHANDLER( call_1 )         { burn_cycles(2); execute_call(argument_fetch() | 0x100); }
OPHANDLER( call_2 )         { burn_cycles(2); execute_call(argument_fetch() | 0x200); }
OPHANDLER( call_3 )         { burn_cycles(2); execute_call(argument_fetch() | 0x300); }
OPHANDLER( call_4 )         { burn_cycles(2); execute_call(argument_fetch() | 0x400); }
OPHANDLER( call_5 )         { burn_cycles(2); execute_call(argument_fetch() | 0x500); }
OPHANDLER( call_6 )         { burn_cycles(2); execute_call(argument_fetch() | 0x600); }
OPHANDLER( call_7 )         { burn_cycles(2); execute_call(argument_fetch() | 0x700); }

OPHANDLER( clr_a )          { burn_cycles(1); m.acc = 0; }
OPHANDLER( clr_c )          { burn_cycles(1); m.psw &= ~C_FLAG; }
OPHANDLER( clr_f0 )         { burn_cycles(1); m.psw &= ~F_FLAG; }
OPHANDLER( clr_f1 )         { burn_cycles(1); m.f1 = false; }

OPHANDLER( cpl_a )          { burn_cycles(1); m.acc ^= 0xff; }
OPHANDLER( cpl_c )          { burn_cycles(1); m.psw ^= C_FLAG; }
OPHANDLER( cpl_f0 )         { burn_cycles(1); m.psw ^= F_FLAG; }
OPHANDLER( cpl_f1 )         { burn_cycles(1); m.f1 = !m.f1; }

OPHANDLER( da_a ) {
    burn_cycles(1);

    if ((m.acc & 0x0f) > 0x09 || (m.psw & A_FLAG)) {
        if (m.acc > 0xf9)
            m.psw |= C_FLAG;
        m.acc += 0x06;
    }
    if ((m.acc & 0xf0) > 0x90 || (m.psw & C_FLAG)) {
        m.acc += 0x60;
        m.psw |= C_FLAG;
    }
}

OPHANDLER( dec_a )          { burn_cycles(1); m.acc--; }
OPHANDLER( dec_r0 )         { burn_cycles(1); R0--; }
OPHANDLER( dec_r1 )         { burn_cycles(1); R1--; }
OPHANDLER( dec_r2 )         { burn_cycles(1); R2--; }
OPHANDLER( dec_r3 )         { burn_cycles(1); R3--; }
OPHANDLER( dec_r4 )         { burn_cycles(1); R4--; }
OPHANDLER( dec_r5 )         { burn_cycles(1); R5--; }
OPHANDLER( dec_r6 )         { burn_cycles(1); R6--; }
OPHANDLER( dec_r7 )         { burn_cycles(1); R7--; }

OPHANDLER( dis_i )          { burn_cycles(1); m.xirq_enabled = false; }
OPHANDLER( dis_tcnti )      { burn_cycles(1); m.tirq_enabled = false; m.timer_overflow = false; }

OPHANDLER( djnz_r0 )        { burn_cycles(2); execute_jcc(--R0 != 0); }
OPHANDLER( djnz_r1 )        { burn_cycles(2); execute_jcc(--R1 != 0); }
OPHANDLER( djnz_r2 )        { burn_cycles(2); execute_jcc(--R2 != 0); }
OPHANDLER( djnz_r3 )        { burn_cycles(2); execute_jcc(--R3 != 0); }
OPHANDLER( djnz_r4 )        { burn_cycles(2); execute_jcc(--R4 != 0); }
OPHANDLER( djnz_r5 )        { burn_cycles(2); execute_jcc(--R5 != 0); }
OPHANDLER( djnz_r6 )        { burn_cycles(2); execute_jcc(--R6 != 0); }
OPHANDLER( djnz_r7 )        { burn_cycles(2); execute_jcc(--R7 != 0); }

OPHANDLER( en_i )           { burn_cycles(1); m.xirq_enabled = true; }
OPHANDLER( en_tcnti )       { burn_cycles(1); m.tirq_enabled = true; }

OPHANDLER( inc_a )          { burn_cycles(1); m.acc++; }
OPHANDLER( inc_r0 )         { burn_cycles(1); R0++; }
OPHANDLER( inc_r1 )         { burn_cycles(1); R1++; }
OPHANDLER( inc_r2 )         { burn_cycles(1); R2++; }
OPHANDLER( inc_r3 )         { burn_cycles(1); R3++; }
OPHANDLER( inc_r4 )         { burn_cycles(1); R4++; }
OPHANDLER( inc_r5 )         { burn_cycles(1); R5++; }
OPHANDLER( inc_r6 )         { burn_cycles(1); R6++; }
OPHANDLER( inc_r7 )         { burn_cycles(1); R7++; }
OPHANDLER( inc_xr0 )        { burn_cycles(1); ram_write(R0, ram_read(R0) + 1); }
OPHANDLER( inc_xr1 )        { burn_cycles(1); ram_write(R1, ram_read(R1) + 1); }

OPHANDLER( jb_0 )           { burn_cycles(2); execute_jcc((m.acc & 0x01) != 0); }
OPHANDLER( jb_1 )           { burn_cycles(2); execute_jcc((m.acc & 0x02) != 0); }
OPHANDLER( jb_2 )           { burn_cycles(2); execute_jcc((m.acc & 0x04) != 0); }
OPHANDLER( jb_3 )           { burn_cycles(2); execute_jcc((m.acc & 0x08) != 0); }
OPHANDLER( jb_4 )           { burn_cycles(2); execute_jcc((m.acc & 0x10) != 0); }
OPHANDLER( jb_5 )           { burn_cycles(2); execute_jcc((m.acc & 0x20) != 0); }
OPHANDLER( jb_6 )           { burn_cycles(2); execute_jcc((m.acc & 0x40) != 0); }
OPHANDLER( jb_7 )           { burn_cycles(2); execute_jcc((m.acc & 0x80) != 0); }
OPHANDLER( jc )             { burn_cycles(2); execute_jcc((m.psw & C_FLAG) != 0); }
OPHANDLER( jf0 )            { burn_cycles(2); execute_jcc((m.psw & F_FLAG) != 0); }
OPHANDLER( jf1 )            { burn_cycles(2); execute_jcc(m.f1); }
OPHANDLER( jnc )            { burn_cycles(2); execute_jcc((m.psw & C_FLAG) == 0); }
OPHANDLER( jni )            { burn_cycles(2); m.irq_polled = (m.irq_state == 0); execute_jcc(m.irq_state != 0); }
OPHANDLER( jnt_0 )          { burn_cycles(2); execute_jcc(t0_read() == 0); }
OPHANDLER( jnt_1 )          { burn_cycles(2); execute_jcc(t1_read() == 0); }
OPHANDLER( jnz )            { burn_cycles(2); execute_jcc(m.acc != 0); }
OPHANDLER( jtf )            { burn_cycles(2); execute_jcc(m.timer_flag); m.timer_flag = false; }
OPHANDLER( jt_0 )           { burn_cycles(2); execute_jcc(t0_read() != 0); }
OPHANDLER( jt_1 )           { burn_cycles(2); execute_jcc(t1_read() != 0); }
OPHANDLER( jz )             { burn_cycles(2); execute_jcc(m.acc == 0); }

OPHANDLER( jmp_0 )          { burn_cycles(2); execute_jmp(argument_fetch() | 0x000); }
OPHANDLER( jmp_1 )          { burn_cycles(2); execute_jmp(argument_fetch() | 0x100); }
OPHANDLER( jmp_2 )          { burn_cycles(2); execute_jmp(argument_fetch() | 0x200); }
OPHANDLER( jmp_3 )          { burn_cycles(2); execute_jmp(argument_fetch() | 0x300); }
OPHANDLER( jmp_4 )          { burn_cycles(2); execute_jmp(argument_fetch() | 0x400); }
OPHANDLER( jmp_5 )          { burn_cycles(2); execute_jmp(argument_fetch() | 0x500); }
OPHANDLER( jmp_6 )          { burn_cycles(2); execute_jmp(argument_fetch() | 0x600); }
OPHANDLER( jmp_7 )          { burn_cycles(2); execute_jmp(argument_fetch() | 0x700); }
OPHANDLER( jmpp_xa )        { burn_cycles(2); m.pc &= 0xf00; m.pc |= rom_read(m.pc | m.acc); }

OPHANDLER( mov_a_n )        { burn_cycles(2); m.acc = argument_fetch(); }
OPHANDLER( mov_a_psw )      { burn_cycles(1); m.acc = m.psw | 0x08; }
OPHANDLER( mov_a_r0 )       { burn_cycles(1); m.acc = R0; }
OPHANDLER( mov_a_r1 )       { burn_cycles(1); m.acc = R1; }
OPHANDLER( mov_a_r2 )       { burn_cycles(1); m.acc = R2; }
OPHANDLER( mov_a_r3 )       { burn_cycles(1); m.acc = R3; }
OPHANDLER( mov_a_r4 )       { burn_cycles(1); m.acc = R4; }
OPHANDLER( mov_a_r5 )       { burn_cycles(1); m.acc = R5; }
OPHANDLER( mov_a_r6 )       { burn_cycles(1); m.acc = R6; }
OPHANDLER( mov_a_r7 )       { burn_cycles(1); m.acc = R7; }
OPHANDLER( mov_a_xr0 )      { burn_cycles(1); m.acc = ram_read(R0); }
OPHANDLER( mov_a_xr1 )      { burn_cycles(1); m.acc = ram_read(R1); }
OPHANDLER( mov_a_t )        { burn_cycles(1); m.acc = m.timer_counter; }

OPHANDLER( mov_psw_a )      { burn_cycles(1); m.psw = m.acc & ~0x08; update_reg_ptr(); }
OPHANDLER( mov_r0_a )       { burn_cycles(1); R0 = m.acc; }
OPHANDLER( mov_r1_a )       { burn_cycles(1); R1 = m.acc; }
OPHANDLER( mov_r2_a )       { burn_cycles(1); R2 = m.acc; }
OPHANDLER( mov_r3_a )       { burn_cycles(1); R3 = m.acc; }
OPHANDLER( mov_r4_a )       { burn_cycles(1); R4 = m.acc; }
OPHANDLER( mov_r5_a )       { burn_cycles(1); R5 = m.acc; }
OPHANDLER( mov_r6_a )       { burn_cycles(1); R6 = m.acc; }
OPHANDLER( mov_r7_a )       { burn_cycles(1); R7 = m.acc; }
OPHANDLER( mov_r0_n )       { burn_cycles(2); R0 = argument_fetch(); }
OPHANDLER( mov_r1_n )       { burn_cycles(2); R1 = argument_fetch(); }
OPHANDLER( mov_r2_n )       { burn_cycles(2); R2 = argument_fetch(); }
OPHANDLER( mov_r3_n )       { burn_cycles(2); R3 = argument_fetch(); }
OPHANDLER( mov_r4_n )       { burn_cycles(2); R4 = argument_fetch(); }
OPHANDLER( mov_r5_n )       { burn_cycles(2); R5 = argument_fetch(); }
OPHANDLER( mov_r6_n )       { burn_cycles(2); R6 = argument_fetch(); }
OPHANDLER( mov_r7_n )       { burn_cycles(2); R7 = argument_fetch(); }
OPHANDLER( mov_t_a )        { burn_cycles(1); m.timer_counter = m.acc; }
OPHANDLER( mov_xr0_a )      { burn_cycles(1); ram_write(R0, m.acc); }
OPHANDLER( mov_xr1_a )      { burn_cycles(1); ram_write(R1, m.acc); }
OPHANDLER( mov_xr0_n )      { burn_cycles(2); ram_write(R0, argument_fetch()); }
OPHANDLER( mov_xr1_n )      { burn_cycles(2); ram_write(R1, argument_fetch()); }

OPHANDLER( movp_a_xa )      { burn_cycles(2); m.acc = rom_read((m.pc & 0xf00) | m.acc); }
OPHANDLER( movp3_a_xa )     { burn_cycles(2); m.acc = rom_read(0x300 | m.acc); }

OPHANDLER( movx_a_xr0 )     { burn_cycles(2); m.acc = ext_mem_read(R0); }
OPHANDLER( movx_a_xr1 )     { burn_cycles(2); m.acc = ext_mem_read(R1); }
OPHANDLER( movx_xr0_a )     { burn_cycles(2); ext_mem_write(R0, m.acc); }
OPHANDLER( movx_xr1_a )     { burn_cycles(2); ext_mem_write(R1, m.acc); }

OPHANDLER( nop )            { burn_cycles(1); }

OPHANDLER( orl_a_r0 )       { burn_cycles(1); m.acc |= R0; }
OPHANDLER( orl_a_r1 )       { burn_cycles(1); m.acc |= R1; }
OPHANDLER( orl_a_r2 )       { burn_cycles(1); m.acc |= R2; }
OPHANDLER( orl_a_r3 )       { burn_cycles(1); m.acc |= R3; }
OPHANDLER( orl_a_r4 )       { burn_cycles(1); m.acc |= R4; }
OPHANDLER( orl_a_r5 )       { burn_cycles(1); m.acc |= R5; }
OPHANDLER( orl_a_r6 )       { burn_cycles(1); m.acc |= R6; }
OPHANDLER( orl_a_r7 )       { burn_cycles(1); m.acc |= R7; }
OPHANDLER( orl_a_xr0 )      { burn_cycles(1); m.acc |= ram_read(R0); }
OPHANDLER( orl_a_xr1 )      { burn_cycles(1); m.acc |= ram_read(R1); }
OPHANDLER( orl_a_n )        { burn_cycles(2); m.acc |= argument_fetch(); }

OPHANDLER( orl_p1_n )       { burn_cycles(2); port1_write(m.p1 | argument_fetch()); }
OPHANDLER( orl_p2_n )       { burn_cycles(2); port2_write((m.p2 | argument_fetch()) & P2_MASK); }

OPHANDLER( ret )            { burn_cycles(2); pull_pc(); }
OPHANDLER( retr ) {
    burn_cycles(2);

    // implicitly clear the IRQ in progress flip flop
    m.irq_in_progress = false;
    pull_pc_psw();
}

OPHANDLER( rl_a )           { burn_cycles(1); m.acc = (m.acc << 1) | (m.acc >> 7); }
OPHANDLER( rlc_a )          { burn_cycles(1); u8 newc = m.acc & C_FLAG; m.acc = (m.acc << 1) | (m.psw >> 7); m.psw = (m.psw & ~C_FLAG) | newc; }

OPHANDLER( rr_a )           { burn_cycles(1); m.acc = (m.acc >> 1) | (m.acc << 7); }
OPHANDLER( rrc_a )          { burn_cycles(1); u8 newc = (m.acc << 7) & C_FLAG; m.acc = (m.acc >> 1) | (m.psw & C_FLAG); m.psw = (m.psw & ~C_FLAG) | newc; }

OPHANDLER( sel_mb0 )        { burn_cycles(1); m.a11 = 0x000; }
OPHANDLER( sel_mb1 )        { burn_cycles(1); m.a11 = 0x800; }

OPHANDLER( sel_rb0 )        { burn_cycles(1); m.psw &= ~B_FLAG; update_reg_ptr(); }
OPHANDLER( sel_rb1 )        { burn_cycles(1); m.psw |=  B_FLAG; update_reg_ptr(); }

OPHANDLER( stop_tcnt )      { burn_cycles(1); m.timecount_enabled = 0; }
OPHANDLER( strt_t )         { burn_cycles(1); m.timecount_enabled = TIMER_ENABLED; m.prescaler = 0; }
OPHANDLER( strt_cnt ) {
    burn_cycles(1);
    if (!(m.timecount_enabled & COUNTER_ENABLED))
        m.t1_history = t1_read();

    m.timecount_enabled = COUNTER_ENABLED;
}

OPHANDLER( swap_a )         { burn_cycles(1); m.acc = (m.acc << 4) | (m.acc >> 4); }

OPHANDLER( xch_a_r0 )       { burn_cycles(1); u8 tmp = m.acc; m.acc = R0; R0 = tmp; }
OPHANDLER( xch_a_r1 )       { burn_cycles(1); u8 tmp = m.acc; m.acc = R1; R1 = tmp; }
OPHANDLER( xch_a_r2 )       { burn_cycles(1); u8 tmp = m.acc; m.acc = R2; R2 = tmp; }
OPHANDLER( xch_a_r3 )       { burn_cycles(1); u8 tmp = m.acc; m.acc = R3; R3 = tmp; }
OPHANDLER( xch_a_r4 )       { burn_cycles(1); u8 tmp = m.acc; m.acc = R4; R4 = tmp; }
OPHANDLER( xch_a_r5 )       { burn_cycles(1); u8 tmp = m.acc; m.acc = R5; R5 = tmp; }
OPHANDLER( xch_a_r6 )       { burn_cycles(1); u8 tmp = m.acc; m.acc = R6; R6 = tmp; }
OPHANDLER( xch_a_r7 )       { burn_cycles(1); u8 tmp = m.acc; m.acc = R7; R7 = tmp; }
OPHANDLER( xch_a_xr0 )      { burn_cycles(1); u8 tmp = m.acc; m.acc = ram_read(R0); ram_write(R0, tmp); }
OPHANDLER( xch_a_xr1 )      { burn_cycles(1); u8 tmp = m.acc; m.acc = ram_read(R1); ram_write(R1, tmp); }

OPHANDLER( xchd_a_xr0 )     { burn_cycles(1); u8 oldram = ram_read(R0); ram_write(R0, (oldram & 0xf0) | (m.acc & 0x0f)); m.acc = (m.acc & 0xf0) | (oldram & 0x0f); }
OPHANDLER( xchd_a_xr1 )     { burn_cycles(1); u8 oldram = ram_read(R1); ram_write(R1, (oldram & 0xf0) | (m.acc & 0x0f)); m.acc = (m.acc & 0xf0) | (oldram & 0x0f); }

OPHANDLER( xrl_a_r0 )       { burn_cycles(1); m.acc ^= R0; }
OPHANDLER( xrl_a_r1 )       { burn_cycles(1); m.acc ^= R1; }
OPHANDLER( xrl_a_r2 )       { burn_cycles(1); m.acc ^= R2; }
OPHANDLER( xrl_a_r3 )       { burn_cycles(1); m.acc ^= R3; }
OPHANDLER( xrl_a_r4 )       { burn_cycles(1); m.acc ^= R4; }
OPHANDLER( xrl_a_r5 )       { burn_cycles(1); m.acc ^= R5; }
OPHANDLER( xrl_a_r6 )       { burn_cycles(1); m.acc ^= R6; }
OPHANDLER( xrl_a_r7 )       { burn_cycles(1); m.acc ^= R7; }
OPHANDLER( xrl_a_xr0 )      { burn_cycles(1); m.acc ^= ram_read(R0); }
OPHANDLER( xrl_a_xr1 )      { burn_cycles(1); m.acc ^= ram_read(R1); }
OPHANDLER( xrl_a_n )        { burn_cycles(2); m.acc ^= argument_fetch(); }


#define OP(_a) &_a

typedef void (*mcs48_ophandler)(void);

static const mcs48_ophandler s_mcs48_opcodes[256] = {
    OP(nop),        OP(illegal),    OP(illegal),   OP(add_a_n),   OP(jmp_0),     OP(en_i),       OP(illegal),   OP(dec_a),      // 00
    OP(illegal),    OP(illegal),    OP(illegal),   OP(illegal),   OP(illegal),   OP(illegal),    OP(illegal),   OP(illegal),
    OP(inc_xr0),    OP(inc_xr1),    OP(jb_0),      OP(adc_a_n),   OP(call_0),    OP(dis_i),      OP(jtf),       OP(inc_a),      // 10
    OP(inc_r0),     OP(inc_r1),     OP(inc_r2),    OP(inc_r3),    OP(inc_r4),    OP(inc_r5),     OP(inc_r6),    OP(inc_r7),
    OP(xch_a_xr0),  OP(xch_a_xr1),  OP(illegal),   OP(mov_a_n),   OP(jmp_1),     OP(en_tcnti),   OP(jnt_0),     OP(clr_a),      // 20
    OP(xch_a_r0),   OP(xch_a_r1),   OP(xch_a_r2),  OP(xch_a_r3),  OP(xch_a_r4),  OP(xch_a_r5),   OP(xch_a_r6),  OP(xch_a_r7),
    OP(xchd_a_xr0), OP(xchd_a_xr1), OP(jb_1),      OP(illegal),   OP(call_1),    OP(dis_tcnti),  OP(jt_0),      OP(cpl_a),      // 30
    OP(illegal),    OP(illegal),    OP(illegal),   OP(illegal),   OP(illegal),   OP(illegal),    OP(illegal),   OP(illegal),
    OP(orl_a_xr0),  OP(orl_a_xr1),  OP(mov_a_t),   OP(orl_a_n),   OP(jmp_2),     OP(strt_cnt),   OP(jnt_1),     OP(swap_a),     // 40
    OP(orl_a_r0),   OP(orl_a_r1),   OP(orl_a_r2),  OP(orl_a_r3),  OP(orl_a_r4),  OP(orl_a_r5),   OP(orl_a_r6),  OP(orl_a_r7),
    OP(anl_a_xr0),  OP(anl_a_xr1),  OP(jb_2),      OP(anl_a_n),   OP(call_2),    OP(strt_t),     OP(jt_1),      OP(da_a),       // 50
    OP(anl_a_r0),   OP(anl_a_r1),   OP(anl_a_r2),  OP(anl_a_r3),  OP(anl_a_r4),  OP(anl_a_r5),   OP(anl_a_r6),  OP(anl_a_r7),
    OP(add_a_xr0),  OP(add_a_xr1),  OP(mov_t_a),   OP(illegal),   OP(jmp_3),     OP(stop_tcnt),  OP(illegal),   OP(rrc_a),      // 60
    OP(add_a_r0),   OP(add_a_r1),   OP(add_a_r2),  OP(add_a_r3),  OP(add_a_r4),  OP(add_a_r5),   OP(add_a_r6),  OP(add_a_r7),
    OP(adc_a_xr0),  OP(adc_a_xr1),  OP(jb_3),      OP(illegal),   OP(call_3),    OP(illegal),    OP(jf1),       OP(rr_a),       // 70
    OP(adc_a_r0),   OP(adc_a_r1),   OP(adc_a_r2),  OP(adc_a_r3),  OP(adc_a_r4),  OP(adc_a_r5),   OP(adc_a_r6),  OP(adc_a_r7),
    OP(movx_a_xr0), OP(movx_a_xr1), OP(illegal),   OP(ret),       OP(jmp_4),     OP(clr_f0),     OP(jni),       OP(illegal),    // 80
    OP(illegal),    OP(orl_p1_n),   OP(orl_p2_n),  OP(illegal),   OP(illegal),   OP(illegal),    OP(illegal),   OP(illegal),
    OP(movx_xr0_a), OP(movx_xr1_a), OP(jb_4),      OP(retr),      OP(call_4),    OP(cpl_f0),     OP(jnz),       OP(clr_c),      // 90
    OP(illegal),    OP(anl_p1_n),   OP(anl_p2_n),  OP(illegal),   OP(illegal),   OP(illegal),    OP(illegal),   OP(illegal),
    OP(mov_xr0_a),  OP(mov_xr1_a),  OP(illegal),   OP(movp_a_xa), OP(jmp_5),     OP(clr_f1),     OP(illegal),   OP(cpl_c),      // A0
    OP(mov_r0_a),   OP(mov_r1_a),   OP(mov_r2_a),  OP(mov_r3_a),  OP(mov_r4_a),  OP(mov_r5_a),   OP(mov_r6_a),  OP(mov_r7_a),
    OP(mov_xr0_n),  OP(mov_xr1_n),  OP(jb_5),      OP(jmpp_xa),   OP(call_5),    OP(cpl_f1),     OP(jf0),       OP(illegal),    // B0
    OP(mov_r0_n),   OP(mov_r1_n),   OP(mov_r2_n),  OP(mov_r3_n),  OP(mov_r4_n),  OP(mov_r5_n),   OP(mov_r6_n),  OP(mov_r7_n),
    OP(illegal),    OP(illegal),    OP(illegal),   OP(illegal),   OP(jmp_6),     OP(sel_rb0),    OP(jz),        OP(mov_a_psw),  // C0
    OP(dec_r0),     OP(dec_r1),     OP(dec_r2),    OP(dec_r3),    OP(dec_r4),    OP(dec_r5),     OP(dec_r6),    OP(dec_r7),
    OP(xrl_a_xr0),  OP(xrl_a_xr1),  OP(jb_6),      OP(xrl_a_n),   OP(call_6),    OP(sel_rb1),    OP(illegal),   OP(mov_psw_a),  // D0
    OP(xrl_a_r0),   OP(xrl_a_r1),   OP(xrl_a_r2),  OP(xrl_a_r3),  OP(xrl_a_r4),  OP(xrl_a_r5),   OP(xrl_a_r6),  OP(xrl_a_r7),
    OP(illegal),    OP(illegal),    OP(illegal),   OP(movp3_a_xa),OP(jmp_7),     OP(sel_mb0),    OP(jnc),       OP(rl_a),       // E0
    OP(djnz_r0),    OP(djnz_r1),    OP(djnz_r2),   OP(djnz_r3),   OP(djnz_r4),   OP(djnz_r5),    OP(djnz_r6),   OP(djnz_r7),
    OP(mov_a_xr0),  OP(mov_a_xr1),  OP(jb_7),      OP(illegal),   OP(call_7),    OP(sel_mb1),    OP(jc),        OP(rlc_a),      // F0
    OP(mov_a_r0),   OP(mov_a_r1),   OP(mov_a_r2),  OP(mov_a_r3),  OP(mov_a_r4),  OP(mov_a_r5),   OP(mov_a_r6),  OP(mov_a_r7)
};


// *****************************************************************************
// Public functions
// *****************************************************************************

cpu_t *cpu_get(void) {
    return &m;
}

// void cpu_power_on() {
//     m.prev_pc = 0;
//     m.pc = 0;
// 
//     m.acc = 0;
//     m.psw = 0;
//     m.f1 = false;
//     m.a11 = 0;
//     m.p1 = 0;
//     m.p2 = 0;
//     m.timer = 0;
//     m.prescaler = 0;
//     m.t1_history = 0;
// 
//     m.irq_state = false;
//     m.irq_polled = false;
//     m.irq_in_progress = false;
//     m.timer_overflow = false;
//     m.timer_flag = false;
//     m.tirq_enabled = false;
//     m.xirq_enabled = false;
//     m.timecount_enabled = 0;
// 
//     // ensure that reg_ptr is valid before get_info gets called
//     update_reg_ptr();
// }

void cpu_reset(void) {
    // confirmed from reset description
    m.pc = 0;
    m.psw = m.psw & (C_FLAG | A_FLAG);
    update_reg_ptr();
    m.f1 = false;
    m.a11 = 0;

    m.tirq_enabled = false;
    m.xirq_enabled = false;
    m.timecount_enabled = 0;
    m.timer_flag = false;

    // confirmed from interrupt logic description
    m.irq_in_progress = false;
    m.timer_overflow = false;

    m.irq_polled = false;

    // port 1 and port 2 are set to input mode
    port1_write(0xff);
    port2_write(0xff);
}

void cpu_execute(int num_cycles) {
    m.icount += num_cycles;
    update_reg_ptr();

    // iterate over remaining cycles, guaranteeing at least one instruction
    do {
        // check interrupts
        check_irqs();
        m.irq_polled = false;

        m.prev_pc = m.pc;

        // fetch and process opcode
        unsigned opcode = opcode_fetch();
        (*s_mcs48_opcodes[opcode])();
    } while (m.icount > 0);
}

#define DRAW_TEXT(x, y, msg, ...) \
    DrawTextLeft(g_defaultFont, g_colourBlack, g_window->bmp, x, y, msg, ##__VA_ARGS__)

void cpu_draw_state(int _x, int _y) {
    cpu_t *cpu = cpu_get();
    int x = _x + g_defaultFont->maxCharWidth;
    int y = _y + g_defaultFont->charHeight;
    DRAW_TEXT(x, y, "KLR Microcontroller state");
    DRAW_TEXT(x + 1, y, "KLR Microcontroller state");
    x += g_defaultFont->maxCharWidth;
    y += g_defaultFont->charHeight * 1.2;
    x += DRAW_TEXT(x, y, "PC:%03x  ", cpu->pc);
    x += DRAW_TEXT(x, y, "MasterClk:%d  ", cpu->master_clk);
    x += DRAW_TEXT(x, y, "T:%d  ", cpu->timer_counter);
    x += DRAW_TEXT(x, y, "MemBank:%d  ", !!cpu->a11);

    x = g_defaultFont->maxCharWidth * 2;
    y += g_defaultFont->charHeight * 2;
    x += DRAW_TEXT(x, y, "RAM:    ");
    for (unsigned a = 0; a < 16; a++) {
        x += DRAW_TEXT(x, y, " %x ", a);
    }
    for (unsigned a = 0; a < 128; a++) {
        if ((a & 0xf) == 0) {
            x = g_defaultFont->maxCharWidth * 2;
            y += g_defaultFont->charHeight;
            x += DRAW_TEXT(x, y, "     %x0 ", a >> 4);
        }
        x += DRAW_TEXT(x, y, "%02x ", cpu->ram[a]);
    }

    y += g_defaultFont->charHeight * 1.7;
    HLine(g_window->bmp, 0, y, g_window->bmp->width, g_colourBlack);
}
