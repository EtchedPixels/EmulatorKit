/*
 *	Emulation of a SASI disk interface
 *
 *	SASI is the predecessor to SCSI. The bus is basically the same and
 *	the signalling very much akin to early SCSI
 *
 *	There are 8 bidirectional data lines and 8 control lines some of
 *	which are totem pole.
 *
 *	BSY can be pulled high by any device to indicate the bus is in use
 *	SEL is pulled high by a device when the bus is idle and is used
 *	    to own the bus and select a target
 *	CD is high during transfers to indicate command/status info and low
 *	    to indicate data (yes C/D is a lot older than SPI TFT panels!)
 *	IO indicates the direction. High indicates transfer from the device
 *	MSG is driven during the message phase
 *	REQ is driven to indicate a data handshake is wanted
 *	ACK is driven by the initiator to respond to REQ
 *	RST is another totem pole allowing any device to reset the bus
 *
 *	We take a few shortcuts because we know
 *	- our devices being emulated will follow the spec perfectly
 *	- we don't worry about the state of BSY very much because
 *	  we own it not the controller and we won't fail and abort
 *	  stuff in any weird way.
 *
 *	The drive itself is modelled on a CDC WREN II which is one of the
 *	common devices of the time bridging the SASI/SCSI realm. It's a
 *	convenient choice because it only understands the very basic command
 *	set, but is new enough that it does all the error management internally
 *	so all the error and sparing commands are no-ops.
 *
 *	TODO:
 *	- all the phase error handling
 *	- delays
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "sasi.h"

#define NR_LUN	8

#define CHECK_CONDITION	0x02

struct sasi_bus;

struct sasi_disk
{
	int fd;
	uint32_t blocks;
	uint16_t sectorsize;
	struct sasi_bus *bus;

	uint32_t lba;
	uint16_t count;
	unsigned int ecc;

	uint8_t cmd[16];
	uint8_t dbuf[516];	/* Good enough for 512 bytes + ecc */
	uint8_t sensebuf[4];

	unsigned int dptr;
	unsigned int dlen;
	unsigned int cmd_len;
	unsigned int direction;

	unsigned int status;

};

struct sasi_bus {
	unsigned control;	/* Control and status lines */
	unsigned init_ctl;	/* Initiator control lines */
	uint8_t data_val;	/* Last data out */
	unsigned int state;
#define BUS_IDLE	0x00
#define BUS_RESET	0x01
#define BUS_TRANSFER	0x02
#define BUS_ENDSEL	0x03	/* End of the selection phases */
	struct sasi_disk *device[NR_LUN];
	struct sasi_disk *selected;
};

/*
 *	Emulated disk I/O
 */

static int do_read(struct sasi_disk *sd)
{
	if (lseek(sd->fd, sd->lba * sd->sectorsize, SEEK_SET) < 0)
		return -1;
	if (read(sd->fd, sd->dbuf, sd->sectorsize) != sd->sectorsize)
		return -1;
	return 0;
}

static int do_write(struct sasi_disk *sd)
{
	if (lseek(sd->fd, sd->lba * sd->sectorsize, SEEK_SET) < 0)
		return -1;
	if (write(sd->fd, sd->dbuf, sd->sectorsize) != sd->sectorsize)
		return -1;
	return 0;
}


/*
 *	Complete a command and initiate sending of the status
 */
static void sasi_status_in(struct sasi_disk *sd, uint8_t status)
{
	sd->status = status;
	sd->dptr = 0;		/* Used just as a counter */
	sd->bus->control &= ~SASI_MSG;
	sd->bus->control |= SASI_CD | SASI_IO | SASI_REQ;
}

static void sasi_data_out(struct sasi_disk *sd, unsigned int length)
{
	sd->bus->control &= ~(SASI_CD | SASI_IO | SASI_MSG);
	sd->bus->control |= SASI_REQ;
	sd->dptr = 0;
	sd->dlen = length;
}

static void sasi_data_in(struct sasi_disk *sd, unsigned int length)
{
	sd->bus->control &= ~(SASI_CD|SASI_MSG);
	sd->bus->control |= SASI_IO | SASI_REQ;
	sd->dptr = 0;
	sd->dlen = length;
}

static void sasi_sense_clear(struct sasi_disk *sd)
{
	memset(sd->sensebuf, 0, 4);
}

static void sasi_sense_only(struct sasi_disk *sd, uint8_t error)
{
	sasi_sense_clear(sd);
	sd->sensebuf[0] = error;
}

static void sasi_sense_with_lba(struct sasi_disk *sd, uint8_t error)
{
	sd->sensebuf[1] = sd->lba >> 16;
	sd->sensebuf[2] = sd->lba >> 8;
	sd->sensebuf[3] = sd->lba;
	sd->sensebuf[0] = error | 0x80;
}

static void sasi_check_lba(struct sasi_disk *sd, uint32_t lba, uint16_t len)
{
	if (lba + len < lba || lba + len >= sd->blocks) {
		sasi_sense_with_lba(sd, 0x21);
		sasi_status_in(sd, CHECK_CONDITION);
	} else
		sasi_status_in(sd, 0);
}

/* Called each time a block completes */
static void sasi_read_block(struct sasi_disk *sd)
{
	if (sd->lba == sd->blocks) {
		sasi_sense_with_lba(sd, 0x21);
		sasi_status_in(sd, CHECK_CONDITION);
		return;
	}
	if (sd->count == 0) {
		sasi_sense_clear(sd);
		sasi_status_in(sd, 0);
		return;
	}
	if (do_read(sd) < 0) {
		sasi_sense_with_lba(sd, 0x14);	/* Target sector not found */
		sasi_status_in(sd, CHECK_CONDITION);
		return;
	}
	sd->lba++;
	sd->count--;
	sasi_data_in(sd, sd->sectorsize + 4 * sd->ecc);
}

static void sasi_read_command(struct sasi_disk *sd, uint32_t lba,
	uint16_t count, unsigned int ecc)
{
	sd->lba = lba;
	sd->count = count;
	sd->ecc = ecc;
	sasi_read_block(sd);
}

/* Called each time a block completes */
static void sasi_write_block(struct sasi_disk *sd)
{
	if (sd->lba == sd->blocks) {
		sasi_sense_with_lba(sd, 0x21);
		sasi_status_in(sd, CHECK_CONDITION);
		return;
	}
	/* Write the data received */
	if (do_write(sd)) {
		sasi_sense_with_lba(sd, 0x14);	/* Target sector not found */
		sasi_status_in(sd, CHECK_CONDITION);
		return;
	}
	sd->lba++;
	sd->count--;
	/* And done */
	if (sd->count == 0) {
		sasi_sense_clear(sd);
		sasi_status_in(sd, 0);
		return;
	}
	sasi_data_out(sd, sd->sectorsize + 4 * sd->ecc);
}

/*
 *	Start the write state machine up by requesting the first block
 *	of data from the host.
 */
static void sasi_write_command(struct sasi_disk *sd, uint32_t lba,
	uint16_t count, unsigned int ecc)
{
	sd->lba = lba;
	sd->count = count;
	sd->ecc = ecc;
	sasi_data_out(sd, sd->sectorsize + 4 * sd->ecc);
}

/*
 *	Disk format is a fire and forget. We format all the blocks
 *	from the LBA given to the media end
 */
static void sasi_format_disk(struct sasi_disk *sd)
{
	if (sd->lba >= sd->blocks) {
		sasi_status_in(sd, CHECK_CONDITION);
		return;
	}
	while(sd->lba < sd->blocks) {
		if (do_write(sd)) {
			sasi_sense_with_lba(sd, 0x1A);
			sasi_status_in(sd, CHECK_CONDITION);
		}
		sd->lba++;
	}
	sasi_status_in(sd, 0);
}

/*
 *	Command emulation. All SASI commands are 6 byte
 */
static void sasi_command_process(struct sasi_disk *sd)
{
	uint32_t lba;
	uint8_t lun;
	if (sd->cmd_len < 6)
		return;

	lba = (sd->cmd[1] & 0x01F) << 16;
	lba |= sd->cmd[2] << 8;
	lba |= sd->cmd[3];
	lun = sd->cmd[1] >> 5;

	if (lun) {
		sasi_sense_only(sd, 0x21);
		sasi_status_in(sd, CHECK_CONDITION);
		sd->dptr = 0;
		return;
	}
	switch(sd->cmd[0]) {
	/* Group 0 */
	case 0x00:		/* TUR */
		sasi_status_in(sd, 0);
		break;
	case 0x01:		/* RECAL */
		sasi_status_in(sd, 0);
		break;
	case 0x03:		/* REQ SENSE */
		memcpy(sd->dbuf, sd->sensebuf, 4);
		sasi_data_in(sd, 4);
		break;
	case 0x04:		/* FORMAT */
		if (!(sd->cmd[5] & 0x20))
			memset(sd->dbuf, 0x6C, sd->sectorsize);
		sd->lba = lba;
		sasi_format_disk(sd);
		break;
	case 0x05:		/* CHECK TRACK FORMAT */
		sasi_status_in(sd, 0);
		break;
	case 0x06:		/* FORMAT TRACK */
		sasi_status_in(sd, 0);
		break;
	case 0x07:		/* FORMAT BAD TRACK */
		sasi_status_in(sd, 0);
		break;
	case 0x08:		/* READ */
		sasi_read_command(sd, lba, sd->cmd[4] ? sd->cmd[4] : 256, 0);
		break;
	case 0x09:		/* READ VERIFY */
		sasi_check_lba(sd, lba,  sd->cmd[4] ? sd->cmd[4] : 256);
		break;
	case 0x0A:		/* WRITE */
		sasi_write_command(sd, lba, sd->cmd[4] ? sd->cmd[4] : 256, 0);
		break;
	case 0x0B:		/* SEEK */
		sasi_check_lba(sd, lba, 0);
		break;
	case 0x0C:		/* INIT DEV CHARACTERISTICS */
		sasi_data_out(sd, 8);
		break;
	case 0x0D:		/* READ ECC BURST ERROR LENGTH */
		sasi_data_in(sd, 1);
		break;
	case 0x0E:		/* FORMAT ALTERNATE TRACK */
		sasi_data_out(sd, 3);
		break;
	case 0x0F:		/* WRITE SECTOR BUFFER */
		sasi_data_out(sd, sd->sectorsize);
		break;
	case 0x10:		/* READ SECTOR BUFFER */
		sasi_data_in(sd, sd->sectorsize);
		break;
	/* Group 7 - diagnostics read/write long etc */
	case 0xE0:		/* RAM DIAGNOSTIC */
		sasi_status_in(sd, 0);
		break;
	case 0xE3:		/* DRIVE DIAGNOSTIC */
		sasi_status_in(sd, 0);
		break;
	case 0xE5:		/* READ LONG */
		sasi_read_command(sd, lba, sd->cmd[4] ? sd->cmd[4] : 256, 1);
		break;
	case 0xE6:		/* WRITE LONG */
		sasi_write_command(sd, lba, sd->cmd[4] ? sd->cmd[4] : 256, 1);
		break;
	case 0xE7:		/* RETRY STATISTICS */
		memset(sd->dbuf, 0, 8);
		sasi_data_in(sd, 8);
		return;
	default:
		sasi_sense_only(sd, 0x20);
		sasi_status_in(sd, CHECK_CONDITION);
		sd->dptr = 0;
		return;
	}
}

/* A data in command has completed the data in, now act on it */
static void sasi_command_execute_in(struct sasi_disk *sd)
{
	switch(sd->cmd[0]) {
	case 0x03:
		sasi_status_in(sd, 0);
		break;
	case 0x08:
		sasi_read_block(sd);
		break;
	case 0x0D:
		sasi_status_in(sd, 0);
		break;
	case 0x10:
		sasi_status_in(sd, 0);
		break;
	case 0xE5:
		sasi_read_block(sd);
		break;
	case 0xE7:
		sasi_status_in(sd, 0);
		break;
	default:
		fprintf(stderr, "sasi_command_execute: state error cmd %02x\n",
			sd->cmd[0]);
	}
}

static void sasi_command_execute_out(struct sasi_disk *sd)
{
	switch(sd->cmd[0]) {
	case 0x0A:
		sasi_write_block(sd);
		break;
	case 0x0C:
		sasi_status_in(sd, 0);
		break;
	case 0x0E:
		sasi_status_in(sd, 0);
		break;
	case 0x0F:
		sasi_status_in(sd, 0);
		break;
	case 0xE6:
		sasi_write_block(sd);
		break;
	}
}

/*
 *	The controller asserts SASI_SEL and data lines. The device responds
 *	to its data bit by asserting SASI_BSY. We don't deal with disconnect
 *	reconnect at this point as it's a later SCSI thing really.
 */
static void sasi_device_select(struct sasi_disk *sd)
{
	/* We indicate to the initiator that we were selected */
	sd->bus->control |= SASI_BSY | SASI_REQ;
//	sd->bus->control &= ~SASI_SEL;
	sd->bus->selected = sd;
	sd->bus->state = BUS_ENDSEL;
}

static void sasi_device_begin(struct sasi_disk *sd)
{
	/* We expect a command */
	sd->bus->control &= ~(SASI_IO | SASI_MSG);
	sd->bus->control |= SASI_CD | SASI_BSY;
	sd->dptr = 0;
	sd->cmd_len = 0;
}

static void sasi_device_reset(struct sasi_disk *sd)
{
}

/*
 *	The disk is in DATA OUT phase, each byte read goes to the controller
 *	until done, then we change state accordingly
 */

static void sasi_disk_ack_read(struct sasi_disk *sd)
{
	sd->dptr++;
	if (sd->dptr == sd->dlen)
		sasi_command_execute_in(sd);
	if (sd->dptr >= sizeof(sd->dbuf))
		sd->dptr = 0;
}

static uint8_t sasi_disk_read(struct sasi_disk *sd)
{
	return sd->dbuf[sd->dptr];
}

/*
 *	Put the SASI bus into free state
 */
static void sasi_bus_idle(struct sasi_bus *bus)
{
	bus->control = 0;
	bus->selected = NULL;
	bus->state = BUS_IDLE;
}

/*
 *	Status byte read
 */

static uint8_t sasi_disk_status(struct sasi_disk *sd)
{
	if (sd->dptr == 0)
		return sd->status;
	else
		return 0;
}

static void sasi_disk_ack_status(struct sasi_disk *sd)
{
	sd->dptr++;
	if (sd->dptr == 2)
		sasi_bus_idle(sd->bus);
}

/*
 *	Nessage phase byte - just return 0
 */
static uint8_t sasi_disk_mesg(struct sasi_disk *sd)
{
	return 0;
}

static void sasi_disk_ack_mesg(struct sasi_disk *sd)
{
	sd->bus->control &= ~(SASI_BSY|SASI_MSG|SASI_CD|SASI_IO|SASI_REQ);
}

/*
 *	The disk is in data in phase, we accumulate data until the
 *	request is completed and thene execute the command. After this
 *	we will change state accordingly
 */
static void sasi_disk_write(struct sasi_disk *sd, uint8_t r)
{
	sd->dbuf[sd->dptr++] = r;
	if (sd->dptr == sd->dlen)
		sasi_command_execute_out(sd);
}

/*
 *	Command phase, bytes are accumulated in the command buffer until
 *	the command is completed. At that point we execute the command and
 *	change bus state to indicate to the host what the next step is.
 */
static void sasi_disk_command(struct sasi_disk *sd, uint8_t val)
{
	sd->cmd[sd->cmd_len++] = val;
	/* If this is a complete command now then this routine will drop SASI_CD
	   and set any data direction needed or status info */
	sasi_command_process(sd);
}

/*
 *	The SASI_SEL line has been driven, find out if anyone is going
 *	to respond
 */
static void sasi_bus_select(struct sasi_bus *bus, uint8_t data)
{
	unsigned int i;
	/* Should only be one bit set or you get a mess */
	/* FIXME: sanity check one bit set and log error if not */

	if (bus->init_ctl & SASI_BSY)
		return;
	for (i = 0; i < NR_LUN; i++) {
		if ((data & (1 << i)) && bus->device[i]) {
			sasi_device_select(bus->device[i]);
		}
	}
}

/*
 *	The drive not the host controls the phases in SASI, so we
 *	have a lot less to worry about when emulating because we know our
 *	virtual drive will be 'correct'.
 *
 *	We don't emulate REQ/ACK at this level as most hardware handles
 *	it automatically. If your interface for emulation does not then
 *	store the byte internally on the write, and call the transfer
 *	routines on the ACK.
 */
void sasi_write_data(struct sasi_bus *bus, uint8_t data)
{
	bus->data_val = data;
	switch(bus->state) {
	case BUS_RESET:
		break;
	case BUS_IDLE:
		if (bus->control & SASI_SEL) {
			sasi_bus_select(bus, data);
			if (bus->control & SASI_BSY)
				bus->state = BUS_TRANSFER;
		}
		break;
		/* We don't have BUS_SEELCTION as a state as we don't model delays
		   on selection yet */
	case BUS_TRANSFER:
		if (bus->control & SASI_MSG) {
			/* Can't happen as we don't emulate multi-master */
			return;
		}
		if (bus->control & SASI_IO) {
			/* Wrong direction */
			return;
		}
		if (bus->control & SASI_CD)
			sasi_disk_command(bus->selected, data);
		else
			sasi_disk_write(bus->selected, data);
		break;
	}
}

/* Load bus but don't touch strobes. Used for some of the more software
   based controllers */
void sasi_set_data(struct sasi_bus *bus, uint8_t data)
{
	bus->data_val = data;
}

/*
 *	Same logic for read basically but we don't have to worry about
 *	selection as we don't support multi-master so won't respond
 */

uint8_t sasi_read_bus(struct sasi_bus *bus)
{
	if (bus->state == BUS_IDLE || bus->state == BUS_RESET)
		return 0xFF;	/* Reading a free bus */
	if (bus->control & SASI_MSG)
		return sasi_disk_mesg(bus->selected);
	if (!(bus->control & SASI_IO))
		return 0xFF;	/* Wrong direction */
	if (bus->control & SASI_CD)
		return sasi_disk_status(bus->selected);
	else
		return sasi_disk_read(bus->selected);
}

void sasi_ack_bus(struct sasi_bus *bus)
{
	if (bus->control & SASI_MSG)
		sasi_disk_ack_mesg(bus->selected);
	if (!(bus->control & SASI_IO))
		return;		/* Wrong direction */
	if (bus->control & SASI_CD)
		sasi_disk_ack_status(bus->selected);
	else
		sasi_disk_ack_read(bus->selected);
}

uint8_t sasi_read_data(struct sasi_bus *bus)
{
	uint8_t r;
	if (bus->state == BUS_IDLE || bus->state == BUS_RESET)
		return 0xFF;	/* Reading a free bus */
	r = sasi_read_bus(bus);
	sasi_ack_bus(bus);
	return r;
}

static void sasi_bus_exit_reset(struct sasi_bus *bus)
{
	int i;
	for (i = 0; i < NR_LUN; i++) {
		bus->selected = NULL;
		if (bus->device[i])
			sasi_device_reset(bus->device[i]);
	}
	sasi_bus_idle(bus);
}

/*
 *	Lines driven from the host end (we ignore multi-master)
 *
 *	TODO;  we should keep track of functional pairs of signals
 *	and compute the totem poles for this lot. It would actually
 *	be cleaner than the current tricks
 */
void sasi_bus_control(struct sasi_bus *bus, unsigned val)
{
	bus->init_ctl = val;
	/* Reset handling */
	if (val & SASI_RST)
		bus->state = BUS_RESET;
	else if (bus->state == BUS_RESET)
		sasi_bus_exit_reset(bus);
	else if (bus->state == BUS_ENDSEL) {
		/* Initiator dropped SEL */
		if (!(val & SASI_SEL)) {
			sasi_device_begin(bus->selected);
			bus->state = BUS_TRANSFER;
		}
	}
	/* Select line */
	if (val & SASI_SEL) {
		bus->control |= SASI_SEL;
		sasi_bus_select(bus, bus->data_val);
	} else
		bus->control &= ~SASI_SEL;
	if (val & SCSI_ATN)
		bus->control |= SCSI_ATN;
	else
		bus->control &= ~SCSI_ATN;
}

unsigned sasi_bus_state(struct sasi_bus *bus)
{
	/* This is a bit of a fudge for now. We need to track this based upon
	   the device being in a transfer state */
	if (bus->state != BUS_IDLE && bus->state != BUS_RESET)
		bus->control |= SASI_REQ;
	else
		bus->control &= ~SASI_REQ;
	return bus->control;
}

static void *alloc(size_t size)
{
	void *p = malloc(size);
	if (p == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	memset(p, 0, size);
	return p;
}

struct sasi_bus *sasi_bus_create(void)
{
	return alloc(sizeof(struct sasi_bus));
}

void sasi_disk_attach(struct sasi_bus *bus, unsigned int lun, const char *path, unsigned int sectorsize)
{
	struct sasi_disk *sd = alloc(sizeof(struct sasi_disk));
	sd->bus = bus;
	sd->sectorsize = sectorsize;
	sd->fd = open(path, O_RDWR);
	if (sd->fd == -1) {
		perror(path);
		exit(1);
	}
	if (lseek(sd->fd, (off_t)0, 2) == -1) {
		perror(path);
		exit(1);
	}
	sd->blocks = lseek(sd->fd, (off_t)0, 1) / sd->sectorsize;
	bus->device[lun] = sd;
}

static void sasi_disk_free(struct sasi_disk *sd)
{
	close(sd->fd);
	free(sd);
}

void sasi_bus_free(struct sasi_bus *bus)
{
	unsigned int i;
	for (i = 0; i < NR_LUN; i++) {
		if (bus->device[i])
			sasi_disk_free(bus->device[i]);
	}
	free(bus);
}

void sasi_bus_reset(struct sasi_bus *bus)
{
	sasi_bus_exit_reset(bus);
}
