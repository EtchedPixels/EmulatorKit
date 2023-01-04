struct zxkey;

uint8_t zxkey_scan(struct zxkey *zx, uint16_t addr);
void zxkey_reset(struct zxkey *zx);
struct zxkey *zxkey_create(unsigned type);
/* 1 = original zxkey 2 = spectrify */
void zxkey_trace(struct zxkey *zx, int onoff);
void zxkey_write(struct zxkey *zx, uint8_t data);

#ifdef WITH_SDL
bool zxkey_SDL2event(struct zxkey *zx, SDL_Event *ev);
#endif
