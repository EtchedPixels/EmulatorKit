struct i82c55a;

extern void i82c55a_reset(struct i82c55a *ppi);
extern uint8_t i82c55a_read(struct i82c55a *ppi, uint8_t addr);
extern void i82c55a_write(struct i82c55a *ppi, uint8_t addr, uint8_t val);

extern void i82c55a_trace(struct i82c55a *ppi, int onoff);
extern struct i82c55a *i82c55a_create(void);
extern void i82c55a_free(struct i82c55a *ppi);

/* User supplied */
extern uint8_t i82c55a_input(struct i82c55a *ppi, int port);
extern void i82c55a_output(struct i82c55a *ppi, int port, uint8_t data);
