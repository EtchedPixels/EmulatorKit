struct mits1_uart;

extern struct mits1_uart *mits1_create(void);
extern void mits1_free(struct mits1_uart *mu);
extern void mits1_trace(struct mits1_uart *mu, int onoff);
extern uint8_t mits1_read(struct mits1_uart *mu, uint16_t addr);
extern void mits1_write(struct mits1_uart *mu, uint16_t addr, uint8_t val);
extern void mits1_timer(struct mits1_uart *mu);
extern uint8_t mits1_irq_pending(struct mits1_uart *mu);
extern void mits1_attach(struct mits1_uart *mu, struct serial_device *dev);
