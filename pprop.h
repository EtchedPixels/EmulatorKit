struct pprop;

uint8_t pprop_read(struct pprop *prop, uint8_t addr);
void pprop_write(struct pprop *prop, uint8_t addr, uint8_t val);
struct pprop *pprop_create(const char *path);
void pprop_free(struct pprop *prop);
void pprop_reset(struct pprop *prop);
void pprop_set_input(struct pprop *prop, int onoff);
void pprop_trace(struct pprop *prop, int onoff);

/* Caller proviced */
extern unsigned int next_char(void);
extern unsigned int check_chario(void);
