/*
  Intel 8080 emulator in C
  Written by Mike Chambers, April 2018

  Use this code for whatever you want. I don't care. It's officially public domain.
  Credit would be appreciated.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "intel_8080_emulator.h"

#define ALLOW_UNDEFINED

static char *i8080_disassemble(uint16_t addr);

#define reg16_PSW (((uint16_t)reg8[A] << 8) | (uint16_t)reg8[FLAGS])
#define reg16_BC (((uint16_t)reg8[B] << 8) | (uint16_t)reg8[C])
#define reg16_DE (((uint16_t)reg8[D] << 8) | (uint16_t)reg8[E])
#define reg16_HL (((uint16_t)reg8[H] << 8) | (uint16_t)reg8[L])

uint8_t reg8[9], INTE = 0;
uint16_t reg_SP, reg_PC;

#define set_S() reg8[FLAGS] |= 0x80
#define set_Z() reg8[FLAGS] |= 0x40
#define set_AC() reg8[FLAGS] |= 0x10
#define set_P() reg8[FLAGS] |= 0x04
#define set_C() reg8[FLAGS] |= 0x01
#define clear_S() reg8[FLAGS] &= 0x7F
#define clear_Z() reg8[FLAGS] &= 0xBF
#define clear_AC() reg8[FLAGS] &= 0xEF
#define clear_P() reg8[FLAGS] &= 0xFB
#define clear_C() reg8[FLAGS] &= 0xFE
#define test_S() (reg8[FLAGS] & 0x80)
#define test_Z() (reg8[FLAGS] & 0x40)
#define test_AC() (reg8[FLAGS] & 0x10)
#define test_P() (reg8[FLAGS] & 0x04)
#define test_C() (reg8[FLAGS] & 0x01)

static unsigned halted;

FILE *i8080_log;

static uint8_t intpend;

static const uint8_t parity[0x100] = {
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};

uint16_t read_RP(uint8_t rp) {
	switch (rp) {
		case 0x00:
			return reg16_BC;
		case 0x01:
			return reg16_DE;
		case 0x02:
			return reg16_HL;
		case 0x03:
			return reg_SP;
	}
	return 0;
}

uint16_t read_RP_PUSHPOP(uint8_t rp) {
	switch (rp) {
		case 0x00:
			return reg16_BC;
		case 0x01:
			return reg16_DE;
		case 0x02:
			return reg16_HL;
		case 0x03:
			return (reg16_PSW | 0x02) & 0xFFD7;
	}
	return 0;
}

void write_RP(uint8_t rp, uint8_t lb, uint8_t hb) {
	switch (rp) {
		case 0x00:
			reg8[C] = lb;
			reg8[B] = hb;
			break;
		case 0x01:
			reg8[E] = lb;
			reg8[D] = hb;
			break;
		case 0x02:
			reg8[L] = lb;
			reg8[H] = hb;
			break;
		case 0x03:
			reg_SP = (uint16_t)lb | ((uint16_t)hb << 8);
			break;
	}
}

void write16_RP(uint8_t rp, uint16_t value) {
	switch (rp) {
		case 0x00:
			reg8[C] = value & 0x00FF;
			reg8[B] = value >> 8;
			break;
		case 0x01:
			reg8[E] = value & 0x00FF;
			reg8[D] = value >> 8;
			break;
		case 0x02:
			reg8[L] = value & 0x00FF;
			reg8[H] = value >> 8;
			break;
		case 0x03:
			reg_SP = value;
			break;
	}
}

void write16_RP_PUSHPOP(uint8_t rp, uint16_t value) {
	switch (rp) {
		case 0x00:
			reg8[C] = value & 0x00FF;
			reg8[B] = value >> 8;
			break;
		case 0x01:
			reg8[E] = value & 0x00FF;
			reg8[D] = value >> 8;
			break;
		case 0x02:
			reg8[L] = value & 0x00FF;
			reg8[H] = value >> 8;
			break;
		case 0x03:
			reg8[FLAGS] = ((value & 0x00FF) | 0x02) & 0xD7;
			reg8[A] = value >> 8;
			break;
	}
}

void calc_SZP(uint8_t value) {
	if (value == 0) set_Z(); else clear_Z();
	if (value & 0x80) set_S(); else clear_S();
	if (parity[value]) set_P(); else clear_P();
}

void calc_AC(uint8_t val1, uint8_t val2) {
	if (((val1 & 0x0F) + (val2 & 0x0F)) > 0x0F) {
		set_AC();
	} else {
		clear_AC();
	}
}

void calc_AC_carry(uint8_t val1, uint8_t val2) {
	if (((val1 & 0x0F) + (val2 & 0x0F)) >= 0x0F) {
		set_AC();
	} else {
		clear_AC();
	}
}

void calc_subAC(int8_t val1, uint8_t val2) {
	if ((val2 & 0x0F) <= (val1 & 0x0F)) {
		set_AC();
	} else {
		clear_AC();
	}
}

void calc_subAC_borrow(int8_t val1, uint8_t val2) {
	if ((val2 & 0x0F) < (val1 & 0x0F)) {
		set_AC();
	} else {
		clear_AC();
	}
}

uint8_t test_cond(uint8_t code) {
	switch (code) {
		case 0: //Z not set
			if (!test_Z()) return 1; else return 0;
		case 1: //Z set
			if (test_Z()) return 1; else return 0;
		case 2: //C not set
			if (!test_C()) return 1; else return 0;
		case 3: //C set
			if (test_C()) return 1; else return 0;
		case 4: //P not set
			if (!test_P()) return 1; else return 0;
		case 5: //P set
			if (test_P()) return 1; else return 0;
		case 6: //S not set
			if (!test_S()) return 1; else return 0;
		case 7: //S set
			if (test_S()) return 1; else return 0;
	}
	return 0;
}

void i8080_push(uint16_t value) {
	i8080_write(--reg_SP, value >> 8);
	i8080_write(--reg_SP, (uint8_t)value);
}

uint16_t i8080_pop(void) {
	uint16_t temp;
	temp = i8080_read(reg_SP++);
	temp |= (uint16_t)i8080_read(reg_SP++) << 8;
	return temp;
}

void i8080_set_int(int n)
{
	intpend |= n;
}

void i8080_clear_int(int n)
{
	intpend &= ~n;
}


void i8080_jump(uint16_t addr) {
	reg_PC = addr;
}

void i8080_reset(void) {
	reg_PC = reg_SP = 0x0000;
	//reg8[FLAGS] = 0x02;
}

void i8080_write_reg8(reg_t reg, uint8_t value) {
	if (reg == M) {
		i8080_write(reg16_HL, value);
	} else {
		reg8[reg] = value;
	}
}

uint8_t i8080_read_reg8(reg_t reg) {
	if (reg == M) {
		return i8080_read(reg16_HL);
	} else {
		return reg8[reg];
	}
}

uint16_t i8080_read_reg16(reg_t reg) {
	switch (reg) {
		case AF: return reg16_PSW;
		case BC: return reg16_BC;
		case DE: return reg16_DE;
		case HL: return reg16_HL;
		case SP: return reg_SP;
		case PC: return reg_PC;
		default:;
	}
	return 0;
}

void i8080_write_reg16(reg_t reg, uint16_t value) {
	switch (reg) {
		case AF: reg8[A] = value>>8; reg8[FLAGS] = value; break;
		case BC: reg8[B] = value>>8; reg8[C] = value; break;
		case DE: reg8[D] = value>>8; reg8[E] = value; break;
		case HL: reg8[H] = value>>8; reg8[L] = value; break;
		case SP: reg_SP = value; break;
		case PC: reg_PC = value; break;
		default:;
	}
}

static char *i8080_flags(uint8_t v)
{
	static char buf[9];
	char *fp = "SZKA-PVC";
	char *t = buf;

	strcpy(buf, "--------");

	while(*fp) {
		if (v & 0x80)
			*t = *fp;
		t++;
		fp++;
		v <<= 1;
	}
	return buf;
}

int i8080_exec(int cycles) {
	uint8_t opcode, temp8, reg, reg2;
	uint16_t temp16;
	uint32_t temp32;

	/* TODO: merge in from 8085 interrupt code */
	while (cycles > 0) {

		if (intpend & INT_NMI) {
			INTE = 0;
			intpend &= ~INT_NMI;
			i8080_push(reg_PC + halted);
			reg_PC = 0x24;
			cycles -= 12;	/* FIXME: 8080 clocking check */
			if (i8080_log)
				fprintf(i8080_log, "NMI taken.\n");
		} else if (INTE && (intpend & INT_IRQ)) {
			INTE = 0;
			if (i8080_log)
				fprintf(i8080_log, "IRQ taken\n");
			opcode = i8080_get_vector();
			if (i8080_log)
				fprintf(i8080_log, "IRQ taken (vector op %02X)\n", opcode);
			if (halted)
				reg_PC++;
			cycles -= 12;	/* Check 8080 timings */
			if (i8080_log)
				fprintf(i8080_log, "IRQ opcode %02X executed , PC was %04X\n",
					opcode, reg_PC);
		} else {
			opcode = i8080_read(reg_PC);
			if (i8080_log)
				fprintf(i8080_log, "%04X : %02X %02X %02X : %6s %02X %04X %04X %04X %04X %s\n",
					reg_PC, i8080_debug_read(reg_PC), i8080_debug_read(reg_PC + 1), i8080_debug_read(reg_PC + 2),
					i8080_flags(reg8[FLAGS]), reg8[A], reg16_BC, reg16_DE, reg16_HL, reg_SP,
						i8080_disassemble(reg_PC));
			reg_PC++;
		}
		/* if we are re-executing a hlt it'll set halted again */
		halted = 0;

		switch (opcode) {
			case 0x3A: //LDA a - load A from memory
				temp16 = (uint16_t)i8080_read(reg_PC) | ((uint16_t)i8080_read(reg_PC+1)<<8);
				reg8[A] = i8080_read(temp16);
				reg_PC += 2;
				cycles -= 13;
				break;
			case 0x32: //STA a - store A to memory
				temp16 = (uint16_t)i8080_read(reg_PC) | ((uint16_t)i8080_read(reg_PC+1)<<8);
				i8080_write(temp16, reg8[A]);
				reg_PC += 2;
				cycles -= 13;
				break;
			case 0x2A: //LHLD a - load H:L from memory
				temp16 = (uint16_t)i8080_read(reg_PC) | ((uint16_t)i8080_read(reg_PC+1)<<8);
				reg8[L] = i8080_read(temp16++);
				reg8[H] = i8080_read(temp16);
				reg_PC += 2;
				cycles -= 16;
				break;
			case 0x22: //SHLD a - store H:L to memory
				temp16 = (uint16_t)i8080_read(reg_PC) | ((uint16_t)i8080_read(reg_PC+1)<<8);
				i8080_write(temp16++, reg8[L]);
				i8080_write(temp16, reg8[H]);
				reg_PC += 2;
				cycles -= 16;
				break;
			case 0xEB: //XCHG - exchange DE and HL content
				temp8 = reg8[D];
				reg8[D] = reg8[H];
				reg8[H] = temp8;
				temp8 = reg8[E];
				reg8[E] = reg8[L];
				reg8[L] = temp8;
				cycles -= 5;
				break;
			case 0xC6: //ADI # - add immediate to A
				temp8 = i8080_read(reg_PC++);
				temp16 = (uint16_t)reg8[A] + (uint16_t)temp8;
				if (temp16 & 0xFF00) set_C(); else clear_C();
				calc_AC(reg8[A], temp8);
				calc_SZP((uint8_t)temp16);
				reg8[A] = (uint8_t)temp16;
				cycles -= 7;
				break;
			case 0xCE: //ACI # - add immediate to A with carry
				temp8 = i8080_read(reg_PC++);
				temp16 = (uint16_t)reg8[A] + (uint16_t)temp8 + (uint16_t)test_C();
				if (test_C()) calc_AC_carry(reg8[A], temp8); else calc_AC(reg8[A], temp8);
				if (temp16 & 0xFF00) set_C(); else clear_C();
				calc_SZP((uint8_t)temp16);
				reg8[A] = (uint8_t)temp16;
				cycles -= 7;
				break;
			case 0xD6: //SUI # - subtract immediate from A
				temp8 = i8080_read(reg_PC++);
				temp16 = (uint16_t)reg8[A] - (uint16_t)temp8;
				if (((temp16 & 0x00FF) >= reg8[A]) && temp8) set_C(); else clear_C();
				calc_subAC(reg8[A], temp8);
				calc_SZP((uint8_t)temp16);
				reg8[A] = (uint8_t)temp16;
				cycles -= 7;
				break;
			case 0x27: //DAA - decimal adjust accumulator
				temp16 = reg8[A];
				if (((temp16 & 0x0F) > 0x09) || test_AC()) {
					if (((temp16 & 0x0F) + 0x06) & 0xF0) set_AC(); else clear_AC();
					temp16 += 0x06;
					if (temp16 & 0xFF00) set_C(); //can also cause carry to be set during addition to the low nibble
				}
				if (((temp16 & 0xF0) > 0x90) || test_C()) {
					temp16 += 0x60;
					if (temp16 & 0xFF00) set_C(); //doesn't clear it if this clause is false
				}
				calc_SZP((uint8_t)temp16);
				reg8[A] = (uint8_t)temp16;
				cycles -= 4;
				break;
			case 0xE6: //ANI # - AND immediate with A
				temp8 = i8080_read(reg_PC++);
				if ((reg8[A] | temp8) & 0x08) set_AC(); else clear_AC();
				reg8[A] &= temp8;
				clear_C();
				calc_SZP(reg8[A]);
				cycles -= 7;
				break;
			case 0xF6: //ORI # - OR immediate with A
				reg8[A] |= i8080_read(reg_PC++);
				clear_AC();
				clear_C();
				calc_SZP(reg8[A]);
				cycles -= 7;
				break;
			case 0xEE: //XRI # - XOR immediate with A
				reg8[A] ^= i8080_read(reg_PC++);
				clear_AC();
				clear_C();
				calc_SZP(reg8[A]);
				cycles -= 7;
				break;
			case 0xDE: //SBI # - subtract immediate from A with borrow
				temp8 = i8080_read(reg_PC++);
				temp16 = (uint16_t)reg8[A] - (uint16_t)temp8 - (uint16_t)test_C();
				if (test_C()) calc_subAC_borrow(reg8[A], temp8); else calc_subAC(reg8[A], temp8);
				if (((temp16 & 0x00FF) >= reg8[A]) && (temp8 | test_C())) set_C(); else clear_C();
				calc_SZP((uint8_t)temp16);
				reg8[A] = (uint8_t)temp16;
				cycles -= 7;
				break;
			case 0xFE: //CPI # - compare immediate with A
				temp8 = i8080_read(reg_PC++);
				temp16 = (uint16_t)reg8[A] - (uint16_t)temp8;
				if (((temp16 & 0x00FF) >= reg8[A]) && temp8) set_C(); else clear_C();
				calc_subAC(reg8[A], temp8);
				calc_SZP((uint8_t)temp16);
				cycles -= 7;
				break;
			case 0x07: //RLC - rotate A left
				if (reg8[A] & 0x80) set_C(); else clear_C();
				reg8[A] = (reg8[A] >> 7) | (reg8[A] << 1);
				cycles -= 4;
				break;
			case 0x0F: //RRC - rotate A right
				if (reg8[A] & 0x01) set_C(); else clear_C();
				reg8[A] = (reg8[A] << 7) | (reg8[A] >> 1);
				cycles -= 4;
				break;
			case 0x17: //RAL - rotate A left through carry
				temp8 = test_C();
				if (reg8[A] & 0x80) set_C(); else clear_C();
				reg8[A] = (reg8[A] << 1) | temp8;
				cycles -= 4;
				break;
			case 0x1F: //RAR - rotate A right through carry
				temp8 = test_C();
				if (reg8[A] & 0x01) set_C(); else clear_C();
				reg8[A] = (reg8[A] >> 1) | (temp8 << 7);
				cycles -= 4;
				break;
			case 0x2F: //CMA - compliment A
				reg8[A] = ~reg8[A];
				cycles -= 4;
				break;
			case 0x3F: //CMC - compliment carry flag
				reg8[FLAGS] ^= 1;
				cycles -= 4;
				break;
			case 0x37: //STC - set carry flag
				set_C();
				cycles -= 4;
				break;
			case 0xC7: //RST n - restart (call n*8)
			case 0xD7:
			case 0xE7:
			case 0xF7:
			case 0xCF:
			case 0xDF:
			case 0xEF:
			case 0xFF:
				i8080_push(reg_PC);
				reg_PC = (uint16_t)((opcode >> 3) & 7) << 3;
				cycles -= 11;
				break;
			case 0xE9: //PCHL - jump to address in H:L
				reg_PC = reg16_HL;
				cycles -= 5;
				break;
			case 0xE3: //XTHL - swap H:L with top word on stack
				temp16 = i8080_pop();
				i8080_push(reg16_HL);
				write16_RP(2, temp16);
				cycles -= 18;
				break;
			case 0xF9: //SPHL - set SP to content of HL
				reg_SP = reg16_HL;
				cycles -= 5;
				break;
			case 0xDB: //IN p - read input port into A
				reg8[A] = i8080_inport(i8080_read(reg_PC++));
				cycles -= 10;
				break;
			case 0xD3: //OUT p - write A to output port
				i8080_outport(i8080_read(reg_PC++), reg8[A]);
				cycles -= 10;
				break;
			case 0xFB: //EI - enable interrupts
				INTE = 1;
				cycles -= 4;
				break;
			case 0xF3: //DI - disbale interrupts
				INTE = 0;
				cycles -= 4;
				break;
			case 0x76: //HLT - halt processor
				reg_PC--;
				cycles -= 7;
				halted = 1;
				break;
			case 0x00: //NOP - no operation
#ifdef ALLOW_UNDEFINED
			case 0x10:
			case 0x20:
			case 0x30:
			case 0x08:
			case 0x18:
			case 0x28:
			case 0x38:
#endif
				cycles -= 4;
				break;
			case 0x40: case 0x50: case 0x60: case 0x70: //MOV D,S - move register to register
			case 0x41: case 0x51: case 0x61: case 0x71:
			case 0x42: case 0x52: case 0x62: case 0x72:
			case 0x43: case 0x53: case 0x63: case 0x73:
			case 0x44: case 0x54: case 0x64: case 0x74:
			case 0x45: case 0x55: case 0x65: case 0x75:
			case 0x46: case 0x56: case 0x66:
			case 0x47: case 0x57: case 0x67: case 0x77:
			case 0x48: case 0x58: case 0x68: case 0x78:
			case 0x49: case 0x59: case 0x69: case 0x79:
			case 0x4A: case 0x5A: case 0x6A: case 0x7A:
			case 0x4B: case 0x5B: case 0x6B: case 0x7B:
			case 0x4C: case 0x5C: case 0x6C: case 0x7C:
			case 0x4D: case 0x5D: case 0x6D: case 0x7D:
			case 0x4E: case 0x5E: case 0x6E: case 0x7E:
			case 0x4F: case 0x5F: case 0x6F: case 0x7F:
				reg = (opcode >> 3) & 7;
				reg2 = opcode & 7;
				i8080_write_reg8(reg, i8080_read_reg8(reg2));
				if ((reg == M) || (reg2 == M)) {
					cycles -= 7;
				} else {
					cycles -= 5;
				}
				break;
			case 0x06: //MVI D,# - move immediate to register
			case 0x16:
			case 0x26:
			case 0x36:
			case 0x0E:
			case 0x1E:
			case 0x2E:
			case 0x3E:
				reg = (opcode >> 3) & 7;
				i8080_write_reg8(reg, i8080_read(reg_PC++));
				if (reg == M) {
					cycles -= 10;
				} else {
					cycles -= 7;
				}
				break;
			case 0x01: //LXI RP,# - load register pair immediate
			case 0x11:
			case 0x21:
			case 0x31:
				reg = (opcode >> 4) & 3;
				write_RP(reg, i8080_read(reg_PC), i8080_read(reg_PC + 1));
				reg_PC += 2;
				cycles -= 10;
				break;
			case 0x0A: //LDAX BC - load A indirect through BC
				reg8[A] = i8080_read(reg16_BC);
				cycles -= 7;
				break;
			case 0x1A: //LDAX DE - load A indirect through DE
				reg8[A] = i8080_read(reg16_DE);
				cycles -= 7;
				break;
			case 0x02: //STAX BC - store A indirect through BC
				i8080_write(reg16_BC, reg8[A]);
				cycles -= 7;
				break;
			case 0x12: //STAX DE - store A indirect through DE
				i8080_write(reg16_DE, reg8[A]);
				cycles -= 7;
				break;
			case 0x04: //INR D - increment register
			case 0x14:
			case 0x24:
			case 0x34:
			case 0x0C:
			case 0x1C:
			case 0x2C:
			case 0x3C:
				reg = (opcode >> 3) & 7;
				temp8 = i8080_read_reg8(reg); //reg8[reg];
				calc_AC(temp8, 1);
				calc_SZP(temp8 + 1);
				i8080_write_reg8(reg, temp8 + 1); //reg8[reg]++;
				if (reg == M) {
					cycles -= 10;
				} else {
					cycles -= 5;
				}
				break;
			case 0x05: //DCR D - decrement register
			case 0x15:
			case 0x25:
			case 0x35:
			case 0x0D:
			case 0x1D:
			case 0x2D:
			case 0x3D:
				reg = (opcode >> 3) & 7;
				temp8 = i8080_read_reg8(reg); //reg8[reg];
				calc_subAC(temp8, 1);
				calc_SZP(temp8 - 1);
				i8080_write_reg8(reg, temp8 - 1); //reg8[reg]--;
				if (reg == M) {
					cycles -= 10;
				} else {
					cycles -= 5;
				}
				break;
			case 0x03: //INX RP - increment register pair
			case 0x13:
			case 0x23:
			case 0x33:
				reg = (opcode >> 4) & 3;
				write16_RP(reg, read_RP(reg) + 1);
				cycles -= 5;
				break;
			case 0x0B: //DCX RP - decrement register pair
			case 0x1B:
			case 0x2B:
			case 0x3B:
				reg = (opcode >> 4) & 3;
				write16_RP(reg, read_RP(reg) - 1);
				cycles -= 5;
				break;
			case 0x09: //DAD RP - add register pair to HL
			case 0x19:
			case 0x29:
			case 0x39:
				reg = (opcode >> 4) & 3;
				temp32 = (uint32_t)reg16_HL + (uint32_t)read_RP(reg);
				write16_RP(2, (uint16_t)temp32);
				if (temp32 & 0xFFFF0000) set_C(); else clear_C();
				cycles -= 10;
				break;
			case 0x80: //ADD S - add register or memory to A
			case 0x81:
			case 0x82:
			case 0x83:
			case 0x84:
			case 0x85:
			case 0x86:
			case 0x87:
				reg = opcode & 7;
				temp8 = i8080_read_reg8(reg);
				temp16 = (uint16_t)reg8[A] + (uint16_t)temp8;
				if (temp16 & 0xFF00) set_C(); else clear_C();
				calc_AC(reg8[A], temp8);
				calc_SZP((uint8_t)temp16);
				reg8[A] = (uint8_t)temp16;
				if (reg == M) {
					cycles -= 7;
				} else {
					cycles -= 4;
				}
				break;
			case 0x88: //ADC S - add register or memory to A with carry
			case 0x89:
			case 0x8A:
			case 0x8B:
			case 0x8C:
			case 0x8D:
			case 0x8E:
			case 0x8F:
				reg = opcode & 7;
				temp8 = i8080_read_reg8(reg);
				temp16 = (uint16_t)reg8[A] + (uint16_t)temp8 + (uint16_t)test_C();
				if (test_C()) calc_AC_carry(reg8[A], temp8); else calc_AC(reg8[A], temp8);
				if (temp16 & 0xFF00) set_C(); else clear_C();
				calc_SZP((uint8_t)temp16);
				reg8[A] = (uint8_t)temp16;
				if (reg == M) {
					cycles -= 7;
				} else {
					cycles -= 4;
				}
				break;
			case 0x90: //SUB S - subtract register or memory from A
			case 0x91:
			case 0x92:
			case 0x93:
			case 0x94:
			case 0x95:
			case 0x96:
			case 0x97:
				reg = opcode & 7;
				temp8 = i8080_read_reg8(reg);
				temp16 = (uint16_t)reg8[A] - (uint16_t)temp8;
				if (((temp16 & 0x00FF) >= reg8[A]) && temp8) set_C(); else clear_C();
				calc_subAC(reg8[A], temp8);
				calc_SZP((uint8_t)temp16);
				reg8[A] = (uint8_t)temp16;
				if (reg == M) {
					cycles -= 7;
				} else {
					cycles -= 4;
				}
				break;
			case 0x98: //SBB S - subtract register or memory from A with borrow
			case 0x99:
			case 0x9A:
			case 0x9B:
			case 0x9C:
			case 0x9D:
			case 0x9E:
			case 0x9F:
				reg = opcode & 7;
				temp8 = i8080_read_reg8(reg);
				temp16 = (uint16_t)reg8[A] - (uint16_t)temp8 - (uint16_t)test_C();
				if (test_C()) calc_subAC_borrow(reg8[A], temp8); else calc_subAC(reg8[A], temp8);
				if (((temp16 & 0x00FF) >= reg8[A]) && (temp8 | test_C())) set_C(); else clear_C();
				calc_SZP((uint8_t)temp16);
				reg8[A] = (uint8_t)temp16;
				if (reg == M) {
					cycles -= 7;
				} else {
					cycles -= 4;
				}
				break;
			case 0xA0: //ANA S - AND register with A
			case 0xA1:
			case 0xA2:
			case 0xA3:
			case 0xA4:
			case 0xA5:
			case 0xA6:
			case 0xA7:
				reg = opcode & 7;
				temp8 = i8080_read_reg8(reg);
				if ((reg8[A] | temp8) & 0x08) set_AC(); else clear_AC();
				reg8[A] &= temp8;
				clear_C();
				calc_SZP(reg8[A]);
				if (reg == M) {
					cycles -= 7;
				} else {
					cycles -= 4;
				}
				break;
			case 0xB0: //ORA S - OR register with A
			case 0xB1:
			case 0xB2:
			case 0xB3:
			case 0xB4:
			case 0xB5:
			case 0xB6:
			case 0xB7:
				reg = opcode & 7;
				reg8[A] |= i8080_read_reg8(reg);
				clear_AC();
				clear_C();
				calc_SZP(reg8[A]);
				if (reg == M) {
					cycles -= 7;
				} else {
					cycles -= 4;
				}
				break;
			case 0xA8: //XRA S - XOR register with A
			case 0xA9:
			case 0xAA:
			case 0xAB:
			case 0xAC:
			case 0xAD:
			case 0xAE:
			case 0xAF:
				reg = opcode & 7;
				reg8[A] ^= i8080_read_reg8(reg);
				clear_AC();
				clear_C();
				calc_SZP(reg8[A]);
				if (reg == M) {
					cycles -= 7;
				} else {
					cycles -= 4;
				}
				break;
			case 0xB8: //CMP S - compare register with A
			case 0xB9:
			case 0xBA:
			case 0xBB:
			case 0xBC:
			case 0xBD:
			case 0xBE:
			case 0xBF:
				reg = opcode & 7;
				temp8 = i8080_read_reg8(reg);
				temp16 = (uint16_t)reg8[A] - (uint16_t)temp8;
				if (((temp16 & 0x00FF) >= reg8[A]) && temp8) set_C(); else clear_C();
				calc_subAC(reg8[A], temp8);
				calc_SZP((uint8_t)temp16);
				if (reg == M) {
					cycles -= 7;
				} else {
					cycles -= 4;
				}
				break;
			case 0xC3: //JMP a - unconditional jump
#ifdef ALLOW_UNDEFINED
			case 0xCB:
#endif
				temp16 = (uint16_t)i8080_read(reg_PC) | (((uint16_t)i8080_read(reg_PC + 1)) << 8);
				reg_PC = temp16;
				cycles -= 10;
				break;
			case 0xC2: //Jccc - conditional jumps
			case 0xCA:
			case 0xD2:
			case 0xDA:
			case 0xE2:
			case 0xEA:
			case 0xF2:
			case 0xFA:
				temp16 = (uint16_t)i8080_read(reg_PC) | (((uint16_t)i8080_read(reg_PC + 1)) << 8);
				if (test_cond((opcode >> 3) & 7)) reg_PC = temp16; else reg_PC += 2;
				cycles -= 10;
				break;
			case 0xCD: //CALL a - unconditional call
#ifdef ALLOW_UNDEFINED
			case 0xDD:
			case 0xED:
			case 0xFD:
#endif
				temp16 = (uint16_t)i8080_read(reg_PC) | (((uint16_t)i8080_read(reg_PC + 1)) << 8);
				i8080_push(reg_PC + 2);
				reg_PC = temp16;
				cycles -= 17;
				break;
			case 0xC4: //Cccc - conditional calls
			case 0xCC:
			case 0xD4:
			case 0xDC:
			case 0xE4:
			case 0xEC:
			case 0xF4:
			case 0xFC:
				temp16 = (uint16_t)i8080_read(reg_PC) | (((uint16_t)i8080_read(reg_PC + 1)) << 8);
				if (test_cond((opcode >> 3) & 7)) {
					i8080_push(reg_PC + 2);
					reg_PC = temp16;
					cycles -= 17;
				} else {
					reg_PC += 2;
					cycles -= 11;
				}
				break;
			case 0xC9: //RET - unconditional return
#ifdef ALLOW_UNDEFINED
			case 0xD9:
#endif
				reg_PC = i8080_pop();
				cycles -= 10;
				break;
			case 0xC0: //Rccc - conditional returns
			case 0xC8:
			case 0xD0:
			case 0xD8:
			case 0xE0:
			case 0xE8:
			case 0xF0:
			case 0xF8:
				if (test_cond((opcode >> 3) & 7)) {
					reg_PC = i8080_pop();
					cycles -= 11;
				} else {
					cycles -= 5;
				}
				break;
			case 0xC5: //PUSH RP - push register pair on the stack
			case 0xD5:
			case 0xE5:
			case 0xF5:
				reg = (opcode >> 4) & 3;
				i8080_push(read_RP_PUSHPOP(reg));
				cycles -= 11;
				break;
			case 0xC1: //POP RP - pop register pair from the stack
			case 0xD1:
			case 0xE1:
			case 0xF1:
				reg = (opcode >> 4) & 3;
				write16_RP_PUSHPOP(reg, i8080_pop());
				cycles -= 10;
				break;

#ifndef ALLOW_UNDEFINED
			default:
				printf("UNRECOGNIZED INSTRUCTION @ %04Xh: %02X\n", reg_PC - 1, opcode);
				exit(0);
#endif
		}

	}

	return cycles;
}

/*
 *	8080 disassembler - added by Alan Cox 2025 (from the 8085 one)
 */


struct i8080_addr {
	unsigned addr;
	struct i8080_addr *next;
	char name[16];
	char type;
};

static struct i8080_addr *sym[0x40];

static unsigned ahash(unsigned addr)
{
	return (addr >> 6) & 0x3F;
}

static struct i8080_addr *i8080_addr_find(unsigned addr)
{
	unsigned hash = ahash(addr);
	struct i8080_addr *a = sym[hash];
	while(a) {
		if (a->addr == addr)
			return a;
		a = a->next;
	}
	return NULL;
}

static void i8080_add_symbol(unsigned addr, char type, char *name)
{
	struct i8080_addr *a = malloc(sizeof(struct i8080_addr));
	unsigned hash = ahash(addr);
	strncpy(a->name, name, 16);
	a->addr = addr;
	a->type = type;
	a->next = sym[hash];
	sym[hash] = a;
}

void i8080_load_symbols(const char *path)
{
	char buf[64];
	unsigned addr;
	char type;
	char name[16];
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		perror(path);
		return;
	}
	while(fgets(buf, 63, fp) != NULL) {
		if (sscanf(buf, "%x %c %16s", &addr, &type, name) == 3)
			i8080_add_symbol(addr, type, name);
		else
			fprintf(stderr, "format error %s\n", buf);
	}
	fclose(fp);
}

static char opbuf[32];

static char rname[8] = { "bcdehlma" };
static char *rpair_s[4] = { "bc", "de", "hl", "sp" };
static char *rpair_p[4] = { "bc", "de", "hl", "psw" };
static char *cc[8] = { "nz", "z", "nc", "c", "po", "pe", "p", "m" };

static char *blk00[] = {
	"nop", "ill(dsub)", "ill(arhl)", "ill(rdel)", "ill(rim)", "ill(ldhi)", "ill(sim)", "ill(ldsi)"
};

static char *blk02[] = {
	"stax b", "ldax b", "stax d", "ldax d",
	"shld", "lhld", "sta", "lda"
};

static char *blk07[] = {
	"rlc", "rrc", "ral", "rar",
	"daa", "cma", "stc", "cmc"
};

static char *idrw(unsigned addr)
{
	static char buf[16];
	struct i8080_addr *a;
	unsigned v = i8080_debug_read(addr);
	v |= i8080_debug_read(addr + 1) << 8;
	a = i8080_addr_find(v);
	if (a)
		return a->name;
	else {
		sprintf(buf, "%04X", v);
		return buf;
	}
}

static void dis0(uint8_t op, uint16_t addr)
{
	unsigned y = (op >> 3) & 7;
	switch(op & 7) {
		case 0:
			/* Real mix */
			if (y == 5 || y == 7)
				sprintf(opbuf, "%s %02X", blk00[y], i8080_debug_read(addr));
			else
				strcpy(opbuf, blk00[y]);
			break;
		case 1:
			if (y & 1)
				sprintf(opbuf, "dad %s", rpair_s[y >> 1]);
			else
				sprintf(opbuf, "lxi %s, %s", rpair_s[y >> 1], idrw(addr));
			break;
		case 2:
			if (y & 4)
				sprintf(opbuf, "%s %s", blk02[y],
					idrw(addr));
			else
				strcpy(opbuf, blk02[y]);
			break;
		case 3:
			if (!(y & 1))
				sprintf(opbuf, "inx %s", rpair_s[y >> 1]);
			else
				sprintf(opbuf, "dcx %s", rpair_s[y >> 1]);
			break;
		case 4:
			sprintf(opbuf, "inr %c", rname[y]);
			break;
		case 5:
			sprintf(opbuf, "dcr %c", rname[y]);
			break;
		case 6:
			sprintf(opbuf, "mvi %c,%02X", rname[y],
				i8080_debug_read(addr));
			break;
		case 7:
			strcpy(opbuf, blk07[y]);
			break;
	}
}

static void dis1(uint8_t op, uint16_t addr)
{
	if (op == 0x76)
		strcpy(opbuf, "hlt");
	else
		sprintf(opbuf, "mov %c,%c", rname[(op >> 3) & 7], rname[op & 7]);
}

static char *aluop[] = {
	"add", "adc", "sub", "sbb", "ana", "xra", "ora", "cmp"
};

static char *aluim[] = {
	"adi", "aci", "sui", "sbi", "ani", "xri", "ori", "cmi"
};

static void dis2(uint8_t op, uint16_t addr)
{
	sprintf(opbuf, "%s %c", aluop[(op >> 3) & 7], rname[op & 7]);
}

static char *blk31[] = {
	"ret", "ill(shlx)", "pchl", "sphl"
};

static void dis3(uint8_t op, uint16_t addr)
{
	unsigned y = (op >> 3) & 7;
	switch(op & 7) {
	case 0:
		sprintf(opbuf, "r%s", cc[y]);
		break;
	case 1:
		if ((y & 1) == 0)
			sprintf(opbuf, "pop %s", rpair_p[y >> 1]);
		else
			strcpy(opbuf, blk31[y >> 1]);
		break;
	case 2:
		sprintf(opbuf, "j%s %s", cc[y], idrw(addr));
		break;
	case 3:
		/* This one appears to have been the dumping ground */
		switch(y) {
		case 0:
			sprintf(opbuf, "jmp %s", idrw(addr));
			break;
		case 1:
			strcpy(opbuf, "rstv");
			return;
		case 2:
			sprintf(opbuf, "out %02X", i8080_debug_read(addr));
			return;
		case 3:
			sprintf(opbuf, "in %02X", i8080_debug_read(addr));
			return;
		case 4:
			strcpy(opbuf, "xthl");
			return;
		case 5:
			strcpy(opbuf, "xchg");
			return;
		case 6:
			strcpy(opbuf, "di");
			return;
		case 7:
			strcpy(opbuf, "ei");
			return;
		}
		break;
	case 4:
		sprintf(opbuf, "c%s %s", cc[y], idrw(addr));
		break;
	case 5:
		if (!(y & 1)) {
			sprintf(opbuf, "push %s", rpair_p[y >> 1]);
			break;
		}
		switch(y >> 1) {
		case 0:
			sprintf(opbuf, "call %s", idrw(addr));
			break;
		case 1:
			sprintf(opbuf, "ill(jnx %s)", idrw(addr));
			break;
		case 2:
			strcpy(opbuf, "ill(lhlx)");
			break;
		case 3:
			sprintf(opbuf, "ill(jx %s)", idrw(addr));
			break;
		}
		break;
	case 6:
		sprintf(opbuf, "%s %02X", aluim[y], i8080_debug_read(addr));
		break;
	case 7:
		sprintf(opbuf, "rst %d", y);
		break;
	}
}

static char *i8080_disassemble(uint16_t addr)
{
	uint8_t op = i8080_debug_read(addr++);
	switch (op & 0xC0) {
	case 0x00:
		dis0(op, addr);
		break;
	case 0x40:
		dis1(op, addr);
		break;
	case 0x80:
		dis2(op, addr);
		break;
	case 0xC0:
		dis3(op, addr);
		break;
	}
	return opbuf;
}
