struct m6821;

#define M6821_IRQA		1
#define M6821_IRQB		2

#define M6821_CA1		1
#define M6821_CA2		2
#define M6821_CB1		4
#define M6821_CB2		8

extern int m6821_irq_pending(struct m6821 *pia);
extern void m6821_reset(struct m6821 *pia);
extern uint8_t m6821_read(struct m6821 *pia, uint8_t addr);
extern void m6821_write(struct m6821 *pia, uint8_t addr, uint8_t val);
extern void m6821_set_control(struct m6821 *pia, int cline, int onoff);

extern void m6821_trace(struct m6821 *pia, int onoff);
extern struct m6821 *m6821_create(void);
extern void m6821_free(struct m6821 *pia);

/* User supplied */
extern void m6821_ctrl_change(struct m6821 *pia, uint8_t ctrl);
extern uint8_t m6821_input(struct m6821 *pia, int port);
extern void m6821_output(struct m6821 *pia, uint8_t data);
extern void m6821_strobe(struct m6821 *pia, int pin);
