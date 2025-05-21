
struct tms9918a_renderer;


extern void tms9918a_render(struct tms9918a_renderer *render);
extern void tms9918a_renderer_free(struct tms9918a_renderer *render);
extern struct tms9918a_renderer *tms9918a_renderer_create(struct tms9918a *vdp);
