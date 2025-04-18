struct m6551;

extern struct m6551 *m6551_create(void);
extern void m6551_free(struct m6551 *m6551);
extern void m6551_trace(struct m6551 *m6551, int onoff);
extern uint8_t m6551_read(struct m6551 *m6551, uint16_t addr);
extern void m6551_write(struct m6551 *m6551, uint16_t addr, uint8_t val);
extern void m6551_timer(struct m6551 *m6551);
extern uint8_t m6551_irq_pending(struct m6551 *m6551);
extern void m6551_attach(struct m6551 *m6551, struct serial_device *dev);
extern void m6551_reset(struct m6551 *m6551);
