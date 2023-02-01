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
void uart16x50_signal_change(struct uart16x50 *uart16x50, uint8_t mcr);
void uart16x50_signal_event(struct uart16x50 *uart16x50, uint8_t msr);

/* These are inverse of the actual signal level for 5v TTL */
#define MCR_DTR		0x01
#define MCR_RTS		0x02
#define MCR_OUT1	0x04
#define MCR_OUT2	0x08

/* These are inverse of the actual signal level for 5v TTL */
#define MSR_CTS		0x10
#define MSR_DSR		0x20
#define MSR_RI		0x40
#define MSR_DCD		0x80

/* Caller proviced */
extern unsigned int next_char(void);
extern unsigned int check_chario(void);

