#ifndef sn76489_H
#define sn76489_H

#include <stdint.h>

struct sn76489;
extern struct sn76489 *sn76489_create();
extern uint8_t sn76489_readReady(struct sn76489*); 
extern void sn76489_writeIO(struct sn76489*, uint8_t);
extern void sn76489_destroy(struct sn76489*);

#endif
