struct dgvideo;

extern void dgvideo_write(struct dgvideo *dg, uint8_t val);
extern struct dgvideo *dgvideo_create(void);
extern void dgvideo_free(struct dgvideo *dg);

extern uint32_t *dgvideo_get_raster(struct dgvideo *dg);
