
struct tft_dumb
{
	uint8_t cmd;
	uint16_t top;
	uint16_t bottom;
	uint16_t left;
	uint16_t right;
	uint32_t data;
	uint16_t xpos;
	uint16_t ypos;
	unsigned step;

	unsigned width;
	unsigned height;
	unsigned bytesperpixel;	/* Must be 3 right now */

	uint8_t row_port;
	uint8_t col_port;
	uint8_t start_port;
	uint8_t data_port;
	uint32_t *rasterbuffer;

	unsigned trace;
};

extern struct tft_dumb *tft_create(unsigned type);
extern void tft_free(struct tft_dumb *tft);
extern void tft_trace(struct tft_dumb *tft, unsigned onoff);
extern void tft_rasterize(struct tft_dumb *tft);
extern void tft_write(struct tft_dumb *tft, unsigned cd, uint8_t data);

