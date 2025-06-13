/* i2c bus emulation.
	 Emulation of an i2c interface 'bitbanged' from a CPU. This
	 implementation assumes two signals from the CPU (2 bit interface):

	 + clock signal
	 + data signal

	 This file is limited to decoding and implementing the i2c
	 protocol.

	 Individual i2c peripherals must be registered by calling
	 `i2c_register` - providing the peripherals i2c address
	 and a callback function.

	 Limitations:
	 + ACK bits aren't checked
	 + No attempt to simulate "stretched writes" wherein the slave pulls the clock low for slow operations.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include "system.h"
#include "i2c_bitbang.h"


/* States for the i2c protocol */
typedef enum {
	IDLE = 1,  /* Waiting for start condition to be met */
	ID,        /* Receiving first input byte identifying target i2c device and read/write direction */
	DATA_IN,   /* Data being sent *to* the i2c device */
	DATA_OUT,  /* Data being sent *from* the i2c device */
} I2C_STATE;

struct i2c_client {
	struct i2c_client *next;   /* Linked list of clients on this i2c bus. */
	void              *device; /* The private peripheral structure */
	uint8_t            id;     /* The i2c address for the peripheral */
	                           /* Callback TO the client with/for data */
	uint8_t          (*cb)(void *client, I2C_OP op, uint8_t data);
};

struct i2c_bus {
	uint16_t    cycles;  /* Debug count of clock cycles on the i2c bus */
	uint8_t     byte;    /* Byte currently being sent or received with the CPU */
	uint8_t     output;  /* The current state of the data line on read (DATA_OUT) transitions */
	uint8_t     clkdata; /* Previous values of the clock/data lines */
	uint8_t     bitcnt;  /* Count of in/out bits in current byte */
	I2C_STATE   state;
	uint8_t     stable;  /* FALSE if the data bit changes with clock high */
	uint8_t     trace;   /* Non-zero to cause trace output */

	uint8_t     devid;   /* i2c address of currently active device */
	struct i2c_client *clientlist;
	struct i2c_client *current; /* Pointer to addressed device. */
};

enum {
	DATA_BIT = 0x01,
	CLK_BIT  = 0x02
};
enum {
	FALSE = 0,
	TRUE
};

/* i2c_register
 * Register a new i2c peripheral. Each device has an 'address' which
 * appears in the first byte of each transaction from the controlling
 * device.
 *
 * Note that the ID here is the 7 bit value which appears in the upper
 * 7 bits of the first received byte. So, for example, a DS1307+ Uses
 * an ID of 1101000b (7 bits, 68h). This will be received in the upper
 * 8 bits of the first received byte: 1101000x (D0h/D1h). This function
 * would reguire the value 68h as the 'id' parameter.
 */
void i2c_register(struct i2c_bus *i2c, void *device, uint8_t id,
		uint8_t (*cb)(void *client, I2C_OP op, uint8_t data))
{

	/* Warn if more than one device is registered with the same address. */
	struct i2c_client *dev = i2c->clientlist;
	while (dev && dev->id != id)
		dev = dev->next;
	if (dev != NULL) {
		/* Duplicated address - warn. */
		fprintf(stderr, "i2c: More than one device registered with same ID: %0x2", id);
		return;
	}
	dev = malloc(sizeof(struct i2c_client));
	if (dev == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	dev->device     = device;
	dev->id         = id;
	dev->cb         = cb;
	dev->next       = i2c->clientlist;
	i2c->clientlist = dev;

	if (i2c->trace)
		fprintf(stderr, "i2c: Peripheral registered. ID: %02x\n", id);
}

/* i2c_read
 * Read *always* returns the the current output bit, calculated on
 * the last low-going clock transition. The same value is returned
 * regardless of the number of reads until the next clock cycle.
 *
 * Both lines are open collector, pulled high so the output is
 * a bitwise AND of the output from the i2c driver and the output
 * from the master. This emulation DOES NOT support slaves driving
 * the clock low.
 */
uint8_t i2c_read(struct i2c_bus *i2c) {
	uint8_t res = i2c->output && (i2c->clkdata & 1);
	if (i2c->trace)
		fprintf(stderr, "i2c: Read bit %d outdata %d indata %d res %d\n",
				i2c->bitcnt, i2c->output, (i2c->clkdata & 1), res);
	return res;
}

/* i2c_op
 * Send data/control to a selected device, if there is one, and return data.
 */
static uint8_t i2c_op(struct i2c_bus *i2c, I2C_OP op, uint8_t data) {
	return (i2c->current == NULL) ?
		0xFF :
		i2c->current->cb(i2c->current->device, op, data);
}

/* i2c_byte
 * Bit 8 of a data cycle. Byte is stored in i2c->byte. */
static void i2c_byte(struct i2c_bus *i2c) {
	if (i2c->state == ID) {
		/* First byte is the direction and the addressed device ID */
		i2c->state = (i2c->byte & 1) ? DATA_OUT : DATA_IN;

		if (i2c->trace)
			fprintf(stderr, "ID complete. Device ID: %02x, direction: TO %s. New state: %s\n",
							i2c->devid, ((i2c->byte & 1) ? "MASTER" : "SLAVE"),
							i2c->state == DATA_OUT ? "DATA_OUT" : "DATA_IN");

		/* Is there a registered device with the requested ID? */
		struct i2c_client *dev = i2c->clientlist;

		while (dev && dev->id != i2c->devid)
			dev = dev->next;

		if (dev == NULL && i2c->trace)
			fprintf(stderr, "i2c: Device Unknown: %02x\n", i2c->devid);

		i2c->current = dev; /* Which will be NULL if no matching device */

		if (dev)
			i2c_op(i2c, START, 0);
	}
	else if (i2c->state == DATA_IN) {
		if (i2c->trace)
			fprintf(stderr, "Sending byte to device: %02x\n", i2c->byte);
		i2c_op(i2c, WRITE, i2c->byte);
	}
}

/* i2c_end.
 * Terminate any existing i2c transaction. Return to IDLE state
 * and wait for the next START condition. */
static void i2c_end(struct i2c_bus *i2c) {

	if (i2c->current != NULL) {
		i2c_op(i2c, END, 0);
		i2c->current = NULL;
	}
	i2c->state   = IDLE;
	i2c->byte    = 0;
	i2c->bitcnt  = 0;
	i2c->current = 0;
}

/* i2c_start
 * The master device has generated a START event */
static void i2c_start(struct i2c_bus *i2c) {
	i2c_end(i2c);
	i2c->state = ID; /* Next byte to be received is the ID/direction */
	i2c->cycles = 0;
	i2c_op(i2c, START, 0);
}

/* i2c_databit
 * A valid data bit cycle has been seen (no data transition during high clock).
 */
static void i2c_databit(struct i2c_bus *i2c, uint8_t data) {
	/* Shift the data register */
	if (i2c->state == DATA_OUT)
		data = 0;

	i2c->output = (i2c->byte & 0x80)!=0;
	i2c->byte   = (i2c->byte << 1) | data;

	if (i2c->trace) {
		if (i2c->state==ID || i2c->state==DATA_IN) {
			fprintf(stderr, "i2c: %s BIT %d VALUE %d BYTE %02x OUTPUT %d\n",
							i2c->bitcnt==8 ? "ACK" : "RX",
							i2c->bitcnt, data, i2c->byte, i2c->output);
		}
		else {
			fprintf(stderr, "i2c: TX BIT %d VALUE %d BYTE %02x\n",
							i2c->bitcnt, i2c->output, i2c->byte);
		}
	}

	i2c->bitcnt++;

	if (i2c->state==ID && i2c->bitcnt==7)
		i2c->devid = i2c->byte;
	else if (i2c->bitcnt==8) {
		i2c_byte(i2c);
		if (i2c->state == DATA_OUT) {
			i2c->byte   = i2c_op(i2c, READ, 0);
			i2c->output = 1; /* Ack generated by master (pull data low) */
		}
		else
			i2c->output = 0; /* We're sending ACK - pull data low */
	}
	else if (i2c->bitcnt == 9) {
		/* Ack bit complet - back ready for the next byte */
		i2c->bitcnt = 0;
	}
}


/** i2c_write
 *  Write from the master CPU.
 * 'clkdata' bit 0: data line
 * 'clkdata' bit 1: clock line
 * Neither, one or both lines might have changed.
 */
void i2c_write(struct i2c_bus *i2c, uint8_t clkdata) {
	uint8_t changed = (i2c->clkdata ^ clkdata) & (I2C_DATA | I2C_CLK);
	uint8_t clk;
	uint8_t data;

	if (changed == 0)
		return;

	if (i2c->trace && (changed & I2C_CLK)) {
		/* I2C clock state has changed */
		fprintf(stderr, "i2c: Cycle (%3d) %s - DATA: %s\n",
				i2c->cycles,
				((clkdata & I2C_CLK) ? "LOW => HIGH" : "HIGH => LOW"),
				 (clkdata & I2C_DATA) ? "1" : "0");
	}

	/* Track clock changes */
	clk  = !!(clkdata & I2C_CLK);
	data = !!(clkdata & I2C_DATA);

	if (changed & I2C_CLK) {
		/* Mostly interested in clock changes */
		if (!clk) {
			/* LOW going edge - check for data or START/END */
			i2c->cycles++;

			if (i2c->stable) {
				/* This cycle completed with no special conditions - data bit */
				i2c_databit(i2c, data);
			}
			else {
				/* Data transition during HIGH clock so special condition */
				if (data==0) {
					/* Start condition */
					i2c_start(i2c);
					if (i2c->trace)
						fprintf(stderr,
							"i2c: ====================================================\n"
							"i2c: START condition detected\n");
				}
				else {
					/* End condition*/
					if (i2c->trace)
						fprintf(stderr,
						"i2c: ====================================================\n"
						"i2c: END condition detected\n");
					i2c_end(i2c);
				}
				i2c->stable = TRUE; /* ready for the next cycle */
			}
		}
	}
	if ((changed & I2C_DATA) && clk) {
		/* Special START/END condition - this won't be a valid data cycle */
		i2c->stable = FALSE;
	}
	i2c->clkdata = clkdata;
}

void i2c_reset(struct i2c_bus *i2c) {
	memset(i2c, 0, sizeof(struct i2c_bus));
	i2c->clkdata = I2C_CLK | I2C_DATA;
	i2c->state   = IDLE;
	i2c->stable  = TRUE;
}

struct i2c_bus *i2c_create(void) {
	struct i2c_bus *i2c = malloc(sizeof(struct i2c_bus));
	if (i2c == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	i2c_reset(i2c);
	return i2c;
}

void i2c_free(struct i2c_bus *i2c) {
	free(i2c);
}

void i2c_trace(struct i2c_bus *i2c, int onoff) {
	i2c->trace = !!onoff;
}
