struct i8251;

extern struct i8251 *i8251_create(void);
extern void i8251_free(struct i8251 *i8251);
extern void i8251_trace(struct i8251 *i8251, int onoff);
extern uint8_t i8251_read(struct i8251 *i8251, uint16_t addr);
extern void i8251_write(struct i8251 *i8251, uint16_t addr, uint8_t val);
extern void i8251_timer(struct i8251 *i8251);
extern unsigned i8251_irq_pending(struct i8251 *i8251);
extern void i8251_attach(struct i8251 *i8251, struct serial_device *dev);

