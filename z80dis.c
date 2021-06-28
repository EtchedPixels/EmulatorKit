/*
 *	Very simple Z80 disassembler for debug work
 *
 *	Doesn't handle all the ilegals yet - that needs it teaching
 *	LD R, RL (ix+d) type CB ops
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "z80dis.h" 
 
static uint8_t prefix;
static const char *hlname;
static uint16_t pc;

/*
 *	Glue to caller. Caller provides a single helper that returns
 *	the next decode byte (f this is a space with side effects then
 *	it needs to avoid the side effect)
 */

static int8_t offs8(void)
{
    return z80dis_byte(pc++);
}

static uint8_t imm8(void)
{
    return z80dis_byte(pc++);
}

static uint16_t imm16(void)
{
    uint16_t r = z80dis_byte(pc++);
    r |= z80dis_byte(pc++) << 8;
    return r;
}


static const char *rname[8] = {
    "B", "C", "D", "E", "H", "L", "M", "A"
};

static const char *rname16[4] = {
    "BC", "DE", "HL", "SP"
};

static const char *rotshift[8] = {
    "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"
};

static const char *logic8[8] = {
    "ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"
};

static const char *bitop[8] = {
    "", "BIT", "RES", "SET"
};

static const char *ccode[8] = {
    "NZ", "Z", "NC", "C", "PO", "PE", "P", "M"
};

/* Decode group helper tables */

static const char *opgroup00[] = {
    "NOP", "EX AF,AF'", "DJNZ 0x%04X", "JR 0x%04X",
    "JR NZ, 0x%04X", "JR Z, 0x%04X", "JR NC, 0x%04X", "JR C, 0x%04X"
};
static const char *opgroup02[] = {
    "LD (BC), A", "LD A, (BC)",
    "LD (DE), A", "LD A, (DE)",
    "LD (0x%04X), %s", "LD %s, (0x%04X)",
    "LD (0x%04X), A", "LD A, (0x%04X)"
};
static const char *opgroup07[] = {
    "RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF"
};
static const char *opgroup31[] = {
    "RET" , "EXX", "JP %s", "LD SP, %s"
};
static const char *opgroup33[] = {
    NULL, NULL, "OUT (0x%02X), A", "IN A, (0x%02X)",
    "EX (SP),%s", "EX DE, HL", "DI", "EI"
};
static const char *opgrouped17[] = {
    "LD I,A", "LD R,A", "LD A,I", "LD A,R",
    "RRD", "RLD", "NOP", "NOP"
};
static const char *opgrouped2[] = {
    "LD", "CP", "IN", "OUT"
};

static const char *reg8_str(uint8_t r, uint8_t ro, int8_t offs)
{
    static char tmpbuf[16];
    if (r == 6) {
        if (prefix) {
            if (offs < 0)
                snprintf(tmpbuf, 16, "(%s%d)", hlname, offs);
            else if (offs > 1)
                snprintf(tmpbuf, 16, "(%s+%d)", hlname, offs);
            else
                sprintf(tmpbuf, "(%s)", hlname);
        } else
            sprintf(tmpbuf, "(%s)", hlname);
        return tmpbuf;
    }
    if ((r == 4  || r == 5) && ro != 6) {
        if (prefix == 0xFD)
            return r == 4 ? "IYh" : "IYl";
        if (prefix == 0xDD)
            return r == 4 ? "IXh" : "IXl";
    }
    return rname[r];
}

static const char *reg8_offs(uint8_t r, int8_t offs)
{
    return reg8_str(r, 0, offs);
}

static const char *reg8(uint8_t r)
{
    if (prefix && r == 6)
        return reg8_str(r, 0, offs8());
    else
        return reg8_str(r, 0, 0);
}

static const char *reg8pair(uint8_t r, uint8_t ro)
{
    if (prefix && r == 6)
        return reg8_str(r, ro, offs8());
    else
        return reg8_str(r, ro, 0);
}

static const char *rpair(uint8_t r)
{
    if (prefix && r == 2)
        return hlname;
    return rname16[r];
}

static const char *rpairstack(uint8_t r)
{
    if (prefix && r == 2)
        return hlname;
    if (r == 3)
        return "AF";
    return rname16[r];
}

/*
 *	Disassemble an instruction
 */
void z80_disasm(char *buf, uint16_t addr)
{
    uint8_t opcode, y, z, p, q;
    const char *tp;
    uint16_t relbase = addr + 2;	/* JR maths base */

    pc = addr;

    *buf = 0;
    hlname = "HL";
    prefix = 0;
    
restart:
    opcode = imm8();
    y = (opcode >> 3) & 7;
    z = opcode & 7;
    p = y >> 1;
    q = y & 1;
    switch(opcode) {
    case 0xDD:
        prefix = opcode;
        hlname = "IX";
        goto restart;
    case 0xFD:
        prefix = opcode;
        hlname = "IY";
        goto restart;
    case 0xCB: {
        int8_t offs;
        /* IX and IY illegals are weird so bother to decode them so we
           don't get in a mess. Don't bother decoding them specially though */
        if (prefix && y == 6)
            offs = offs8();
        opcode = imm8();
        y = (opcode >> 3) & 7;
        z = opcode & 7;
        if (opcode < 0x40) {
            sprintf(buf, "%s %s", rotshift[y], reg8_offs(z, offs));
            return;
        }
        sprintf(buf, "%s %d, %s", bitop[opcode >> 6], y, reg8_offs(z, offs));
        return;
    }
    case 0xED:
        /* ED is fairly empty */
        prefix = 0;		/* DD ED isn't a thing */
        opcode = imm8();
        y = (opcode >> 3) & 7;
        z = opcode & 7;
        p = y >> 1;
        q = y & 1;
        
        switch(opcode & 0xC0) {
        case 0:
        case 0xC0:
            sprintf(buf, "NONI NOP");
            return;
        case 0x40:	/* Mixed */
            switch(z) {
            case 0:
                if (y == 6)
                    strcpy(buf, "IN (C)");
                else
                    sprintf(buf, "IN %s, (C)", reg8(y));
                return;
            case 1:
                if (y == 6)
                    strcpy(buf, "OUT (C),255/0");
                else
                    sprintf(buf, "OUT (C), %s", reg8(y));
                return;
            case 2:
                sprintf(buf, "%sC HL, %s", q?"AD":"SB", rpair(p));
                return;
            case 3:
                if (q == 0)
                    sprintf(buf, "LD (0x%04X), %s", imm16(), rpair(p));
                else
                    sprintf(buf, "LD %s, (0x%04X)", rpair(p), imm16());
                return;
            case 4:
                strcpy(buf, "NEG");
                return;
            case 5:
                if (y == 1)
                    strcpy(buf, "RETI");
                else
                    strcpy(buf, "RETN");
                return;
            case 6:
                /* The ilegal IM 0/1 we don't care about */
                y &= 3;
                if (y)
                    y--;
                sprintf(buf, "IM %d", y);
                return;
            case 7:
                strcpy(buf, opgrouped17[y]);
                return;
            }
            return;
        case 0x80:	/* Block ops */
            if (z < 4) {
                sprintf(buf, "%s%c%s",
                    opgrouped2[z], "ID"[y & 1], y & 2 ? "R": "");
            } else
                strcpy(buf, "NONI NOP");
            return;                
        }
        break;
    case 0x76:
        sprintf(buf, "HALT");
        return;
    }
    switch(opcode & 0xC0) {
    case 0x00:
        switch(z) {
        case 0x00:
            if (y > 1)
                sprintf(buf, opgroup00[y], relbase + offs8());
            else
                strcpy(buf, opgroup00[y]);
            return;
        case 0x01:
            if (q == 0)
                sprintf(buf, "LD %s,0x%04X", rpair(p), imm16());
            else
                sprintf(buf, "ADD %s,%s", hlname, rpair(p));
            return;
        case 0x02:
            /* Ugly .. needs work */
            if (p > 1) {
                if (q == 0)
                    sprintf(buf, opgroup02[y], imm16(), hlname);
                else if (p != 3)
                    sprintf(buf, opgroup02[y], hlname, imm16());
                else
                    sprintf(buf, opgroup02[y], imm16());
            } else
                strcpy(buf, opgroup02[y]);
            return;
        case 0x03:
            if (q == 0)
                sprintf(buf, "INC %s", rpair(p));
            else
                sprintf(buf, "DEC %s", rpair(p));
            return;
        case 0x04:
            sprintf(buf, "INC %s", reg8(y));
            return;
        case 0x05:
            sprintf(buf, "DEC %s", reg8(y));
            return;
        case 0x06:
            /* Force evaluation order so we get
                LD (IX+d),n correct */
            tp = reg8(y);
            sprintf(buf, "LD %s,0x%02X", tp, imm8());
            return;
        case 0x07:
            strcpy(buf, opgroup07[y]);
            return;
        }
        break;
    case 0x40:
        sprintf(buf, "LD %s,%s", reg8pair(y,z), reg8pair(z,y));
        return;
    case 0x80:
        sprintf(buf, "%s A,%s", logic8[y], reg8(z));
        return;
    case 0xC0:
        switch(z) {
        case 0x00:
            sprintf(buf, "RET %s", ccode[y]);
            return;
        case 0x01:
            if (q == 0)
                sprintf(buf, "POP %s", rpairstack(p));
            else
                sprintf(buf, opgroup31[p], hlname);
            return;
        case 0x02:
            sprintf(buf, "JP %s,0x%04X", ccode[y], imm16());
            return;
        case 0x03:	/* This one is a right mix .. */
            if (y == 0)
                sprintf(buf, "JP 0x%04X", imm16());
            else if (y < 4)
                sprintf(buf, opgroup33[y], imm8());
            else
                sprintf(buf, opgroup33[y], hlname);
            return;
        case 0x04:
            sprintf(buf, "CALL %s,%04X", ccode[y], imm16());
            return;
        case 0x05:
            if (q == 0)
                sprintf(buf, "PUSH %s", rpairstack(p));
            else	/* Other 3 forms are prefixes grabbed earlier */
                sprintf(buf, "CALL 0x%04X", imm16());
            return;
        case 0x06:
            sprintf(buf, "%s A,0x%02X", logic8[y], imm8());
            return;
        case 0x07:
            sprintf(buf, "RST %02X", y);
            return;
        }
        break;
    }
}
