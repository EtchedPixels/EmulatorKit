struct scopewriter;

extern void scopewriter_write(struct scopewriter *sw, uint8_t byte);
extern void scopewriter_switches(struct scopewriter *sw, uint8_t bit, uint8_t val);
extern uint32_t *scopewriter_get_raster(struct scopewriter *sw);
extern struct scopewriter *scopewriter_create(void);
extern void scopewriter_free(struct scopewriter *sw);

#define SW_PB	0x01
#define SW_LOAD	0x02
#define SW_RD	0x04
