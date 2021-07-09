struct propio;

uint8_t propio_read(struct propio *prop, uint8_t addr);
void propio_write(struct propio *prop, uint8_t addr, uint8_t val);
struct propio *propio_create(const char *path);
void propio_free(struct propio *prop);
void propio_reset(struct propio *prop);
void propio_set_input(struct propio *prop, int onoff);
void propio_trace(struct propio *prop, int onoff);

/* Caller proviced */
extern unsigned int next_char(void);
extern unsigned int check_chario(void);
