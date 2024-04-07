struct propio;

uint8_t propio_read(struct propio *prop, uint8_t addr);
void propio_write(struct propio *prop, uint8_t addr, uint8_t val);
struct propio *propio_create(const char *path);
void propio_free(struct propio *prop);
void propio_reset(struct propio *prop);
void propio_attach(struct propio *prop, struct serial_device *dev);
void propio_trace(struct propio *prop, int onoff);
