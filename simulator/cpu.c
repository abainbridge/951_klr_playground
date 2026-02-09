// MCS-80 Instruction Set Simulator, based on the CPU simulator from:
//
//   O2EM Free Odyssey2 / Videopac+ Emulator
//   Created by Daniel Boris <dboris@comcast.net>  (c) 1997, 1998
//   Developed by Andre de la Rocha   <adlroc@users.sourceforge.net>
//                Arlindo M. de Oliveira <dgtec@users.sourceforge.net>

// Own header
#include "cpu.h"

// Deadfrog headers
#include "df_font.h"
#include "df_window.h"

// Standard headers
#include <stdio.h>

void push(Byte d) {
    ram[sp++] = d;
    if (sp > 23) {
        sp = 8;
    }
}

#define pull() (sp--, (sp < 8) ? (sp = 23) : 0, ram[sp])

void make_psw(void) {
    psw = (carry << 7) | ac | f0 | reg_bank | 0x08;
    psw |= (sp - 8) >> 1;
}

#define illegal(o)                                                             \
    {                                                                          \
    }
#define undef(i)                                                               \
    {                                                                          \
        printf("** unimplemented instruction %x, %x**\n", i, pc);              \
    }
#define ROM(adr) (rom[(adr) & 0xfff])


Byte acc;   // Accumulator
ADDRESS pc; // Program counter
long clk;   // Number of cycles taken by the current instruction.

Byte timer_counter; // timer/event-counter register
Byte reg_pnt;  // pointer to register bank. Is an index into RAM. 0=reg bank 0, 24=reg bank 1
Byte timer_on; // 0=timer off/1=timer on
Byte count_on; // 0=count off/1=count on
Byte psw;      // Processor status word
Byte sp;       // Stack pointer (part of psw)

Byte p1;        // I/O port 1
Byte p2;        // I/O port 2
Byte xirq_pend; // external IRQ pending
Byte tirq_pend; // timer IRQ pending
Byte t_flag;    // Timer flag

ADDRESS A11; // PC bit 11
ADDRESS A11ff;
Byte reg_bank;// Register Bank (part of psw)
Byte f0;      // Flag Bit (part of psw)
Byte f1;      // Flag Bit 1
Byte ac;      // Aux Carry (part of psw)
Byte carry;   // Carry flag (part of psw)
Byte xirq_en; // external IRQ's enabled
Byte tirq_en; // Timer IRQ enabled
Byte irq_ex;  // IRQ executing

int timer_cycle_accumulator;
int int_clk;    // counter for length of /INT pulses for JNI
int master_clk;

Byte ram[128];
Byte rom[4096];


void cpu_reset(void) {
    pc = 0;
    sp = 8;
    reg_bank = 0;
    write_p1(0xff);
    write_p2(0xff);
    ac = carry = f0 = 0;
    A11 = A11ff = 0;
    timer_on = 0;
    count_on = 0;
    reg_pnt = 0;
    tirq_en = xirq_en = irq_ex = xirq_pend = tirq_pend = 0;
}

void ext_IRQ(void) {
    int_clk = 5; // length of pulse on /INT
    if (xirq_en && !irq_ex) {
        irq_ex = 1;
        xirq_pend = 0;
        clk += 2;
        make_psw();
        push(pc & 0xFF);
        push(((pc & 0xF00) >> 8) | (psw & 0xF0));
        pc = 0x03;
        A11ff = A11;
        A11 = 0;
    }
}

void tim_IRQ(void) {
    if (tirq_en && !irq_ex) {
        irq_ex = 2;
        tirq_pend = 0;
        clk += 2;
        make_psw();
        push(pc & 0xFF);
        push(((pc & 0xF00) >> 8) | (psw & 0xF0));
        pc = 0x07;
        A11ff = A11;
        A11 = 0;
    }
}

#define DRAW_TEXT(x, y, msg, ...) \
    DrawTextLeft(g_defaultFont, g_colourBlack, g_window->bmp, x, y, msg, ##__VA_ARGS__)

void cpu_draw_state(int _x, int _y) {
    int x = _x + g_defaultFont->maxCharWidth;
    int y = _y + g_defaultFont->charHeight;
    DRAW_TEXT(x, y, "KLR Microcontroller state");
    DRAW_TEXT(x+1, y, "KLR Microcontroller state");
    x += g_defaultFont->maxCharWidth;
    y += g_defaultFont->charHeight * 1.2;
    x += DRAW_TEXT(x, y, "PC:%03x  ", pc);
    x += DRAW_TEXT(x, y, "MasterClk:%d  ", master_clk);
    x += DRAW_TEXT(x, y, "T:%d  ", timer_counter);
    x += DRAW_TEXT(x, y, "MemBank:%d  ", !!A11); // TODO: figure out what to do with A11ff

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
        x += DRAW_TEXT(x, y, "%02x ", ram[a]);
    }

    y += g_defaultFont->charHeight * 1.7;
    HLine(g_window->bmp, 0, y, g_window->bmp->width, g_colourBlack);
}

void cpu_exec(unsigned num_cycles) {
    Byte op;
    ADDRESS adr;
    Byte dat;
    int temp;

    int target_master_clk = master_clk + num_cycles;
    while(master_clk < target_master_clk) {
        clk = 0;
        if (pc == 0x489)
            pc = pc;
        op = ROM(pc++);

        switch (op) {
        case 0x00: // NOP
            clk++;
            break;
        case 0x01: // ILL
            illegal(op);
            clk++;
            break;
        case 0x02: // OUTL BUS,A
            clk += 2;
            undef(0x02);
            break;
        case 0x03: // ADD A,#data
            clk += 2;
            carry = ac = 0;
            dat = ROM(pc++);
            if (((acc & 0x0f) + (dat & 0x0f)) > 0x0f)
                ac = 0x40;
            temp = acc + dat;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x04: // JMP
            pc = ROM(pc) | A11;
            clk += 2;
            break;
        case 0x05: // EN I
            xirq_en = 1;
            clk++;
            break;
        case 0x06: // ILL
            clk++;
            illegal(op);
            break;
        case 0x07: // DEC A
            acc--;
            clk++;
            break;

        // These opcodes do not occur in 951 code
        case 0x08: // IN A,BUS
        case 0x09: // IN A,Pp
        case 0x0A: // IN A,Pp
        case 0x0B: // ILL
        case 0x0C: // MOVD A,P4
        case 0x0D: // MOVD A,P5
        case 0x0E: // MOVD A,P6
        case 0x0F: // MOVD A,P7
            clk++;
            illegal(op);
            break;

        case 0x10: // INC @Ri
            ram[ram[reg_pnt] & 0x7F]++;
            clk++;
            break;
        case 0x11: // INC @Ri
            ram[ram[reg_pnt + 1] & 0x7F]++;
            clk++;
            break;
        case 0x12: // JBb address
            clk += 2;
            dat = ROM(pc);
            if (acc & 0x01)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0x13: // ADDC A,#data
            clk += 2;
            dat = ROM(pc++);
            ac = 0;
            if (((acc & 0x0f) + (dat & 0x0f) + carry) > 0x0f)
                ac = 0x40;
            temp = acc + dat + carry;
            carry = 0;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;

        case 0x14: // CALL
            make_psw();
            adr = ROM(pc) | A11;
            pc++;
            clk += 2;
            push(pc & 0xFF);
            push(((pc & 0xF00) >> 8) | (psw & 0xF0));
            pc = adr;
            break;
        case 0x15: // DIS I
            xirq_en = 0;
            clk++;
            break;
        case 0x16: // JTF
            clk += 2;
            dat = ROM(pc);
            if (t_flag)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            t_flag = 0;
            break;
        case 0x17: // INC A
            acc++;
            clk++;
            break;
        case 0x18: // INC Rr
            ram[reg_pnt]++;
            clk++;
            break;
        case 0x19: // INC Rr
            ram[reg_pnt + 1]++;
            clk++;
            break;
        case 0x1A: // INC Rr
            ram[reg_pnt + 2]++;
            clk++;
            break;
        case 0x1B: // INC Rr
            ram[reg_pnt + 3]++;
            clk++;
            break;
        case 0x1C: // INC Rr
            ram[reg_pnt + 4]++;
            clk++;
            break;
        case 0x1D: // INC Rr
            ram[reg_pnt + 5]++;
            clk++;
            break;
        case 0x1E: // INC Rr
            ram[reg_pnt + 6]++;
            clk++;
            break;
        case 0x1F: // INC Rr
            ram[reg_pnt + 7]++;
            clk++;
            break;
        case 0x20: // XCH A,@Ri
            clk++;
            dat = acc;
            acc = ram[ram[reg_pnt] & 0x7F];
            ram[ram[reg_pnt] & 0x7F] = dat;
            break;
        case 0x21: // XCH A,@Ri
            clk++;
            dat = acc;
            acc = ram[ram[reg_pnt + 1] & 0x7F];
            ram[ram[reg_pnt + 1] & 0x7F] = dat;
            break;
        case 0x22: // ILL
            clk++;
            illegal(op);
            break;
        case 0x23: // MOV a,#data
            clk += 2;
            acc = ROM(pc++);
            break;

        case 0x24: // JMP
            pc = ROM(pc) | 0x100 | A11;
            clk += 2;
            break;
        case 0x25: // EN TCNTI
            tirq_en = 1;
            clk++;
            break;
        case 0x26: // JNT0
            illegal(op);
            break;
//             clk += 2;
//             dat = ROM(pc);
//             if (!get_voice_status())
//                 pc = (pc & 0xF00) | dat;
//             else
//                 pc++;
            break;
        case 0x27: // CLR A
            clk++;
            acc = 0;
            break;
        case 0x28: // XCH A,Rr
            dat = acc;
            acc = ram[reg_pnt];
            ram[reg_pnt] = dat;
            clk++;
            break;
        case 0x29: // XCH A,Rr
            dat = acc;
            acc = ram[reg_pnt + 1];
            ram[reg_pnt + 1] = dat;
            clk++;
            break;
        case 0x2A: // XCH A,Rr
            dat = acc;
            acc = ram[reg_pnt + 2];
            ram[reg_pnt + 2] = dat;
            clk++;
            break;
        case 0x2B: // XCH A,Rr
            dat = acc;
            acc = ram[reg_pnt + 3];
            ram[reg_pnt + 3] = dat;
            clk++;
            break;
        case 0x2C: // XCH A,Rr
            dat = acc;
            acc = ram[reg_pnt + 4];
            ram[reg_pnt + 4] = dat;
            clk++;
            break;
        case 0x2D: // XCH A,Rr
            dat = acc;
            acc = ram[reg_pnt + 5];
            ram[reg_pnt + 5] = dat;
            clk++;
            break;
        case 0x2E: // XCH A,Rr
            dat = acc;
            acc = ram[reg_pnt + 6];
            ram[reg_pnt + 6] = dat;
            clk++;
            break;
        case 0x2F: // XCH A,Rr
            dat = acc;
            acc = ram[reg_pnt + 7];
            ram[reg_pnt + 7] = dat;
            clk++;
            break;
        case 0x30: // XCHD A,@Ri
            clk++;
            adr = ram[reg_pnt] & 0x7F;
            dat = acc & 0x0F;
            acc = acc & 0xF0;
            acc = acc | (ram[adr] & 0x0F);
            ram[adr] &= 0xF0;
            ram[adr] |= dat;
            break;
        case 0x31: // XCHD A,@Ri
            clk++;
            adr = ram[reg_pnt + 1] & 0x7F;
            dat = acc & 0x0F;
            acc = acc & 0xF0;
            acc = acc | (ram[adr] & 0x0F);
            ram[adr] &= 0xF0;
            ram[adr] |= dat;
            break;
        case 0x32: // JBb address
            clk += 2;
            dat = ROM(pc);
            if (acc & 0x02)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0x33: // ILL
            clk++;
            illegal(op);
            break;
        case 0x34: // CALL
            make_psw();
            adr = ROM(pc) | 0x100 | A11;
            pc++;
            clk += 2;
            push(pc & 0xFF);
            push(((pc & 0xF00) >> 8) | (psw & 0xF0));
            pc = adr;
            break;
        case 0x35: // DIS TCNTI
            tirq_en = 0;
            tirq_pend = 0;
            clk++;
            break;
        case 0x36: // JT0
            clk += 2;
//             dat = ROM(pc);
//             if (get_voice_status())
//                 pc = (pc & 0xF00) | dat;
//             else
//                 pc++;
            illegal(0);
            break;
        case 0x37: // CPL A
            acc = acc ^ 0xFF;
            clk++;
            break;
        case 0x38: // ILL
            clk++;
            illegal(op);
            break;
        case 0x39: // OUTL P1,A
            clk += 2;
            write_p1(acc);
            break;
        case 0x3A: // OUTL P2,A
            clk += 2;
            p2 = acc;
            break;
        case 0x3B: // ILL
            clk++;
            illegal(op);
            break;
        case 0x3C: // MOVD P4,A
            clk += 2;
            write_PB(0, acc);
            break;
        case 0x3D: // MOVD P5,A
            clk += 2;
            write_PB(1, acc);
            break;
        case 0x3E: // MOVD P6,A
            clk += 2;
            write_PB(2, acc);
            break;
        case 0x3F: // MOVD P7,A
            clk += 2;
            write_PB(3, acc);
            break;
        case 0x40: // ORL A,@Ri
            clk++;
            acc = acc | ram[ram[reg_pnt] & 0x7F];
            break;
        case 0x41: // ORL A,@Ri
            clk++;
            acc = acc | ram[ram[reg_pnt + 1] & 0x7F];
            break;
        case 0x42: // MOV A,T
            clk++;
            acc = timer_counter;
            break;
        case 0x43: // ORL A,#data
            clk += 2;
            acc = acc | ROM(pc++);
            break;
        case 0x44: // JMP
            pc = ROM(pc) | 0x200 | A11;
            clk += 2;
            break;
        case 0x45: // STRT CNT
            // printf("START: %d=%d\n",master_clk/22,itimer);
            count_on = 1;
            clk++;
            break;
        case 0x46: // JNT1
            clk += 2;
            dat = ROM(pc);
            if (!read_t1())
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0x47: // SWAP A
            clk++;
            dat = (acc & 0xF0) >> 4;
            acc = acc << 4;
            acc = acc | dat;
            break;
        case 0x48: // ORL A,Rr
            clk++;
            acc = acc | ram[reg_pnt];
            break;
        case 0x49: // ORL A,Rr
            clk++;
            acc = acc | ram[reg_pnt + 1];
            break;
        case 0x4A: // ORL A,Rr
            clk++;
            acc = acc | ram[reg_pnt + 2];
            break;
        case 0x4B: // ORL A,Rr
            clk++;
            acc = acc | ram[reg_pnt + 3];
            break;
        case 0x4C: // ORL A,Rr
            clk++;
            acc = acc | ram[reg_pnt + 4];
            break;
        case 0x4D: // ORL A,Rr
            clk++;
            acc = acc | ram[reg_pnt + 5];
            break;
        case 0x4E: // ORL A,Rr
            clk++;
            acc = acc | ram[reg_pnt + 6];
            break;
        case 0x4F: // ORL A,Rr
            clk++;
            acc = acc | ram[reg_pnt + 7];
            break;

        case 0x50: // ANL A,@Ri
            acc = acc & ram[ram[reg_pnt] & 0x7F];
            clk++;
            break;
        case 0x51: // ANL A,@Ri
            acc = acc & ram[ram[reg_pnt + 1] & 0x7F];
            clk++;
            break;
        case 0x52: // JBb address
            clk += 2;
            dat = ROM(pc);
            if (acc & 0x04)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0x53: // ANL A,#data
            clk += 2;
            acc = acc & ROM(pc++);
            break;
        case 0x54: // CALL
            make_psw();
            adr = ROM(pc) | 0x200 | A11;
            pc++;
            clk += 2;
            push(pc & 0xFF);
            push(((pc & 0xF00) >> 8) | (psw & 0xF0));
            pc = adr;
            break;
        case 0x55: // STRT T
            timer_on = 1;
            timer_cycle_accumulator = 0;
            clk++;
            break;
        case 0x56: // JT1
            clk += 2;
            dat = ROM(pc);
            if (read_t1())
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0x57: // DA A
            clk++;
            if (((acc & 0x0F) > 0x09) || ac) {
                if (acc > 0xf9)
                    carry = 1;
                acc += 6;
            }
            dat = (acc & 0xF0) >> 4;
            if ((dat > 9) || carry) {
                dat += 6;
                carry = 1;
            }
            acc = (acc & 0x0F) | (dat << 4);
            break;
        case 0x58: // ANL A,Rr
            clk++;
            acc = acc & ram[reg_pnt];
            break;
        case 0x59: // ANL A,Rr
            clk++;
            acc = acc & ram[reg_pnt + 1];
            break;
        case 0x5A: // ANL A,Rr
            clk++;
            acc = acc & ram[reg_pnt + 2];
            break;
        case 0x5B: // ANL A,Rr
            clk++;
            acc = acc & ram[reg_pnt + 3];
            break;
        case 0x5C: // ANL A,Rr
            clk++;
            acc = acc & ram[reg_pnt + 4];
            break;
        case 0x5D: // ANL A,Rr
            clk++;
            acc = acc & ram[reg_pnt + 5];
            break;
        case 0x5E: // ANL A,Rr
            clk++;
            acc = acc & ram[reg_pnt + 6];
            break;
        case 0x5F: // ANL A,Rr
            clk++;
            acc = acc & ram[reg_pnt + 7];
            break;

        case 0x60: // ADD A,@Ri
            clk++;
            carry = ac = 0;
            dat = ram[ram[reg_pnt] & 0x7F];
            if (((acc & 0x0f) + (dat & 0x0f)) > 0x0f)
                ac = 0x40;
            temp = acc + dat;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x61: // ADD A,@Ri
            clk++;
            carry = ac = 0;
            dat = ram[ram[reg_pnt + 1] & 0x7F];
            if (((acc & 0x0f) + (dat & 0x0f)) > 0x0f)
                ac = 0x40;
            temp = acc + dat;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x62: // MOV T,A
            clk++;
            timer_counter = acc;
            break;
        case 0x63: // ILL
            clk++;
            illegal(op);
            break;
        case 0x64: // JMP
            pc = ROM(pc) | 0x300 | A11;
            clk += 2;
            break;
        case 0x65: // STOP TCNT
            clk++;
            // printf("STOP %d\n",master_clk/22);
            count_on = timer_on = 0;
            break;
        case 0x66: // ILL
            clk++;
            illegal(op);
            break;
        case 0x67: // RRC A
            dat = carry;
            carry = acc & 0x01;
            acc = acc >> 1;
            if (dat)
                acc = acc | 0x80;
            else
                acc = acc & 0x7F;
            clk++;
            break;
        case 0x68: // ADD A,Rr
            clk++;
            carry = ac = 0;
            dat = ram[reg_pnt];
            if (((acc & 0x0f) + (dat & 0x0f)) > 0x0f)
                ac = 0x40;
            temp = acc + dat;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x69: // ADD A,Rr
            clk++;
            carry = ac = 0;
            dat = ram[reg_pnt + 1];
            if (((acc & 0x0f) + (dat & 0x0f)) > 0x0f)
                ac = 0x40;
            temp = acc + dat;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x6A: // ADD A,Rr
            clk++;
            carry = ac = 0;
            dat = ram[reg_pnt + 2];
            if (((acc & 0x0f) + (dat & 0x0f)) > 0x0f)
                ac = 0x40;
            temp = acc + dat;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x6B: // ADD A,Rr
            clk++;
            carry = ac = 0;
            dat = ram[reg_pnt + 3];
            if (((acc & 0x0f) + (dat & 0x0f)) > 0x0f)
                ac = 0x40;
            temp = acc + dat;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x6C: // ADD A,Rr
            clk++;
            carry = ac = 0;
            dat = ram[reg_pnt + 4];
            if (((acc & 0x0f) + (dat & 0x0f)) > 0x0f)
                ac = 0x40;
            temp = acc + dat;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x6D: // ADD A,Rr
            clk++;
            carry = ac = 0;
            dat = ram[reg_pnt + 5];
            if (((acc & 0x0f) + (dat & 0x0f)) > 0x0f)
                ac = 0x40;
            temp = acc + dat;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x6E: // ADD A,Rr
            clk++;
            carry = ac = 0;
            dat = ram[reg_pnt + 6];
            if (((acc & 0x0f) + (dat & 0x0f)) > 0x0f)
                ac = 0x40;
            temp = acc + dat;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x6F: // ADD A,Rr
            clk++;
            carry = ac = 0;
            dat = ram[reg_pnt + 7];
            if (((acc & 0x0f) + (dat & 0x0f)) > 0x0f)
                ac = 0x40;
            temp = acc + dat;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x70: // ADDC A,@Ri
            clk++;
            ac = 0;
            dat = ram[ram[reg_pnt] & 0x7F];
            if (((acc & 0x0f) + (dat & 0x0f) + carry) > 0x0f)
                ac = 0x40;
            temp = acc + dat + carry;
            carry = 0;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x71: // ADDC A,@Ri
            clk++;
            ac = 0;
            dat = ram[ram[reg_pnt + 1] & 0x7F];
            if (((acc & 0x0f) + (dat & 0x0f) + carry) > 0x0f)
                ac = 0x40;
            temp = acc + dat + carry;
            carry = 0;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;

        case 0x72: // JBb address
            clk += 2;
            dat = ROM(pc);
            if (acc & 0x08)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0x73: // ILL
            clk++;
            illegal(op);
            break;
        case 0x74: // CALL
            make_psw();
            adr = ROM(pc) | 0x300 | A11;
            pc++;
            clk += 2;
            push(pc & 0xFF);
            push(((pc & 0xF00) >> 8) | (psw & 0xF0));
            pc = adr;
            break;
        case 0x75: // EN CLK
            clk++;
            undef(op);
            break;
        case 0x76: // JF1 address
            clk += 2;
            dat = ROM(pc);
            if (f1)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0x77: // RR A
            clk++;
            dat = acc & 0x01;
            acc = acc >> 1;
            if (dat)
                acc = acc | 0x80;
            else
                acc = acc & 0x7f;
            break;

        case 0x78: // ADDC A,Rr
            clk++;
            ac = 0;
            dat = ram[reg_pnt];
            if (((acc & 0x0f) + (dat & 0x0f) + carry) > 0x0f)
                ac = 0x40;
            temp = acc + dat + carry;
            carry = 0;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x79: // ADDC A,Rr
            clk++;
            ac = 0;
            dat = ram[reg_pnt + 1];
            if (((acc & 0x0f) + (dat & 0x0f) + carry) > 0x0f)
                ac = 0x40;
            temp = acc + dat + carry;
            carry = 0;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x7A: // ADDC A,Rr
            clk++;
            ac = 0;
            dat = ram[reg_pnt + 2];
            if (((acc & 0x0f) + (dat & 0x0f) + carry) > 0x0f)
                ac = 0x40;
            temp = acc + dat + carry;
            carry = 0;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x7B: // ADDC A,Rr
            clk++;
            ac = 0;
            dat = ram[reg_pnt + 3];
            if (((acc & 0x0f) + (dat & 0x0f) + carry) > 0x0f)
                ac = 0x40;
            temp = acc + dat + carry;
            carry = 0;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x7C: // ADDC A,Rr
            clk++;
            ac = 0;
            dat = ram[reg_pnt + 4];
            if (((acc & 0x0f) + (dat & 0x0f) + carry) > 0x0f)
                ac = 0x40;
            temp = acc + dat + carry;
            carry = 0;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x7D: // ADDC A,Rr
            clk++;
            ac = 0;
            dat = ram[reg_pnt + 5];
            if (((acc & 0x0f) + (dat & 0x0f) + carry) > 0x0f)
                ac = 0x40;
            temp = acc + dat + carry;
            carry = 0;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x7E: // ADDC A,Rr
            clk++;
            ac = 0;
            dat = ram[reg_pnt + 6];
            if (((acc & 0x0f) + (dat & 0x0f) + carry) > 0x0f)
                ac = 0x40;
            temp = acc + dat + carry;
            carry = 0;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;
        case 0x7F: // ADDC A,Rr
            clk++;
            ac = 0;
            dat = ram[reg_pnt + 7];
            if (((acc & 0x0f) + (dat & 0x0f) + carry) > 0x0f)
                ac = 0x40;
            temp = acc + dat + carry;
            carry = 0;
            if (temp > 0xFF)
                carry = 1;
            acc = (temp & 0xFF);
            break;

        case 0x80: // MOVX A,@R0
            acc = read_external_mem(ram[reg_pnt]);
            clk += 2;
            break;
        case 0x81: // MOVX A,@R1
            acc = read_external_mem(ram[reg_pnt + 1]);
            clk += 2;
            break;
        case 0x82: // ILL
            clk++;
            illegal(op);
            break;
        case 0x83: // RET
            clk += 2;
            pc = ((pull() & 0x0F) << 8);
            pc = pc | pull();
            break;
        case 0x84: // JMP
            pc = ROM(pc) | 0x400 | A11;
            clk += 2;
            break;
        case 0x85: // CLR F0
            clk++;
            f0 = 0;
            break;
        case 0x86: // JNI address
            clk += 2;
            dat = ROM(pc);
            if (int_clk > 0)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0x87: // ILL
            illegal(op);
            clk++;
            break;
        case 0x88: // BUS,#data
            clk += 2;
            undef(op); // Never happens in 951 code
            break;
        case 0x89: // ORL Pp,#data
            write_p1(p1 | ROM(pc++));
            clk += 2;
            break;
        case 0x8A: // ORL Pp,#data
            write_p2(p2 | ROM(pc++));
            clk += 2;
            break;
        case 0x8B: // ILL
            illegal(op);
            clk++;
            break;
        case 0x8C: // ORLD P4,A
            write_PB(0, read_PB(0) | acc);
            clk += 2;
            break;
        case 0x8D: // ORLD P5,A
            write_PB(1, read_PB(1) | acc);
            clk += 2;
            break;
        case 0x8E: // ORLD P6,A
            write_PB(2, read_PB(2) | acc);
            clk += 2;
            break;
        case 0x8F: // ORLD P7,A
            write_PB(3, read_PB(3) | acc);
            clk += 2;
            break;
        case 0x90: // MOVX @Ri,A
            ram[reg_pnt] = acc;
            clk += 2;
            break;
        case 0x91: // MOVX @Ri,A
            ram[reg_pnt + 1] = acc;
            clk += 2;
            break;
        case 0x92: // JBb address
            clk += 2;
            dat = ROM(pc);
            if (acc & 0x10)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0x93: // RETR
            // printf("RETR %d\n",master_clk/22);
            clk += 2;
            dat = pull();
            pc = (dat & 0x0F) << 8;
            carry = (dat & 0x80) >> 7;
            ac = dat & 0x40;
            f0 = dat & 0x20;
            reg_bank = dat & 0x10;
            if (reg_bank)
                reg_pnt = 24;
            else
                reg_pnt = 0;
            pc = pc | pull();
            irq_ex = 0;
            A11 = A11ff;
            break;
        case 0x94: // CALL
            make_psw();
            adr = ROM(pc) | 0x400 | A11;
            pc++;
            clk += 2;
            push(pc & 0xFF);
            push(((pc & 0xF00) >> 8) | (psw & 0xF0));
            pc = adr;
            break;
        case 0x95: // CPL F0
            f0 = f0 ^ 0x20;
            clk++;
            break;
        case 0x96: // JNZ address
            clk += 2;
            dat = ROM(pc);
            if (acc != 0)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0x97: // CLR C
            carry = 0;
            clk++;
            break;
        case 0x98: // ANL BUS,#data
            clk += 2;
            undef(op);
            break;
        case 0x99: // ANL Pp,#data
            write_p1(p1 & ROM(pc++));
            clk += 2;
            break;
        case 0x9A: // ANL Pp,#data
            write_p2(p2 & ROM(pc++));
            clk += 2;
            break;
        case 0x9B: // ILL
            illegal(op);
            clk++;
            break;
        case 0x9C: // ANLD P4,A
            write_PB(0, read_PB(0) & acc);
            clk += 2;
            break;
        case 0x9D: // ANLD P5,A
            write_PB(1, read_PB(1) & acc);
            clk += 2;
            break;
        case 0x9E: // ANLD P6,A
            write_PB(2, read_PB(2) & acc);
            clk += 2;
            break;
        case 0x9F: // ANLD P7,A
            write_PB(3, read_PB(3) & acc);
            clk += 2;
            break;
        case 0xA0: // MOV @Ri,A
            ram[ram[reg_pnt] & 0x7F] = acc;
            clk++;
            break;
        case 0xA1: // MOV @Ri,A
            ram[ram[reg_pnt + 1] & 0x7F] = acc;
            clk++;
            break;
        case 0xA2: // ILL
            clk++;
            illegal(op);
            break;
        case 0xA3: // MOVP A,@A
            acc = ROM((pc & 0xF00) | acc);
            clk += 2;
            break;
        case 0xA4: // JMP
            pc = ROM(pc) | 0x500 | A11;
            clk += 2;
            break;
        case 0xA5: // CLR F1
            clk++;
            f1 = 0;
            break;
        case 0xA6: // ILL
            illegal(op);
            clk++;
            break;
        case 0xA7: // CPL C
            carry = carry ^ 0x01;
            clk++;
            break;
        case 0xA8: // MOV Rr,A
            ram[reg_pnt] = acc;
            clk++;
            break;
        case 0xA9: // MOV Rr,A
            ram[reg_pnt + 1] = acc;
            clk++;
            break;
        case 0xAA: // MOV Rr,A
            ram[reg_pnt + 2] = acc;
            clk++;
            break;
        case 0xAB: // MOV Rr,A
            ram[reg_pnt + 3] = acc;
            clk++;
            break;
        case 0xAC: // MOV Rr,A
            ram[reg_pnt + 4] = acc;
            clk++;
            break;
        case 0xAD: // MOV Rr,A
            ram[reg_pnt + 5] = acc;
            clk++;
            break;
        case 0xAE: // MOV Rr,A
            ram[reg_pnt + 6] = acc;
            clk++;
            break;
        case 0xAF: // MOV Rr,A
            ram[reg_pnt + 7] = acc;
            clk++;
            break;
        case 0xB0: // MOV @Ri,#data
            ram[ram[reg_pnt] & 0x7F] = ROM(pc++);
            clk += 2;
            break;
        case 0xB1: // MOV @Ri,#data
            ram[ram[reg_pnt + 1] & 0x7F] = ROM(pc++);
            clk += 2;
            break;
        case 0xB2: // JBb address
            clk += 2;
            dat = ROM(pc);
            if (acc & 0x20)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0xB3: // JMPP @A
            adr = (pc & 0xF00) | acc;
            pc = (pc & 0xF00) | ROM(adr);
            clk += 2;
            break;
        case 0xB4: // CALL
            make_psw();
            adr = ROM(pc) | 0x500 | A11;
            pc++;
            clk += 2;
            push(pc & 0xFF);
            push(((pc & 0xF00) >> 8) | (psw & 0xF0));
            pc = adr;
            break;
        case 0xB5: // CPL F1
            f1 = f1 ^ 0x01;
            clk++;
            break;
        case 0xB6: // JF0 address
            clk += 2;
            dat = ROM(pc);
            if (f0)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0xB7: // ILL
            clk++;
            illegal(op);
            break;
        case 0xB8: // MOV Rr,#data
            ram[reg_pnt] = ROM(pc++);
            clk += 2;
            break;
        case 0xB9: // MOV Rr,#data
            ram[reg_pnt + 1] = ROM(pc++);
            clk += 2;
            break;
        case 0xBA: // MOV Rr,#data
            ram[reg_pnt + 2] = ROM(pc++);
            clk += 2;
            break;
        case 0xBB: // MOV Rr,#data
            ram[reg_pnt + 3] = ROM(pc++);
            clk += 2;
            break;
        case 0xBC: // MOV Rr,#data
            ram[reg_pnt + 4] = ROM(pc++);
            clk += 2;
            break;
        case 0xBD: // MOV Rr,#data
            ram[reg_pnt + 5] = ROM(pc++);
            clk += 2;
            break;
        case 0xBE: // MOV Rr,#data
            ram[reg_pnt + 6] = ROM(pc++);
            clk += 2;
            break;
        case 0xBF: // MOV Rr,#data
            ram[reg_pnt + 7] = ROM(pc++);
            clk += 2;
            break;
        case 0xC0: // ILL
            illegal(op);
            clk++;
            break;
        case 0xC1: // ILL
            illegal(op);
            clk++;
            break;
        case 0xC2: // ILL
            illegal(op);
            clk++;
            break;
        case 0xC3: // ILL
            illegal(op);
            clk++;
            break;
        case 0xC4: // JMP
            pc = ROM(pc) | 0x600 | A11;
            clk += 2;
            break;
        case 0xC5: // SEL RB0
            reg_bank = reg_pnt = 0;
            clk++;
            break;
        case 0xC6: // JZ address
            clk += 2;
            dat = ROM(pc);
            if (acc == 0)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0xC7: // MOV A,PSW
            clk++;
            make_psw();
            acc = psw;
            break;
        case 0xC8: // DEC Rr
            ram[reg_pnt]--;
            clk++;
            break;
        case 0xC9: // DEC Rr
            ram[reg_pnt + 1]--;
            clk++;
            break;
        case 0xCA: // DEC Rr
            ram[reg_pnt + 2]--;
            clk++;
            break;
        case 0xCB: // DEC Rr
            ram[reg_pnt + 3]--;
            clk++;
            break;
        case 0xCC: // DEC Rr
            ram[reg_pnt + 4]--;
            clk++;
            break;
        case 0xCD: // DEC Rr
            ram[reg_pnt + 5]--;
            clk++;
            break;
        case 0xCE: // DEC Rr
            ram[reg_pnt + 6]--;
            clk++;
            break;
        case 0xCF: // DEC Rr
            ram[reg_pnt + 7]--;
            clk++;
            break;
        case 0xD0: // XRL A,@Ri
            acc = acc ^ ram[ram[reg_pnt] & 0x7F];
            clk++;
            break;
        case 0xD1: // XRL A,@Ri
            acc = acc ^ ram[ram[reg_pnt + 1] & 0x7F];
            clk++;
            break;
        case 0xD2: // JBb address
            clk += 2;
            dat = ROM(pc);
            if (acc & 0x40)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0xD3: // XRL A,#data
            clk += 2;
            acc = acc ^ ROM(pc++);
            break;
        case 0xD4: // CALL
            make_psw();
            adr = ROM(pc) | 0x600 | A11;
            pc++;
            clk += 2;
            push(pc & 0xFF);
            push(((pc & 0xF00) >> 8) | (psw & 0xF0));
            pc = adr;
            break;
        case 0xD5: // SEL RB1
            reg_bank = 0x10;
            reg_pnt = 24;
            clk++;
            break;
        case 0xD6: // ILL
            illegal(op);
            clk++;
            break;
        case 0xD7: // MOV PSW,A
            psw = acc;
            clk++;
            carry = (psw & 0x80) >> 7;
            ac = psw & 0x40;
            f0 = psw & 0x20;
            reg_bank = psw & 0x10;
            if (reg_bank)
                reg_pnt = 24;
            else
                reg_pnt = 0;
            sp = (psw & 0x07) << 1;
            sp += 8;
            break;
        case 0xD8: // XRL A,Rr
            acc = acc ^ ram[reg_pnt];
            clk++;
            break;
        case 0xD9: // XRL A,Rr
            acc = acc ^ ram[reg_pnt + 1];
            clk++;
            break;
        case 0xDA: // XRL A,Rr
            acc = acc ^ ram[reg_pnt + 2];
            clk++;
            break;
        case 0xDB: // XRL A,Rr
            acc = acc ^ ram[reg_pnt + 3];
            clk++;
            break;
        case 0xDC: // XRL A,Rr
            acc = acc ^ ram[reg_pnt + 4];
            clk++;
            break;
        case 0xDD: // XRL A,Rr
            acc = acc ^ ram[reg_pnt + 5];
            clk++;
            break;
        case 0xDE: // XRL A,Rr
            acc = acc ^ ram[reg_pnt + 6];
            clk++;
            break;
        case 0xDF: // XRL A,Rr
            acc = acc ^ ram[reg_pnt + 7];
            clk++;
            break;
        case 0xE0: // ILL
            clk++;
            illegal(op);
            break;
        case 0xE1: // ILL
            clk++;
            illegal(op);
            break;
        case 0xE2: // ILL
            clk++;
            illegal(op);
            break;
        case 0xE3: // MOVP3 A,@A

            adr = 0x300 | acc;
            acc = ROM(adr);
            clk += 2;
            break;
        case 0xE4: // JMP
            pc = ROM(pc) | 0x700 | A11;
            clk += 2;
            break;
        case 0xE5: // SEL MB0
            A11 = 0;
            A11ff = 0;
            clk++;
            break;
        case 0xE6: // JNC address
            clk += 2;
            dat = ROM(pc);
            if (!carry)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0xE7: // RL A
            clk++;
            dat = acc & 0x80;
            acc = acc << 1;
            if (dat)
                acc = acc | 0x01;
            else
                acc = acc & 0xFE;
            break;
        case 0xE8: // DJNZ Rr,address
            clk += 2;
            ram[reg_pnt]--;
            dat = ROM(pc);
            if (ram[reg_pnt] != 0) {
                pc = pc & 0xF00;
                pc = pc | dat;
            } else
                pc++;
            break;
        case 0xE9: // DJNZ Rr,address
            clk += 2;
            ram[reg_pnt + 1]--;
            dat = ROM(pc);
            if (ram[reg_pnt + 1] != 0) {
                pc = pc & 0xF00;
                pc = pc | dat;
            } else
                pc++;
            break;
        case 0xEA: // DJNZ Rr,address
            clk += 2;
            ram[reg_pnt + 2]--;
            dat = ROM(pc);
            if (ram[reg_pnt + 2] != 0) {
                pc = pc & 0xF00;
                pc = pc | dat;
            } else
                pc++;
            break;
        case 0xEB: // DJNZ Rr,address
            clk += 2;
            ram[reg_pnt + 3]--;
            dat = ROM(pc);
            if (ram[reg_pnt + 3] != 0) {
                pc = pc & 0xF00;
                pc = pc | dat;
            } else
                pc++;
            break;
        case 0xEC: // DJNZ Rr,address
            clk += 2;
            ram[reg_pnt + 4]--;
            dat = ROM(pc);
            if (ram[reg_pnt + 4] != 0) {
                pc = pc & 0xF00;
                pc = pc | dat;
            } else
                pc++;
            break;
        case 0xED: // DJNZ Rr,address
            clk += 2;
            ram[reg_pnt + 5]--;
            dat = ROM(pc);
            if (ram[reg_pnt + 5] != 0) {
                pc = pc & 0xF00;
                pc = pc | dat;
            } else
                pc++;
            break;
        case 0xEE: // DJNZ Rr,address
            clk += 2;
            ram[reg_pnt + 6]--;
            dat = ROM(pc);
            if (ram[reg_pnt + 6] != 0) {
                pc = pc & 0xF00;
                pc = pc | dat;
            } else
                pc++;
            break;
        case 0xEF: // DJNZ Rr,address
            clk += 2;
            ram[reg_pnt + 7]--;
            dat = ROM(pc);
            if (ram[reg_pnt + 7] != 0) {
                pc = pc & 0xF00;
                pc = pc | dat;
            } else
                pc++;
            break;
        case 0xF0: // MOV A,@R0
            clk++;
            acc = ram[ram[reg_pnt] & 0x7F];
            break;
        case 0xF1: // MOV A,@R1
            clk++;
            acc = ram[ram[reg_pnt + 1] & 0x7F];
            break;
        case 0xF2: // JBb address
            clk += 2;
            dat = ROM(pc);
            if (acc & 0x80)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0xF3: // ILL
            illegal(op);
            clk++;
            break;
        case 0xF4: // CALL
            clk += 2;
            make_psw();
            adr = ROM(pc) | 0x700 | A11;
            pc++;
            push(pc & 0xFF);
            push(((pc & 0xF00) >> 8) | (psw & 0xF0));
            pc = adr;
            break;
        case 0xF5: // SEL MB1
            if (irq_ex) {
                A11ff = 0x800;
            } else {
                A11 = 0x800;
                A11ff = 0x800;
            }
            clk++;
            break;
        case 0xF6: // JC address
            clk += 2;
            dat = ROM(pc);
            if (carry)
                pc = (pc & 0xF00) | dat;
            else
                pc++;
            break;
        case 0xF7: // RLC A
            dat = carry;
            carry = (acc & 0x80) >> 7;
            acc = acc << 1;
            if (dat)
                acc = acc | 0x01;
            else
                acc = acc & 0xFE;
            clk++;
            break;
        case 0xF8: // MOV A,Rr
            clk++;
            acc = ram[reg_pnt];
            break;
        case 0xF9: // MOV A,Rr
            clk++;
            acc = ram[reg_pnt + 1];
            break;
        case 0xFA: // MOV A,Rr
            clk++;
            acc = ram[reg_pnt + 2];
            break;
        case 0xFB: // MOV A,Rr
            clk++;
            acc = ram[reg_pnt + 3];
            break;
        case 0xFC: // MOV A,Rr
            clk++;
            acc = ram[reg_pnt + 4];
            break;
        case 0xFD: // MOV A,Rr
            clk++;
            acc = ram[reg_pnt + 5];
            break;
        case 0xFE: // MOV A,Rr
            clk++;
            acc = ram[reg_pnt + 6];
            break;
        case 0xFF: // MOV A,Rr
            clk++;
            acc = ram[reg_pnt + 7];
            break;
        }

        master_clk += clk;

        // flag for JNI
        if (int_clk > clk)
            int_clk -= clk;
        else
            int_clk = 0;

        // pending IRQs
        if (xirq_pend)
            ext_IRQ();
        if (tirq_pend)
            tim_IRQ();

        if (timer_on) {
            timer_cycle_accumulator += clk;
            if (timer_cycle_accumulator > 31) {
                timer_cycle_accumulator -= 31;
                timer_counter++;
                if (timer_counter == 0) {
                    t_flag = 1;
                    tim_IRQ();
                }
            }
        }
    }

    return master_clk - target_master_clk + num_cycles;
}
