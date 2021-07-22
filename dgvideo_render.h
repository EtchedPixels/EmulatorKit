
struct dgvideo_renderer;

extern void dgvideo_render(struct dgvideo_renderer *render);
extern void dgvideo_renderer_free(struct dgvideo_renderer *render);
extern struct dgvideo_renderer *dgvideo_renderer_create(struct dgvideo *dg);
