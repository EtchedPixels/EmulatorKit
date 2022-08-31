/*
 *	68HC11 timer and I/O structures
 */

struct prescaler {
    uint16_t count;
    uint16_t limit;
};

struct  m68hc11 {
    /* Timer chain */
    struct prescaler pr_tcnt;
    struct prescaler e13;
    struct prescaler rti;
    struct prescaler cop;

    uint16_t lock;
    uint16_t flags;
#define CPUIO_HC811_CONFIG	1	/* Device has HC811 style CONFIG reg */

    uint8_t *eerom;
    const uint8_t *rom;
    const uint8_t *bootrom;

    /* I/O ports */
    uint16_t iobase;
    uint16_t ioend;
    uint16_t irambase;
    uint16_t iramend;
    uint16_t iramsize;
    uint16_t erombase;
    uint16_t eromend;
    uint16_t rombase;

    /* We don't model non expanded mode */
    uint8_t padr;
    uint8_t paddr;
    uint8_t pioc;
    uint8_t ddrc;
    uint8_t pddr;
    uint8_t ddrd;
    uint8_t pedr;

    /* Timer control force */
    uint8_t cforc;

    /* Output compare */
    uint8_t oc1m;
    uint8_t oc1d;

    /* Timer events */
    uint16_t tcnt;
    uint16_t tic1;
    uint16_t tic2;
    uint16_t tic3;

    uint16_t toc1;
    uint16_t toc2;
    uint16_t toc3;
    uint16_t toc4;
    uint16_t toc5;

    uint8_t tctl1;
    uint8_t tctl2;
    
    uint8_t tmsk1;
    uint8_t tflg1;
#define TF1_OC1F		0x80
#define TF1_OC2F		0x40
#define TF1_OC3F		0x20
#define TF1_OC4F		0x10
#define TF1_OC5F		0x08
#define TF1_IC1F		0x04
#define TF1_IC2F		0x02
#define TF1_IC3F		0x01

    uint8_t tmsk2;
    uint8_t tflg2;
#define TF2_TOF			0x80
#define TF2_RTIF		0x40
#define TF2_PAOVF		0x20
#define TF2_PAIF		0x10

    /* Pulse accumulator */
    uint8_t pactl;
    uint8_t pacnt;

    /* SPI */
    uint8_t spcr;
#define SPCR_SPIE		0x80
#define SPCR_SPE		0x40
#define SPCR_MSTR		0x10
#define SPCR_SPR		0x03
    uint8_t spsr;
#define SPSR_SPIF		0x80
    uint8_t spdr_r;
    uint8_t spdr_w;
    /* our internal timer: not a real register */
    uint16_t spi_ticks;

    /* Serial */
    uint8_t baud;
    uint8_t sccr1;
    uint8_t sccr2;
#define SCCR2_TIE		0x80
#define SCCR2_TCIE		0x40
#define SCCR2_RIE		0x20
#define SCCR2_ILIE		0x10
#define SCCR2_TE		0x08
#define SCCR2_RE		0x04
#define SCCR2_RWU		0x02
#define SCCR2_SBK		0x01
    uint8_t scdr_w;
    uint8_t scdr_r;
    uint8_t scsr;
#define SCSR_TDRE		0x80
#define SCSR_TC			0x40
#define SCSR_RDRF		0x20
#define SCSR_OR			0x08
    /* Internal implementation help value - not a register */
    uint8_t last_scsr_read;

    /* A2D convertors */    
    uint8_t adctl;
    uint8_t adr1;
    uint8_t adr2;
    uint8_t adr3;
    uint8_t adr4;

    /* System Control */
    uint8_t bprot;
    uint8_t eprog;
    uint8_t option;
    uint8_t coprst;		/* We use this to hold the last write */
    uint8_t pprog;
    uint8_t hprio;
#define HPRIO_RBOOT	0x80
#define HPRIO_SMOD	0x40
#define HPRIO_MDA	0x20
#define HPRIO_IRV	0x10
    uint8_t init;
    uint8_t config;
#define CFG_NOSEC	0x08
#define CFG_NOCOP	0x04
#define CFG_ROMON	0x02
#define CFG_EEON	0x01

    uint8_t config_latch;	/* Actual boot latches we really use */
};    

#define HC11_VEC_SCI		0xFFD6
#define HC11_VEC_SPI		0xFFD8
#define HC11_VEC_PAIE		0xFFDA
#define HC11_VEC_PAOV		0xFFDC
#define HC11_VEC_TOI		0xFFDE
#define HC11_VEC_TIC4O5		0xFFE0
#define HC11_VEC_TOC4		0xFFE2
#define HC11_VEC_TOC3		0xFFE4
#define HC11_VEC_TOC2		0xFFE6
#define HC11_VEC_TOC1		0xFFE8
#define HC11_VEC_TIC3		0xFFEA
#define HC11_VEC_TIC2		0xFFEC
#define HC11_VEC_TIC1		0xFFEE
#define HC11_VEC_RTI		0xFFF0
#define HC11_VEC_IRQ		0xFFF2
#define HC11_VEC_XIRQ		0xFFF4
#define HC11_VEC_SWI		0xFFF6
#define HC11_VEC_ILL		0xFFF8
#define HC11_VEC_COP		0xFFFA
#define HC11_VEC_CME		0xFFFC
#define HC11_VEC_RESET		0xFFFE

/*
 *	6800 processor state
 */

struct m6800 {
    uint8_t a;
    uint8_t b;
    uint8_t p;
    uint16_t s;
    uint16_t x;
    uint16_t y;
    uint16_t pc;

    /* Internal state */
    int wait;
    int oc_hold;
    int type;
#define CPU_6800	0
#define CPU_6803	1
#define	CPU_6303	2
#define CPU_6XA1	3	/* NAMCO 6301 variant with op 12/13 */
#define	CPU_68HC11	4	/* May need a special type for some types */
    int intio;
#define INTIO_NONE	0
#define INTIO_6802	1
#define INTIO_6803	2
#define INTIO_HC11	3
    uint32_t irq;
    uint8_t mode;
    int debug;

    /* I/O and memory */
    uint8_t iram_base;
    uint8_t iram[768];		/* Can be 192 bytes on late 6303, 768 on HC11 */

    uint8_t p1ddr;
    uint8_t p2ddr;
    uint8_t p1dr;
    uint8_t p2dr;
    uint8_t tcsr;
    uint16_t counter;
    uint16_t ocr;
    uint8_t rmcr;
    uint8_t trcsr;
    uint8_t rdr;
    uint8_t tdr;
    uint8_t ramcr;

    struct m68hc11 io;		/* Need to make this a nice union of CPU
                                   variants eventually */
};

#define P_C		1
#define P_V		2
#define P_Z		4
#define P_N		8
#define P_I		16
#define P_H		32
#define P_X		64
#define P_S		128

#define IRAM_BASE_6803	0x80
#define IRAM_BASE_6303X	0x40

#define TCSR_OLVL	0x01
#define TCSR_IEDG	0x02
#define TCSR_ETOI	0x04
#define TCSR_EOCI	0x08
#define TCSR_EICI	0x10
#define TCSR_TOF	0x20
#define TCSR_OCF	0x40
#define TCSR_ICF	0x80

#define TRCSR_WU	0x01
#define TRCSR_TE	0x02
#define TRCSR_TIE	0x04
#define TRCSR_RE	0x08
#define TRCSR_RIE	0x10
#define TRCSR_TDRE	0x20
#define TRCSR_ORFE	0x40
#define TRCSR_RDRF	0x80

#define RAMCR_RAME	0x40

#define IRQ_NMI		0x01
#define IRQ_IRQ1	0x02
#define IRQ_ICF		0x04
#define IRQ_OCF		0x08
#define IRQ_TOF		0x10
#define IRQ_SCI		0x20
#define IRQ_SPI		0x40
#define IRQ_RTI		0x80
#define IRQ_OC1		0x100
#define IRQ_OC2		0x200
#define IRQ_OC3		0x400
#define IRQ_OC4		0x800
#define IRQ_IC4OC5	0x1000
#define IRQ_IC1		0x2000
#define IRQ_IC2		0x4000
#define IRQ_IC3		0x8000
#define IRQ_PAOV	0x10000
#define IRQ_PAI		0x20000

extern uint8_t m6800_read(struct m6800 *cpu, uint16_t addr);
extern uint8_t m6800_debug_read(struct m6800 *cpu, uint16_t addr);
extern void m6800_write(struct m6800 *cpu, uint16_t addr, uint8_t data);

extern void m6800_sci_change(struct m6800 *cpu);
extern void m6800_tx_byte(struct m6800 *cpu, uint8_t byte);
extern void m6800_port_output(struct m6800 *cpu, int port);
extern uint8_t m6800_port_input(struct m6800 *cpu, int port);

extern void m68hc11_port_direction(struct m6800 *cpu, int port);
extern void m68hc11_spi_begin(struct m6800 *cpu, uint8_t out);
extern uint8_t m68hc11_spi_done(struct m6800 *cpu);

extern void m6800_tx_done(struct m6800 *cpu);
extern void m68hc11_tx_done(struct m6800 *cpu);

/* Provided by the 6800 emulator */
extern void m6800_reset(struct m6800 *cpu, int type, int mode, int io);
extern void m68hc11a_reset(struct m6800 *cpu, int variant, uint8_t cfg, const uint8_t *rom, uint8_t *eerom);
extern void m68hc11e_reset(struct m6800 *cpu, int variant, uint8_t cfg, const uint8_t *rom, uint8_t *eerom);
extern int m6800_execute(struct m6800 *cpu);
extern int m68hc11_execute(struct m6800 *cpu);
extern void m6800_clear_interrupt(struct m6800 *cpu, int irq);
extern void m6800_raise_interrupt(struct m6800 *cpu, int irq);
extern void m6800_rx_byte(struct m6800 *cpu, uint8_t byte);
extern void m68hc11_rx_byte(struct m6800 *cpu, uint8_t byte);

/* These are more internal but useful for debug/trace */
extern void m6800_do_write(struct m6800 *cpu, uint16_t addr, uint8_t val);
extern uint8_t m6800_do_read(struct m6800 *cpu, uint16_t addr);
extern uint8_t m6800_do_debug_read(struct m6800 *cpu, uint16_t addr);
