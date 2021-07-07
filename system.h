/* We may need the context unused for DMA on some platforms */

extern uint8_t mem_read(int unused, uint16_t addr);
extern void mem_write(int unused, uint16_t addr, uint8_t val);
extern uint8_t io_read(int unused, uint16_t port);
extern void io_write(int unused, uint16_t port, uint8_t val);

/* Serial interface from the core serial helpers */

extern unsigned int check_chario(void);
extern unsigned int next_char(void);

/* Interrupt helpers */
extern void recalc_interrupts(void);

/* GUI polling hook */
extern void ui_event(void);

extern volatile int emulator_done;
