struct mm58174;

struct mm58174 *mm58174_create(void);
void mm58174_free(struct mm58174 *rtc);
void mm58174_reset(struct mm58174 *rtc);
void mm58174_write(struct mm58174 *rtc, uint8_t reg, uint8_t val);
uint8_t mm58174_read(struct mm58174 *rtc, uint8_t reg);
void mm58174_trace(struct mm58174 *rtc, unsigned int onoff);
unsigned int mm58174_irqpending(struct mm58174 *rtc);
void mm58174_tick(struct mm58174 *rtc);
