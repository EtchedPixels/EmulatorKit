struct z80dma;

extern void z80dma_write(struct z80dma *dma, uint8_t val);
extern uint8_t z80dma_read(struct z80dma *dma);
extern int z80_dma_run(struct z80dma *dma, int cycles);
extern struct z80dma *z80dma_create(void);
extern void z80dma_free(struct z80dma *d);
extern void z80dma_trace(struct z80dma *d, int onoff);



