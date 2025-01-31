struct duart;

extern struct duart *duart_create(void);
extern void duart_free(struct duart *duart);
extern void duart_trace(struct duart *duart, int onoff);
extern uint8_t duart_read(struct duart *duart, uint16_t addr);
extern void duart_write(struct duart *duart, uint16_t addr, uint8_t val);
extern void duart_tick(struct duart *duart);
extern void duart_reset(struct duart *duart);
extern uint8_t duart_irq_pending(struct duart *duart);
extern void duart_set_input(struct duart *duart, int port);
extern uint8_t duart_vector(struct duart *duart);

/* Caller proviced */
extern unsigned int next_char(void);
extern unsigned int check_chario(void);
extern void recalc_interrupts(void);
extern void duart_signal_change(struct duart *d, uint8_t opr);
