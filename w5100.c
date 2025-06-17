/*

   Chopped up by Alan Cox out of the W5100 emulation layer for FUSE. Added
   indirect mode support.

   Emulates a minimal subset of the Wiznet W5100 TCP/IP controller.

   Copyright (c) 2011 Philip Kendall

   $Id: w5100.c 4912 2013-03-24 19:34:06Z sbaldovi $

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "system.h"
#include "w5100.h"

typedef enum w5100_socket_mode {
	W5100_SOCKET_MODE_CLOSED = 0x00,
	W5100_SOCKET_MODE_TCP,
	W5100_SOCKET_MODE_UDP,
	W5100_SOCKET_MODE_IPRAW,
	W5100_SOCKET_MODE_MACRAW,
	W5100_SOCKET_MODE_PPPOE,
} w5100_socket_mode;

typedef enum w5100_socket_state {
	W5100_SOCKET_STATE_CLOSED = 0x00,

	W5100_SOCKET_STATE_INIT = 0x13,
	W5100_SOCKET_STATE_LISTEN,
	W5100_SOCKET_STATE_CONNECTING,
	W5100_SOCKET_STATE_ACCEPTING,
	W5100_SOCKET_STATE_ESTABLISHED = 0x17,
	W5100_SOCKET_STATE_CLOSE_WAIT = 0x1c,

	W5100_SOCKET_STATE_UDP = 0x22,
} w5100_socket_state;

enum w5100_socket_registers {
	W5100_SOCKET_MR = 0x00,
	W5100_SOCKET_CR,
	W5100_SOCKET_IR,
	W5100_SOCKET_SR,

	W5100_SOCKET_PORT0,
	W5100_SOCKET_PORT1,

	W5100_SOCKET_DIPR0 = 0x0c,
	W5100_SOCKET_DIPR1,
	W5100_SOCKET_DIPR2,
	W5100_SOCKET_DIPR3,

	W5100_SOCKET_DPORT0,
	W5100_SOCKET_DPORT1,

	W5100_SOCKET_TX_FSR0 = 0x20,
	W5100_SOCKET_TX_FSR1,
	W5100_SOCKET_TX_RR0,
	W5100_SOCKET_TX_RR1,
	W5100_SOCKET_TX_WR0,
	W5100_SOCKET_TX_WR1,

	W5100_SOCKET_RX_RSR0,
	W5100_SOCKET_RX_RSR1,
	W5100_SOCKET_RX_RD0,
	W5100_SOCKET_RX_RD1,
};

typedef struct nic_w5100_socket_t {

	int id; /* For debug use only */

	/* W5100 properties */

	w5100_socket_mode mode;
	uint8_t flags;

	w5100_socket_state state;

	uint8_t ir;      /* Interrupt register */

	uint8_t ptr_low;
	uint8_t mr;

	uint8_t port[2]; /* Source port */

	uint8_t dip[4];  /* Destination IP address */
	uint8_t dport[2];/* Destination port */

	uint16_t tx_rr;   /* Transmit read pointer */
	uint16_t tx_wr;   /* Transmit write pointer */

	uint16_t rx_rsr;  /* Received size */
	uint16_t rx_rd;   /* Received read pointer */

	uint16_t old_rx_rd; /* Used in RECV command processing */

	uint8_t tx_buffer[0x800];  /* Transmit buffer */
	uint8_t rx_buffer[0x800];  /* Received buffer */

	/* Host properties */

	int fd;                   /* Socket file descriptor */
	int bind_count;           /* Number of writes to the Sn_PORTx registers we've received */
	int socket_bound;         /* True once we've bound the socket to a port */
	int write_pending;        /* True if we're waiting to write data on this socket */

	int last_send;            /* The value of Sn_TX_WR when the SEND command was last sent */
	int datagram_lengths[0x20]; /* The lengths of datagrams to be sent */
	int datagram_count;

	/* Flag used to indicate that a socket has been closed since we started
	   waiting for it in a select() call and therefore the socket should no
	   longer be used */
	int ok_for_io;

} nic_w5100_socket_t;

struct nic_w5100_t {
	uint8_t gw[4];   /* Gateway IP address */
	uint8_t sub[4];  /* Our subnet mask */
	uint8_t sha[6];  /* MAC address */
	uint8_t sip[4];  /* Our IP address */
	uint8_t mr;
	uint16_t ar;
	nic_w5100_socket_t socket[4];
};

/* Define this to spew debugging info to stdout */
#define W5100_DEBUG 0

void nic_w5100_debug( const char *format, ... );
void nic_w5100_vdebug( const char *format, va_list ap );
void nic_w5100_error( int severity, const char *format, ... );

enum w5100_registers {
	W5100_MR = 0x000,
	W5100_IDM_AR0 = 0x001,
	W5100_IDM_AR1 = 0x002,
	W5100_IDM_DR = 0x003,

	W5100_GWR0 = 0x001,
	W5100_GWR1,
	W5100_GWR2,
	W5100_GWR3,

	W5100_SUBR0,
	W5100_SUBR1,
	W5100_SUBR2,
	W5100_SUBR3,

	W5100_SHAR0,
	W5100_SHAR1,
	W5100_SHAR2,
	W5100_SHAR3,
	W5100_SHAR4,
	W5100_SHAR5,

	W5100_SIPR0,
	W5100_SIPR1,
	W5100_SIPR2,
	W5100_SIPR3,

	W5100_IR = 0x015,
	W5100_IMR = 0x016,

	W5100_RMSR = 0x01a,
	W5100_TMSR,
};



void
nic_w5100_debug( const char *format, ... )
{
	if( W5100_DEBUG ) {
		va_list ap;
		va_start( ap, format );
		vprintf( format, ap );
		va_end( ap );
		fflush(stdout);
	}
}

void
nic_w5100_vdebug( const char *format, va_list ap )
{
	if( W5100_DEBUG ) {
		vprintf( format, ap );
		fflush(stdout);
	}
}

void
nic_w5100_error( int severity, const char *format, ... )
{
	va_list ap;

	va_start( ap, format );
	nic_w5100_vdebug( format, ap );
	va_end( ap );
}

enum w5100_socket_command {
	W5100_SOCKET_COMMAND_OPEN = 1 << 0,
	W5100_SOCKET_COMMAND_LISTEN = 1 << 1,
	W5100_SOCKET_COMMAND_CONNECT = 1 << 2,
	W5100_SOCKET_COMMAND_DISCON = 1 << 3,
	W5100_SOCKET_COMMAND_CLOSE = 1 << 4,
	W5100_SOCKET_COMMAND_SEND = 1 << 5,
	W5100_SOCKET_COMMAND_RECV = 1 << 6,
};

static void w5100_socket_init_common( nic_w5100_socket_t *socket )
{
	socket->fd = -1;
	socket->bind_count = 0;
	socket->socket_bound = 0;
	socket->ok_for_io = 0;
	socket->write_pending = 0;
}

void nic_w5100_socket_init( nic_w5100_socket_t *socket, int which )
{
	socket->id = which;
	w5100_socket_init_common( socket );
}

static void w5100_socket_clean( nic_w5100_socket_t *socket )
{
	socket->ir = 0;
	memset( socket->port, 0, sizeof( socket->port ) );
	memset( socket->dip, 0, sizeof( socket->dip ) );
	memset( socket->dport, 0, sizeof( socket->dport ) );
	socket->tx_rr = socket->tx_wr = 0;
	socket->rx_rsr = 0;
	socket->old_rx_rd = socket->rx_rd = 0;

	socket->last_send = 0;
	socket->datagram_count = 0;

	if( socket->fd != -1) {
		close( socket->fd );
		w5100_socket_init_common( socket );
	}
}

void
nic_w5100_socket_reset( nic_w5100_socket_t *socket )
{
	socket->mode = W5100_SOCKET_MODE_CLOSED;
	socket->flags = 0;
	socket->state = W5100_SOCKET_STATE_CLOSED;

	w5100_socket_clean( socket );
}

void nic_w5100_socket_end( nic_w5100_socket_t *socket )
{
	nic_w5100_socket_reset( socket );
}

static void
w5100_write_socket_mr( nic_w5100_socket_t *socket, uint8_t b )
{
	nic_w5100_debug( "w5100: writing 0x%02x to S%d_MR\n", b, socket->id );

	w5100_socket_mode mode = b & 0x0f;
	uint8_t flags = b & 0xf0;

	switch( mode ) {
		case W5100_SOCKET_MODE_CLOSED:
			break;
		case W5100_SOCKET_MODE_TCP:
			/* We support only "disable no delayed ACK" */
			if( flags != 0x20 ) {
				fprintf( stderr, "w5100: unsupported flags 0x%02x set for TCP mode on socket %d\n", b & 0xf0, socket->id );
				flags = 0x20;
			}
			break;
		case W5100_SOCKET_MODE_UDP:
			/* We don't support multicast */
			if( flags != 0x00 ) {
				fprintf( stderr, "w5100: unsupported flags 0x%02x set for UDP mode on socket %d\n", b & 0xf0, socket->id );
				flags = 0;
			}
			break;
		case W5100_SOCKET_MODE_IPRAW:
		case W5100_SOCKET_MODE_MACRAW:
		case W5100_SOCKET_MODE_PPPOE:
		default:
			fprintf( stderr, "w5100: unsupported mode 0x%02x set on socket %d\n", b, socket->id );
			mode = W5100_SOCKET_MODE_CLOSED;
			break;
	}

	socket->mode = mode;
	socket->flags = flags;
}

static void
w5100_socket_open( nic_w5100_socket_t *socket_obj )
{
	if( ( socket_obj->mode == W5100_SOCKET_MODE_UDP ||
			socket_obj->mode == W5100_SOCKET_MODE_TCP ) &&
		socket_obj->state == W5100_SOCKET_STATE_CLOSED ) {

		int tcp = socket_obj->mode == W5100_SOCKET_MODE_TCP;
		int type = tcp ? SOCK_STREAM : SOCK_DGRAM;
		int protocol = tcp ? IPPROTO_TCP : IPPROTO_UDP;
		const char *description = tcp ? "TCP" : "UDP";
		int final_state = tcp ? W5100_SOCKET_STATE_INIT : W5100_SOCKET_STATE_UDP;
#ifndef WIN32
		int one = 1;
#endif

		w5100_socket_clean( socket_obj );

		socket_obj->fd = socket( AF_INET, type, protocol );
		if( socket_obj->fd == -1) {
			fprintf(stderr,
				"w5100: failed to open %s socket for socket %d; errno %d: %s\n",
				description, socket_obj->id, errno, strerror(errno) );
			return;
		}
		fcntl(socket_obj->fd, F_SETFL, FNDELAY);

		if( setsockopt( socket_obj->fd, SOL_SOCKET, SO_REUSEADDR, &one,
			sizeof(one) ) == -1 ) {
			fprintf(stderr,
				"w5100: failed to set SO_REUSEADDR on socket %d; errno %d: %s\n",
				socket_obj->id, errno , strerror(errno) );
		}

		socket_obj->state = final_state;

		nic_w5100_debug( "w5100: opened %s fd %d for socket %d\n", description, socket_obj->fd, socket_obj->id );
	}
}

static int
w5100_socket_bind_port( nic_w5100_t *self, nic_w5100_socket_t *socket )
{
	struct sockaddr_in sa;

	memset( &sa, 0, sizeof(sa) );
	sa.sin_family = AF_INET;
	memcpy( &sa.sin_port, socket->port, 2 );
	memcpy( &sa.sin_addr.s_addr, self->sip, 4 );

	nic_w5100_debug( "w5100: attempting to bind socket %d to %s:%d\n", socket->id, inet_ntoa(sa.sin_addr), ntohs(sa.sin_port) );
	if( bind( socket->fd, (struct sockaddr*)&sa, sizeof(sa) ) == -1 ) {
		fprintf(stderr, "w5100: failed to bind socket %d; errno %d: %s\n",
				socket->id, errno, strerror(errno));

		socket->ir |= 1 << 3;
		socket->state = W5100_SOCKET_STATE_CLOSED;
		return -1;
	}

	socket->socket_bound = 1;
	nic_w5100_debug( "w5100: successfully bound socket %d\n", socket->id );

	return 0;
}

static void
w5100_socket_listen( nic_w5100_t *self, nic_w5100_socket_t *socket )
{
	if( socket->state == W5100_SOCKET_STATE_INIT ) {

		if( !socket->socket_bound )
			if( w5100_socket_bind_port( self, socket ) )
				return;

		if( listen( socket->fd, 1 ) == -1 ) {
			fprintf(stderr, "w5100: failed to listen on socket %d; errno %d: %s\n",
					socket->id, errno, strerror(errno));
			return;
		}

		socket->state = W5100_SOCKET_STATE_LISTEN;

		nic_w5100_debug( "w5100: listening on socket %d\n", socket->id );
	}
}

static void
w5100_socket_connect( nic_w5100_t *self, nic_w5100_socket_t *socket )
{
	if( socket->state == W5100_SOCKET_STATE_INIT ) {
		struct sockaddr_in sa;

		if( !socket->socket_bound )
			if( w5100_socket_bind_port( self, socket ) )
				return;

		memset( &sa, 0, sizeof(sa) );
		sa.sin_family = AF_INET;
		memcpy( &sa.sin_port, socket->dport, 2 );
		memcpy( &sa.sin_addr.s_addr, socket->dip, 4 );

		if( connect( socket->fd, (struct sockaddr*)&sa, sizeof(sa) ) == -1 ) {
			if (errno != EINPROGRESS) {
				fprintf(stderr,
					"w5100: failed to connect socket %d to 0x%08x:0x%04x; errno %d: %s\n",
					socket->id, ntohl(sa.sin_addr.s_addr), ntohs(sa.sin_port),
					errno, strerror(errno) );

				socket->ir |= 1 << 3;
				socket->state = W5100_SOCKET_STATE_CLOSED;
				return;
			}
			nic_w5100_debug("w5100: socket %d moves to connecting.\n", socket->id);
			socket->state = W5100_SOCKET_STATE_CONNECTING;
			return;
		}

		nic_w5100_debug("w5100: socket %d moves directly to connected.\n", socket->id);
		socket->ir |= 1 << 0;
		socket->state = W5100_SOCKET_STATE_ESTABLISHED;
	}
}

static void
w5100_socket_close( nic_w5100_t *self, nic_w5100_socket_t *socket )
{
	if( socket->fd != -1 ) {
		close( socket->fd );
		socket->fd = -1;
		socket->socket_bound = 0;
		socket->ok_for_io = 0;
		socket->state = W5100_SOCKET_STATE_CLOSED;
		nic_w5100_debug( "w5100: closed socket %d\n", socket->id );
	}
}

static void
w5100_socket_discon( nic_w5100_t *self, nic_w5100_socket_t *socket )
{
	if( socket->state == W5100_SOCKET_STATE_ESTABLISHED ||
			socket->state == W5100_SOCKET_STATE_CLOSE_WAIT ) {
		socket->ir |= 1 << 1;
		socket->state = W5100_SOCKET_STATE_CLOSED;
		nic_w5100_debug( "w5100: disconnected socket %d\n", socket->id );
		w5100_socket_close(self, socket);
	}
}

static void
w5100_socket_send( nic_w5100_t *self, nic_w5100_socket_t *socket )
{
	if( socket->state == W5100_SOCKET_STATE_UDP ) {

		if( !socket->socket_bound )
			if( w5100_socket_bind_port( self, socket ) )
				return;

		socket->datagram_lengths[socket->datagram_count++] =
			socket->tx_wr - socket->last_send;
		socket->last_send = socket->tx_wr;
		socket->write_pending = 1;
	}
	else if( socket->state == W5100_SOCKET_STATE_ESTABLISHED ) {
		socket->write_pending = 1;
	}
}

static void
w5100_socket_recv( nic_w5100_t *self, nic_w5100_socket_t *socket )
{
	if( socket->state == W5100_SOCKET_STATE_UDP ||
		socket->state == W5100_SOCKET_STATE_ESTABLISHED ||
		socket->state == W5100_SOCKET_STATE_CLOSE_WAIT ) {
		socket->rx_rsr -= socket->rx_rd - socket->old_rx_rd;
		socket->old_rx_rd = socket->rx_rd;
		if( socket->rx_rsr != 0 )
			socket->ir |= 1 << 2;
	}
}

static void
w5100_write_socket_cr( nic_w5100_t *self, nic_w5100_socket_t *socket, uint8_t b )
{
	nic_w5100_debug( "w5100: writing 0x%02x to S%d_CR\n", b, socket->id );

	switch( b ) {
		case W5100_SOCKET_COMMAND_OPEN:
			w5100_socket_open( socket );
			break;
		case W5100_SOCKET_COMMAND_LISTEN:
			w5100_socket_listen( self, socket );
			break;
		case W5100_SOCKET_COMMAND_CONNECT:
			w5100_socket_connect( self, socket );
			break;
		case W5100_SOCKET_COMMAND_DISCON:
			w5100_socket_discon( self, socket );
			break;
		case W5100_SOCKET_COMMAND_CLOSE:
			w5100_socket_close( self, socket );
			break;
		case W5100_SOCKET_COMMAND_SEND:
			w5100_socket_send( self, socket );
			break;
		case W5100_SOCKET_COMMAND_RECV:
			w5100_socket_recv( self, socket );
			break;
		default:
			fprintf( stderr, "w5100: unknown command 0x%02x sent to socket %d\n", b, socket->id );
			break;
	}
}

static void
w5100_write_socket_port( nic_w5100_t *self, nic_w5100_socket_t *socket, int which, uint8_t b )
{
	nic_w5100_debug( "w5100: writing 0x%02x to S%d_PORT%d\n", b, socket->id, which );
	socket->port[which] = b;
	if( ++socket->bind_count == 2 ) {
		if( socket->state == W5100_SOCKET_STATE_UDP && !socket->socket_bound ) {
			if( w5100_socket_bind_port( self, socket ) ) {
				socket->bind_count = 0;
				return;
			}
		}
		socket->bind_count = 0;
	}
}

uint8_t
nic_w5100_socket_read( nic_w5100_t *self, uint16_t reg )
{
	nic_w5100_socket_t *socket = &self->socket[(reg >> 8) - 4];
	int socket_reg = reg & 0xff;
	int reg_offset;
	uint16_t fsr;
	uint8_t b;

	/* FIXME: add indirect mode logic */
	switch( socket_reg ) {
		case W5100_SOCKET_MR:
			b = socket->mode;
			nic_w5100_debug( "w5100: reading 0x%02x from S%d_MR\n", b, socket->id );
			break;
		case W5100_SOCKET_IR:
			b = socket->ir;
			nic_w5100_debug( "w5100: reading 0x%02x from S%d_IR\n", b, socket->id );
			break;
		case W5100_SOCKET_CR:
			b = 0;	/* All commands finish instantly */
			nic_w5100_debug( "w5100: reading 0x%02x from S%d_CR\n", b, socket->id );
			break;
		case W5100_SOCKET_SR:
			b = socket->state;
			nic_w5100_debug( "w5100: reading 0x%02x from S%d_SR\n", b, socket->id );
			break;
		case W5100_SOCKET_PORT0: case W5100_SOCKET_PORT1:
			b = socket->port[socket_reg - W5100_SOCKET_PORT0];
			nic_w5100_debug( "w5100: reading 0x%02x from S%d_PORT%d\n", b, socket->id, socket_reg - W5100_SOCKET_PORT0 );
			break;
		case W5100_SOCKET_TX_FSR0: case W5100_SOCKET_TX_FSR1:
			reg_offset = socket_reg - W5100_SOCKET_TX_FSR0;
			fsr = 0x0800 - (socket->tx_wr - socket->tx_rr);
			b = ( fsr >> ( 8 * ( 1 - reg_offset ) ) ) & 0xff;
			nic_w5100_debug( "w5100: reading 0x%02x from S%d_TX_FSR%d\n", b, socket->id, reg_offset );
			break;
		case W5100_SOCKET_TX_RR0: case W5100_SOCKET_TX_RR1:
			reg_offset = socket_reg - W5100_SOCKET_TX_RR0;
			b = ( socket->tx_rr >> ( 8 * ( 1 - reg_offset ) ) ) & 0xff;
			nic_w5100_debug( "w5100: reading 0x%02x from S%d_TX_RR%d\n", b, socket->id, reg_offset );
			break;
		case W5100_SOCKET_TX_WR0: case W5100_SOCKET_TX_WR1:
			reg_offset = socket_reg - W5100_SOCKET_TX_WR0;
			b = ( socket->tx_wr >> ( 8 * ( 1 - reg_offset ) ) ) & 0xff;
			nic_w5100_debug( "w5100: reading 0x%02x from S%d_TX_WR%d\n", b, socket->id, reg_offset );
			break;
		case W5100_SOCKET_RX_RSR0: case W5100_SOCKET_RX_RSR1:
			reg_offset = socket_reg - W5100_SOCKET_RX_RSR0;
			b = ( socket->rx_rsr >> ( 8 * ( 1 - reg_offset ) ) ) & 0xff;
			nic_w5100_debug( "w5100: reading 0x%02x from S%d_RX_RSR%d\n", b, socket->id, reg_offset );
			break;
		case W5100_SOCKET_RX_RD0: case W5100_SOCKET_RX_RD1:
			reg_offset = socket_reg - W5100_SOCKET_RX_RD0;
			b = ( socket->rx_rd >> ( 8 * ( 1 - reg_offset ) ) ) & 0xff;
			nic_w5100_debug( "w5100: reading 0x%02x from S%d_RX_RD%d\n", b, socket->id, reg_offset );
			break;
		default:
			b = 0xff;
			fprintf( stderr, "w5100: reading 0x%02x from unsupported register 0x%03x\n", b, reg );
			break;
	}
	return b;
}

void
nic_w5100_socket_write( nic_w5100_t *self, uint16_t reg, uint8_t b )
{
	nic_w5100_socket_t *socket = &self->socket[(reg >> 8) - 4];
	int socket_reg = reg & 0xff;

	/* FIXME: add indirect mode logic */
	switch( socket_reg ) {
		case W5100_SOCKET_MR:
			w5100_write_socket_mr( socket, b );
			break;
		case W5100_SOCKET_CR:
			w5100_write_socket_cr( self, socket, b );
			break;
		case W5100_SOCKET_IR:
			nic_w5100_debug( "w5100: writing 0x%02x to S%d_IR\n", b, socket->id );
			socket->ir &= ~b;
			break;
		case W5100_SOCKET_PORT0: case W5100_SOCKET_PORT1:
			w5100_write_socket_port( self, socket, socket_reg - W5100_SOCKET_PORT0, b );
			break;
		case W5100_SOCKET_DIPR0: case W5100_SOCKET_DIPR1: case W5100_SOCKET_DIPR2: case W5100_SOCKET_DIPR3:
			nic_w5100_debug( "w5100: writing 0x%02x to S%d_DIPR%d\n", b, socket->id, socket_reg - W5100_SOCKET_DIPR0 );
			socket->dip[socket_reg - W5100_SOCKET_DIPR0] = b;
			break;
		case W5100_SOCKET_DPORT0: case W5100_SOCKET_DPORT1:
			nic_w5100_debug( "w5100: writing 0x%02x to S%d_DPORT%d\n", b, socket->id, socket_reg - W5100_SOCKET_DPORT0 );
			socket->dport[socket_reg - W5100_SOCKET_DPORT0] = b;
			break;
		case W5100_SOCKET_TX_WR0:
			nic_w5100_debug( "w5100: writing 0x%02x to S%d_TX_WR0\n", b, socket->id );
			socket->tx_wr = (socket->tx_wr & 0xff) | (b << 8);
			break;
		case W5100_SOCKET_TX_WR1:
			nic_w5100_debug( "w5100: writing 0x%02x to S%d_TX_WR1\n", b, socket->id );
			socket->tx_wr = (socket->tx_wr & 0xff00) | b;
			break;
		case W5100_SOCKET_RX_RD0:
			nic_w5100_debug( "w5100: writing 0x%02x to S%d_RX_RD0\n", b, socket->id );
			socket->rx_rd = (socket->rx_rd & 0xff) | (b << 8);
			break;
		case W5100_SOCKET_RX_RD1:
			nic_w5100_debug( "w5100: writing 0x%02x to S%d_RX_RD1\n", b, socket->id );
			socket->rx_rd = (socket->rx_rd & 0xff00) | b;
			break;
		default:
			fprintf( stderr, "w5100: writing 0x%02x to unsupported register 0x%03x\n", b, reg );
			break;
	}

	if( socket_reg != W5100_SOCKET_PORT0 && socket_reg != W5100_SOCKET_PORT1 )
		socket->bind_count = 0;
}

uint8_t
nic_w5100_socket_read_rx_buffer( nic_w5100_t *self, uint16_t reg )
{
	nic_w5100_socket_t *socket = &self->socket[(reg - 0x6000) / 0x0800];
	int offset = reg & 0x7ff;
	uint8_t b = socket->rx_buffer[offset];
	nic_w5100_debug( "w5100: reading 0x%02x from socket %d rx buffer offset 0x%03x\n", b, socket->id, offset );
	return b;
}

void
nic_w5100_socket_write_tx_buffer( nic_w5100_t *self, uint16_t reg, uint8_t b )
{
	nic_w5100_socket_t *socket = &self->socket[(reg - 0x4000) / 0x0800];
	int offset = reg & 0x7ff;
	nic_w5100_debug( "w5100: writing 0x%02x to socket %d tx buffer offset 0x%03x\n", b, socket->id, offset );
	socket->tx_buffer[offset] = b;
}

void
nic_w5100_socket_add_to_sets( nic_w5100_socket_t *socket, fd_set *readfds,
	fd_set *writefds, int *max_fd )
{
	if( socket->fd != -1 ) {
		/* We can process a UDP read if we're in a UDP state and there are at least
		   9 bytes free in our buffer (8 byte UDP header and 1 byte of actual
		   data). */
		int udp_read = socket->state == W5100_SOCKET_STATE_UDP &&
			0x800 - socket->rx_rsr >= 9;
		/* We can process a TCP read if we're in the established state and have
		   any room in our buffer (no header necessary for TCP). */
		int tcp_read = socket->state == W5100_SOCKET_STATE_ESTABLISHED &&
			0x800 - socket->rx_rsr >= 1;

		int tcp_listen = socket->state == W5100_SOCKET_STATE_LISTEN;

		socket->ok_for_io = 1;

		if( udp_read || tcp_read || tcp_listen ) {
			FD_SET( socket->fd, readfds );
			if( socket->fd > *max_fd )
				*max_fd = socket->fd;
			nic_w5100_debug( "w5100: checking for read on socket %d with fd %d; max fd %d\n", socket->id, socket->fd, *max_fd );
		}

		if( socket->write_pending || socket->state == W5100_SOCKET_STATE_CONNECTING) {
			FD_SET( socket->fd, writefds );
			if( socket->fd > *max_fd )
				*max_fd = socket->fd;
			nic_w5100_debug( "w5100: write pending on socket %d with fd %d; max fd %d\n", socket->id, socket->fd, *max_fd );
		}
	}
}

static void
w5100_socket_process_accept( nic_w5100_socket_t *socket )
{
	struct sockaddr_in sa;
	socklen_t sa_length = sizeof(sa);
	int new_fd;

	new_fd = accept( socket->fd, (struct sockaddr*)&sa, &sa_length );
	if( new_fd == -1 ) {
		nic_w5100_debug( "w5100: error from accept on socket %d; errno %d: %s\n",
				 socket->id, errno, strerror(errno));
		return;
	}

	nic_w5100_debug( "w5100: accepted connection from %s:%d on socket %d\n", inet_ntoa(sa.sin_addr), ntohs(sa.sin_port), socket->id );

	if( close( socket->fd ) == -1 )
		nic_w5100_debug( "w5100: error attempting to close fd %d for socket %d\n", socket->fd, socket->id );
	socket->fd = new_fd;
	socket->state = W5100_SOCKET_STATE_ESTABLISHED;
}

static void
w5100_socket_process_read( nic_w5100_socket_t *socket , nic_w5100_t *self)
{
	uint8_t buffer[0x800];
	int bytes_free = 0x800 - socket->rx_rsr;
	ssize_t bytes_read;
	struct sockaddr_in sa;

	int udp = socket->state == W5100_SOCKET_STATE_UDP;
	const char *description = udp ? "UDP" : "TCP";

	nic_w5100_debug( "w5100: reading from socket %d\n", socket->id );

	if( udp ) {
		socklen_t sa_length = sizeof(sa);
		bytes_read = recvfrom( socket->fd, (char*)buffer + 8, bytes_free - 8, 0,
			(struct sockaddr*)&sa, &sa_length );
	}
	else
		bytes_read = recv( socket->fd, (char*)buffer, bytes_free, 0 );

	nic_w5100_debug( "w5100: read 0x%03x bytes from %s socket %d\n", (int)bytes_read, description, socket->id );

	if( bytes_read > 0 || (udp && bytes_read == 0) ) {
		int offset = (socket->old_rx_rd + socket->rx_rsr) & 0x7ff;
		uint8_t *dest = &socket->rx_buffer[offset];

		if( udp ) {
			/* Add the W5100's UDP header */
			memcpy( buffer, &sa.sin_addr.s_addr, 4 );
			memcpy( buffer + 4, &sa.sin_port, 2 );
			buffer[6] = (bytes_read >> 8) & 0xff;
			buffer[7] = bytes_read & 0xff;
			bytes_read += 8;
		}

		socket->rx_rsr += bytes_read;
		socket->ir |= 1 << 2;

		if( offset + bytes_read <= 0x800 ) {
			memcpy( dest, buffer, bytes_read );
		}
		else {
			int first_chunk = 0x800 - offset;
			memcpy( dest, buffer, first_chunk );
			memcpy( socket->rx_buffer, buffer + first_chunk, bytes_read - first_chunk );
		}
	}
	else if( bytes_read == 0 ) {  /* TCP */
		if (socket->state == W5100_SOCKET_STATE_CLOSE_WAIT) {
			socket->ir |= 1 << 1;
			w5100_socket_close(self, socket);
		}
		else
			socket->state = W5100_SOCKET_STATE_CLOSE_WAIT;
		nic_w5100_debug( "w5100: EOF on %s socket %d\n",
				 description, socket->id);
	}
	else {
		nic_w5100_debug( "w5100: error %d reading from %s socket %d: %s\n",
				 errno, description, socket->id, strerror(errno));
	}
}

static void
w5100_socket_process_udp_write( nic_w5100_socket_t *socket )
{
	ssize_t bytes_sent;
	int offset = socket->tx_rr & 0x7ff;
	uint16_t length = socket->datagram_lengths[0];
	uint8_t *data = &socket->tx_buffer[ offset ];
	struct sockaddr_in sa;
	uint8_t buffer[0x800];

	nic_w5100_debug( "w5100: writing to UDP socket %d\n", socket->id );

	/* If the data wraps round the write buffer, we need to coalesce it into
	   one chunk for the call to sendto() */
	if( offset + length > 0x800 ) {
		int first_chunk = 0x800 - offset;
		memcpy( buffer, data, first_chunk );
		memcpy( buffer + first_chunk, socket->tx_buffer, length - first_chunk );
		data = buffer;
	}

	memset( &sa, 0, sizeof(sa) );
	sa.sin_family = AF_INET;
	memcpy( &sa.sin_port, socket->dport, 2 );
	memcpy( &sa.sin_addr.s_addr, socket->dip, 4 );

	bytes_sent = sendto( socket->fd, (const char*)data, length, 0, (struct sockaddr*)&sa, sizeof(sa) );
	nic_w5100_debug( "w5100: sent 0x%03x bytes of 0x%03x to UDP socket %d\n",
			 (int)bytes_sent, length, socket->id );

	if( bytes_sent == length ) {
		if( --socket->datagram_count )
			memmove( socket->datagram_lengths, &socket->datagram_lengths[1],
				0x1f * sizeof(int) );

		socket->tx_rr += bytes_sent;
		if( socket->datagram_count == 0 ) {
			socket->write_pending = 0;
			socket->ir |= 1 << 4;
		}
	}
	else if( bytes_sent != -1 )
		nic_w5100_debug( "w5100: didn't manage to send full datagram to UDP socket %d?\n", socket->id );
	else
		nic_w5100_debug( "w5100: error %d writing to UDP socket %d: %s\n",
				 errno, socket->id, strerror(errno));
}

static void
w5100_socket_process_tcp_write( nic_w5100_socket_t *socket )
{
	ssize_t bytes_sent;
	int offset = socket->tx_rr & 0x7ff;
	uint16_t length = socket->tx_wr - socket->tx_rr;
	uint8_t *data = &socket->tx_buffer[ offset ];

	nic_w5100_debug( "w5100: writing to TCP socket %d\n", socket->id );

	/* If the data wraps round the write buffer, write it in two chunks */
	if( offset + length > 0x800 )
		length = 0x800 - offset;

	bytes_sent = send( socket->fd, (const char*)data, length, 0 );
	nic_w5100_debug( "w5100: sent 0x%03x bytes of 0x%03x to TCP socket %d\n",
			 (int)bytes_sent, length, socket->id );

	if( bytes_sent != -1 ) {
		socket->tx_rr += bytes_sent;
		if( socket->tx_rr == socket->tx_wr ) {
			socket->write_pending = 0;
			socket->ir |= 1 << 4;
		}
	}
	else
		nic_w5100_debug( "w5100: error %d writing to TCP socket %d: %s\n",
				 errno, socket->id, strerror(errno));
}

void w5100_socket_process_connect(nic_w5100_socket_t *socket)
{
	struct sockaddr_in sa;
	memset( &sa, 0, sizeof(sa) );
	sa.sin_family = AF_INET;
	memcpy( &sa.sin_port, socket->dport, 2 );
	memcpy( &sa.sin_addr.s_addr, socket->dip, 4 );
	if (connect(socket->fd,  (struct sockaddr *)&sa, sizeof(sa)) == 0) {
		socket->state = W5100_SOCKET_STATE_ESTABLISHED;
		socket->ir |= (1 << 0);
		nic_w5100_debug( "w5100: socket %d moves to established.\n", socket->id);
	} else {
		nic_w5100_socket_reset(socket);
		nic_w5100_debug("w5100: socket %d connect failed.\n", socket->id);
		socket->ir |= (1 << 0);
	}
}

void
nic_w5100_socket_process_io( nic_w5100_socket_t *socket, fd_set readfds,
	fd_set writefds, nic_w5100_t *self )
{
	/* Process only if we're an open socket, and we haven't been closed and
	   re-opened since the select() started */
	if( socket->fd != -1 && socket->ok_for_io ) {
//		printf("Socket %d scan\n", socket->id);
		if( FD_ISSET( socket->fd, &readfds ) ) {
//			printf("Read ready on socket %d\n", socket->id);
			if( socket->state == W5100_SOCKET_STATE_LISTEN )
				w5100_socket_process_accept( socket );
			else
				w5100_socket_process_read( socket , self);
		}

		if( FD_ISSET( socket->fd, &writefds ) ) {
//			fprintf(stderr, "Write ready on socket %d\n", socket->id);
			if( socket->state == W5100_SOCKET_STATE_UDP ) {
				w5100_socket_process_udp_write( socket );
			}
			else if( socket->state == W5100_SOCKET_STATE_ESTABLISHED ) {
				w5100_socket_process_tcp_write( socket );
			}
			else if (socket->state == W5100_SOCKET_STATE_CONNECTING) {
				w5100_socket_process_connect( socket );
			}
		}
	}
}

void
nic_w5100_reset( nic_w5100_t *self )
{
	size_t i;

	nic_w5100_debug( "w5100: reset\n" );

	memset( self->gw, 0, sizeof( self->gw ) );
	memset( self->sub, 0, sizeof( self->sub ) );
	memset( self->sha, 0, sizeof( self->sha ) );
	memset( self->sip, 0, sizeof( self->sip ) );

	for( i = 0; i < 4; i++ )
		nic_w5100_socket_reset( &self->socket[i] );
}

void w5100_process(nic_w5100_t *self)
{
	int i;
	fd_set readfds, writefds;
	int active;
	int max_fd = 0;
	static struct timeval nowait = { 0, 0 };

	FD_ZERO( &readfds );
	FD_ZERO( &writefds );

	for( i = 0; i < 4; i++ ) {
		nic_w5100_socket_add_to_sets( &self->socket[i], &readfds, &writefds,
			&max_fd );

		/* Note that if a socket is closed between when we added it to the sets
		   above and when we call select() below, it will cause the select to fail
		   with EBADF. We catch this and just run around the loop again - the
		   offending socket will not be added to the sets again as it's now been
		   closed */

//		nic_w5100_debug( "w5100: io thread select\n" );

		active = select( max_fd + 1, &readfds, &writefds, NULL, &nowait );

//		nic_w5100_debug( "w5100: io thread wake; %d active\n", active );

		if( active != -1 ) {
			for( i = 0; i < 4; i++ )
				nic_w5100_socket_process_io( &self->socket[i], readfds, writefds , self);
		}
		else if( errno == EBADF ) {
			/* Do nothing - just loop again */
		}
		else {
			nic_w5100_debug( "w5100: select returned unexpected errno %d: %s\n",
					 errno, strerror(errno));
		}
	}
}

nic_w5100_t *nic_w5100_alloc( void )
{
	int i;

	nic_w5100_t *self = calloc(1, sizeof(*self));
	if( !self ) {
		fprintf(stderr, "%s:%d out of memory", __FILE__, __LINE__ );
		exit(1);
	}
	for( i = 0; i < 4; i++ )
		nic_w5100_socket_init( &self->socket[i], i );
	nic_w5100_reset( self );
	return self;
}

void
nic_w5100_free( nic_w5100_t *self )
{
	int i;

	if( self ) {
		for( i = 0; i < 4; i++ )
			nic_w5100_socket_end( &self->socket[i] );
		free(self);
	}
}

static uint8_t nic_w5100_compute_ir(nic_w5100_t *self)
{
	uint8_t r = 0x00;
	int i;
	for (i = 0; i < 4; i++)
		if (self->socket[i].ir)
			r |= (1 << i);
	return r;
}

uint8_t nic_w5100_read( nic_w5100_t *self, uint16_t reg )
{
	uint8_t b;

	if (self->mr & 0x01) {
		switch(reg) {
			case W5100_MR:
				return self->mr;
			case W5100_IDM_AR0:
				return self->ar >> 8;
			case W5100_IDM_AR1:
				return self->ar;
			case W5100_IDM_DR:
				reg = self->ar;
				if (self->mr & 0x02)
					self->ar++;
			}
	}
//	if (reg != W5100_IR)
//		printf("w5100: Read %04X\n", reg);

	if( reg < 0x030 ) {
		switch( reg ) {
			case W5100_MR:
				b = self->mr;
				nic_w5100_debug( "w5100: reading 0x%02x from MR\n", b );
				break;
			case W5100_GWR0: case W5100_GWR1: case W5100_GWR2: case W5100_GWR3:
				b = self->gw[reg - W5100_GWR0];
				nic_w5100_debug( "w5100: reading 0x%02x from GWR%d\n", b, reg - W5100_GWR0 );
				break;
			case W5100_SUBR0: case W5100_SUBR1: case W5100_SUBR2: case W5100_SUBR3:
				b = self->sub[reg - W5100_SUBR0];
				nic_w5100_debug( "w5100: reading 0x%02x from SUBR%d\n", b, reg - W5100_SUBR0 );
				break;
			case W5100_SHAR0: case W5100_SHAR1: case W5100_SHAR2:
			case W5100_SHAR3: case W5100_SHAR4: case W5100_SHAR5:
				b = self->sha[reg - W5100_SHAR0];
				nic_w5100_debug( "w5100: reading 0x%02x from SHAR%d\n", b, reg - W5100_SHAR0 );
				break;
			case W5100_SIPR0: case W5100_SIPR1: case W5100_SIPR2: case W5100_SIPR3:
				b = self->sip[reg - W5100_SIPR0];
				nic_w5100_debug( "w5100: reading 0x%02x from SIPR%d\n", b, reg - W5100_SIPR0 );
				break;
			case W5100_IR:
				b = nic_w5100_compute_ir(self);
				if (b)
					nic_w5100_debug( "w5100: reading 0x%02x from IR\n", b);
				break;
			case W5100_IMR:
				/* We support only "allow all" */
				b = 0xef;
				nic_w5100_debug( "w5100: reading 0x%02x from IMR\n", b );
				break;
			case W5100_RMSR: case W5100_TMSR:
				/* We support only 2K per socket */
				b = 0x55;
				nic_w5100_debug( "w5100: reading 0x%02x from %s\n", b, reg == W5100_RMSR ? "RMSR" : "TMSR" );
				break;
			default:
				b = 0xff;
				/* This is a debug rather than a warning because it happens on snapshot save */
				nic_w5100_debug( "w5100: reading 0x%02x from unsupported register 0x%03x\n", b, reg );
				break;
		}
	}
	else if( reg >= 0x400 && reg < 0x800 ) {
		b = nic_w5100_socket_read( self, reg );
	}
	else if( reg >= 0x6000 && reg < 0x8000 ) {
		b = nic_w5100_socket_read_rx_buffer( self, reg );
	}
	else {
		b = 0xff;
		fprintf( stderr,
			"w5100: reading 0x%02x from unsupported register 0x%03x\n", b, reg );
	}

	return b;
}

static void
w5100_write_mr( nic_w5100_t *self, uint8_t b )
{
	nic_w5100_debug( "w5100: writing 0x%02x to MR\n", b );

	if( b & 0x80 )
		nic_w5100_reset( self );
	self->mr = b;

	if( b & 0x3C)
		fprintf( stderr,
				"w5100: unsupported value 0x%02x written to MR\n", b );
}

static void
w5100_write_imr( nic_w5100_t *self, uint8_t b )
{
	nic_w5100_debug( "w5100: writing 0x%02x to IMR\n", b );
	/* FIXME */
	if( b != 0xef )
		fprintf( stderr,
				"w5100: unsupported value 0x%02x written to IMR\n", b );
}


static void
w5100_write__msr( nic_w5100_t *self, uint16_t reg, uint8_t b )
{
	const char *regname = reg == W5100_RMSR ? "RMSR" : "TMSR";

	nic_w5100_debug( "w5100: writing 0x%02x to %s\n", b, regname );

	if( b != 0x55 )
		fprintf( stderr,
				"w5100: unsupported value 0x%02x written to %s\n",
				b, regname );
}

void
nic_w5100_write( nic_w5100_t *self, uint16_t reg, uint8_t b )
{
	if (self->mr & 0x01) {
		switch(reg) {
			case W5100_MR:
				w5100_write_mr(self, b);
				return;
			case W5100_IDM_AR0:
				self->ar &= 0xFF;
				self->ar |= ((uint16_t) b) << 8;
				return;
			case W5100_IDM_AR1:
				self->ar &= 0xFF00;
				self->ar |= b;
				return;
			case W5100_IDM_DR:
				reg = self->ar;
				if (self->mr & 0x02)
					self->ar++;
				break;
			default:
				fprintf(stderr, "Invalid reg in indirect mode %d\n", reg);
				return;
			}
	}
//	printf("w5100: Write %04X=%02X\n", reg, b);
	if( reg < 0x030 ) {
		switch( reg ) {
			case W5100_MR:
				w5100_write_mr( self, b );
				break;
			case W5100_GWR0: case W5100_GWR1: case W5100_GWR2: case W5100_GWR3:
				nic_w5100_debug( "w5100: writing 0x%02x to GWR%d\n", b, reg - W5100_GWR0 );
				self->gw[reg - W5100_GWR0] = b;
				break;
			case W5100_SUBR0: case W5100_SUBR1: case W5100_SUBR2: case W5100_SUBR3:
				nic_w5100_debug( "w5100: writing 0x%02x to SUBR%d\n", b, reg - W5100_SUBR0 );
				self->sub[reg - W5100_SUBR0] = b;
				break;
			case W5100_SHAR0: case W5100_SHAR1: case W5100_SHAR2:
			case W5100_SHAR3: case W5100_SHAR4: case W5100_SHAR5:
				nic_w5100_debug( "w5100: writing 0x%02x to SHAR%d\n", b, reg - W5100_SHAR0 );
				self->sha[reg - W5100_SHAR0] = b;
				break;
			case W5100_SIPR0: case W5100_SIPR1: case W5100_SIPR2: case W5100_SIPR3:
				nic_w5100_debug( "w5100: writing 0x%02x to SIPR%d\n", b, reg - W5100_SIPR0 );
				self->sip[reg - W5100_SIPR0] = b;
				break;
			case W5100_IMR:
				w5100_write_imr( self, b );
				break;
			case W5100_RMSR: case W5100_TMSR:
				w5100_write__msr( self, reg, b );
				break;
			default:
				/* This is a debug rather than a warning because it happens on snapshot load */
				nic_w5100_debug( "w5100: writing 0x%02x to unsupported register 0x%03x\n", b, reg );
				break;
		}
	}
	else if( reg >= 0x400 && reg < 0x800 ) {
		nic_w5100_socket_write( self, reg, b );
	}
	else if( reg >= 0x4000 && reg < 0x6000 ) {
		nic_w5100_socket_write_tx_buffer( self, reg, b );
	}
	else
		fprintf( stderr,
				"w5100: writing 0x%02x to unsupported register 0x%03x\n",
				b, reg );
}
