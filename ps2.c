/*
 *	Reverse implementation of a PS/2 keyboard (ie from the keyboard
 *	end).
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PS2_INTERNAL
#include "ps2.h"

static void ps2_idle(struct ps2 *ps2);
static void ps2_abort(struct ps2 *ps2);
static void ps2_poll(struct ps2 *ps2);
static void ps2_byte_received(struct ps2 *ps2, uint8_t byte);
static void ps2_byte_sent(struct ps2 *ps2);
static int ps2_receive_byte(struct ps2 *ps2, int clocks);

/* Replace with a table */
static int parity_even(uint8_t b)
{
	int i;
	int c = 0;
	for (i = 0; i < 8; i++) {
		if (b & (1 << i))
			c++;
	}
	if (c & 1)
		return 0;
	return 1;
}

#define DELAY_BIT_TO_CLOCK	30		/* 30uS */
#define DELAY_SEND_BYTE		50		/* 50us */

/* There is nothing happening */
static int ps2_state_idle(struct ps2 *ps2, int clocks)
{
	/* Go into host wait mode as it wants to talk to us */
	if (ps2->clock_in == 0) {
		ps2->busy = 1;
		if (ps2->trace)
			fprintf(stderr, "PS2 host pulled clock low for receive.\n");
		ps2_abort(ps2);
	}
	return 0;
}

static void ps2_idle(struct ps2 *ps2)
{
	if (ps2->trace)
		fprintf(stderr, "PS2 goes idle.\n");
	ps2->state = ps2_state_idle;
	ps2->clock_out = 1;
	ps2->data_out = 1;
	ps2->busy = 0;
}

/*
 *	The host has pulled clock low and data low. It should now let
 *	clock float
 */
static int ps2_wait_host_2(struct ps2 *ps2, int clocks)
{
	if (ps2->clock_in == 0)
		return 0;
	/* Ok it's going to send us stuff */
	ps2->state = ps2_receive_byte;
	ps2->count = 10;	/* 10 bits in */
	ps2->receive = 0;	/* Clear receive buffer */
	ps2->step = 0;
	ps2->wait = DELAY_BIT_TO_CLOCK;
	if (ps2->trace)
		fprintf(stderr, "PS2 ready to receive.\n");
	return clocks;
}

/*
 *	The host pulled the clock low and should pull data low
 */
static int ps2_wait_host(struct ps2 *ps2, int clocks)
{
	if (ps2->clock_in != 0) {
		ps2_idle(ps2);
		return 0;
	}
	if (ps2->data_in == 0) {
		if (ps2->trace)
			fprintf(stderr, "PS2 host pulled data low for receive.\n");
		ps2->state = ps2_wait_host_2;
		return clocks;
	}
	return 0;
}

/*
 *	The host aborted our transmission
 */
static void ps2_abort(struct ps2 *ps2)
{
	ps2->state = ps2_wait_host;
}

/* We are sending a byte. We have the bits loaded to go low to high and
   they are start (0), 8 data, parity (odd), stop (1), */

static int ps2_send_byte(struct ps2 *ps2, int clocks)
{
	if (ps2->wait) {
		if (clocks < ps2->wait) {
			ps2->wait -= clocks;
			return 0;
		}
		clocks -= ps2->wait;
		ps2->step++;
	}
	/* Load a bit onto the bus with the clock high */
	if (ps2->step == 1) {			/* Data */
		if (ps2->clock_in == 0) {	/* Pulled low by remote */
			if (ps2->trace)
				fprintf(stderr, "PS2: Host aborted our transmit bits left %d.\n",
					ps2->count);
			ps2_abort(ps2);
			return clocks;
		}
		if (ps2->trace)
			fprintf(stderr, "PS2: load bit for host.\n");
		if (ps2->trace && ps2->data_in == 0)
			fprintf(stderr, "PS2: **error** host has data pulled down.\n");
		ps2->data_out = ps2->send & 0x01;
		ps2->send >>= 1;
		ps2->wait = DELAY_BIT_TO_CLOCK;
		return --clocks;
	}
	/* Pull the clock low - the remote will then read the bit */
	if (ps2->step == 2)	{	/* Clock toggle */
		if (ps2->trace)
			fprintf(stderr, "PS2: clock low - bit ready for host (%d).\n", ps2->data_out);
		ps2->clock_out = 0;
		ps2->wait = DELAY_BIT_TO_CLOCK;
		return --clocks;
	}
	/* Pull the clock high indicating we are going to send a new bit */
	if (ps2->step == 3) {
		if (ps2->trace)
			fprintf(stderr, "PS2: clock back high on send.\n");
		ps2->clock_out = 1;
		ps2->wait = DELAY_BIT_TO_CLOCK;
		return --clocks;
	}
	/* Cycle complete */
	if (ps2->step == 4) {
		if (!--ps2->count) {
			ps2_byte_sent(ps2);
			ps2_idle(ps2);
			return clocks;
		}
		ps2->step = 0;
		ps2->wait = DELAY_BIT_TO_CLOCK;
		return clocks;
	}
	fprintf(stderr, "Invalid step %d in ps2_send_byte.\n", ps2->step);
	exit(1);
}

/*
 *	We want to send data
 */

static int ps2_send_wait(struct ps2 *ps2, int clocks)
{
	if (ps2->clock_in == 0)
		return 0;
	/* The host has let the clock float, we can talk - in 50ms time. If it
	   pulls it low again it will abort */
	if (ps2->trace)
		fprintf(stderr, "PS2: host has released clock.\n");
	ps2->state = ps2_send_byte;
	ps2->wait = DELAY_SEND_BYTE;
	ps2->step = 0;
	return clocks;
}

/*
 *	Begin a send from an idle keyboard
 */
static void ps2_begin_sending(struct ps2 *ps2, uint8_t byte)
{
	uint16_t r = byte << 1;
	ps2->last_sent = byte;
	r |= 0x400;
	if (parity_even(byte))
		r |= 0x200;		/* Make parity odd */
	ps2->send = r;
	ps2->busy = 1;
	ps2->state = ps2_send_wait;
	ps2->count = 11;		/* Send 11 bits */
	if (ps2->trace)
		fprintf(stderr, "PS2: begin sending %02X (%X).\n", byte, r);
}

/*
 *	The host sent us a byte. We are now doing the ack procedure
 */
static int ps2_ack_byte(struct ps2 *ps2, int clocks)
{
	if (ps2->wait) {
		if (clocks < ps2->wait) {
			ps2->wait -= clocks;
			return 0;
		}
		clocks -= ps2->wait;
		ps2->step++;
	}
	/* Wait for data release */
	if (ps2->step == 1) {
		if (ps2->data_in == 0) {
			ps2->step--;
			return 0;
		}
		ps2->wait = DELAY_BIT_TO_CLOCK;
		if (ps2->trace)
			fprintf(stderr, "PS2: host released data line.\n");
		return clocks;
	}
	/* The host let the data line float. Pull it low */
	if (ps2->step == 2) {
		ps2->data_out = 0;
		ps2->wait = DELAY_BIT_TO_CLOCK;
		if (ps2->trace)
			fprintf(stderr, "PS2: pull data low.\n");
		return clocks;
	}
	/* We pull clock down */
	if (ps2->step == 3) {
		ps2->clock_out = 0;
		ps2->wait = DELAY_BIT_TO_CLOCK;
		if (ps2->trace)
			fprintf(stderr, "PS2: pull clock low.\n");
		return clocks;
	}
	/* We let the data and clock float */
	if (ps2->step == 4) {
		ps2->clock_out = 1;
		ps2->data_out = 1;
		/* And the cycle is over */
		/* Process the byte that arrived */
		if (ps2->trace) {
			fprintf(stderr, "PS2: release clock and data.\n");
			fprintf(stderr, "PS2: received %04X.\n", ps2->receive);
		}
		ps2_byte_received(ps2, ps2->receive);
		ps2_idle(ps2);
		return clocks;
	}
	fprintf(stderr, "Invalid step %d in ps2_ack_mode.\n", ps2->step);
	exit(1);
}

/*
 *	When the computer sends us data it sets the data when the clock is
 *	low and we read it when high. We still own the clock.
 */
static int ps2_receive_byte(struct ps2 *ps2, int clocks)
{
	if (ps2->clock_in == 0) {	/* Pulled low by remote */
		ps2_abort(ps2);
		return clocks;
	}
	if (ps2->wait) {
		if (clocks < ps2->wait) {
			ps2->wait -= clocks;
			return 0;
		}
		clocks -= ps2->wait;
		ps2->step++;
	}
	/* Take the clock low - the remote end will now load a new bit */
	if (ps2->step == 1) {
		ps2->clock_out = 0;
		ps2->wait = DELAY_BIT_TO_CLOCK;
		if (ps2->trace)
			fprintf(stderr, "PS2: receive clock low\n");
		return --clocks;
	}
	/* Take the clock back high indicating we will sample it */
	if (ps2->step == 2) {
		ps2->clock_out = 1;
		ps2->wait = DELAY_BIT_TO_CLOCK;
		if (ps2->trace)
			fprintf(stderr, "PS2: receive clock high\n");
		return --clocks;
	}
	/* Sample the bit */
	if (ps2->step == 3)	{	/* Data */
		ps2->receive >>= 1;
		/* This will shift down losing start and leaving the parity and
		   stop in bits 8-9. We should check parity FIXME */
		/* FIXME: should also consider data_out being low */
		ps2->receive |= (ps2->data_in) ? (1 << 9) : 0;
		ps2->wait = DELAY_BIT_TO_CLOCK;
		if (ps2->trace)
			fprintf(stderr, "PS2: sample - %d\n", ps2->data_in);
		return --clocks;
	}
	/* Bit complete, move on */
	if (ps2->step == 4) {
		if (--ps2->count) {
			ps2->step = 0;
			ps2->wait = DELAY_BIT_TO_CLOCK;
			return clocks;
		}
		/* We now need to ack the byte */
		ps2->state = ps2_ack_byte;
		ps2->step = 0;
		ps2->wait = DELAY_BIT_TO_CLOCK;
		return clocks;
	}
	fprintf(stderr, "Invalid step %d in ps2_send_byte.\n", ps2->step);
	exit(1);
}


static unsigned ps2step;

static void ps2_clocks(struct ps2 *ps2, int clocks)
{
	/* Turn system clocks into our steps - about 15KHz per bit is the
	   desired result */
	ps2step += clocks;
	clocks = ps2step / ps2->divider;
	ps2step %= ps2->divider;

	while(clocks > 0) {
		clocks = ps2->state(ps2, clocks);
	}
}

/*
 *	Higher level
 */

static void ps2_queue_key(struct ps2 *ps2, uint8_t key)
{
	if (ps2->disabled)
		return;
	/* For now don't emulate overflows */
	if (ps2->bufptr == PS2_BUFSIZ)
		return;
	ps2->buffer[ps2->bufptr++] = key;
}

static void ps2_queue_reply(struct ps2 *ps2, uint8_t r)
{
	if (ps2->rbufptr == PS2_BUFSIZ)
		return;
	ps2->rbuffer[ps2->rbufptr++] = r;
	if (ps2->trace)
		fprintf(stderr, "PS2: queued reply %02X rbufptr %d\n", r, ps2->rbufptr);
}

/* A send completed, adjust our buffers */
static void ps2_byte_sent(struct ps2 *ps2)
{
	if (ps2->replymode) {
		ps2->rbufptr--;
		if (ps2->rbufptr)
			memmove(ps2->rbuffer, ps2->rbuffer+1, ps2->rbufptr);
	} else {
		ps2->bufptr--;
		if (ps2->bufptr)
			memmove(ps2->buffer, ps2->buffer+1, ps2->bufptr);
	}
}

/*
 *	Check for new work to dispatch if we are idle
 */
static void ps2_poll(struct ps2 *ps2)
{
	if (ps2->busy)
		return;

	if (ps2->rbufptr) {
		ps2->replymode = 1;
		ps2_begin_sending(ps2, ps2->rbuffer[0]);
	} else if (ps2->bufptr) {
		ps2->replymode = 0;
		ps2_begin_sending(ps2, ps2->buffer[0]);
	}
}

/* We got a message from the host */
/* Lots of expansion needed here even for stuff we ignore */
static void ps2_byte_received(struct ps2 *ps2, uint8_t byte)
{
	/* Discard byte */
	if (ps2->rxstate == 1) {
		ps2->rxstate = 0;
		return;
	}
	/* Stream of scan codes */
	if (ps2->rxstate == 2) {
		if (byte >= 0xED) {
			ps2->rxstate = 0;
			ps2->disabled = 0;
		}
		ps2_queue_reply(ps2, 0xFA);	/* TODO - is this sent on the terminator ? */
	}
	/* Set scan code set */
	if (ps2->rxstate == 3) {
		ps2_queue_reply(ps2, 0xFA);
		/* Reply 2 to if a query of scan codd state */
		if (byte == 0x00)
			ps2_queue_reply(ps2, 2);
		ps2->rxstate = 0;
	}
	switch(byte) {
		case 0xFF:	/* Reset */
			ps2_queue_reply(ps2, 0xFA);
			break;
		case 0xFE:	/* Resend */
			if (ps2->last_sent != 0xFE)
				ps2_queue_reply(ps2, ps2->last_sent);
			break;
		case 0xFD:	/* This one is hard... we have to ack a series of
				   set 3 codes */
			ps2->rxstate = 2;
			ps2_queue_reply(ps2, 0xFA);
			ps2->disabled = 1;
			break;
		/* We don't bother emulating these - just reply FA */
		case 0xFC:	/* Make/break */
		case 0xFB:	/* Typematic */
		case 0xFA:	/* All keys typematic/make/break */
		case 0xF9:	/* All keys make */
		case 0xF8:	/* All keys make/break */
		case 0xF7:	/* All keys typematic */
			ps2_queue_reply(ps2, 0xFA);
			break;
		case 0xF6:	/* Set default */
			break;
		case 0xF5:
			ps2->disabled = 1;
			break;
		case 0xF4:
			ps2->disabled = 0;
			break;
		case 0xF3:	/* Set typematic delay.. ignored - eat the parameter */
			ps2->rxstate = 1;
			break;
		case 0xF2:	/* Read ID */
			ps2_queue_reply(ps2, 0xAB);
			ps2_queue_reply(ps2, 0x83);
			break;
		case 0xF0:	/* Set scan code set */
			ps2_queue_reply(ps2, 0xFA);
			ps2->rxstate = 3;
			break;
		case 0xEE:
			ps2_queue_reply(ps2, 0xEE);
			break;
		case 0xED:
			/* Ignore LED state */
			ps2->rxstate = 1;
			break;
		default:
			ps2_queue_reply(ps2, 0xFE);
			break;
	}
}

/*
 *	Actual interface to the outside world
 */

void ps2_reset(struct ps2 *ps2)
{
	ps2_idle(ps2);
	ps2_queue_reply(ps2, 0xAA);
}

struct ps2 *ps2_create(unsigned int divider)
{
	struct ps2 *ps2 = malloc(sizeof(struct ps2));
	if (ps2 == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(ps2, 0, sizeof(struct ps2));
	ps2->divider = divider;
	ps2_reset(ps2);
	return ps2;
}

void ps2_free(struct ps2 *ps2)
{
	free(ps2);
}

void ps2_set_lines(struct ps2 *ps2, unsigned int clock, unsigned int data)
{
	ps2->clock_in = clock;
	ps2->data_in = data;
}

unsigned int ps2_get_clock(struct ps2 *ps2)
{
	return ps2->clock_in & ps2->clock_out;
}

unsigned int ps2_get_data(struct ps2 *ps2)
{
	return ps2->data_in & ps2->data_out;
}

void ps2_event(struct ps2 *ps2, unsigned int clocks)
{
	ps2_clocks(ps2, clocks);
	ps2_poll(ps2);
}

void ps2_trace(struct ps2 *ps2, int onoff)
{
	ps2->trace = onoff;
}

void ps2_queue_byte(struct ps2 *ps2, uint8_t byte)
{
	ps2_queue_key(ps2, byte);
}
