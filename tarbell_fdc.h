extern uint8_t tbfdc_read(struct wd17xx *fdc, unsigned addr);
extern void tbfdc_write(struct wd17xx *fdc, unsigned addr, uint8_t val);
extern struct wd17xx *tbfdc_create(void);
