struct keymatrix;

uint8_t keymatrix_input(struct keymatrix *km, uint16_t scanbits);
bool keymatrix_SDL2event(struct keymatrix *km, SDL_Event *ev);
void keymatrix_free(struct keymatrix *km);
struct keymatrix *keymatrix_create(unsigned int rows, unsigned int cols, SDL_Keycode *matrix);
void keymatrix_reset(struct keymatrix *km);
void keymatrix_trace(struct keymatrix *km, int onoff);
void keymatrix_add_events(struct keymatrix *km);
void keymatrix_translator(struct keymatrix *km, void (*translator)(SDL_Event *ev));
