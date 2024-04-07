
struct tms9902;

extern uint8_t tms9902_cru_read(struct tms9902 *tms, uint16_t addr);
extern void tms9902_reset(struct tms9902 *tms);
extern void tms9902_cru_write(struct tms9902 *tms, uint16_t addr, uint8_t bit);
extern void tms9902_event(struct tms9902 *tms);
extern void tms9902_attach(struct tms9902 *tms9902, struct serial_device *dev);
extern void tms9902_trace(struct tms9902 *tms9902, int onoff);
extern uint8_t tms9902_irq_pending(struct tms9902 *d);
extern struct tms9902 *tms9902_create(void);
extern void tms9902_free(struct tms9902 *d);
