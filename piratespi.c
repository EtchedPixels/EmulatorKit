#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#include "piratespi.h"

struct piratespi {
	int fd;
	unsigned int cs;
};

static int pirate_open(struct piratespi *spi, const char *path)
{
	struct termios tc;
	spi->fd = open(path, O_RDWR);
	if (spi->fd == -1) {
		perror(path);
		return -1;
	}
	if (tcgetattr(spi->fd, &tc) == -1) {
		perror("tcgetattr");
		close(spi->fd);
		spi->fd = -1;
		return -1;
	}
	tc.c_iflag = 0;
	tc.c_oflag = 0;
	tc.c_cflag = CS8 | CREAD | B115200 | CLOCAL;
	tc.c_lflag = 0;
	tc.c_cc[VMIN] = 0;
	tc.c_cc[VTIME] = 1;
	if (tcsetattr(spi->fd, TCSANOW, &tc) == -1) {
		perror("tcsetattr");
		close(spi->fd);
		spi->fd = -1;
		return -1;
	}
	return 0;
}

static void flush_pirate(struct piratespi *spi)
{
	char c;
	while (read(spi->fd, &c, 1) == 1);
}

static void send_pirate(struct piratespi *spi, const char *p)
{
	/* TODO - check short writes and do blocking around them */
//    printf(">\"%s\"\n", p);
//    fflush(stdout);
	write(spi->fd, p, strlen(p));
}

static void send_pirate_byte(struct piratespi *spi, uint8_t c)
{
//    printf(">%02X", c);
//    fflush(stdout);
	write(spi->fd, &c, 1);
}

static int wait_pirate_byte(struct piratespi *spi)
{
	uint8_t c;
	if (read(spi->fd, &c, 1) != 1) {
//        printf("!");
//        fflush(stdout);
		return -1;
	}
//    printf("<%02X", c);
//    fflush(stdout);
	return c;
}

static int wait_string(struct piratespi *spi, const char *p)
{
	while (*p && wait_pirate_byte(spi) == *p)
		p++;
	if (*p == 0)
		return 0;
	return -1;
}

static void pirate_close(struct piratespi *spi)
{
	send_pirate_byte(spi, 0x00);
	send_pirate_byte(spi, 0x0F);
	close(spi->fd);
	spi->fd = -1;
}

static int pirate_init(struct piratespi *spi)
{
	int ct = 0;

	send_pirate_byte(spi, 0x00);
	send_pirate_byte(spi, 0x0F);
	flush_pirate(spi);

	do {
		send_pirate_byte(spi, 0);
	} while (wait_pirate_byte(spi) != 'B' && ++ct < 25);
	if (ct < 25 && wait_string(spi, "BIO1") == 0)
		return 0;

	send_pirate(spi, "\n\n\n\n\n\n\n\n\n\n#\n");
	flush_pirate(spi);

	ct = 0;
	do {
		send_pirate_byte(spi, 0);
	} while (wait_pirate_byte(spi) != 'B' && ++ct < 25);
	if (ct == 25)
		return -1;
	if (wait_string(spi, "BIO1"))
		return -1;
	return 0;
}

static int pirate_spi(struct piratespi *spi)
{
	send_pirate_byte(spi, 0x1);
	if (wait_string(spi, "SPI1") == -1)
		return -1;
	send_pirate_byte(spi, 0x61);	/* 250KHz */
	wait_pirate_byte(spi);
	send_pirate_byte(spi, 0x4B);	/* Power, AUX, CS enable */
	wait_pirate_byte(spi);
	send_pirate_byte(spi, 0x8A);	/* 3.3v, 0/1 0 */
	wait_pirate_byte(spi);
	return 0;
}

int piratespi_cs(struct piratespi *spi, int cs)
{
	if (cs)
		send_pirate_byte(spi, 0x03);
	else
		send_pirate_byte(spi, 0x02);
	spi->cs = cs;
	return wait_pirate_byte(spi);
}

int piratespi_alt(struct piratespi *spi, int alt)
{
	uint8_t set = 0x48;	/* Power on, set aux and cs accordingly */
	if (spi->cs)
		set |= 0x01;
	if (alt)
		set |= 0x02;
	send_pirate_byte(spi, set);
	return wait_pirate_byte(spi);
}

int piratespi_txrx(struct piratespi *spi, uint8_t byte)
{
	send_pirate_byte(spi, 0x10);
	wait_pirate_byte(spi);
	send_pirate_byte(spi, byte);
	return wait_pirate_byte(spi);
}

void piratespi_free(struct piratespi *spi)
{
	pirate_close(spi);
	free(spi);
}

struct piratespi *piratespi_create(const char *path)
{
	struct piratespi *spi = malloc(sizeof(struct piratespi));
	if (spi == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	spi->cs = 1;
	if (pirate_open(spi, path)) {
		free(spi);
		return NULL;
	}
	if (pirate_init(spi)) {
		piratespi_free(spi);
		return NULL;
	}
	if (pirate_spi(spi)) {
		piratespi_free(spi);
		return NULL;
	}
	/* Get into a known state */
	piratespi_cs(spi, 1);
	piratespi_alt(spi, 1);
	return spi;
}
