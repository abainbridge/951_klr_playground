#if _MSC_VER
#pragma warning(disable: 4996)
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


unsigned char g_image[4096];

void my_exit(int i) {
    FILE *out = fopen("out.bin", "wb");
    if (!out) {
        printf("Couldn't open output file\n");
        my_exit(1);
    }

    fwrite(g_image, 1, sizeof(g_image), out);

    puts("\nPress return");
    getchar();
    exit(i);
}

void err_unsupported_instruction(unsigned addr) {
    printf("\nUnsupported instruction at address 0x%x\n", addr);
    my_exit(1);
}

char *get_token(char *c) {
    static char blank_token[] = "*******************";
    if (!c) return blank_token;
    while (*c != ' ' && *c != ',') {
        if (*c == '\n' || *c == '\0')
            return blank_token;
        c++;
    }

    while (*c == ' ' || *c == ',') {
        if (*c == '\n' || *c == '\0')
            return blank_token;
        c++;
    }

    return c;
}

bool token_is(char *buf, char *prefix) {
    while (1) {
        if (*buf != *prefix) {
            if (*prefix == '\0' && (isspace(*buf) || *buf == ',' || *buf == ';'))
                return true;
            return false;
        }
        buf++;
        prefix++;
    }

    return true;
}

unsigned parse_immediate(char *buf) {
    return strtoul(buf, NULL, 16);
}

int main() {
    FILE *in = fopen("Annotated_Stock1987_951KLR.asm", "r");
    if (!in) {
        printf("Couldn't open input file\n");
        my_exit(1);
    }
     
    while (!feof(in)) {
        char line[128];
        fgets(line, sizeof(line), in);
        if (line[0] != '0') continue;
//        printf("%s", line);

        unsigned addr = strtoul(line, NULL, 16);
        printf("\n0x%02x ", addr);

        char *mnemonic = get_token(line);
        char *arg0 = get_token(mnemonic);
        char *arg1 = get_token(arg0);

        if (token_is(mnemonic, ".db")) {
            if (arg0[0] == '0' && arg0[1] == 'x') {
                unsigned val = strtoul(arg0 + 2, NULL, 16);
                g_image[addr] = val;
                printf(".db 0x%02x", val);
                continue;
            }
        }
        else if (token_is(mnemonic, "add")) {
            if (token_is(arg0, "a") && arg1[0] == '#' && arg1[1] == '$') {
                // add a,#$C1
                unsigned val = parse_immediate(arg1 + 2);
                g_image[addr] = 0x03;
                g_image[addr + 1] = val;
                printf("add a,#$%02x", val);
                continue;
            }
            else if (token_is(arg0, "a") && arg1[0] == 'r') {
                // add a,r6
                unsigned reg_id = arg1[1] - '0';
                if (reg_id < 8) {
                    g_image[addr] = 0x68 + reg_id;
                    printf("add a,@r%d", reg_id);
                    continue;
                }
            }
            else if (token_is(arg0, "a") && arg1[0] == '@' && arg1[1] == 'r') {
                // add a,@r1
                unsigned reg_id = arg1[2] - '0';
                if (reg_id < 2) {
                    g_image[addr] = 0x60 + reg_id;
                    printf("add a,@r%d", reg_id);
                    continue;
                }
            }
        }
        else if (token_is(mnemonic, "addc")) {
            if (token_is(arg0, "a") && arg1[0] == 'r') {
                // addc a,r4
                unsigned reg_id = arg1[1] - '0';
                if (reg_id < 8) {
                    g_image[addr] = 0x78 + reg_id;
                    printf("addc a, r%d", reg_id);
                    continue;
                }
            }
            if (token_is(arg0, "a") && arg1[0] == '#' && arg1[1] == '$') {
                // addc a,#$0
                unsigned val = parse_immediate(arg1 + 2);
                g_image[addr] = 0x13;
                g_image[addr + 1] = val;
                printf("addc a, #$%02x", val);
                continue;
            }
        }
        else if (token_is(mnemonic, "anl") || token_is(mnemonic, "orl")) {
            if (arg0[0] == 'p' && arg1[0] == '#' && arg1[1] == '$') {
                // anl p2,#$BF
                // orl p2,#$80
                unsigned port_id = arg0[1] - '0';
                unsigned mask = parse_immediate(arg1 + 2);
                if ((port_id == 1 || port_id == 2) && mask < 256) {
                    if (token_is(mnemonic, "anl")) {
                        g_image[addr] = 0x98 + port_id;
                        printf("anl p%d, #$%02x", port_id, mask);
                    }
                    else {
                        g_image[addr] = 0x88 + port_id;
                        printf("orl p%d, #$%02x", port_id, mask);
                    }
                    
                    g_image[addr + 1] = mask;
                    continue;
                }
            }
            else if (token_is(arg0, "a") && arg1[0] == 'r') {
                if (token_is(mnemonic, "orl")) {
                    // orl a,r4
                    unsigned reg_id = arg1[1] - '0';
                    if (reg_id < 8) {
                        g_image[addr] = 0x48 + reg_id;
                        printf("orl a, r%d", reg_id);
                        continue;
                    }
                }
            }
            else if (token_is(arg0, "a") && arg1[0] == '@' && arg1[1] == 'r') {
                if (token_is(mnemonic, "orl")) {
                    // orl a,@r1
                    unsigned reg_id = arg1[2] - '0';
                    if (reg_id < 8) {
                        g_image[addr] = 0x40 + reg_id;
                        printf("orl a, @r%d", reg_id);
                        continue;
                    }
                }
            }
            else if (token_is(arg0, "a") && arg1[0] == '#' && arg1[1] == '$') {
                unsigned val = parse_immediate(arg1 + 2);
                if (token_is(mnemonic, "anl")) {
                    // anl a, #$77
                    g_image[addr] = 0x53;
                    printf("anl a, #$%02x", val);
                }
                else {
                    // orl a, #$77
                    g_image[addr] = 0x43;
                    printf("orl a, #$%02x", val);
                }
                g_image[addr + 1] = val;
                continue;
            }
            else if (token_is(arg0, "bus") && arg1[0] == '#' && arg1[1] == '$') {
                if (token_is(mnemonic, "anl")) {
                    // anl bus,#$77
                    unsigned val = parse_immediate(arg1 + 2);
                    g_image[addr] = 0x98;
                    g_image[addr + 1] = val;
                    printf("anl bus, #$%02x", val);
                    continue;
                }
            }
        }
        else if (token_is(mnemonic, "anld") || token_is(mnemonic, "orld")) {
            if (arg0[0] == 'p' && token_is(arg1, "a")) {
                // anld p4,a
                // orld p4,a
                unsigned port_id = arg0[1] - '0';
                if (port_id >= 4 && port_id <= 7) {
                    if (token_is(mnemonic, "anld")) {
                        g_image[addr] = 0x9c + (port_id - 4);
                        printf("anld p%d,a");
                    }
                    else {
                        g_image[addr] = 0x8c + (port_id - 4);
                        printf("orld p%d,a");
                    }
                    continue;
                }
            }
        }
        else if (token_is(mnemonic, "clr")) {
            if (token_is(arg0, "a")) {
                g_image[addr] = 0x27;
                printf("clr a");
                continue;
            }
            else if (token_is(arg0, "c")) {
                g_image[addr] = 0x97;
                printf("clr c");
                continue;
            }
            else if (token_is(arg0, "f0")) {
                g_image[addr] = 0x85;
                printf("clr f0");
                continue;
            }
            else if (token_is(arg0, "f1")) {
                g_image[addr] = 0xa5;
                printf("clr f1");
                continue;
            }
        }
        else if (token_is(mnemonic, "cpl")) {
            if (token_is(arg0, "a")) {
                g_image[addr] = 0x37;
                printf("cpl a");
                continue;
            }
            else if (token_is(arg0, "c")) {
                g_image[addr] = 0xa7;
                printf("cpl c");
                continue;
            }
            else if (token_is(arg0, "f0")) {
                g_image[addr] = 0x95;
                printf("cpl f0");
                continue;
            }
            else if (token_is(arg0, "f1")) {
                g_image[addr] = 0xb5;
                printf("cpl f1");
                continue;
            }
        }
        else if (token_is(mnemonic, "dec")) {
            if (arg0[0] == 'a') {
                // dec a
                g_image[addr] = 0x07;
                printf("dec a");
                continue;
            }
            else if (arg0[0] == 'r') {
                // dec r0
                unsigned reg_id = arg0[1] - '0';
                if (reg_id < 8) {
                    g_image[addr] = 0xc8 + reg_id;
                    printf("dec r%d", reg_id);
                    continue;
                }
            }
        }
        else if (token_is(mnemonic, "dis")) {
                if (token_is(arg0, "i")) {
                g_image[addr] = 0x15;
                printf("dis i");
                continue;
            }
        }
        else if (token_is(mnemonic, "djnz")) {
            if (arg0[0] == 'r' && arg1[0] == '$') {
                // djnz r6,$0010
                unsigned reg_id = arg0[1] - '0';
                unsigned target_addr = parse_immediate(arg1 + 1) & 0xff;
                if (reg_id < 8) {
                    g_image[addr] = 0xe8 + reg_id;
                    g_image[addr + 1] = target_addr;
                    printf("djnz r%d,$%04x", reg_id, target_addr);                  
                    continue;
                }
            }
        }
        else if (token_is(mnemonic, "en")) {
            if (token_is(arg0, "i")) {
                // en i
                g_image[addr] = 0x05;
                printf("en i");
                continue;
            }
            else if (token_is(arg0, "tcnti")) {
                g_image[addr] = 0x25;
                printf("en tcnti");
                continue;
            }
        }
        else if (token_is(mnemonic, "inc")) {
            if (arg0[0] == 'a') {
                // inc a
                g_image[addr] = 0x17;
                printf("inc a");
                continue;
            }
            else if (arg0[0] == 'r') {
                // inc r0
                unsigned reg_id = arg0[1] - '0';
                if (reg_id < 8) {
                    g_image[addr] = 0x18 + reg_id;
                    printf("inc r%d", reg_id);
                    continue;
                }
            }
            else if (arg0[0] == '@') {
                // inc @r0
                unsigned reg_id = arg0[2] - '0';
                if (reg_id < 2) {
                    g_image[addr] = 0x10 + reg_id;
                    printf("inc @r0");
                    continue;
                }
            }
        }
        else if (mnemonic[0] == 'j' && mnemonic[1] == 'b') {
            unsigned bit = parse_immediate(mnemonic + 2);
            if (bit < 8 && arg0[0] == '$') {
                // jb1 $003F
                unsigned target_addr = parse_immediate(arg0 + 1);
                g_image[addr] = 0x12 + (bit << 5);
                g_image[addr + 1] = target_addr;
                printf("jb%d $%04x", bit, target_addr);
                continue;
            }
        }
        else if (token_is(mnemonic, "jmp") || token_is(mnemonic, "call")) {
            if (arg0[0] == '$') {
                // call $0077
                // jmp $0077
                unsigned target_addr = parse_immediate(arg0 + 1);
                unsigned target_addr_hi3 = target_addr >> 8;
                if (token_is(mnemonic, "call")) {
                    g_image[addr] = (target_addr_hi3 << 5) + 0x14;
                    printf("call 0x%x", target_addr);
                }
                else {
                    g_image[addr] = (target_addr_hi3 << 5) + 0x4;
                    printf("jmp 0x%x", target_addr);
                }
                g_image[addr + 1] = target_addr & 0xff;
                continue;
            }
        }
        else if (token_is(mnemonic, "jmpp")) {
            if (token_is(arg0, "@a")) {
                g_image[addr] = 0xb3;
                printf("jmpp @a");
                continue;
            }
        }
        else if (token_is(mnemonic, "jnc") || token_is(mnemonic, "jnz") ||
                 token_is(mnemonic, "jc") || token_is(mnemonic, "jf0") ||
                 token_is(mnemonic, "jf1") || token_is(mnemonic, "jz")) {
            if (arg0[0] == '$') {
                // jnc $0023
                unsigned target_addr = parse_immediate(arg0 + 1) & 0xff;
                g_image[addr + 1] = target_addr;

                if (token_is(mnemonic, "jnc")) {
                    g_image[addr] = 0xe6;
                    printf("jnc $%04x", target_addr);
                    continue;
                }
                else if (token_is(mnemonic, "jnz")) {
                    g_image[addr] = 0x96;
                    printf("jnz $%04x", target_addr);
                    continue;
                }
                else if (token_is(mnemonic, "jc")) {
                    g_image[addr] = 0xf6;
                    printf("jc $%04x", target_addr);
                    continue;
                }
                else if (token_is(mnemonic, "jf0")) {
                    g_image[addr] = 0xb6;
                    printf("jf0 $%04x", target_addr);
                    continue;
                }
                else if (token_is(mnemonic, "jf1")) {
                    g_image[addr] = 0x76;
                    printf("jf1 $%04x", target_addr);
                    continue;
                }
                else if (token_is(mnemonic, "jz")) {
                    g_image[addr] = 0xc6;
                    printf("jz $%04x", target_addr);
                    continue;
                }
            }
        }
        else if (token_is(mnemonic, "jnt1")) {
            // jnt1 $001A
            if (arg0[0] == '$') {
                unsigned target_addr = parse_immediate(arg0+1);
                g_image[addr] = 0x46;
                g_image[addr + 1] = target_addr;
                printf("jnt1 $%04x", target_addr);
                continue;
            }
        }
        else if (token_is(mnemonic, "mov")) {
            if (token_is(arg0, "a") && arg1[0] == '#') {
                // mov a,#$fc
                unsigned val = parse_immediate(arg1 + 2);
                if (val < 256) {
                    g_image[addr] = 0x23;
                    g_image[addr + 1] = val;
                    printf("mov a,#$%02x", val);
                    continue;
                }
            }
            else if (token_is(arg0, "a") && arg1[0] == 'r') {
                // mov a,r7
                unsigned reg_id = arg1[1] - '0';
                if (reg_id < 8) {
                    g_image[addr] = 0xf8 + reg_id;
                    printf("mov a,r%d", reg_id);
                    continue;
                }
            }
            else if (token_is(arg0, "a") && arg1[0] == '@' && arg1[1] == 'r') {
                // mov a, @r0
                unsigned reg_id = arg1[2] - '0';
                if (reg_id < 2) {
                    g_image[addr] = 0xf0 + reg_id;
                    printf("mov a,@r%d", reg_id);
                    continue;
                }
            }
            else if (arg0[0] == 'r' && arg1[0] == 'a') {
                // mov r2,a
                unsigned reg_id = arg0[1] - '0';
                if (reg_id < 8) {
                    g_image[addr] = 0xa8 + reg_id;
                    printf("mov r%d,a", reg_id);
                    continue;
                }
            }
            else if (arg0[0] == '@' && arg0[1] == 'r' && token_is(arg1, "a")) {
                // mov @r0,a
                unsigned reg_id = arg0[2] - '0';
                if (reg_id <= 1) {
                    g_image[addr] = 0xa0 + reg_id;
                    printf("mov @r%d,a", reg_id);
                    continue;
                }
            }
            else if (arg0[0] == '@' && arg0[1] == 'r' && arg1[0] == '#') {
                // mov @r0,#$ff
                unsigned reg_id = arg0[2] - '0';
                unsigned val = parse_immediate(arg1 + 2);
                if (reg_id <= 1) {
                    g_image[addr] = 0xb0 + reg_id;
                    g_image[addr + 1] = val;
                    printf("mov @r%d,#$%02x", reg_id, val);
                    continue;
                }
            }
            else if (arg0[0] == 'r' && arg1[0] == '#' && arg1[1] == '$') {
                // mov r0,#$26
                unsigned reg_id = arg0[1] - '0';
                unsigned val = parse_immediate(arg1 + 2);
                if (reg_id < 8) {
                    g_image[addr] = 0xb8 + reg_id;
                    g_image[addr + 1] = val;
                    printf("mov r%d,#$%02x", reg_id, val);
                    continue;
                }
            }
            else if (token_is(arg0, "t") && token_is(arg1, "a")) {
                // mov t,a
                g_image[addr] = 0x62;
                printf("mov t,a");
                continue;
            }
        }
        else if (token_is(mnemonic, "movd")) {
            if (arg0[0] == 'p' && token_is(arg1, "a")) {
                // movd p4,a
                unsigned port_id = arg0[1] - '0';
                if (port_id >= 4 && port_id <= 7) {
                    g_image[addr] = 0x3c + (port_id - 4);
                    printf("movd p%d,a", port_id);
                    continue;
                }
            }
        }
        else if (token_is(mnemonic, "movp")) {
            if (token_is(arg0, "a") && token_is(arg1, "@a")) {
                // movp a,@a
                g_image[addr] = 0xa3;
                printf("movp a,@a");
                continue;
            }
        }
        else if (token_is(mnemonic, "movx")) {
            if (token_is(arg0, "a") && arg1[0] == '@' && arg1[1] == 'r') {
                // movx a,@r1
                unsigned reg_id = arg1[2] - '0';
                if (reg_id < 2) {
                    g_image[addr] = 0x80 + reg_id;
                    printf("movx a,@r%d", reg_id);
                    continue;
                }
            }
            else if (arg0[0] == '@' && arg0[1] == 'r' && token_is(arg1, "a")) {
                // movx @r1,a
                unsigned reg_id = arg0[2] - '0';
                if (reg_id < 2) {
                    g_image[addr] = 0x90 + reg_id;
                    printf("movx @r%d,a", reg_id);
                    continue;
                }
            }
        }
        else if (token_is(mnemonic, "nop")) {
            printf("nop");
            continue;
        }
        else if (token_is(mnemonic, "ret")) {
            g_image[addr] = 0x83;
            printf("ret");
            continue;
        }
        else if (token_is(mnemonic, "retr")) {
            g_image[addr] = 0x93;
            printf("retr");
            continue;
        }
        else if (token_is(mnemonic, "rl")) {
            if (token_is(arg0, "a")) {
                g_image[addr] = 0xe7;
                printf("rl a");
                continue;
            }
        }
        else if (token_is(mnemonic, "rr")) {
            if (token_is(arg0, "a")) {
                g_image[addr] = 0x77;
                printf("rr a");
                continue;
            }
        }
        else if (token_is(mnemonic, "rrc")) {
            if (token_is(arg0, "a")) {
                // rrc a
                g_image[addr] = 0x67;
                printf("rrc a");
                continue;
            }
        }
        else if (token_is(mnemonic, "sel")) {
            if (token_is(arg0, "mb0")) {
                g_image[addr] = 0xe5;
                printf("sel mb0");
                continue;
            }
            else if (token_is(arg0, "mb1")) {
                g_image[addr] = 0xf5;
                printf("sel mb1");
                continue;
            }  
            if (token_is(arg0, "rb0")) {
                g_image[addr] = 0xc5;
                printf("sel rb0");
                continue;
            }
            else if (token_is(arg0, "rb1")) {
                g_image[addr] = 0xd5;
                printf("sel rb1");
                continue;
            }
        }
        else if (token_is(mnemonic, "rlc")) {
            if (token_is(arg0, "a")) {
                // rlc a
                g_image[addr] = 0xf7;
                printf("rlc a");
                continue;
            }
        }
        else if (token_is(mnemonic, "stop")) {
            if (token_is(arg0, "tcnt")) {
                g_image[addr] = 0x65;
                printf("stop tcnt");
                continue;
            }
        }
        else if (token_is(mnemonic, "strt")) {
            if (token_is(arg0, "t")) {
                printf("start timer");
                g_image[addr] = 0x55;
                continue;
            }
        }
        else if (token_is(mnemonic, "swap")) {
            if (token_is(arg0, "a")) {
                // swap a
                g_image[addr] = 0x47;
                printf("swap a");
                continue;
            }
        }
        else if (token_is(mnemonic, "xch")) {
            if (token_is(arg0, "a") && arg1[0] == 'r') {
                // xch a,r2
                unsigned reg_id = arg1[1] - '0';
                if (reg_id < 8) {
                    g_image[addr] = 0x28 + reg_id;
                    printf("xch a,r%d", reg_id);
                    continue;
                }
            }
            else if (token_is(arg0, "a") && arg1[0] == '@' && arg1[1] == 'r') {
                // xch a,@r1
                unsigned reg_id = arg1[2] - '0';
                if (reg_id < 2) {
                    g_image[addr] = 0x20 + reg_id;
                    printf("xch a,@r%d", reg_id);
                    continue;
                }
            }
        }
        else if (token_is(mnemonic, "xchd")) {
            if (token_is(arg0, "a") && arg1[0] == '@' && arg1[1] == 'r') {
                // xchd a, @r0
                unsigned reg_id = arg1[2] - '0';
                if (reg_id < 2) {
                    g_image[addr] = 0x30 + reg_id;
                    printf("xchd a, @r%d", reg_id);
                    continue;
                }
            }
        }
        else if (token_is(mnemonic, "xrl")) {
            if (token_is(arg0, "a") && arg1[0] == '#' && arg1[1] == '$') {
                // xrl a,#$28
                unsigned val = parse_immediate(arg1 + 2);
                g_image[addr] = 0xd3;
                g_image[addr + 1] = val;
                printf("xlr a,#$%02x", val);
                continue;
            }
        }

        err_unsupported_instruction(addr);
    }

    my_exit(0);
}
