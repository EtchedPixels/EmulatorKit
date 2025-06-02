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

		gdb->waiting_on_ack = true;
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

		/* we found a valid packet, ack it */
		if (gdb->client >= 0) {
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
			if (*p == gdb_c_ack) {
				/* ack */
				gdb->waiting_on_ack = false;

				/* reset buffer */
				gdb->output_overflow = false;
				gdb->output_next = gdb->output_buf;
			}
			if (*p == gdb_c_nack) {
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

static void gdb_write_start(struct gdb_server *gdb);
static void gdb_write_end(struct gdb_server *gdb);

static void gdb_write_str(struct gdb_server *gdb, const char *str);

static void gdb_write_unsupported(struct gdb_server *gdb);
static void gdb_write_ok(struct gdb_server *gdb);

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
	if (gdb->waiting_on_ack) {
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

/* ============== *
 * Packet Parsing *
 * ============== */

static void gdb_packet_restore(struct gdb_packet *p, struct gdb_packet *other);

static const char *gdb_packet_take(struct gdb_packet *p, size_t amt);
static bool gdb_packet_skip(struct gdb_packet *p, size_t amt);
static bool gdb_packet_match(struct gdb_packet *p, const char *prefix);
static const char *gdb_packet_until(struct gdb_packet *p, const char *delimiters);

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

/* =============================== *
 * Packet Handling and Debug Logic *
 * =============================== */

/* why is the target stopped: ? */
static void gdb_handle_why(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	gdb_write_start(gdb);
	gdb_write_str(gdb, "S05");
	gdb_write_end(gdb);
}

/* read registers: g */
static void gdb_handle_read_registers(struct gdb_server *gdb, struct gdb_packet *p, bool upper)
{
	gdb_write_start(gdb);
	for (int i = 0; i < 13; i++) {
		gdb_writef(gdb, "%04x", gdb_swap_u16(0x1234));
	}
	gdb_write_end(gdb);
}

struct gdb_handler {
	const char *cmd;
	void (*handle)(struct gdb_server *gdb, struct gdb_packet *p, bool upper);
};

static const struct gdb_handler gdb_handlers[] = {
	{"?", gdb_handle_why},
	{"g", gdb_handle_read_registers},
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

	/* not used yet, but will be. silence warnings. */
	(void)&gdb_packet_until;
	(void)&gdb_write_ok;
}
