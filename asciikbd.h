struct asciikbd;

extern struct asciikbd *asciikbd_create(void);
extern void asciikbd_free(struct asciikbd *kbd);
extern uint8_t asciikbd_read(struct asciikbd *kbd);
extern unsigned int asciikbd_ready(struct asciikbd *kbd);
extern void asciikbd_ack(struct asciikbd *kbd);
extern void asciikbd_bind(struct asciikbd *kbd, uint32_t window_id);
