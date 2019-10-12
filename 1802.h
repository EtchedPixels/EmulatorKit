/*
 *	The 1802 CPU structure
 */

struct cp1802 {
    uint8_t d;
    uint16_t r[16];
    uint8_t p;
    uint8_t x;
    uint8_t df;
    uint8_t ie;
    uint8_t q;
    uint8_t t;
    uint8_t event;
    uint8_t ipend;
    unsigned int mcycles;
};


/* Provided by the platform */
extern uint8_t cp1802_read(struct cp1802 *, uint16_t);
extern void cp1802_write(struct cp1802 *, uint16_t, uint8_t);
extern uint8_t cp1802_ef(struct cp1802 *);
extern void cp1802_q_set(struct cp1802 *);
/* There is no real in or out 0 but we pass it to the user for misuse as
   breakpointing etc */
extern void cp1802_out(struct cp1802 *, uint8_t, uint8_t);
extern uint8_t cp1802_in(struct cp1802 *, uint8_t);
extern uint8_t cp1802_dma_in(struct cp1802 *);
extern void cp1802_dma_out(struct cp1802 *, uint8_t);

/* Provided by the CPU emulation */
extern void cp1802_reset(struct cp1802 *);
extern void cp1802_interrupt(struct cp1802 *, int);
extern int cp1802_run(struct cp1802 *);
extern void cp1802_dma_in_cycle(struct cp1802 *);
extern void cp1802_dma_out_cycle(struct cp1802 *);
