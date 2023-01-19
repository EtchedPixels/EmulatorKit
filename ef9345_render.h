
struct ef9345_renderer;

extern void ef9345_render(struct ef9345_renderer *render);
extern void ef9345_renderer_free(struct ef9345_renderer *render);
extern struct ef9345_renderer *ef9345_renderer_create(struct ef9345 *ef9345);
