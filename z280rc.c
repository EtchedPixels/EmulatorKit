/*
 *	Zilog Z280 in ZBUS mode
 *	2MB SRAM
 *	IDE (16bit wide) @0x0000C0
 *	Bitbang DS1302 on port 0x0000A2
 *
 *	Optional RC2014 devices
 *	RC2014 IRQ is z280 INTB
 *
 *	Bootstrap is weird and not currently modelled. All CPU reads
 *	in the low 512 bytes cause the next byte of the CF adapter
 *	to be fetched until I/O 0x0000A0 is accessed
 *
 *	The IDE is byteswapped
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>

#include "z280/z280.h"
#include "ide.h"
#include "rtc_bitbang.h"

int VERBOSE = 0;		/* FIXME: make a trace flag */
static uint8_t ram[0x20000];

struct z280_device *cpu;
struct ide_controller *ide;
struct rtc *rtc;

static uint8_t fast = 0;
static uint8_t int_recalc = 0;
static uint8_t fifo_off = 1;	/* Until can emulate it right */

volatile int emulator_done;

#define TRACE_MEM	0x000001
#define TRACE_IO	0x000002
#define TRACE_UNK	0x000004
#define TRACE_RTC	0x000008
#define TRACE_CPU	0x000010
#define TRACE_IRQ	0x000020
#define TRACE_IDE	0x000040

#define XTALCLK		(2 * 147456000)

static int trace = 0;

/* FIFO emulation doesn't work as the CPU emulation doesn't
   seem to correctly word fetch instructions */
static uint8_t do_mem_read8(unsigned addr)
{
	if (addr < 512 && fifo_off == 0)
		return ide_read16(ide, ide_data);
	/* TOOD: FIFO model */
	return ram[addr & 0x1FFFF];
}

uint8_t mem_read8(unsigned addr)
{
	uint8_t r = do_mem_read8(addr);
	if (trace & TRACE_MEM)
		fprintf(stderr, "R %04X -> %02X\n", addr, r);
	return r;
}

void mem_write8(unsigned addr, uint8_t value)
{
	/* TODO FIFO model */
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %04X <- %02X\n", addr, value);
	ram[addr & 0x1FFFF] = value;
}

/* Little endian */
uint16_t mem_read16(unsigned addr)
{
	if (addr < 512 && fifo_off == 0)
		return ide_read16(ide, ide_data);
	return mem_read8(addr) + (mem_read8(addr + 1) << 8);
}

void mem_write16(unsigned addr, uint16_t value)
{
	mem_write8(addr, value);
	mem_write8(addr + 1, value >> 8);
}

static const uint8_t idemap[16] = {
	ide_data,
	0,
	ide_error_r,
	0,
	0,
	ide_sec_count,
	0,
	ide_sec_num,
	/*ide_altst_r */ 0,
	ide_cyl_low,
	0,
	ide_cyl_hi,
	0,
	ide_dev_head,
	0,
	ide_status_r
};


uint8_t io_read_byte(unsigned addr)
{
	addr &= 0xFF;
	if (addr == 0xA2)
		return rtc_read(rtc);
	if (addr >= 0xC0 && addr <= 0xCF)
		return ide_read16(ide, idemap[addr & 0x0F]);
	return 0xFF;
}

void io_write_byte(unsigned addr, uint8_t value)
{
	addr &= 0xFF;
	if (addr == 0xA0) {
		fifo_off = 1;
		return;
	}
	if (addr == 0xA2) {
		rtc_write(rtc, value);
		return;
	}
	if (addr >= 0xC0 && addr <= 0xCF) {
		ide_write16(ide, idemap[addr & 0x0F], value);
		return;
	}
}

uint16_t io_read_word(unsigned addr)
{
	addr &= 0xFF;
	if (addr == 0xA2)
		return rtc_read(rtc);
	if (addr >= 0xC0 && addr <= 0xCF)
		return ide_read16(ide, idemap[addr & 0x0F]);
	return 0xFFFF;
}

void io_write_word(unsigned addr, uint16_t value)
{
	addr &= 0xFF;
	if (addr == 0xA0) {
		fifo_off = 1;
		return;
	}
	if (addr == 0xA2) {
		rtc_write(rtc, value);
		return;
	}
	if (addr >= 0xC0 && addr <= 0xCF) {
		ide_write16(ide, idemap[addr & 0x0F], value);
		return;
	}
}

unsigned int check_chario(void)
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

unsigned int next_char(void)
{
	char c;
	if (read(0, &c, 1) != 1) {
		printf("(tty read without ready byte)\n");
		return 0xFF;
	}
	if (c == 0x0A)
		c = '\r';
	return c;
}

void recalc_interrupts(void)
{
	int_recalc = 1;
}

static void poll_irq_event(void)
{
}

void z280_uart_tx(void * device, int channel, uint8_t value)
{
	write(1, &value, 1);
}

int z280_uart_rx(void * device, int channel)
{
	if (check_chario() & 1)
		return next_char();
	return -1;
}

uint8_t init_bti(void * device)
{
	return 0;
}

int irq0ackcallback(void * device, int irqnum)
{
	return 0;
}

uint8_t debugger_getmem(void *device, offs_t addr)
{
	return ram[addr];
}

void z280_debug(device_t *device, offs_t curpc)
{
	char ibuf[20];
	offs_t dres,i;
	char fbuf[10];
	offs_t transpc;

	if(trace & TRACE_CPU) {
		cpu_string_export_z280(device,STATE_GENFLAGS,fbuf);
		fprintf(stderr, "%s AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SSP=%04X USP=%04X MSR=%04X\n",fbuf,
			cpu_get_state_z280(device,Z280_AF),
			cpu_get_state_z280(device,Z280_BC),
			cpu_get_state_z280(device,Z280_DE),
			cpu_get_state_z280(device,Z280_HL),
			cpu_get_state_z280(device,Z280_IX),
			cpu_get_state_z280(device,Z280_IY),
			cpu_get_state_z280(device,Z280_SSP),
			cpu_get_state_z280(device,Z280_USP),
			cpu_get_state_z280(device,Z280_CR_MSR));
		transpc = curpc;
		cpu_translate_z280(device,AS_PROGRAM,0,&transpc);
		/* Jumped into oblivion */
		if (transpc >= sizeof(ram)) {
			fprintf(stderr, "%06X: jumped to fishkill\n", transpc);
			exit(1);
		}
		dres = cpu_disassemble_z280(device,ibuf,transpc,&ram[transpc],0);
		fprintf(stderr, "%06X: ",transpc);
		for (i=0;i<(dres &DASMFLAG_LENGTHMASK);i++) fprintf(stderr, "%02X",ram[transpc+i]);
		for ( ; i < 7; i++) {
			fputc(' ', stderr);
			fputc(' ', stderr);
		}
		fprintf(stderr, " %s\n",ibuf);
	}
}

static struct address_space memspace = {
	mem_read8,
	mem_read16,
	mem_write8,
	mem_write16,
	mem_read8,
	mem_read16
};

struct address_space iospace = {
	io_read_byte,
	io_read_word,
	io_write_byte,
	io_write_word,
	NULL,
	NULL
};


static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	emulator_done = 1;
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "z280rc: [-f] [-i idepath] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *idepath = NULL;

	while ((opt = getopt(argc, argv, "d:fi:")) != -1) {
		switch (opt) {
		case 'i':
			idepath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	ide = ide_allocate("cf");
	if (idepath == NULL) {
		fprintf(stderr, "z280rc: IDE path required.\n");
		exit(1);
	}
	fd = open(idepath, O_RDWR);
	if (fd == -1) {
		perror(idepath);
		exit(1);
	}
	if (ide_attach(ide, 0, fd) == 0)
		ide_reset_begin(ide);
	else
		exit(1);
	if (pread(fd, ram, 512, 1024) != 512) {
		fprintf(stderr, "z280rc: couldn't read bootstrap.\n");
		exit(1);
	}

	rtc = rtc_create();
	rtc_trace(rtc, trace & TRACE_RTC);

	/* 20ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 20000000L;

	if (tcgetattr(0, &term) == 0) {
		saved_term = term;
		atexit(exit_cleanup);
		signal(SIGINT, cleanup);
		signal(SIGQUIT, cleanup);
		signal(SIGPIPE, cleanup);
		term.c_lflag &= ~(ICANON | ECHO);
		term.c_cc[VMIN] = 0;
		term.c_cc[VTIME] = 1;
		term.c_cc[VINTR] = 0;
		term.c_cc[VSUSP] = 0;
		term.c_cc[VSTOP] = 0;
		tcsetattr(0, TCSADRAIN, &term);
	}

	cpu = cpu_create_z280("Z280", Z280_TYPE_Z280, XTALCLK / 2, &memspace, &iospace, irq0ackcallback, NULL /*daisychain */ ,
			      init_bti, 1 /*Z-BUS */ ,
			      0, XTALCLK / 16, 0, z280_uart_rx, z280_uart_tx);

	cpu_reset_z280(cpu);
	// DMA2,3 /RDY are tied to GND
	z280_set_rdy_line(cpu, 2, ASSERT_LINE);
	z280_set_rdy_line(cpu, 3, ASSERT_LINE);

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!emulator_done) {
		unsigned int i;
		/* We have to run the DMA engine and Z180 in step per
		   instruction otherwise we will mess up on stalling DMA */

		/* Do an emulated 20ms of work (368640 clocks) */
		for (i = 0; i < 50; i++) {
			cpu_execute_z280(cpu, 10000);	/* FIXME RATE */
		}
		if (!fast)
			nanosleep(&tc, NULL);
		poll_irq_event();
	}
	exit(0);
}
