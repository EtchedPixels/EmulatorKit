struct z80copro {
	Z80Context cpu;
	int unit;
	uint8_t eprom[16384];
	uint8_t ram[8][65536];
	uint16_t latches;
#define MAINT	0x8000
#define ROMEN	0x4000
	uint16_t masterbits;
#define COIRQ		0x8000
#define CONMI		0x4000
#define CORESET		0x2000
	uint8_t rambank;
	int tstates;
	int nmi_pending;
	int irq_pending;
	int trace;
};

#define MAX_COPRO	4

extern void z80copro_reset(struct z80copro *c);
extern uint8_t *z80copro_eprom(struct z80copro *c);
extern void z80copro_run(struct z80copro *c);
extern void z80copro_iowrite(struct z80copro *c, uint16_t addr, uint8_t bits);
extern uint8_t z80copro_ioread(struct z80copro *c, uint16_t addr);
extern int z80copro_intraised(struct z80copro *c);
extern struct z80copro *z80copro_create(void);
extern void z80copro_free(struct z80copro *c);
extern void z80copro_trace(struct z80copro *c, int onoff);
