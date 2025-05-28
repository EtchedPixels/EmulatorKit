/* LICENSING NOTICE:

	This file contains changes incorporated from the non public domain 
	Commander X16 emulator.

	However, the fake6502 code in their repository is still marked public domain!

	If Michael Steil or Paul Robson (or others who worked on the fake6502 implementation
	in the X16 repo) have any issues whatsoever with the public domain license in use here,

	please get them to leave an issue.
*/

/* Fake65c02 CPU emulator core v1.4 ******************
 *Original Author:Mike Chambers (miker00lz@gmail.com)*
 *  Author 2: Paul Robson                            *
 *New Author:David MHS Webster (github.com/gek169)   *
 *    Leave a star on github to show thanks for this *
 *        FULLY PUBLIC DOMAIN, CC0 CODE              *
 * Which I give to you and the world with absolutely *
 *  no attribution, monetary compensation, or        *
 *  copyleft requirement. Just write code!           *
 *****************************************************
 *       Let all that you do be done with love       *
 *****************************************************
 *This version has been overhauled with major bug    *
 *fixes relating to decimal mode and adc/sbc. I've   *
 *put the emulator through its paces in kernalemu    *
 *as well as run it through an instruction exerciser *
 *to make sure it works properly. I also discovered  *
 *bugs in the instruction exerciser while I was at it*
 *I might contribute some fixes back to them.        *
 *****************************************************
 * v1.4 - Update for 65c02 compatibility.            *
 * v1.3 - refactoring and more bug fixes             *
 * v1.2 - Major bug fixes in handling adc and sbc    *
 * v1.1 - Small bugfix in BIT opcode, but it was the *
 *        difference between a few games in my NES   *
 *        emulator working and being broken!         *
 *        I went through the rest carefully again    *
 *        after fixing it just to make sure I didn't *
 *        have any other typos! (Dec. 17, 2011)      *
 *                                                   *
 * v1.0 - First release (Nov. 24, 2011)              *
 *****************************************************
 * LICENSE: This source code is released into the    *
 * public domain, but if you use it please do give   *
 * credit. I put a lot of effort into writing this!  *
 * Note by GEK: this is not a requirement.           *
 *****************************************************
 * Fake6502 is a MOS Technology 6502 CPU emulation   *
 * engine in C. It was written as part of a Nintendo *
 * Entertainment System emulator I've been writing.  *
 *                                                   *
 * A couple important things to know about are two   *
 * defines in the code. One is "UNDOCUMENTED" which, *
 * when defined, allows Fake6502 to compile with     *
 * full support for the more predictable             *
 * undocumented instructions of the 6502. If it is   *
 * undefined, undocumented opcodes just act as NOPs. *
 *                                                   *
 * The other define is "NES_CPU", which causes the   *
 * code to compile without support for binary-coded  *
 * decimal (BCD) support for the ADC and SBC         *
 * opcodes. The Ricoh 2A03 CPU in the NES does not   *
 * support BCD, but is otherwise identical to the    *
 * standard MOS 6502. (Note that this define is      *
 * enabled in this file if you haven't changed it    *
 * yourself. If you're not emulating a NES, you      *
 * should comment it out.)                           *
 *                                                   *
 * If you do discover an error in timing accuracy,   *
 * or operation in general please e-mail me at the   *
 * address above so that I can fix it. Thank you!    *
 *                                                   *
 *****************************************************
 * Usage:                                            *
 *                                                   *
 * Fake6502 requires you to provide two external     *
 * functions:                                        *
 *                                                   *
 * uint8 read6502(ushort address)                    *
 * void write6502(ushort address, uint8 value)       *
 *                                                   *
 * You may optionally pass Fake6502 the pointer to a *
 * function which you want to be called after every  *
 * emulated instruction. This function should be a   *
 * void with no parameters expected to be passed to  *
 * it.                                               *
 *                                                   *
 * This can be very useful. For example, in a NES    *
 * emulator, you check the number of clock ticks     *
 * that have passed so you can know when to handle   *
 * APU events.                                       *
 *                                                   *
 * To pass Fake6502 this pointer, use the            *
 * hookexternal(void *funcptr) function provided.    *
 *                                                   *
 * To disable the hook later, pass NULL to it.       *
 *****************************************************
 * Useful functions in this emulator:                *
 *                                                   *
 * void reset6502()                                  *
 *   - Call this once before you begin execution.    *
 *                                                   *
 * uint32 exec6502(uint32 tickcount)                 *
 *   - Execute 6502 code up to the next specified    *
 *     count of clock ticks.                         *
 *                                                   *
 * uint32 step6502()                                   *
 *   - Execute a single instrution.                  *
 *                                                   *
 * void irq6502()                                    *
 *   - Trigger a hardware IRQ in the 6502 core.      *
 *                                                   *
 * void nmi6502()                                    *
 *   - Trigger an NMI in the 6502 core.              *
 *                                                   *
 * void hookexternal(void *funcptr)                  *
 *   - Pass a pointer to a void function taking no   *
 *     parameters. This will cause Fake6502 to call  *
 *     that function once after each emulated        *
 *     instruction.                                  *
 *                                                   *
 *****************************************************
 * Useful variables in this emulator:                *
 *                                                   *
 * uint32 clockticks6502                             *
 *   - A running total of the emulated cycle count   *
 *     during a call to exec6502.                    *
 * uint32 instructions                               *
 *   - A running total of the total emulated         *
 *     instruction count. This is not related to     *
 *     clock cycle timing.                           *
 *                                                   *
 *****************************************************/


/*
	6510 EMULATION NOTES:
	1) On the 6510 processor, the only difference is that the addresses 0 and 1 are used
	for data direction and data, respectively.

	2) The initial value of address 0 should always be 0.

	3) Read this page
	https://ist.uwaterloo.ca/~schepers/MJK/6510.html

*/

#include <stdio.h>
#ifdef FAKE6502_USE_STDINT
#include <stdint.h>
typedef uint16_t ushort;
typedef unsigned char uint8;
typedef uint32_t uint32
#else
typedef unsigned short ushort ;
typedef unsigned char uint8;

#ifdef FAKE6502_USE_LONG
typedef unsigned long uint32;
#else
typedef unsigned int uint32;
#endif

#endif
/*
	when this is defined, undocumented opcodes are handled.
	otherwise, they're simply treated as NOPs.
*/
#define UNDOCUMENTED

/*
* #define NES_CPU
* when this is defined, the binary-coded decimal (BCD)
* status flag is not honored by ADC and SBC. the 2A03
* CPU in the Nintendo Entertainment System does not
* support BCD operation.
*/


#define FLAG_CARRY     0x01
#define FLAG_ZERO      0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_DECIMAL   0x08
/*bits 4 and 5.*/
#define FLAG_BREAK     0x10
#define FLAG_CONSTANT  0x20
#define FLAG_OVERFLOW  0x40
#define FLAG_SIGN      0x80

#define BASE_STACK     0x100

#define saveaccum(n) a = (uint8)((n) & 0x00FF)


/*flag modifier macros*/
#define setcarry() status |= FLAG_CARRY
#define clearcarry() status &= (~FLAG_CARRY)
#define setzero() status |= FLAG_ZERO
#define clearzero() status &= (~FLAG_ZERO)
#define setinterrupt() status |= FLAG_INTERRUPT
#define clearinterrupt() status &= (~FLAG_INTERRUPT)
#define setdecimal() status |= FLAG_DECIMAL
#define cleardecimal() status &= (~FLAG_DECIMAL)
#define setoverflow() status |= FLAG_OVERFLOW
#define clearoverflow() status &= (~FLAG_OVERFLOW)
#define setsign() status |= FLAG_SIGN
#define clearsign() status &= (~FLAG_SIGN)


/*flag calculation macros*/
#define zerocalc(n) {\
    if ((n) & 0x00FF) clearzero();\
        else setzero();\
}

#define signcalc(n) {\
    if ((n) & 0x0080) setsign();\
        else clearsign();\
}

#define carrycalc(n) {\
    if ((n) & 0xFF00) setcarry();\
        else clearcarry();\
}


#define overflowcalc(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n) ^ (ushort)(m)) & ((n) ^ (o)) & 0x0080) setoverflow();\
        else clearoverflow();\
}


#ifdef FAKE6502_NOT_STATIC
/*6502 CPU registers*/
ushort pc;
uint8 sp, a, x, y, status;
/*helper variables*/
uint32 instructions = 0; 
uint32 clockticks6502 = 0;
uint32 clockgoal6502 = 0;
ushort oldpc, ea, reladdr, value, result;
uint8 opcode, oldstatus, waiting6502 = 0;
void reset6502(void);
void nmi6502(void);
void irq6502(void);
void irq6502(void);
uint32 exec6502(uint32 tickcount);
uint32 step6502(void);
void hookexternal(void *funcptr);
#else
static ushort pc;
static uint8 sp, a, x, y, status;
static uint32 instructions = 0; 
static uint32 clockticks6502 = 0;
static uint32 clockgoal6502 = 0; 
static ushort oldpc, ea, reladdr, value, result;
static uint8 opcode, waiting6502 = 0;
#endif
/*externally supplied functions*/
extern uint8 read6502(ushort address);
extern void write6502(ushort address, uint8 value);

#ifndef FAKE6502_INCLUDE

/*a few general functions used by various other functions*/
static void push_6502_16(ushort pushval) {
    write6502(BASE_STACK + sp, (pushval >> 8) & 0xFF);
    write6502(BASE_STACK + ((sp - 1) & 0xFF), pushval & 0xFF);
    sp -= 2;
}

static void push_6502_8(uint8 pushval) {
    write6502(BASE_STACK + sp--, pushval);
}

static ushort pull_6502_16(void) {
    ushort temp16;
    temp16 = read6502(BASE_STACK + ((sp + 1) & 0xFF)) | ((ushort)read6502(BASE_STACK + ((sp + 2) & 0xFF)) << 8);
    sp += 2;
    return(temp16);
}

static uint8 pull_6502_8(void) {
    return (read6502(BASE_STACK + ++sp));
}

static ushort mem_6502_read16(ushort addr) {
    return ((ushort)read6502(addr) |
            ((ushort)read6502(addr + 1) << 8));
}

void reset6502(void) {
	/*
	    pc = (ushort)read6502(0xFFFC) | ((ushort)read6502(0xFFFD) << 8);
	    a = 0;
	    x = 0;
	    y = 0;
	    sp = 0xFD;
	    status |= FLAG_CONSTANT;
    */
    pc = mem_6502_read16(0xfffc);
    a = 0;
    x = 0;
    y = 0;
    sp = 0xFD;
    cleardecimal();
    status |= FLAG_CONSTANT;
    setinterrupt();
}


static void (*addrtable[256])(void);
static void (*optable[256])(void);
static uint8 penaltyop, penaltyaddr;

/*addressing mode functions, calculates effective addresses*/
static void imp(void) { 
}

/*addressing mode functions, calculates effective addresses*/
static void acc(void) { 
}

/*addressing mode functions, calculates effective addresses*/
static void imm(void) { 
    ea = pc++;
}

static void zp(void) { /*zero-page*/
    ea = (ushort)read6502((ushort)pc++);
}

static void zpx(void) { /*zero-page,X*/
    ea = ((ushort)read6502((ushort)pc++) + (ushort)x) & 0xFF; /*zero-page wraparound*/
}

static void zpy(void) { /*zero-page,Y*/
    ea = ((ushort)read6502((ushort)pc++) + (ushort)y) & 0xFF; /*zero-page wraparound*/
}

static void rel(void) { /*relative for branch ops (8-bit immediate value, sign-extended)*/
    reladdr = (ushort)read6502(pc++);
    if (reladdr & 0x80) reladdr |= 0xFF00;
}

static void abso(void) { /*absolute*/
    ea = (ushort)read6502(pc) | ((ushort)read6502(pc+1) << 8);
    pc += 2;
}

static void absx(void) { /*absolute,X*/
    ushort startpage;
    ea = ((ushort)read6502(pc) | ((ushort)read6502(pc+1) << 8));
    startpage = ea & 0xFF00;
    ea += (ushort)x;

    if (startpage != (ea & 0xFF00)) { /*one cycle penlty for page-crossing on some opcodes*/
        penaltyaddr = 1;
    }

    pc += 2;
}

static void absy(void) { /*absolute,Y*/
    ushort startpage;
    ea = ((ushort)read6502(pc) | ((ushort)read6502(pc+1) << 8));
    startpage = ea & 0xFF00;
    ea += (ushort)y;

    if (startpage != (ea & 0xFF00)) { /*one cycle penlty for page-crossing on some opcodes*/
        penaltyaddr = 1;
    }

    pc += 2;
}

static void ind(void) { /*indirect*/
    ushort eahelp, eahelp2;
    eahelp = (ushort)read6502(pc) | (ushort)((ushort)read6502(pc+1) << 8);
    /*Page boundary bug is absent on CMOS models.*/
    eahelp2 = (eahelp+1) & 0xffFF;
    ea = (ushort)read6502(eahelp) | ((ushort)read6502(eahelp2) << 8);
    pc += 2;
}

static void indx(void) { /* (indirect,X)*/
    ushort eahelp;
    eahelp = (ushort)(((ushort)read6502(pc++) + (ushort)x) & 0xFF); /*zero-page wraparound for table pointer*/
    ea = (ushort)read6502(eahelp & 0x00FF) | ((ushort)read6502((eahelp+1) & 0x00FF) << 8);
}

static void indy(void) { /* (indirect),Y*/
    ushort eahelp, eahelp2, startpage;
    eahelp = (ushort)read6502(pc++);
    eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); /*zero-page wraparound*/
    ea = (ushort)read6502(eahelp) | ((ushort)read6502(eahelp2) << 8);
    startpage = ea & 0xFF00;
    ea += (ushort)y;

    if (startpage != (ea & 0xFF00)) { /*one cycle penlty for page-crossing on some opcodes*/
        penaltyaddr = 1;
    }
}

static void zprel(void) { /* zero-page, relative for branch ops (8-bit immediate value, sign-extended)*/
	ea = (ushort)read6502(pc);
	reladdr = (ushort)read6502(pc+1);
	if (reladdr & 0x80) reladdr |= 0xFF00;

	pc += 2;
}

static ushort getvalue(void) {
    if (addrtable[opcode] == acc) return((ushort)a);
    else return((ushort)read6502(ea));
}

//static ushort getvalue16(void) {
//    return((ushort)read6502(ea) | ((ushort)read6502(ea+1) << 8));
//}

static void putvalue(ushort saveval) {
    if (addrtable[opcode] == acc) a = (uint8)(saveval & 0x00FF);
    else write6502(ea, (saveval & 0x00FF));
}


/*instruction handler functions*/
static void adc(void) {
    penaltyop = 1;
    if (status & FLAG_DECIMAL) {
        ushort AL, A  /*, result_dec */;
        A = a;
        value = getvalue();
        /*result_dec = (ushort)A + value + (ushort)(status & FLAG_CARRY); dec*/
        AL = (A & 0x0F) + (value & 0x0F) + (ushort)(status & FLAG_CARRY); /*SEQ 1A or 2A*/
        if(AL >= 0xA) AL = ((AL + 0x06) & 0x0F) + 0x10; /*1B or 2B*/
        A = (A & 0xF0) + (value & 0xF0) + AL; /*1C or 2C*/
        if(A >= 0xA0) A += 0x60; /*1E*/
        result = A; /*1F*/
        if(A & 0xff80) setoverflow(); else clearoverflow();
        if(A >= 0x100) setcarry(); else clearcarry(); /*SEQ 1G*/
        zerocalc(result);                /* 65C02 change, Decimal Arithmetic sets NZV */
        signcalc(result);
        clockticks6502++;
    } else {
        value = getvalue();
        result = (ushort)a + value + (ushort)(status & FLAG_CARRY);

        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
    }
    saveaccum(result);
}

static void and(void) {
    penaltyop = 1;
    value = getvalue();
    result = (ushort)a & value;
   
    zerocalc(result);
    signcalc(result);
   
    saveaccum(result);
}

static void asl(void) {
    value = getvalue();
    result = value << 1;

    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void bcc(void) {
    if ((status & FLAG_CARRY) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; /*check if jump crossed a page boundary*/
            else clockticks6502++;
    }
}

static void bcs(void) {
    if ((status & FLAG_CARRY) == FLAG_CARRY) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; /*check if jump crossed a page boundary*/
            else clockticks6502++;
    }
}

static void beq(void) {
    if ((status & FLAG_ZERO) == FLAG_ZERO) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; /*check if jump crossed a page boundary*/
            else clockticks6502++;
    }
}

static void bit(void) {
    value = getvalue();
    result = (ushort)a & value;
    zerocalc(result);
    status = (status & 0x3F) | (uint8)(value & 0xC0);
}

static void bit_imm(void) {
    value = getvalue();
    result = (ushort)a & value;
    zerocalc(result);
}

static void bmi(void) {
    if ((status & FLAG_SIGN) == FLAG_SIGN) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; /*check if jump crossed a page boundary*/
            else clockticks6502++;
    }
}

static void bne(void) {
    if ((status & FLAG_ZERO) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; /*check if jump crossed a page boundary*/
            else clockticks6502++;
    }
}

static void bpl(void) {
    if ((status & FLAG_SIGN) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; /*check if jump crossed a page boundary*/
            else clockticks6502++;
    }
}

static void brk_6502(void) {
    pc++;
    push_6502_16(pc);
    push_6502_8(status | FLAG_BREAK);
    setinterrupt();
    cleardecimal(); /*CMOS change*/
    pc = (ushort)read6502(0xFFFE) | ((ushort)read6502(0xFFFF) << 8);
}

static void bvc(void) {
    if ((status & FLAG_OVERFLOW) == 0) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; /*check if jump crossed a page boundary*/
            else clockticks6502++;
    }
}

static void bvs(void) {
    if ((status & FLAG_OVERFLOW) == FLAG_OVERFLOW) {
        oldpc = pc;
        pc += reladdr;
        if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; /*check if jump crossed a page boundary*/
            else clockticks6502++;
    }
}

static void clc(void) {
    clearcarry();
}

static void cld(void) {
    cleardecimal();
}

static void cli(void) {
    clearinterrupt();
}

static void clv(void) {
    clearoverflow();
}

static void cmp(void) {
    penaltyop = 1;
    value = getvalue();
    result = (ushort)a - value;
   
    if (a >= (uint8)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (a == (uint8)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpx(void) {
    value = getvalue();
    result = (ushort)x - value;
   
    if (x >= (uint8)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (x == (uint8)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpy(void) {
    value = getvalue();
    result = (ushort)y - value;
   
    if (y >= (uint8)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (y == (uint8)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void dec(void) {
    value = getvalue();
    result = value - 1;
   
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void dex(void) {
    x--;
   
    zerocalc(x);
    signcalc(x);
}

static void dey(void) {
    y--;
   
    zerocalc(y);
    signcalc(y);
}

static void eor(void) {
    penaltyop = 1;
    value = getvalue();
    result = (ushort)a ^ value;
   
    zerocalc(result);
    signcalc(result);
   
    saveaccum(result);
}

static void inc(void) {
    value = getvalue();
    result = value + 1;
   
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void inx(void) {
    x++;
   
    zerocalc(x);
    signcalc(x);
}

static void iny(void) {
    y++;
   
    zerocalc(y);
    signcalc(y);
}

static void jmp(void) {
    pc = ea;
    /*if(opcode == 0x6c) clockticks6502++;*/
}

static void jsr(void) {
    push_6502_16(pc - 1);
    pc = ea;
}

static void lda(void) {
    penaltyop = 1;
    value = getvalue();
    a = (uint8)(value & 0x00FF);
   
    zerocalc(a);
    signcalc(a);
}

static void ldx(void) {
    penaltyop = 1;
    value = getvalue();
    x = (uint8)(value & 0x00FF);
   
    zerocalc(x);
    signcalc(x);
}

static void ldy(void) {
    penaltyop = 1;
    value = getvalue();
    y = (uint8)(value & 0x00FF);
   
    zerocalc(y);
    signcalc(y);
}

static void lsr(void) {
    value = getvalue();
    result = value >> 1;
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void nop(void) {
    switch (opcode) {
        case 0x1C:
        case 0x3C:
        case 0x5C:
        case 0x7C:
        case 0xDC:
        case 0xFC:
            penaltyop = 1;
            break;
    }
}

static void ora(void) {
    penaltyop = 1;
    value = getvalue();
    result = (ushort)a | value;
   
    zerocalc(result);
    signcalc(result);
   
    saveaccum(result);
}

static void pha(void) {
    push_6502_8(a);
}

static void php(void) {
    push_6502_8(status | FLAG_BREAK);
}

static void pla(void) {
    a = pull_6502_8();
   
    zerocalc(a);
    signcalc(a);
}

static void plp(void) {
    status = pull_6502_8() | FLAG_CONSTANT;
}

static void rol(void) {
    value = getvalue();
    result = (value << 1) | (status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void ror(void) {
    value = getvalue();
    result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    putvalue(result);
}

static void rti(void) {
    status = pull_6502_8();
    value = pull_6502_16();
    pc = value;
}

static void rts(void) {
    value = pull_6502_16();
    pc = value + 1;
}

static void sbc(void) {
    penaltyop = 1;
    if (status & FLAG_DECIMAL) {
    	ushort result_dec, A, AL, B, C;
    	A = a;
    	C = (ushort)(status & FLAG_CARRY);
     	value = getvalue(); B = value; value = value ^ 0x00FF;
    	result_dec = (ushort)a + value + C;
		/*Both Cmos and Nmos*/
    	carrycalc(result_dec); 
    	overflowcalc(result_dec, a, value); 
		/*SEQUENCE 4 IS CMOS ONLY*/
    	AL = (A & 0x0F) - (B & 0x0F) + C - 1; /*4a*/
    	A = A - B + C - 1; /*4b*/
    	if(A & 0x8000) A = A - 0x60; /*4C*/
    	if(AL & 0x8000) A = A - 0x06; /*4D*/
    	result = A & 0xff; /*4E*/
    	signcalc(result);
    	zerocalc(result);
        clockticks6502++;
    } else {
        value = getvalue() ^ 0x00FF;
        result = (ushort)a + value + (ushort)(status & FLAG_CARRY);
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
    }
    saveaccum(result);
}

static void sec(void) {
    setcarry();
}

static void sed(void) {
    setdecimal();
}

static void sei(void) {
    setinterrupt();
}

static void sta(void) {
    putvalue(a);
}

static void stx(void) {
    putvalue(x);
}

static void sty(void) {
    putvalue(y);
}

static void tax(void) {
    x = a;
   
    zerocalc(x);
    signcalc(x);
}

static void tay(void) {
    y = a;
   
    zerocalc(y);
    signcalc(y);
}

static void tsx(void) {
    x = sp;
   
    zerocalc(x);
    signcalc(x);
}

static void txa(void) {
    a = x;
   
    zerocalc(a);
    signcalc(a);
}

static void txs(void) {
    sp = x;
}

static void tya(void) {
    a = y;
   
    zerocalc(a);
    signcalc(a);
}


/*
		CMOS ADDITIONS
*/
static void ind0(void) {
    ushort eahelp, eahelp2;
    eahelp = (ushort)read6502(pc++);
    eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); /*zero page wrap*/
    ea = (ushort)read6502(eahelp) | ((ushort)read6502(eahelp2) << 8);
}

static void ainx(void) { 		/* abs indexed bra*/
    ushort eahelp, eahelp2;
    eahelp = (ushort)read6502(pc) | (ushort)((ushort)read6502(pc+1) << 8);
    eahelp = (eahelp + (ushort)x) & 0xFFFF;
    eahelp2 = eahelp + 1; /*No bug on CMOS*/
    ea = (ushort)read6502(eahelp) | ((ushort)read6502(eahelp2) << 8);
    pc += 2;
}

static void stz(void){
	putvalue(0);
}

static void bra(void) {
    oldpc = pc;
    pc += reladdr;
    if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; /*page boundary*/
        else clockticks6502++;
}


static void phx(void) {
    push_6502_8(x);
}

static void plx(void) {
    x = pull_6502_8();
   
    zerocalc(x);
    signcalc(x);
}

static void phy(void) {
    push_6502_8(y);
}

static void ply(void) {
    y = pull_6502_8();
  
    zerocalc(y);
    signcalc(y);
}


static void tsb(void) {
    value = getvalue();
    result = (ushort)a & value;
    zerocalc(result);
    result = value | a;
    putvalue(result);
}

static void trb(void) {
    value = getvalue();
    result = (ushort)a & value;
    zerocalc(result);
    result = value & (a ^ 0xFF);
    putvalue(result);
}

static void db6502(void){
	pc--; /*This is how we wait until RESET.*/
	return;
}

static void wai(void) {
	if (~status & FLAG_INTERRUPT) waiting6502 = 1;
}
static void bbr(ushort bitmask)
{
	if ((getvalue() & bitmask) == 0) {
		oldpc = pc;
		pc += reladdr;
		if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; /*check if jump crossed a page boundary*/
		else clockticks6502++;
	}
}
static void bbr0(void) {bbr(0x01);}
static void bbr1(void) {bbr(0x02);}
static void bbr2(void) {bbr(0x04);}
static void bbr3(void) {bbr(0x08);}
static void bbr4(void) {bbr(0x10);}
static void bbr5(void) {bbr(0x20);}
static void bbr6(void) {bbr(0x40);}
static void bbr7(void) {bbr(0x80);}

static void bbs(ushort bitmask)
{
	if ((getvalue() & bitmask) != 0) {
		oldpc = pc;
		pc += reladdr;
		if ((oldpc & 0xFF00) != (pc & 0xFF00)) clockticks6502 += 2; /*check if jump crossed a page boundary*/
		else clockticks6502++;
	}
}
static void bbs0(void) {bbs(0x01);}
static void bbs1(void) {bbs(0x02);}
static void bbs2(void) {bbs(0x04);}
static void bbs3(void) {bbs(0x08);}
static void bbs4(void) {bbs(0x10);}
static void bbs5(void) {bbs(0x20);}
static void bbs6(void) {bbs(0x40);}
static void bbs7(void) {bbs(0x80);}


static void smb0(void) { putvalue(getvalue() | 0x01); }
static void smb1(void) { putvalue(getvalue() | 0x02); }
static void smb2(void) { putvalue(getvalue() | 0x04); }
static void smb3(void) { putvalue(getvalue() | 0x08); }
static void smb4(void) { putvalue(getvalue() | 0x10); }
static void smb5(void) { putvalue(getvalue() | 0x20); }
static void smb6(void) { putvalue(getvalue() | 0x40); }
static void smb7(void) { putvalue(getvalue() | 0x80); }

static void rmb0(void) { putvalue(getvalue() & ~0x01); }
static void rmb1(void) { putvalue(getvalue() & ~0x02); }
static void rmb2(void) { putvalue(getvalue() & ~0x04); }
static void rmb3(void) { putvalue(getvalue() & ~0x08); }
static void rmb4(void) { putvalue(getvalue() & ~0x10); }
static void rmb5(void) { putvalue(getvalue() & ~0x20); }
static void rmb6(void) { putvalue(getvalue() & ~0x40); }
static void rmb7(void) { putvalue(getvalue() & ~0x80); }

/*undocumented instructions~~~~~~~~~~~~~~~~~~~~~~~~~*/
/*
	#define lax nop
	#define sax nop
	#define dcp nop
	#define isb nop
	#define slo nop
	#define rla nop
	#define sre nop
	#define rra nop
*/

static void (*addrtable[256])(void) = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
/* 0 */     imp, indx,  imp,  imp,   zp,   zp,   zp,   zp,  imp,  imm,  acc,  imp, abso, abso, abso,zprel, /* 0 */
/* 1 */     rel, indy, ind0,  imp,   zp,  zpx,  zpx,   zp,  imp, absy,  acc,  imp, abso, absx, absx,zprel, /* 1 */
/* 2 */    abso, indx,  imp,  imp,   zp,   zp,   zp,   zp,  imp,  imm,  acc,  imp, abso, abso, abso,zprel, /* 2 */
/* 3 */     rel, indy, ind0,  imp,  zpx,  zpx,  zpx,   zp,  imp, absy,  acc,  imp, absx, absx, absx,zprel, /* 3 */
/* 4 */     imp, indx,  imp,  imp,  imp,   zp,   zp,   zp,  imp,  imm,  acc,  imp, abso, abso, abso,zprel, /* 4 */
/* 5 */     rel, indy, ind0,  imp,  imp,  zpx,  zpx,   zp,  imp, absy,  imp,  imp,  imp, absx, absx,zprel, /* 5 */
/* 6 */     imp, indx,  imp,  imp,   zp,   zp,   zp,   zp,  imp,  imm,  acc,  imp,  ind, abso, abso,zprel, /* 6 */
/* 7 */     rel, indy, ind0,  imp,  zpx,  zpx,  zpx,   zp,  imp, absy,  imp,  imp, ainx, absx, absx,zprel, /* 7 */
/* 8 */     rel, indx,  imp,  imp,   zp,   zp,   zp,   zp,  imp,  imm,  imp,  imp, abso, abso, abso,zprel, /* 8 */
/* 9 */     rel, indy, ind0,  imp,  zpx,  zpx,  zpy,   zp,  imp, absy,  imp,  imp, abso, absx, absx,zprel, /* 9 */
/* A */     imm, indx,  imm,  imp,   zp,   zp,   zp,   zp,  imp,  imm,  imp,  imp, abso, abso, abso,zprel, /* A */
/* B */     rel, indy, ind0,  imp,  zpx,  zpx,  zpy,   zp,  imp, absy,  imp,  imp, absx, absx, absy,zprel, /* B */
/* C */     imm, indx,  imp,  imp,   zp,   zp,   zp,   zp,  imp,  imm,  imp,  imp, abso, abso, abso,zprel, /* C */
/* D */     rel, indy, ind0,  imp,  imp,  zpx,  zpx,   zp,  imp, absy,  imp,  imp,  imp, absx, absx,zprel, /* D */
/* E */     imm, indx,  imp,  imp,   zp,   zp,   zp,   zp,  imp,  imm,  imp,  imp, abso, abso, abso,zprel, /* E */
/* F */     rel, indy, ind0,  imp,  imp,  zpx,  zpx,   zp,  imp, absy,  imp,  imp,  imp, absx, absx,zprel  /* F */
};

/*
	NOTE: the "db6502" instruction is *supposed* to be "wait until hardware reset"
*/

static void (*optable[256])(void) = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
/* 0 */      brk_6502,  ora,  nop,  nop,  tsb,  ora,  asl, rmb0,  php,  ora,  asl,  nop,  tsb,  ora,  asl, bbr0, /* 0 */
/* 1 */      bpl,  ora,  ora,  nop,  trb,  ora,  asl, rmb1,  clc,  ora,  inc,  nop,  trb,  ora,  asl, bbr1, /* 1 */
/* 2 */      jsr,  and,  nop,  nop,  bit,  and,  rol, rmb2,  plp,  and,  rol,  nop,  bit,  and,  rol, bbr2, /* 2 */
/* 3 */      bmi,  and,  and,  nop,  bit,  and,  rol, rmb3,  sec,  and,  dec,  nop,  bit,  and,  rol, bbr3, /* 3 */
/* 4 */      rti,  eor,  nop,  nop,  nop,  eor,  lsr, rmb4,  pha,  eor,  lsr,  nop,  jmp,  eor,  lsr, bbr4, /* 4 */
/* 5 */      bvc,  eor,  eor,  nop,  nop,  eor,  lsr, rmb5,  cli,  eor,  phy,  nop,  nop,  eor,  lsr, bbr5, /* 5 */
/* 6 */      rts,  adc,  nop,  nop,  stz,  adc,  ror, rmb6,  pla,  adc,  ror,  nop,  jmp,  adc,  ror, bbr6, /* 6 */
/* 7 */      bvs,  adc,  adc,  nop,  stz,  adc,  ror, rmb7,  sei,  adc,  ply,  nop,  jmp,  adc,  ror, bbr7, /* 7 */
/* 8 */      bra,  sta,  nop,  nop,  sty,  sta,  stx, smb0,  dey,  bit_imm,  txa,  nop,  sty,  sta,  stx, bbs0, /* 8 */
/* 9 */      bcc,  sta,  sta,  nop,  sty,  sta,  stx, smb1,  tya,  sta,  txs,  nop,  stz,  sta,  stz, bbs1, /* 9 */
/* A */      ldy,  lda,  ldx,  nop,  ldy,  lda,  ldx, smb2,  tay,  lda,  tax,  nop,  ldy,  lda,  ldx, bbs2, /* A */
/* B */      bcs,  lda,  lda,  nop,  ldy,  lda,  ldx, smb3,  clv,  lda,  tsx,  nop,  ldy,  lda,  ldx, bbs3, /* B */
/* C */      cpy,  cmp,  nop,  nop,  cpy,  cmp,  dec, smb4,  iny,  cmp,  dex,  wai,  cpy,  cmp,  dec, bbs4, /* C */
/* D */      bne,  cmp,  cmp,  nop,  nop,  cmp,  dec, smb5,  cld,  cmp,  phx,  db6502,  nop,  cmp,  dec, bbs5, /* D */
/* E */      cpx,  sbc,  nop,  nop,  cpx,  sbc,  inc, smb6,  inx,  sbc,  nop,  nop,  cpx,  sbc,  inc, bbs6, /* E */
/* F */      beq,  sbc,  sbc,  nop,  nop,  sbc,  inc, smb7,  sed,  sbc,  plx,  nop,  nop,  sbc,  inc, bbs7  /* F */
};

static const uint32 ticktable[256] = {
/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
/* 0 */      7,    6,    2,    2,    5,    3,    5,    5,    3,    2,    2,    2,    6,    4,    6,    2, /* 0 */
/* 1 */      2,    5,    5,    2,    5,    4,    6,    5,    2,    4,    2,    2,    6,    4,    7,    2, /* 1 */
/* 2 */      6,    6,    2,    2,    3,    3,    5,    5,    4,    2,    2,    2,    4,    4,    6,    2, /* 2 */
/* 3 */      2,    5,    5,    2,    4,    4,    6,    5,    2,    4,    2,    2,    4,    4,    7,    2, /* 3 */
/* 4 */      6,    6,    2,    2,    2,    3,    5,    5,    3,    2,    2,    2,    3,    4,    6,    2, /* 4 */
/* 5 */      2,    5,    5,    2,    2,    4,    6,    5,    2,    4,    3,    2,    2,    4,    7,    2, /* 5 */
/* 6 */      6,    6,    2,    2,    3,    3,    5,    5,    4,    2,    2,    2,    6,    4,    6,    2, /* 6 */
/* 7 */      2,    5,    5,    2,    4,    4,    6,    5,    2,    4,    4,    2,    6,    4,    7,    2, /* 7 */
/* 8 */      3,    6,    2,    2,    3,    3,    3,    5,    2,    2,    2,    2,    4,    4,    4,    2, /* 8 */
/* 9 */      2,    6,    5,    2,    4,    4,    4,    5,    2,    5,    2,    2,    4,    5,    5,    2, /* 9 */
/* A */      2,    6,    2,    2,    3,    3,    3,    5,    2,    2,    2,    2,    4,    4,    4,    2, /* A */
/* B */      2,    5,    5,    2,    4,    4,    4,    5,    2,    4,    2,    2,    4,    4,    4,    2, /* B */
/* C */      2,    6,    2,    2,    3,    3,    5,    5,    2,    2,    2,    3,    4,    4,    6,    2, /* C */
/* D */      2,    5,    5,    2,    2,    4,    6,    5,    2,    4,    3,    1,    2,    4,    7,    2, /* D */
/* E */      2,    6,    2,    2,    3,    3,    5,    5,    2,    2,    2,    2,    4,    4,    6,    2, /* E */
/* F */      2,    5,    5,    2,    2,    4,    6,    5,    2,    4,    4,    2,    2,    4,    7,    2  /* F */
};


void nmi6502(void) {
    push_6502_16(pc);
    push_6502_8(status  & ~FLAG_BREAK);
    setinterrupt();
    cleardecimal();
    pc = (ushort)read6502(0xFFFA) | ((ushort)read6502(0xFFFB) << 8);
    waiting6502 = 0;
}

void irq6502(void) {
	/*
	    push_6502_16(pc);
	    push_6502_8(status);
	    status |= FLAG_INTERRUPT;
	    pc = (ushort)read6502(0xFFFE) | ((ushort)read6502(0xFFFF) << 8);
    */
	if ((status & FLAG_INTERRUPT) == 0) {
		push_6502_16(pc);
		push_6502_8(status & ~FLAG_BREAK);
		setinterrupt();
		cleardecimal();
		/*pc = mem_6502_read16(0xfffe);*/
		pc = (ushort)read6502(0xFFFE) | ((ushort)read6502(0xFFFF) << 8);
		waiting6502 = 0;
	}
	
}

uint8 callexternal = 0;
void (*loopexternal)(void);

uint32 exec6502(uint32 tickcount) {
	/*
		BUG FIX:
		overflow of unsigned 32 bit integer causes emulation to hang.
		An instruction might cause the tick count to wrap around into the billions.

		The system is changed so that now clockticks 6502 is reset every single time that exec is called.
	*/
	if(waiting6502) return tickcount;
    clockgoal6502 = tickcount;
    clockticks6502 = 0;
    while (clockticks6502 < clockgoal6502) {
        opcode = read6502(pc++);
        status |= FLAG_CONSTANT;
        penaltyop = 0;
        penaltyaddr = 0;
       	(*addrtable[opcode])();
        (*optable[opcode])();
        clockticks6502 += ticktable[opcode];
        if (penaltyop && penaltyaddr) {clockticks6502++;}
        instructions++;
        if (callexternal) (*loopexternal)();
    }
	return clockticks6502;
}

uint32 step6502(void) {
	if(waiting6502) return 1;
    opcode = read6502(pc++);
    status |= FLAG_CONSTANT;

    penaltyop = 0;
    penaltyaddr = 0;
	clockticks6502 = 0;
    (*addrtable[opcode])();
    (*optable[opcode])();
    clockticks6502 += ticktable[opcode];
    /*The following line goes commented out in Mike Chamber's usage of the 6502 emulator for MOARNES*/
    if (penaltyop && penaltyaddr) clockticks6502++;
    /*clockgoal6502 = clockticks6502; irrelevant.*/ 

    instructions++;

    if (callexternal) (*loopexternal)();
    return clockticks6502;
}

void hookexternal(void (*funcptr)(void)) {
    if (funcptr != NULL) {
        loopexternal = funcptr;
        callexternal = 1;
    } else callexternal = 0;
}

/*
	Check all changes against
	http://6502.org/tutorials/65c02opcodes.html
	and 
	https://github.com/commanderx16/x16-emulator

	The commander X16 emulator has bugs, but it seems to be reasonably solid.
*/
/*FAKE6502 INCLUDE*/
#endif
