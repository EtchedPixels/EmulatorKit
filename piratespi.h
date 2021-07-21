
struct piratespi;

int piratespi_cs(struct piratespi *spi, int cs);
int piratespi_alt(struct piratespi *spi, int alt);
int piratespi_txrx(struct piratespi *sp, uint8_t byte);
void piratespi_free(struct piratespi *spi);
struct piratespi *piratespi_create(const char *path);
