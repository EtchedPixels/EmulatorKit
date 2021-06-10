/*
 *	A simple NS806x implementation
 */

struct ns8070;

extern struct ns8070 *ns8070_create(uint8_t *rom);
extern void ns8070_reset(struct ns8070 *cpu);
extern void ns8070_trace(struct ns8070 *cpu, unsigned int onoff);
extern unsigned int ns8070_execute_one(struct ns8070 *cpu);
extern void ns8070_seta(struct ns8070 *cpu, unsigned int a);
extern void ns8070_setb(struct ns8070 *cpu, unsigned int b);

/*
 *	Helpers required by the implementor
 */

extern uint8_t mem_read(struct ns8070 *cpu, uint16_t addr);
extern void mem_write(struct ns8070 *cpu, uint16_t addr, uint8_t val);
extern void flag_change(struct ns8070 *cpu, uint8_t fbits);
