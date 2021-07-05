struct uart16x50;

extern struct uart16x50 *uart16x50_create(void);
extern void uart16x50_free(struct uart16x50 *uart16x50);
extern void uart16x50_trace(struct uart16x50 *uart16x50, int onoff);
extern uint8_t uart16x50_read(struct uart16x50 *uart16x50, uint8_t addr);
extern void uart16x50_write(struct uart16x50 *uart16x50, uint8_t addr, uint8_t val);
extern void uart16x50_event(struct uart16x50 *uart16x50);
extern void uart16x50_reset(struct uart16x50 *uart16x50);
extern uint8_t uart16x50_irq_pending(struct uart16x50 *uart16x50);
extern void uart16x50_set_input(struct uart16x50 *uart16x50, int port);
extern void uart16x50_dsr_timer(struct uart16x50 *uart16x50);

/* Caller proviced */
extern unsigned int next_char(void);
extern unsigned int check_chario(void);

