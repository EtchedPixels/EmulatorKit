struct z180_io;

bool z180_iospace(struct z180_io *io, uint16_t addr);
uint8_t z180_read(struct z180_io *io, uint8_t addr);
void z180_write(struct z180_io *io, uint8_t addr, uint8_t val);
uint32_t z180_mmu_translate(struct z180_io *io, uint16_t addr);
void z180_event(struct z180_io *io, unsigned int clocks);
void z180_interrupt(struct z180_io *io, uint8_t pin, uint8_t vec, bool on);
unsigned int z180_dma(struct z180_io *io);
struct z180_io *z180_create(Z180Context *cpu);
void z180_free(struct z180_io *io);
void z180_trace(struct z180_io *io, int trace);
void z180_set_input(struct z180_io *io, int port, int onoff);
void z180_set_clock(struct z180_io *io, unsigned hz);

/* Caller proviced */
extern unsigned int next_char(void);
extern unsigned int check_chario(void);

extern uint8_t z180_csio_write(struct z180_io *io, uint8_t val);
extern uint8_t z180_phys_read(int context, uint32_t addr);
extern void z180_phys_write(int context, uint32_t addr, uint8_t data);
