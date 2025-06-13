extern void add_ui_handler(int (*handler)(void *priv, void *ev), void *private);
extern void remove_ui_handler(int (*handler)(void *priv, void *ev), void *private);
extern unsigned ui_event(void);
extern void ui_init(void);

