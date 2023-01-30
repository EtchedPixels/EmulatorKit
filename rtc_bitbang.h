struct rtc;

struct rtc *rtc_create(void);
void rtc_free(struct rtc *rtc);
void rtc_reset(struct rtc *rtc);
void rtc_write(struct rtc *rtc, uint8_t val);
uint8_t rtc_read(struct rtc *rtc);
void rtc_trace(struct rtc *rtc, int onoff);
void rtc_save(struct rtc *rtc, const char *path);
void rtc_load(struct rtc *rtc, const char *path);


