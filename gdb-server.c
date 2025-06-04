/* ================================= *
 * GDB Remote Serial Protocol Server *
 * ================================= */

#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gdb-server.h"

#define GDB_BUFFER_SIZE 512

enum gdb_state {
	GDB_STATE_STOP,
	GDB_STATE_RUN,
};

enum gdb_supported_flags {
	GDB_S_ERROR_MESSAGE	= 1 << 0,
};

struct gdb_server {
	struct gdb_backend *b;

	/* program execution state */
	enum gdb_state state;

	/* the listening socket, if >= 0 */
	int listen;
	/* the connected socket, if >= 0 */
	int client;

	/* are we waiting on an ack? */
	bool waiting_on_ack;
	/* should we use acks? */
	bool use_acks;
	/* features advertised by gdb */
	enum gdb_supported_flags supported;

	/* input buffer and position */
	char input_buf[GDB_BUFFER_SIZE];
	char *input_next;

	/* output buffer and position */
	char output_buf[GDB_BUFFER_SIZE];
	char *output_next;
	/* true when we run out of buffer */
	bool output_overflow;
};

/* ======================================= *
 * Front-end Interface and socket handling *
 * ======================================= */

static const char gdb_c_ack	= '+';
static const char gdb_c_nack	= '-';
static const char gdb_c_start	= '$';
static const char gdb_c_end	= '#';
static const char gdb_c_rle	= '*';
static const char gdb_c_escape	= '}';
static const char gdb_c_mangle	= ' ';

static bool gdb_server_parse_bind(char *bindstr, struct sockaddr_in6 *address);
static void gdb_server_close_listen(struct gdb_server *gdb);
static void gdb_server_close_client(struct gdb_server *gdb);

static void gdb_server_send_buffer(struct gdb_server *gdb);
static bool gdb_server_select(struct gdb_server *gdb, struct timeval *tv);
static char *gdb_server_handle_input(struct gdb_server *gdb);

/* actual debugging logic */
static void gdb_server_handle_packet(struct gdb_server *gdb, struct gdb_packet *p);

/* create a listening gdb server */
struct gdb_server *gdb_server_create(struct gdb_backend *backend, char *bindstr, bool stopped)
{
	struct sockaddr_in6 bind_address;
	int sock = -1;

	if (!backend) {
		goto fail;
	}

	if (!gdb_server_parse_bind(bindstr, &bind_address)) {
		goto fail;
	}

	sock = socket(bind_address.sin6_family, SOCK_STREAM, 0);
	if (sock < 0) {
		goto fail;
	}

	/* allow re-use, so if we exit and restart the address
	   is still available */
	int one = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	/* turn off Nagle's algorithm -- we always send() whole packets
	   and we want them to go out immediately (particularly acks) */
	setsockopt(sock, SOL_TCP, TCP_NODELAY, &one, sizeof(one));

	if (bind(sock, (struct sockaddr*)(&bind_address), sizeof(bind_address)) < 0) {
		goto fail;
	}

	/* max one connection */
	if (listen(sock, 1) < 0) {
		goto fail;
	}

	struct gdb_server *gdb = calloc(1, sizeof(struct gdb_server));
	if (!gdb) {
		goto fail;
	}

	gdb->b = backend;
	gdb->state = stopped ? GDB_STATE_STOP : GDB_STATE_RUN;

	gdb->listen = sock;
	gdb->client = -1;

	gdb->waiting_on_ack = false;
	gdb->use_acks = true;
	gdb->supported = 0;

	gdb->input_next = gdb->input_buf;
	gdb->output_next = gdb->output_buf;
	gdb->output_overflow = false;

	return gdb;

fail:
	if (sock >= 0) {
		close(sock);
	}
	if (backend) {
		if (backend->free) {
			backend->free(backend->ctx);
		}
		free(backend);
	}
	return NULL;
}

/* free the gdb server */
void gdb_server_free(struct gdb_server *gdb)
{
	if (gdb) {
		gdb_server_close_client(gdb);
		gdb_server_close_listen(gdb);
		if (gdb->b->free) {
			gdb->b->free(gdb->b->ctx);
		}
		free(gdb->b);
		free(gdb);
	}
}

/* call once per instruction, before stepping the cpu */
void gdb_server_step(struct gdb_server *gdb, volatile int *done)
{
	do {
		/* 100ms when stopped, no blocking when running */
		struct timeval tv = { 0, (gdb->state == GDB_STATE_STOP) ? 100000 : 0 };
		if (!gdb_server_select(gdb, &tv)) {
			/* server crashed, somehow. set done if we can */
			if (done) {
				*done = 1;
			}
			return;
		}

	} while (gdb->state == GDB_STATE_STOP && !(done && *done));
}

/* notify that memory has been accessed */
void gdb_server_notify(struct gdb_server *gdb, unsigned long addr, unsigned int len, bool write)
{
}

/* parse a [host]:<port> string into a socket address
   if there is no colon, treat the whole thing as a port */
static bool gdb_server_parse_bind(char *bindstr, struct sockaddr_in6 *address)
{
	char *hoststr = NULL;
	char *portstr = bindstr;
	char *colon = strrchr(bindstr, ':');
	struct addrinfo hints = {0};
	struct addrinfo *info = NULL;
	struct addrinfo *chosen = NULL;

	/* split the string at the colon */
	if (colon) {
		*colon = 0;
		if (*bindstr) {
			hoststr = bindstr;
		}
		portstr = colon + 1;
	}

	/* default to ipv4 loopback
	   unfortunately we need two sockets to listen on v4 and v6 loopback */
	if (!hoststr) {
		hoststr = "127.0.0.1";
	}

	hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICHOST;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	/* as a shorthand, address 0 means INADDR_ANY (via AI_PASSIVE) */
	if (hoststr && strcmp(hoststr, "0") == 0) {
		hoststr = NULL;
		hints.ai_flags |= AI_PASSIVE;
	}

	getaddrinfo(hoststr, portstr, &hints, &info);

	/* put the colon back, for error messages to use bindstr later */
	if (colon) {
		*colon = ':';
	}

	if (!info) {
		return false;
	}

	/* choose the first address with the right family */
	chosen = NULL;
	for (struct addrinfo *addr = info; addr; addr = addr->ai_next) {
		if (addr->ai_family != AF_INET && addr->ai_family != AF_INET6) {
			continue;
		}

		/* if we haven't found an address yet, grab this one */
		if (!chosen) {
			chosen = addr;
		}

		/* if we used AI_PASSIVE for INADDR_ANY, prefer the ipv6 addresses
		   (this will accept both v4 and v6 connections) */
		if (hints.ai_flags & AI_PASSIVE) {
			if (chosen->ai_family == AF_INET && addr->ai_family == AF_INET6) {
				chosen = addr;
			}
		}
	}

	if (chosen) {
		memcpy(address, chosen->ai_addr, sizeof(*address));
	}

	freeaddrinfo(info);
	return (chosen != NULL);
}

/* close and unset gdb->listen */
static void gdb_server_close_listen(struct gdb_server *gdb)
{
	if (gdb->listen >= 0) {
		close(gdb->listen);
		gdb->listen = -1;
	}
}

/* close and unset gdb->client */
static void gdb_server_close_client(struct gdb_server *gdb)
{
	if (gdb->client >= 0) {
		close(gdb->client);
		gdb->client = -1;
	}
}

/* send the current output buffer */
static void gdb_server_send_buffer(struct gdb_server *gdb)
{
	if (gdb->client >= 0) {
		size_t len = gdb->output_next - gdb->output_buf;
		if (gdb->output_overflow || send(gdb->client, gdb->output_buf, len, 0) != len) {
			/* error sending or overflow, disconnect */
			gdb_server_close_client(gdb);
			return;
		}

		gdb->waiting_on_ack = gdb->use_acks;
	}
}

/* wait for socket events and dispatch them
   returns true if the server is still alive */
static bool gdb_server_select(struct gdb_server *gdb, struct timeval *tv)
{
	fd_set reads, excepts;
	int maxfd;

	FD_ZERO(&reads);
	FD_ZERO(&excepts);

	if (gdb->client >= 0) {
		/* communicating */
		FD_SET(gdb->client, &reads);
		FD_SET(gdb->client, &excepts);
		maxfd = gdb->client;
	} else if (gdb->listen >= 0) {
		/* listening */
		FD_SET(gdb->listen, &reads);
		FD_SET(gdb->listen, &excepts);
		maxfd = gdb->listen;
	} else {
		/* server has died */
		return false;
	}

	if (select(maxfd + 1, &reads, NULL, &excepts, tv) < 0) {
		/* select error, close everything */
		gdb_server_close_client(gdb);
		gdb_server_close_listen(gdb);
	}

	if (FD_ISSET(gdb->listen, &excepts)) {
		/* exception? close the socket */
		gdb_server_close_listen(gdb);
	}

	if (FD_ISSET(gdb->client, &excepts)) {
		/* exception? close the socket */
		gdb_server_close_client(gdb);
	}

	if (FD_ISSET(gdb->listen, &reads)) {
		/* connection waiting, accept it */
		struct sockaddr_in6 client;
		socklen_t client_len = sizeof(client);
		int sock = accept(gdb->listen, (struct sockaddr*)(&client), &client_len);
		if (sock >= 0) {
			gdb->client = sock;

			/* reset relevant variables */

			gdb->waiting_on_ack = false;
			gdb->use_acks = true;
			gdb->supported = 0;

			gdb->input_next = gdb->input_buf;
			gdb->output_next = gdb->output_buf;
			gdb->output_overflow = false;

			/* stop on connect */
			gdb->state = GDB_STATE_STOP;
		}
	}

	if (FD_ISSET(gdb->client, &reads)) {
		ssize_t amount;
		size_t avail = gdb->input_buf + GDB_BUFFER_SIZE - gdb->input_next;
		amount = read(gdb->client, gdb->input_next, avail);
		if (amount <= 0) {
			/* connection closed on the other end, or we ran out of buffer */
			gdb_server_close_client(gdb);
		} else {
			/* we received data */
			gdb->input_next += amount;
			/* handle repeatedly until nothing is consumed or nothing left */
			while (gdb->input_next > gdb->input_buf) {
				char *unconsumed = gdb_server_handle_input(gdb);
				if (unconsumed == gdb->input_buf) {
					break;
				}
				memmove(gdb->input_buf, unconsumed, gdb->input_next - unconsumed);
				gdb->input_next -= unconsumed - gdb->input_buf;
			}
		}
	}

	/* server is still alive */
	return true;
}

/* handle the data inside input[..input_len]
   return a pointer to the first unconsumed byte
   this also handles nacks and acks */
static char *gdb_server_handle_input(struct gdb_server *gdb)
{
	/* look for the start of a packet, denoted by $ */
	if (gdb->input_buf[0] == gdb_c_start) {
		/* look for the end of packet, # */
		uint8_t checksum = 0;
		char *p;
		for (p = gdb->input_buf + 1; p < gdb->input_next; p++) {
			if (*p == gdb_c_end) {
				break;
			}
			checksum += *p;
		}

		/* make sure we're at the end, and there are two checksum bytes */
		if (*p != gdb_c_end || p + 3 > gdb->input_next) {
			/* end not found, try again with more data later */
			return gdb->input_buf;
		}
		p++;

		/* only bother with the checksum if we use acks */
		if (gdb->use_acks) {
			unsigned int checksum_expected = 0x100;
			sscanf(p, "%02x", &checksum_expected);
			p += 2;
			if (checksum != checksum_expected) {
				/* bad checksum, send nack */
				if (gdb->client >= 0) {
					if (send(gdb->client, &gdb_c_nack, 1, 0) != 1) {
						gdb_server_close_client(gdb);
						return p;
					}
				}

				/* eat this whole packet */
				return p;
			}
		} else {
			/* skip checksum */
			p += 2;
		}

		/* we found a valid packet, ack it */
		if (gdb->use_acks && gdb->client >= 0) {
			if (send(gdb->client, &gdb_c_ack, 1, 0) != 1) {
				gdb_server_close_client(gdb);
				return p;
			}
		}

		/* subtle business: we can write to buf[len] because it contains # */
		struct gdb_packet packet = {
			/* skip the $ */
			gdb->input_buf + 1,
			/* -1 for $, -3 for #CS */
			p - 1 - 3 - gdb->input_buf,
			gdb->input_buf[1],
		};

		/* handle the packet and eat it */
		gdb_server_handle_packet(gdb, &packet);
		return p;
	} else {
		for (char *p = gdb->input_buf; p < gdb->input_next; p++) {
			if (gdb->use_acks && *p == gdb_c_ack) {
				/* ack */
				gdb->waiting_on_ack = false;

				/* reset buffer */
				gdb->output_overflow = false;
				gdb->output_next = gdb->output_buf;
			}
			if (gdb->use_acks && *p == gdb_c_nack) {
				/* nack */
				if (!gdb->waiting_on_ack || gdb->output_next == gdb->output_buf) {
					/* we don't have anything to resend, close connection */
					gdb_server_close_client(gdb);
					return p;
				}

				/* resend buffer */
				gdb_server_send_buffer(gdb);
			}
			if (*p == gdb_c_start) {
				/* found a packet start, discard it */
				return p;
			}
		}

		/* did not find a packet start, discard everything */
		return gdb->input_next;
	}
}

/* ============== *
 * Writing to GDB *
 * ============== */

enum gdb_error {
	GDB_ERR_ARGUMENT,
	GDB_ERR_NOT_FOUND,
};

static void gdb_write_start(struct gdb_server *gdb);
static void gdb_write_end(struct gdb_server *gdb);

static void gdb_write_str(struct gdb_server *gdb, const char *str);
static void gdb_write_bin(struct gdb_server *gdb, const uint8_t *data, size_t len);

static void gdb_write_unsupported(struct gdb_server *gdb);
static void gdb_write_ok(struct gdb_server *gdb);

static void gdb_write_err(struct gdb_server *gdb, enum gdb_error err);
static void gdb_write_errf(struct gdb_server *gdb, enum gdb_error err, const char *fmt, ...) GDB_ATTR_FORMAT(printf, 3, 4);
static void gdb_write_errfv(struct gdb_server *gdb, enum gdb_error err, const char *fmt, va_list args);

/* write a formatted string into the output buffer */
void gdb_writef(struct gdb_server *gdb, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	gdb_writefv(gdb, fmt, args);
	va_end(args);
}

/* write a formatted string into the output buffer, using va_list */
void gdb_writefv(struct gdb_server *gdb, const char *fmt, va_list args)
{
	size_t avail = gdb->output_buf + GDB_BUFFER_SIZE - gdb->output_next;
	size_t amt = vsnprintf(gdb->output_next, avail, fmt, args);
	if (amt >= avail) {
		gdb->output_overflow = true;
	} else {
		gdb->output_next += amt;
	}
}

/* start a new packet in the output buffer */
static void gdb_write_start(struct gdb_server *gdb)
{
	if (gdb->use_acks && gdb->waiting_on_ack) {
		/* can't send a new packet while waiting on the old one */
		gdb_server_close_client(gdb);
		return;
	}

	gdb->output_next = gdb->output_buf;
	gdb->output_overflow = false;
	*(gdb->output_next++) = gdb_c_start;
}

/* finish a packet, compute the checksum, and send it */
static void gdb_write_end(struct gdb_server *gdb)
{
	if (gdb->output_next + 3 > gdb->output_buf + GDB_BUFFER_SIZE) {
		/* overflow */
		gdb->output_overflow = true;
	} else {
		/* tack on the packet end character and checksum */
		uint8_t checksum = 0;
		for (char *p = gdb->output_buf + 1; p < gdb->output_next; p++) {
			checksum += *p;
		}
		gdb_writef(gdb, "%c%02x", gdb_c_end, checksum);
	}
	gdb_server_send_buffer(gdb);
}

/* put a 0-terminated string into the output buffer */
static void gdb_write_str(struct gdb_server *gdb, const char *str)
{
	size_t len = strlen(str);
	if (gdb->output_next + len > gdb->output_buf + GDB_BUFFER_SIZE) {
		gdb->output_overflow = true;
		return;
	}

	memcpy(gdb->output_next, str, len);
	gdb->output_next += len;
}

/* put an escaped binary string into the output buffer */
static void gdb_write_bin(struct gdb_server *gdb, const uint8_t *data, size_t len)
{
	char *output_end = gdb->output_buf + GDB_BUFFER_SIZE;
	while (len) {
		uint8_t val = *data;

		if (val == gdb_c_start || val == gdb_c_end || val == gdb_c_rle || val == gdb_c_escape) {
			val ^= gdb_c_mangle;
			if (gdb->output_next + 1 > output_end) {
				gdb->output_overflow = true;
				break;
			}

			*(gdb->output_next++) = gdb_c_escape;
		}

		if (gdb->output_next + 1 > output_end) {
			gdb->output_overflow = true;
			break;
		}

		*(gdb->output_next++) = val;
		data++;
		len--;
	}
}

/* write an entire empty packet, indicating last packet is unsupported */
static void gdb_write_unsupported(struct gdb_server *gdb)
{
	gdb_write_start(gdb);
	gdb_write_end(gdb);
}

/* write an entire ok packet */
static void gdb_write_ok(struct gdb_server *gdb)
{
	gdb_write_start(gdb);
	gdb_write_str(gdb, "OK");
	gdb_write_end(gdb);
}

/* write an entire error packet */
static void gdb_write_err(struct gdb_server *gdb, enum gdb_error err)
{
	va_list args;
	gdb_write_errfv(gdb, err, NULL, args);
}

/* write an entire error packet, with extra message */
static void gdb_write_errf(struct gdb_server *gdb, enum gdb_error err, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	gdb_write_errfv(gdb, err, fmt, args);
	va_end(args);
}

/* gdb_write_errf but with va_list */
static void gdb_write_errfv(struct gdb_server *gdb, enum gdb_error err, const char *fmt, va_list args)
{
	gdb_write_start(gdb);
	if (gdb->supported & GDB_S_ERROR_MESSAGE) {
		gdb_write_str(gdb, "E.");
		gdb_writef(gdb, "error %02x: ", err);

		const char *msg = "unknown";
		switch (err) {
		case GDB_ERR_ARGUMENT:
			msg = "bad argument";
			break;
		case GDB_ERR_NOT_FOUND:
			msg = "not found";
			break;
		}

		gdb_write_str(gdb, msg);
		if (fmt) {
			gdb_write_str(gdb, ": ");
			gdb_writefv(gdb, fmt, args);
		}
	} else {
		gdb_writef(gdb, "E%02x", err);
	}
	gdb_write_end(gdb);
}

/* ============== *
 * Packet Parsing *
 * ============== */

static void gdb_packet_restore(struct gdb_packet *p, struct gdb_packet *other);

static const char *gdb_packet_take(struct gdb_packet *p, size_t amt);
static bool gdb_packet_skip(struct gdb_packet *p, size_t amt);
static bool gdb_packet_match(struct gdb_packet *p, const char *prefix);
static const char *gdb_packet_until(struct gdb_packet *p, const char *delimiters);
static bool gdb_packet_binary(struct gdb_packet *p);

/* slightly different from scanf, used like:

   int end;
   gdb_packet_scanf(p, &end, "some format", [...]);
   // ... or ...
   _gdb_packet_scanf(p, &end, "some format%n", [...], &end);

   never matches an empty string.
   returns true and consumes up to end on success.
    on failure, returns false and end == -1 */
bool _gdb_packet_scanf(struct gdb_packet *p, int *end, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	bool success = _gdb_packet_scanfv(p, end, fmt, args);
	va_end(args);
	return success;
}

/* _gdb_packet_scanf but with va_list */
bool _gdb_packet_scanfv(struct gdb_packet *p, int *end, const char *fmt, va_list args)
{
	*end = -1;

	/* never match an empty string */
	if (!p->len) {
		return false;
	}

	/* careful: p->len > 0, so setting p->buf[p->len] doesn't touch p->c */
	char last = p->buf[p->len];
	p->buf[p->len] = 0;
	gdb_packet_restore(p, NULL);

	if (vsscanf(p->buf, fmt, args) < 0 || *end <= 0) {
		p->buf[p->len] = last;
		*end = -1;
		return false;
	}

	p->buf[p->len] = last;
	gdb_packet_skip(p, *end);
	return true;
}

/* restore packet state from backup.
   other may be NULL, which only resets buf[0].
   usage:

   struct gdb_packet backup = *p;
   ...;
   gdb_packet_restore(p, &backup); */
static void gdb_packet_restore(struct gdb_packet *p, struct gdb_packet *other)
{
	p->buf[0] = p->c;
	if (other) {
		*p = *other;
	}
}

/* consumes and returns amt bytes, or NULL if not available */
static const char *gdb_packet_take(struct gdb_packet *p, size_t amt)
{
	if (p->len < amt) {
		p->buf += p->len;
		p->len = 0;
		p->c = p->buf[0];
		return NULL;
	}

	/* return the current buffer, restoring buf[0] */
	gdb_packet_restore(p, NULL);
	char *ret = p->buf;

	/* advance by amt and drop a 0 */
	p->buf += amt;
	p->len -= amt;
	p->c = p->buf[0];
	p->buf[0] = 0;

	return ret;
}

/* skips amt bytes. returns true if successful */
static bool gdb_packet_skip(struct gdb_packet *p, size_t amt)
{
	if (p->len < amt) {
		p->buf += p->len;
		p->len = 0;
		p->c = p->buf[0];
		return false;
	}

	/* advance by amt, don't bother restoring buf[0] */
	p->buf += amt;
	p->len -= amt;
	p->c = p->buf[0];

	return true;
}

/* returns true if packet matches prefix. consumes prefix */
static bool gdb_packet_match(struct gdb_packet *p, const char *prefix)
{
	size_t len = strlen(prefix);
	gdb_packet_restore(p, NULL);
	if (strncmp(p->buf, prefix, len) == 0) {
		gdb_packet_skip(p, len);
		return true;
	}
	return false;
}

/* return bytes up to (not including) any delimiter
   or up to the end of the packet
   on success, consumes up to and including the delimiter */
static const char *gdb_packet_until(struct gdb_packet *p, const char *delimiters)
{
	size_t i;
	gdb_packet_restore(p, NULL);
	for (i = 0; i < p->len; i++) {
		if (strchr(delimiters, p->buf[i])) {
			break;
		}
	}

	/* end of string or a delimiter is in p->buf[i] */
	const char *ret = gdb_packet_take(p, i);

	/* skip delimiter */
	gdb_packet_skip(p, 1);

	return ret;
}

/* convert the rest of the packet into unescaped binary data.
   returns true on success and leaves data in p->buf / p->len  */
static bool gdb_packet_binary(struct gdb_packet *p)
{
	char *src = p->buf;
	char *src_end = p->buf + p->len;
	char *dest = p->buf;
	gdb_packet_restore(p, NULL);

	while (src < src_end) {
		if (*src == gdb_c_escape) {
			src++;
			if (src >= src_end) {
				/* unpaired escape */
				return false;
			}

			*src ^= gdb_c_mangle;
			p->len--;
		}

		*(dest++) = *(src++);
	}

	return true;
}

/* =============================== *
 * Packet Handling and Debug Logic *
 * =============================== */

/* why is the target stopped: ? */
static void gdb_handle_why(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	if (!gdb_packet_end(p)) {
		gdb_write_err(gdb, GDB_ERR_ARGUMENT);
		return;
	}

	gdb_write_start(gdb);
	gdb_write_str(gdb, "S05");
	gdb_write_end(gdb);
}

/* read registers: g */
static void gdb_handle_read_registers(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	if (!gdb->b->get_reg) {
		gdb_write_unsupported(gdb);
		return;
	}

	if (!gdb_packet_end(p)) {
		gdb_write_err(gdb, GDB_ERR_ARGUMENT);
		return;
	}

	gdb_write_start(gdb);
	for (unsigned int r = 0; r < gdb->b->register_max; r++) {
		gdb->b->get_reg(gdb->b->ctx, gdb, r);
	}
	gdb_write_end(gdb);
}

/* write registers: G data */
static void gdb_handle_write_registers(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	if (!gdb->b->set_reg) {
		gdb_write_unsupported(gdb);
		return;
	}

	for (unsigned int r = 0; r < gdb->b->register_max; r++) {
		gdb_packet_restore(p, NULL);
		if (!gdb->b->set_reg(gdb->b->ctx, p, r)) {
			gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "register values");
			return;
		}
	}

	if (!gdb_packet_end(p)) {
		gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "register values");
		return;
	}

	gdb_write_ok(gdb);
}

/* memory read:  m addr, length
   memory write: M addr, length: data */
static void gdb_handle_memory(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	int end;
	unsigned long addr, len;

	if ((upper && !gdb->b->write_mem) || (!upper && !gdb->b->read_mem)) {
		gdb_write_unsupported(gdb);
		return;
	}

	if (!gdb_packet_scanf(p, &end, "%lx,%lx", &addr, &len)) {
		gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "address or length");
		return;
	}

	if (upper) {
		if (!gdb_packet_scanf(p, &end, ":") || p->len != 2 * len) {
			gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "data");
			return;
		}

		while (len) {
			uint8_t val;
			gdb_packet_scanf(p, &end, "%02hhx", &val);
			gdb->b->write_mem(gdb->b->ctx, addr, val);
			addr++;
			len--;
		}
		gdb_write_ok(gdb);
	} else {
		if (!gdb_packet_end(p)) {
			gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "length");
			return;
		}

		gdb_write_start(gdb);
		while (len) {
			uint8_t val = gdb->b->read_mem(gdb->b->ctx, addr);
			gdb_writef(gdb, "%02x", val);
			addr++;
			len--;
		}
		gdb_write_end(gdb);
	}
}

/* register read:  p n
   register write: P n=r */
static void gdb_handle_register(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	int end;
	unsigned int reg;

	if (upper) {
		if (!gdb->b->set_reg) {
			gdb_write_unsupported(gdb);
			return;
		}

		if (!gdb_packet_scanf(p, &end, "%x=", &reg)) {
			gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "register");
			return;
		}

		if (reg >= gdb->b->register_max) {
			gdb_write_errf(gdb, GDB_ERR_NOT_FOUND, "register %i", reg);
			return;
		}

		if (!gdb->b->set_reg(gdb->b->ctx, p, reg)) {
			gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "value");
			return;
		}

		if (!gdb_packet_end(p)) {
			gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "value");
			return;
		}

		gdb_write_ok(gdb);
	} else {
		if (!gdb->b->get_reg) {
			gdb_write_unsupported(gdb);
			return;
		}

		if (!gdb_packet_scanf(p, &end, "%x", &reg)) {
			gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "register");
			return;
		}

		if (!gdb_packet_end(p)) {
			gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "register");
			return;
		}

		if (reg >= gdb->b->register_max) {
			gdb_write_errf(gdb, GDB_ERR_NOT_FOUND, "register %i", reg);
			return;
		}

		gdb_write_start(gdb);
		gdb->b->get_reg(gdb->b->ctx, gdb, reg);
		gdb_write_end(gdb);
	}
}

/* memory crc-32: qCRC: addr, length */
static void gdb_handle_q_crc(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	int end;
	unsigned long addr, len;

	if (!gdb->b->read_mem) {
		gdb_write_unsupported(gdb);
		return;
	}

	if (!gdb_packet_scanf(p, &end, ":%lx,%lx", &addr, &len) || !gdb_packet_end(p)) {
		gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "address or length");
		return;
	}

	/* MSB-first CRC32 from IEEE 802.3, inverted input, normal output */
	uint32_t crc = ~0;
	while (len) {
		uint32_t val = gdb->b->read_mem(gdb->b->ctx, addr);
		addr++;
		len--;

		crc ^= val << 24;

		crc = (crc << 1) ^ ((crc & (1 << 31)) ? 0x04c11db7 : 0);
		crc = (crc << 1) ^ ((crc & (1 << 31)) ? 0x04c11db7 : 0);
		crc = (crc << 1) ^ ((crc & (1 << 31)) ? 0x04c11db7 : 0);
		crc = (crc << 1) ^ ((crc & (1 << 31)) ? 0x04c11db7 : 0);

		crc = (crc << 1) ^ ((crc & (1 << 31)) ? 0x04c11db7 : 0);
		crc = (crc << 1) ^ ((crc & (1 << 31)) ? 0x04c11db7 : 0);
		crc = (crc << 1) ^ ((crc & (1 << 31)) ? 0x04c11db7 : 0);
		crc = (crc << 1) ^ ((crc & (1 << 31)) ? 0x04c11db7 : 0);
	}

	gdb_write_start(gdb);
	gdb_writef(gdb, "C%08x", crc);
	gdb_write_end(gdb);
}

/* memory search: qSearch:memory: address; length; pattern */
static void gdb_handle_q_search_memory(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	int end;
	unsigned long addr, len;

	if (!gdb->b->read_mem) {
		gdb_write_unsupported(gdb);
		return;
	}

	if (!gdb_packet_scanf(p, &end, ":%lx;%lx;", &addr, &len)) {
		gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "address or length");
		return;
	}

	if (!gdb_packet_binary(p) || p->len == 0) {
		gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "pattern");
		return;
	}

	uint8_t *pat = (uint8_t *)p->buf;
	uint8_t *pat_start = pat;
	uint8_t *pat_end = pat + p->len;
	while (len) {
		uint8_t val = gdb->b->read_mem(gdb->b->ctx, addr);
		addr++;
		len--;

		if (*pat == val) {
			pat++;
			if (pat >= pat_end) {
				/* we found it, at addr - p->len */
				gdb_write_start(gdb);
				gdb_writef(gdb, "1,%lx", addr - p->len);
				gdb_write_end(gdb);
				return;
			}
		} else {
			pat = pat_start;
		}
	}

	/* we did not find it */
	gdb_write_start(gdb);
	gdb_write_str(gdb, "0");
	gdb_write_end(gdb);
}

/* disable acks: QStartNoAckMode */
static void gdb_handle_q_start_no_ack_mode(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	if (!gdb_packet_end(p)) {
		gdb_write_err(gdb, GDB_ERR_ARGUMENT);
		return;
	}

	gdb->use_acks = false;
	gdb_write_ok(gdb);
}

/* feature definitions for qSupported */
struct gdb_feature {
	enum gdb_supported_flags flag;
	const char *gdbfeature;
	const char *stubfeature;
	bool end;
};

static const struct gdb_feature gdb_features[] = {
	{GDB_S_ERROR_MESSAGE, "error-message+", "error-message+", false},

	{0, NULL, "binary-upload?", false},
	{0, NULL, "QStartNoAckMode+", false},
	{0, NULL, "qXfer:features:read?", false},

	{ .end = true },
};

/* supported features: qSupported [:gdbfeature [;gdbfeature]] */
static void gdb_handle_q_supported(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	int end;
	gdb->supported = 0;
	if (p->len && !gdb_packet_scanf(p, &end, ":")) {
		/* something follows but it's not features */
		gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "features");
		return;
	}

	/* some features follow */
	const char *feature;
	while ((feature = gdb_packet_until(p, ";")) && *feature) {
		for (const struct gdb_feature *feat = gdb_features; !feat->end; feat++) {
			if (feat->gdbfeature && strcmp(feature, feat->gdbfeature) == 0) {
				gdb->supported |= feat->flag;
			}
		}
	}

	/* report our features */
	gdb_write_start(gdb);

	/* our buffer size, excluding room for framing, checksum, and 0 */
	gdb_writef(gdb, "PacketSize=%x", GDB_BUFFER_SIZE - 2 - 2 - 1);

	/* the rest of our features */
	for (const struct gdb_feature *feat = gdb_features; !feat->end; feat++) {
		if (feat->stubfeature && (feat->flag == 0 || (gdb->supported & feat->flag))) {
			gdb_write_str(gdb, ";");
			gdb_write_str(gdb, feat->stubfeature);
		}
	}

	gdb_write_end(gdb);
}

/* read target description: qXfer:features:read: annex: offset, length */
static void gdb_handle_qxfer_features_read(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	int end;
	char annex[64];
	unsigned long offset, len;

	if (!gdb->b->target_description) {
		gdb_write_unsupported(gdb);
		return;
	}

	if (!gdb_packet_scanf(p, &end, ":%63[^:]:%lx,%lx", annex, &offset, &len) || !gdb_packet_end(p)) {
		gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "annex, offset, or length");
		return;
	}

	if (strcmp(annex, "target.xml") != 0) {
		gdb_write_errf(gdb, GDB_ERR_NOT_FOUND, "annex %s", annex);
		return;
	}

	/* some buffer math: leave room for $m and #CS\0 */
	size_t max_len = GDB_BUFFER_SIZE - 6;
	/* pessimistically, all characters need to be escaped */
	max_len /= 2;
	if (len > max_len) {
		len = max_len;
	}

	/* fit offset, amt into the available data */
	size_t source_len = strlen(gdb->b->target_description);
	size_t amt = len;
	if (offset > source_len) {
		offset = source_len;
	}
	if (offset + amt > source_len) {
		amt = source_len - offset;
	}

	gdb_write_start(gdb);
	if (amt < len) {
		/* this is all of it */
		gdb_write_str(gdb, "l");
	} else {
		/* there is still some more */
		gdb_write_str(gdb, "m");
	}
	gdb_write_bin(gdb, (const uint8_t *)gdb->b->target_description, amt);
	gdb_write_end(gdb);
}

/* binary memory read:  x addr, length
   binary memory write: X addr, length: data */
static void gdb_handle_binary_memory(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	int end;
	unsigned long addr, len;

	if ((upper && !gdb->b->write_mem) || (!upper && !gdb->b->read_mem)) {
		gdb_write_unsupported(gdb);
		return;
	}

	if (!gdb_packet_scanf(p, &end, "%lx,%lx", &addr, &len)) {
		gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "address or length");
		return;
	}

	if (upper) {
		if (!gdb_packet_scanf(p, &end, ":") || !gdb_packet_binary(p) || p->len != len) {
			gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "data");
			return;
		}

		while (p->len) {
			gdb->b->write_mem(gdb->b->ctx, addr, *(p->buf));
			addr++;
			p->buf++;
			p->len--;
		}
		gdb_write_ok(gdb);
	} else {
		if (!gdb_packet_end(p)) {
			gdb_write_errf(gdb, GDB_ERR_ARGUMENT, "length");
			return;
		}

		gdb_write_start(gdb);
		gdb_write_str(gdb, "b");
		while (len) {
			uint8_t val = gdb->b->read_mem(gdb->b->ctx, addr);
			gdb_write_bin(gdb, &val, 1);
			addr++;
			len--;
		}
		gdb_write_end(gdb);
	}
}

struct gdb_handler {
	const char *cmd;
	void (*handle)(struct gdb_server *gdb, struct gdb_packet *p, bool upper);
};

static const struct gdb_handler gdb_handlers[] = {
	{"?", gdb_handle_why},
	{"g", gdb_handle_read_registers},
	{"G", gdb_handle_write_registers},
	{"m", gdb_handle_memory},
	{"M", gdb_handle_memory},
	{"p", gdb_handle_register},
	{"P", gdb_handle_register},
	{"qCRC", gdb_handle_q_crc},
	{"qSearch:memory", gdb_handle_q_search_memory},
	{"QStartNoAckMode", gdb_handle_q_start_no_ack_mode},
	{"qSupported", gdb_handle_q_supported},
	{"qXfer:features:read", gdb_handle_qxfer_features_read},
	{"x", gdb_handle_binary_memory},
	{"X", gdb_handle_binary_memory},
	{ 0 },
};

/* handle a complete packet from GDB */
static void gdb_server_handle_packet(struct gdb_server *gdb, struct gdb_packet *p)
{
	bool upper = isupper(p->c);

	gdb_packet_restore(p, NULL);
	for (const struct gdb_handler *handler = gdb_handlers; handler->cmd; handler++) {
		if (gdb_packet_match(p, handler->cmd)) {
			handler->handle(gdb, p, upper);
			return;
		}
	}

	/* unknown packet */
	gdb_write_unsupported(gdb);
}
