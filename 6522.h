/*
 *	Fairly basic 6522 VIA emulation
 */

struct via6522;

extern void via_tick(struct via6522 *via, unsigned int cycles);
extern void via_write(struct via6522 *via, uint8_t addr, uint8_t val);
extern uint8_t via_read(struct via6522 *via, uint8_t addr);
extern struct via6522 *via_create(void);
extern void via_free(struct via6522 *via);
extern void via_trace(struct via6522 *via, int onoff);
extern int via_irq_pending(struct via6522 *via);
extern uint8_t via_get_direction_a(struct via6522 *via);
extern uint8_t via_get_port_a(struct via6522 *via);
extern void via_set_port_a(struct via6522 *via, uint8_t val);
extern uint8_t via_get_direction_b(struct via6522 *via);
extern uint8_t via_get_port_b(struct via6522 *via);
extern void via_set_port_b(struct via6522 *via, uint8_t val);

/* Provided by user to model the attached hardware */
extern void via_recalc_outputs(struct via6522 *via);
extern void via_handshake_a(struct via6522 *via);
extern void via_handshake_b(struct via6522 *via);

