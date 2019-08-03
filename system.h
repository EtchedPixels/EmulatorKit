
/* We may need the context unused for DMA on some platforms */

extern uint8_t mem_read(int unused, uint16_t addr);
extern void mem_write(int unused, uint16_t addr, uint8_t val);
extern uint8_t io_read(int unused, uint16_t port);
extern void io_write(int unused, uint16_t port, uint8_t val);
