struct ds1307    *rtc_create(struct i2c_bus *i2cbus);
void    rtc_free(struct ds1307 *rtc);
void    rtc_reset(struct ds1307 *rtc);
void    rtc_trace(struct ds1307 *rtc, int onoff);
void    rtc_save(struct ds1307 *rtc, const char *path);
void    rtc_load(struct ds1307 *rtc, const char *path);


