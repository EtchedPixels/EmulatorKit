#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include "serialdevice.h"
#include "ttycon.h"

/*
 *	This replaces the old hard coded serial to tty link
 */
static unsigned con_ready(struct serial_device *dev)
{
	fd_set i, o;
	struct timeval tv;
	unsigned int r = 0;

	FD_ZERO(&i);
	FD_SET(0, &i);
	FD_ZERO(&o);
	FD_SET(1, &o);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(2, &i, &o, NULL, &tv) == -1) {
		if (errno == EINTR)
			return 0;
		perror("select");
		exit(1);
	}
	if (FD_ISSET(0, &i))
		r |= 1;
	if (FD_ISSET(1, &o))
		r |= 2;
	return r;
}

static unsigned con_noready(struct serial_device *dev)
{
	return 0;
}

static uint8_t con_get(struct serial_device *dev)
{
	static uint8_t c;
	if (read(0, (char *)&c, 1) != 1)
		return c;
	if (c == 0x0A)
		c = '\r';
	return c;
}

static uint8_t con_noget(struct serial_device *dev)
{
	return 0x00;
}

static void con_put(struct serial_device *dev, uint8_t c)
{
        write(1, &c, 1);
}

static void con_noput(struct serial_device *dev, uint8_t c)
{
}

struct serial_device console = {
    "Console",
    NULL,
    con_get,
    con_put,
    con_ready
};

struct serial_device console_wo = {
    "Console",
    NULL,
    con_get,
    con_noput,
    con_ready
};

struct serial_device nulldev = {
    "Null",
    NULL,
    con_noget,
    con_noput,
    con_noready
};
