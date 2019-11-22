#ifndef __PPIDE_H
#define __PPIDE_H
#include "ide.h"

struct ppide {
    uint8_t pioreg[4];
    struct ide_controller *ide;
    unsigned int trace;
};

extern void ppide_write(struct ppide *ppide, uint8_t addr, uint8_t val);
extern uint8_t ppide_read(struct ppide *ppide, uint8_t addr);
extern void ppide_reset(struct ppide *ppide);
extern struct ppide *ppide_create(const char *name);
extern void ppide_free(struct ppide *ppide);
extern void ppide_trace(struct ppide *ppide, int onoff);
extern int ppide_attach(struct ppide *ppide, int drive, int fd);
#endif
