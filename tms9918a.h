struct tms9918a {
    uint8_t reg[8];	/* We just ignore invalid bits, you can't read them
                           back so who cares */
    uint8_t status;
    uint8_t framebuffer[16384];	/* The memory behind the VDP */
    uint32_t rasterbuffer[256 * 192]; /* Our output texture */
    uint32_t *colourmap;
    uint8_t colbuf[256 + 64];	/* For off edge collisions */
    unsigned int latch;		/* The toggling latch for low/hi */
    unsigned int read;		/* Mode */
    uint16_t addr;		/* Address */
    uint16_t memmask;		/* Address range */

    int trace;
};

extern void tms9918a_rasterize(struct tms9918a *vdp);
extern void tms9918a_write(struct tms9918a *vdp, uint8_t addr, uint8_t val);
extern uint8_t tms9918a_read(struct tms9918a *vdp, uint8_t addr);
extern struct tms9918a *tms9918a_create(void);
extern void tms9918a_free(struct tms9918a *vdp);
extern void tms9918a_reset(struct tms9918a *vdp);
extern void tms9918a_trace(struct tms9918a *vdp, int onoff);
extern int tms9918a_irq_pending(struct tms9918a *vdp);
extern uint32_t *tms9918a_get_raster(struct tms9918a *vdp);
extern void tms9918a_set_colourmap(struct tms9918a *vdp, uint32_t *ctab);
