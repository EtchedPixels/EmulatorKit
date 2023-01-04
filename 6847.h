struct m6847;


/* These are pins rather than registers so this is a bit arbitrary. Pick
   them to make the mode bits nicely maskable */
#define M6847_GM0	1
#define M6847_GM1	2
#define M6847_GM2	4
#define M6847_INV	8
#define M6847_INTEXT	16
#define M6847_AS	32
#define M6847_AG	64
#define M6847_CSS	128

#define M6847		1
#define M6847T1		2

extern void m6847_rasterize(struct m6847 *vdg);
extern struct m6847 *m6847_create(unsigned int type);
extern void m6847_free(struct m6847 *vdg);
extern void m6847_reset(struct m6847 *vdg);
extern void m6847_trace(struct m6847 *cdg, int onoff);
extern uint32_t *m6847_get_raster(struct m6847 *vdg);
extern void m6847_set_colourmap(struct m6847 *vdg, uint32_t *cmap);

/* User supplied */
extern uint8_t m6847_get_config(struct m6847 *vdg);
extern uint8_t m6847_video_read(struct m6847 *vdg, uint16_t addr, uint8_t *cfg);
extern uint8_t m6847_font_rom(struct m6847 *vdg, uint8_t sym, unsigned int row);

