/*
 *	A simple NS807x implementation
 */

struct ns8070;

extern struct ns8070 *ns8070_create(uint8_t *rom);
extern void ns8070_reset(struct ns8070 *cpu);
extern void ns8070_trace(struct ns8070 *cpu, unsigned int onoff);
extern unsigned int ns8070_execute_one(struct ns8070 *cpu);
extern void ns8070_set_a(struct ns8070 *cpu, unsigned int a);
extern void ns8070_set_b(struct ns8070 *cpu, unsigned int b);

/*
 *	Helpers required by the implementor
 */

extern uint8_t mem_read(struct ns8070 *cpu, uint16_t addr);
extern void mem_write(struct ns8070 *cpu, uint16_t addr, uint8_t val);
extern void flag_change(struct ns8070 *cpu, uint8_t fbits);
#define S_CL	0x80		/* Carry / Link */
#define S_OV	0x40		/* Signed overflow */
#define S_SB	0x20		/* Buffered input B */
#define S_SA	0x10		/* Buffered input A */
#define S_F3	0x08		/* CPU flag outputs */
#define S_F2	0x04		/* Basically 3 GPIO */
#define S_F1	0x02		/* lines for bitbang */
#define S_IE	0x01		/* Interrupt enable */

