extern uint8_t p90_read(uint32_t addr);
extern void p90_write(uint32_t addr, uint8_t val);
extern void p90_set_aux(uint8_t auxcon, uint8_t aux);
extern void p90_cycles(unsigned n);
extern int p90_autovector(unsigned n);
extern unsigned p90_interrupts(void);
