struct z80_sio;

extern struct z80_sio *sio_create(void);
extern void sio_destroy(struct z80_sio *sio);
extern void sio_trace(struct z80_sio *sio, unsigned chan, unsigned trace);
extern void sio_attach(struct z80_sio *sio, unsigned chan, struct serial_device *dev);
extern void sio_reset(struct z80_sio *sio);

extern void sio_timer(struct z80_sio *sio);
extern void sio_reti(struct z80_sio *sio);
extern void sio_set_dcd(struct z80_sio *sio, unsigned chan, unsigned onoff);

extern int sio_check_im2(struct z80_sio *sio);

extern uint8_t sio_read(struct z80_sio *sio, uint8_t addr);
extern void sio_write(struct z80_sio *sio, uint8_t addr, uint8_t val);

/* Mappings vary wildly be device. This is the mappings as expected
   by the driver, and thus what should be used for translations */

#define SIOA_D	0x00
#define SIOA_C	0x01
#define SIOB_D	0x02
#define SIOB_C	0x03
