#ifndef SN76489_H
#define SN76489_H

#include <stdint.h>

struct sn76489;
extern struct sn76489 *sn76489_create(void);
extern uint8_t sn76489_ready(struct sn76489 *); 
extern void sn76489_write(struct sn76489 *, uint8_t);
extern void sn76489_destroy(struct sn76489 *);

#endif
