struct ramf;

void ramf_write(struct ramf *ramf, uint8_t addr, uint8_t val);
uint8_t ramf_read(struct ramf *ramf, uint8_t addr);
struct ramf *ramf_create(const char *path);
void ramf_free(struct ramf *ramf);
void ramf_trace(struct ramf *ramf, unsigned int onoff);





