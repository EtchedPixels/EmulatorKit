/*
 *	A minimal fake RiscV platform to experiment with without having
 *	to fight vendor toolkits and 500 page Chinese translations
 *
 *	Very very minimal for the moment. Can be expanded with SD cards
 *	and the like once we get there.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "riscv-disas.h"

#include "sdcard.h"

#define MINIRV32_CUSTOM_MEMORY_BUS
#define MINIRV32_RAM_IMAGE_OFFSET	0x00000000U
#define MINI_RV32_RAM_SIZE		0x60000000U

#define MINIRV32_IO_OFFSET		0x60000000U
#define MINIRV32_IO_SIZE		0x40000000U

#define MINIRV32_LOAD4(addr)		mem_read_32(addr, &trap, &rval)
#define MINIRV32_LOAD2(addr)		mem_read_16(addr, &trap, &rval)
#define MINIRV32_LOAD1(addr)		mem_read_8(addr, &trap, &rval)
#define MINIRV32_STORE4(addr, val)	mem_write_32(addr, val, &trap, &rval)
#define MINIRV32_STORE2(addr, val)	mem_write_16(addr, val, &trap, &rval)
#define MINIRV32_STORE1(addr, val)	mem_write_8(addr, val, &trap, &rval)

/* FIXME */
#define ALIGN	1
#define INVALID	2

static uint8_t ram[384 * 1024];
static struct sdcard *sdcard;
static volatile int done;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_ROM	4
#define TRACE_UNK	8
#define TRACE_CPU	16
#define TRACE_IRQ	32
#define TRACE_SD	64
#define TRACE_UART	128

static int trace = 0;

static uint32_t rv32_glue(uint32_t pc, uint32_t ir, uint32_t retval);
static uint32_t io_out(uint32_t addr, uint32_t val);
static uint32_t io_in(uint32_t addr);
static void csr_out(uint32_t addr, uint32_t val);

static uint32_t mem_read_32(uint32_t addr, uint32_t *trap, uint32_t *rval);
static uint32_t mem_read_16(uint32_t addr, uint32_t *trap, uint32_t *rval);
static uint32_t mem_read_8(uint32_t addr, uint32_t *trap, uint32_t *rval);
static uint32_t mem_write_32(uint32_t addr, uint32_t val, uint32_t *trap, uint32_t *rval);
static uint32_t mem_write_16(uint32_t addr, uint32_t val, uint32_t *trap, uint32_t *rval);
static uint32_t mem_write_8(uint32_t addr, uint32_t val, uint32_t *trap, uint32_t *rval);

#define MINIRV32WARN		printf
#define MINIRV32_DECORATE	static
#define MINIRV32_IMPLEMENTATION
#define MINIRV32_POSTEXEC(pc, ir, retval) \
				trap = rv32_glue(pc, ir, trap)
#define MINIRV32_HANDLE_MEM_STORE_CONTROL(addr, val)	\
				if (io_out(addr, val)) return val;
#define MINIRV32_HANDLE_MEM_LOAD_CONTROL(addr, val) \
				val = io_in(addr)
#define MINIRV32_OTHERCSR_WRITE(csr, value) \
				csr_out(csr, value)

static void disassemble(uint32_t ir, uint32_t addr);

#include "riscv/mini-rv32ima.h"

struct MiniRV32IMAState cpu;

static uint16_t lastch = 0xFFFF;

int check_chario(void)
{
	fd_set i, o;
	struct timeval tv;
	unsigned int r = 0;
	char c;


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
	if (FD_ISSET(0, &i)) {
		r |= 1;
		if (lastch == 0xFFFF) {
			if (read(0, &c, 1) == 1)
				lastch = c;
		}
	}
	if (FD_ISSET(1, &o))
		r |= 2;
	return r;
}

unsigned int next_char(void)
{
	char c = lastch;
	lastch = 0xFFFF;
	if (c == 0x0A)
		c = '\r';
	return c;
}

static void uart_out(unsigned uart, uint32_t addr, uint32_t val)
{
	switch(addr & 0xFFF) {
		case 0:
			putchar(val & 0xFF);
			fflush(stdout);
			break;
	}
}

static uint32_t uart_in(unsigned uart, uint32_t addr)
{
	switch(addr & 0xFFF) {
		case 0:
			return next_char();
		case 4:
			return check_chario();
	}
	return 0xFFFFFFFF;
}

static uint32_t last_spi;

static void spi_out(uint32_t addr, uint32_t val)
{
	addr &= 0xFFF;
	if (!sdcard)
		return;

	if (addr == 0) {
		last_spi = sd_spi_in(sdcard, val);
		return;
	}
	if (addr == 4) {
		if (val & 1)
			sd_spi_lower_cs(sdcard);
		else
			sd_spi_raise_cs(sdcard);
	}
}

static uint32_t spi_in(uint32_t addr)
{
	addr &= 0xFFF;
	if (addr == 0)
		return last_spi;
	return 0xFFFFFFFF;
}

uint32_t io_out(uint32_t addr, uint32_t val)
{
	switch(addr >> 12) {
	case 0x60000:
		uart_out(0, addr, val);
		return 0;
	case 0x60003:
		spi_out(addr, val);
		return 0;
	case 0x60010:
		uart_out(1, addr, val);
		return 0;
	default:
		return 0;
	}
}

uint32_t io_in(uint32_t addr)
{
	switch(addr >> 12) {
	case 0x60000:
		return uart_in(0, addr);
	case 0x60003:
		return spi_in(addr);
	case 0x60010:
		return uart_in(1, addr);
	default:
		return 0;
	}
}

void csr_out(uint32_t addr, uint32_t val)
{
}

/* Can set top bit to cause interrupts */
uint32_t rv32_glue(uint32_t pc, uint32_t ir, uint32_t trap)
{
	return trap;
}

/* FIXME: wrong for reads overlapping end */
static uint8_t *mem_addr(uint32_t addr, uint32_t *trap, uint32_t *rval, unsigned is_write)
{
	if (addr >= 0x3FC80000 && addr <= 0x3FCE0000)
		return ram + (addr & 0x3FFFF);
	if (addr >= 0x4038000 && addr <= 0x403E0000 && !is_write) {
		if (addr & 3) {
			*trap = ALIGN;
			*rval = addr;
			return NULL;
		}
		return ram + (addr & 0x3FFFF);
	}
	/* TODO 50000000-50001FFF */
	*trap = INVALID;
	*rval = addr;
	return NULL;
}

static uint32_t mem_read_8(uint32_t addr, uint32_t *trap, uint32_t *rval)
{
	uint8_t *p = mem_addr(addr, trap, rval, 0);
	if (p)
		return *p;
	else
		return 0;
}

static uint32_t mem_read_16(uint32_t addr, uint32_t *trap, uint32_t *rval)
{
	uint8_t *p = mem_addr(addr, trap, rval, 0);
	if (p == NULL)
		return 0;
	return *p + (p[1] << 8);
}

static uint32_t mem_read_32(uint32_t addr, uint32_t *trap, uint32_t *rval)
{
	uint8_t *p = mem_addr(addr, trap, rval, 0);
	if (p == NULL)
		return 0;
	return *p + (p[1] << 8) + (p[2] << 16) + (p[3] << 24);
}

static uint32_t mem_write_8(uint32_t addr, uint32_t val, uint32_t *trap, uint32_t *rval)
{
	uint8_t *p = mem_addr(addr, trap, rval, 1);
	if (p == NULL)
		return 0;
	*p = val;
	return 0;
}

static unsigned mem_write_16(uint32_t addr, uint32_t val, uint32_t *trap, uint32_t *rval)
{
	uint8_t *p = mem_addr(addr, trap, rval, 1);
	if (p == NULL)
		return 0;
	*p = val;
	p[1] = val >> 8;
	return 0;
}

static unsigned mem_write_32(uint32_t addr, uint32_t val, uint32_t *trap, uint32_t *rval)
{
	uint8_t *p = mem_addr(addr, trap, rval, 1);
	if (p == NULL)
		return 0;
	*p = val;
	p[1] = val >> 8;
	p[2] = val >> 16;
	p[3] = val >> 24;
	return 0;
}

static void disassemble(uint32_t ir, uint32_t addr)
{
	char buf[256];
	if (!(trace & TRACE_CPU))
		return;
	fprintf(stderr, "%08X: ", addr);
	disasm_inst(buf, sizeof(buf), rv32, addr, ir);
	fprintf(stderr, "%s\n", buf);
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	done = 1;
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "mini-riscv: [-r rom] [-S disk] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	static struct timespec tc;
	int opt;
	int fd;
	char *rompath = "mini-riscv.rom";
	char *sdpath = NULL;
//	unsigned int cycles = 0;

	while ((opt = getopt(argc, argv, "r:d:S:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'S':
			sdpath = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	if (read(fd, ram, 131072) < 512) {
		fprintf(stderr, "mini-riscv: short rom '%s'.\n", rompath);
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (sdpath) {
		sdcard = sd_create("sd0");
		fd = open(sdpath, O_RDWR);
		if (fd == -1) {
			perror(sdpath);
			exit(1);
		}
		sd_attach(sdcard, fd);
		if (trace & TRACE_SD)
			sd_trace(sdcard, 1);
		sd_blockmode(sdcard);
	}

	/* 5ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 5000000L;

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

	cpu.pc = 0x3FC80000;//MINIRV32_RAM_IMAGE_OFFSET;
	cpu.regs[10] = 0x00;
	cpu.extraflags |= 3;

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
//		unsigned int i;
		unsigned int j;
		uint32_t elapsed = 0;

		for (j = 0; j < 100; j++) {
			uint32_t ret = MiniRV32IMAStep(&cpu, ram, 0, elapsed, 1024);
			switch(ret) {
			case 0:
			case 1:
			case 3:
				break;
			case 0x7777:
			case 0x5555:
				done = 1;
				break;
			default:
				fprintf(stderr, "invalid rv32 ret %x\n",
					ret);
				done = 1;
				break;
			}
		}
		/* Do 5ms of I/O and delays */
		nanosleep(&tc, NULL);
		/* poll_irq_event(); */
	}
	exit(0);
}
