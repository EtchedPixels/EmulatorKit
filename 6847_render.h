
struct m6847_renderer;

extern void m6847_render(struct m6847_renderer *render);
extern void m6847_renderer_free(struct m6847_renderer *render);
extern struct m6847_renderer *m6847_renderer_create(struct m6847 *vdp);
