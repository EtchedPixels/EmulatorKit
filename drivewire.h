extern uint8_t drivewire_tx(void);
extern void drivewire_rx(uint8_t c);
extern void drivewire_init(void);
extern void drivewire_shutdown(void);
extern int drivewire_attach(unsigned drive, const char *path, unsigned ro);
extern void drivewire_detach(unsigned drive);

/* Platform provided */
extern void drivewire_byte_pending(void);
extern void drivewire_byte_read(void);
