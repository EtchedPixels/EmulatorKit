/*
 *	Fairly basic SD card emulation
 */

struct sdcard;

extern struct sdcard *sd_create(const char *name);
extern void sd_reset(struct sdcard *c);
extern void sd_free(struct sdcard *c);
extern void sd_trace(struct sdcard *c, int onoff);
extern void sd_attach(struct sdcard *c, int fd);
extern void sd_detach(struct sdcard *c);
extern void sd_blockmode(struct sdcard *c);

extern uint8_t sd_spi_in(struct sdcard *c, uint8_t v);
extern void sd_spi_raise_cs(struct sdcard *c);
extern void sd_spi_lower_cs(struct sdcard *c);
