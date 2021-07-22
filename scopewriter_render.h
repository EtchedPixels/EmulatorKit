
struct scopewriter_renderer;

extern void scopewriter_render(struct scopewriter_renderer *render);
extern void scopewriter_renderer_free(struct scopewriter_renderer *render);
extern struct scopewriter_renderer *scopewriter_renderer_create(struct scopewriter *dg);
