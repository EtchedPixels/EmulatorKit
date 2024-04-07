struct z80_sio;

extern struct z80_sio *sio_create(void);
extern void sio_destroy(struct z80_sio *sio);
extern void sio_trace(struct z80_sio *sio, unsigned chan, unsigned trace);
extern void sio_attach(struct z80_sio *sio, unsigned chan, struct serial_device *dev);
extern void sio_reset(struct z80_sio *sio);

extern void sio_timer(struct z80_sio *sio);
extern void sio_reti(struct z80_sio *sio);
extern int sio_check_im2(struct z80_sio *sio);

extern uint8_t sio_read(struct z80_sio *sio, uint8_t addr);
extern void sio_write(struct z80_sio *sio, uint8_t addr, uint8_t val);


