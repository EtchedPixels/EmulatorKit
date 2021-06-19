struct zxkey;

uint8_t zxkey_scan(struct zxkey *zx, uint16_t addr);
void zxkey_reset(struct zxkey *zx);
struct zxkey *zxkey_create(void);
void zxkey_trace(struct zxkey *zx, int onoff);

#ifdef WITH_SDL
bool zxkey_SDL2event(struct zxkey *zx, SDL_Event *ev);
#endif
