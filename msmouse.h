
/* We don't support more than 1 so this is trivial */
extern struct serial_device msmouse;

extern void msmouse_tick(void);
extern void msmouse_update(int dx, int dy, unsigned buttons);
