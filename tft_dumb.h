
struct tft_dumb
{
    uint8_t cmd;
    uint16_t row;
    uint16_t col;
    uint32_t data;
    unsigned step;

    unsigned width;
    unsigned height;
    unsigned bytesperpixel;	/* Must be 3 right now */

    uint8_t row_port;
    uint8_t col_port;
    uint8_t data_port;
    uint32_t *rasterbuffer;

    unsigned trace;
};

extern struct tft_dumb *tft_create(unsigned type);
extern void tft_free(struct tft_dumb *tft);
extern void tft_trace(struct tft_dumb *tft, unsigned onoff);
extern void tft_rasterize(struct tft_dumb *tft);
extern void tft_write(struct tft_dumb *tft, unsigned cd, uint8_t data);

