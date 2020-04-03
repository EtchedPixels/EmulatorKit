struct z8
{
    /* CPU state */
    uint8_t reg[256];
    uint16_t pc;
    uint8_t regmax;	/* Highest non special register present */
    /* Internal emulation state */
    uint8_t arg0, arg1, dreg; uint8_t opl;
    /* Counter states */
    uint8_t psc0, psc1;	/* Prescalers */
    uint8_t t0, t1;	/* Timers */
    /* Instruction timing */
    unsigned long cycles;
    int done_ei;	/* Has done an EI (weirdness with IRR register) */
    int ei_state;	/* Interrupt control */
    int trace;
};


#define R_SIO		240
#define R_TMR		241
#define R_T1		242
#define R_PRE1		243
#define R_T0		244
#define R_PRE0		245
#define R_P2M		246
#define R_P3M		247
#define R_P01M		248
#define R_IPR		249
#define R_IRR		250
#define R_IMR		251
#define R_FLAGS		252
#define R_RP		253
#define R_SPH		254
#define R_SPL		255

#define F_H		0x04
#define F_D		0x08
#define F_V		0x10
#define F_S		0x20
#define F_Z		0x40
#define F_C		0x80


#define CARRY		((z8->reg[R_FLAGS] & F_C) ? 0x01 : 0x00)

/* Provided functions */

extern struct z8 *z8_create(void);
extern void z8_free(struct z8 *z8);
extern void z8_set_irq(struct z8 *z8, int irq);
extern void z8_clear_irq(struct z8 *z8, int irq);
extern void z8_reset(struct z8 *z8);
extern void z8_execute(struct z8 *z8);
extern void z8_raise_irq(struct z8 *z8, int irq);
extern void z8_clear_irq(struct z8 *z8, int irq);
extern void z8_rx_char(struct z8 *z8, uint8_t ch);
extern void z8_set_trace(struct z8 *z8, int onoff);
extern void z8_tx_done(struct z8 *z8);

/* Provided by user */
extern uint8_t z8_read_code(struct z8 *z8, uint16_t addr);
extern uint8_t z8_read_code_debug(struct z8 *z8, uint16_t addr);
extern void z8_write_code(struct z8 *z8, uint16_t addr, uint8_t val);
extern uint8_t z8_read_data(struct z8 *z8, uint16_t addr);
extern void z8_write_data(struct z8 *z8, uint16_t addr, uint8_t val);
extern uint8_t z8_port_read(struct z8 *z8, uint8_t port);
extern void z8_port_write(struct z8 *z8, uint8_t port, uint8_t val);
extern void z8_tx(struct z8 *z8, uint8_t ch);
