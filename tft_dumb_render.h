struct tft_renderer;

extern void tft_render(struct tft_renderer *render);
extern void tft_renderer_free(struct tft_renderer *render);
extern struct tft_renderer *tft_renderer_create(struct tft_dumb *tft);
