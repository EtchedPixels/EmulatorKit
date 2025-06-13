struct z180copro {
	Z180Context cpu;
	struct z180_io *io;
	struct sdcard *sdcard;
	int unit;
	uint8_t shared[1024];
	uint8_t ram[512 * 1024];
	uint16_t state;
#define COPRO_RESET	1
#define COPRO_IRQ_IN	2
#define COPRO_IRQ_OUT	4
	int tstates;
	int irq_pending;
	int trace;
};

#define MAX_COPRO	4

extern void z180copro_reset(struct z180copro *c);
extern uint8_t *z180copro_eprom(struct z180copro *c);
extern void z180copro_run(struct z180copro *c);
extern void z180copro_iowrite(struct z180copro *c, uint16_t addr, uint8_t bits);
extern uint8_t z180copro_ioread(struct z180copro *c, uint16_t addr);
extern int z180copro_intraised(struct z180copro *c);
extern struct z180copro *z180copro_create(void);
extern void z180copro_free(struct z180copro *c);
extern void z180copro_trace(struct z180copro *c, int onoff);
extern void z180copro_attach_sd(struct z180copro *c, int fd);
