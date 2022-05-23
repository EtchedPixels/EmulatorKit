#ifndef INTEL_I8085_EMULATOR_H
#define INTEL_I8085_EMULATOR_H

typedef enum
{
	B=0, C, D, E, H, L, M, A, FLAGS,
	AF, BC, DE, HL, SP, PC
}
reg_t;

extern uint8_t i8085_read(uint16_t addr);
extern void i8085_write(uint16_t addr, uint8_t value);
extern uint8_t i8085_debug_read(uint16_t addr);
extern uint8_t i8085_inport(uint8_t port);
extern void i8085_outport(uint8_t port, uint8_t value);
extern int i8085_get_input(void);
extern void i8085_set_output(int value);

extern void i8085_set_int(int n);
extern void i8085_clear_int(int n);

#define INT_NMI		0x80
#define INT_EXTERN	0x40	/* Assumed to provide 0xFF */

/* These are arranged to line up with SIM for simplicity */
#define INT_RST75	0x04
#define INT_RST65	0x02
#define INT_RST55	0x01


extern uint8_t i8085_read_reg8(reg_t reg);
extern void i8085_write_reg8(reg_t reg, uint8_t value);

extern uint16_t i8085_read_reg16(reg_t reg);
extern void i8085_write_reg16(reg_t reg, uint16_t value);

extern void i8085_reset();

extern int i8085_exec(int cycles);

extern FILE *i8085_log;

extern void i8085_load_symbols(const char *path);

#endif
