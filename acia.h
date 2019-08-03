struct acia;

extern struct acia *acia_create(void);
extern void acia_free(struct acia *acia);
extern void acia_trace(struct acia *acia, int onoff);
extern uint8_t acia_read(struct acia *acia, uint16_t addr);
extern void acia_write(struct acia *acia, uint16_t addr, uint8_t val);
extern void acia_timer(struct acia *acia);
extern uint8_t acia_irq_pending(struct acia *acia);
extern void acia_set_input(struct acia *acia, int onoff);
