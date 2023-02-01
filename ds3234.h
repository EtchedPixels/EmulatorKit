#ifndef DS3234_H
#define DS3234_H

struct ds3234;

uint8_t ds3234_spi_rxtx(struct ds3234 *rtc, uint8_t val);
void ds3234_spi_cs(struct ds3234 *rtc, unsigned cs);
void ds3234_reset(struct ds3234 *rtc);
struct ds3234 *ds3234_create(void);
void ds3234_free(struct ds3234 *rtc);
void ds3234_trace(struct ds3234 *rtc, int onoff);
void ds3234_save(struct ds3234 *rtc, const char *path);
void ds3234_load(struct ds3234 *rtc, const char *path);

#endif
