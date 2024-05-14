/*
 *	A simple NS806x implementation
 */

struct ns8060;

extern struct ns8060 *ns8060_create(void);
extern void ns8060_reset(struct ns8060 *cpu);
extern void ns8060_trace(struct ns8060 *cpu, unsigned int onoff);
extern unsigned int ns8060_execute_one(struct ns8060 *cpu);
extern void ns8060_set_a(struct ns8060 *cpu, unsigned int a);
extern void ns8060_set_b(struct ns8060 *cpu, unsigned int b);

extern int  ns8060_emu_getch(void);
extern void ns8060_emu_putch(int ch);
/*
 *	Helpers required by the implementor
 */

extern uint8_t mem_read(struct ns8060 *cpu, uint16_t addr);
extern void mem_write(struct ns8060 *cpu, uint16_t addr, uint8_t val);
extern uint8_t ser_input(struct ns8060 *cpu);
extern void ser_output(struct ns8060 *cpu, uint8_t bit);
