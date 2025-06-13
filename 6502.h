#ifndef __6502_H__
#define __6502_H__

#define UNDOCUMENTED

extern void init6502(void);
extern void reset6502(void);
extern void nmi6502(void);
extern void irq6502(void);
extern uint64_t exec6502(uint64_t tickcount);
extern void step6502(void);
extern void hookexternal(void (*loopexternal)(void));
extern uint16_t getPC(void);
extern uint64_t getclockticks(void);
extern void waitstates(uint32_t n);
#define SAVE_SIZE 7
extern void save6502(uint8_t *save);
extern void load6502(uint8_t *save);

//externally supplied functions
extern uint8_t read6502(uint16_t address);
extern uint8_t read6502_debug(uint16_t address);
extern void write6502(uint16_t address, uint8_t value);

extern int log_6502;
extern uint8_t mempage;

#ifdef _6502_PRIVATE

extern void disassembler_init(void);
extern char *dis6502(uint16_t addr, uint8_t *p);


extern uint8_t sp, a, x, y, status;
extern uint16_t pc;

//6502 defines
#define UNDOCUMENTED //when this is defined, undocumented opcodes are handled.
		     //otherwise, they're simply treated as NOPs.

#undef NES_CPU	     //when this is defined, the binary-coded decimal (BCD)
		     //status flag is not honored by ADC and SBC. the 2A03
		     //CPU in the Nintendo Entertainment System does not
		     //support BCD operation.

#define FLAG_CARRY     0x01
#define FLAG_ZERO      0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_DECIMAL   0x08
#define FLAG_BREAK     0x10
#define FLAG_CONSTANT  0x20
#define FLAG_OVERFLOW  0x40
#define FLAG_SIGN      0x80

#define BASE_STACK     0x100

#define saveaccum(n) a = (uint8_t)((n) & 0x00FF)


//flag modifier macros
#define setcarry() status |= FLAG_CARRY
#define clearcarry() status &= (~FLAG_CARRY)
#define setzero() status |= FLAG_ZERO
#define clearzero() status &= (~FLAG_ZERO)
#define setinterrupt() status |= FLAG_INTERRUPT
#define clearinterrupt() status &= (~FLAG_INTERRUPT)
#define setdecimal() status |= FLAG_DECIMAL
#define cleardecimal() status &= (~FLAG_DECIMAL)
#define setbreak() status |= FLAG_BREAK
#define clearbreak() status &= (~FLAG_BREAK)
#define setoverflow() status |= FLAG_OVERFLOW
#define clearoverflow() status &= (~FLAG_OVERFLOW)
#define setsign() status |= FLAG_SIGN
#define clearsign() status &= (~FLAG_SIGN)


//flag calculation macros
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
	if (((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & 0x0080) setoverflow();\
		else clearoverflow();\
}
#endif
#endif
