struct m68230;

extern void m68230_write(struct m68230 *pit, unsigned addr, uint8_t val);
extern uint8_t m68230_read(struct m68230 *pit, unsigned addr);
extern void m68230_tick(struct m68230 *pit, unsigned cycles);
extern void m68230_reset(struct m68230 *pit);
extern struct m68230 *m68230_create(void);
extern void m68230_free(struct m68230 *m);
extern void m68230_trace(struct m68230 *pit, unsigned t);
extern uint8_t m68230_port_irq_pending(struct m68230 *pit);
extern uint8_t m68230_port_vector(struct m68230 *pit);
extern uint8_t m68230_timer_irq_pending(struct m68230 *pit);
extern uint8_t m68230_timer_vector(struct m68230 *pit);

extern void m68230_write_port(struct m68230 *pit, unsigned port, uint8_t val);
extern uint8_t m68230_read_port(struct m68230 *pit, unsigned port);
