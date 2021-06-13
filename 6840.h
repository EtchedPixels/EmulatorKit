struct m6840;

extern int m6840_irq_pending(struct m6840 *ptm);
extern void m6840_tick(struct m6840 *ptm, int tstates);
extern void m6840_external_clock(struct m6840 *ptm, int timer);
extern void m6840_external_gate(struct m6840 *ptm, int gate);
extern void m6840_reset(struct m6840 *ptm);
extern uint8_t m6840_read(struct m6840 *ptm, uint8_t addr);
extern void m6840_write(struct m6840 *ptm, uint8_t addr, uint8_t val);

extern void m6840_trace(struct m6840 *ptm, int onoff);
extern struct m6840 *m6840_create(void);
extern void m6840_free(struct m6840 *ptm);

/* User supplied */
extern void m6840_output_change(struct m6840 *ptm, uint8_t outputs);
