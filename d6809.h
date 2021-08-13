/*
 *	Glue between d6809 and our emulator models. Use an interface that
 *	looks like e6809.
 */

extern unsigned char e6809_read8_debug(unsigned addr);

/*
 *	Provided methods
 */

unsigned d6809_disassemble(char *buffer, unsigned pc);
unsigned d6309_disassemble(char *buffer, unsigned pc);

