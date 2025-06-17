/*
 *	Platform features
 *
 *	68000 processor card for rcbus with 20bit addressing mapped
 *	000000-00FFFF	ROM
 *	010000-01FFFF	IO Window (hides ROM in supervisor mode only)
 *	020000-07FFFF	ROM
 *	080000-0FFFFF	RAM
 *
 *	IDE at 0x10-0x17 no high or control access
 *	PPIDE at 0x20
 *	Flat 1MB address space with the low 512K as ROM
 *	RTC at 0x0C
 *	16550A at 0xC0
 *	68B50 at 0xA0
 *
 *	Optional MMU adapter
 *
 *	TODO: QUART or similar and timer emulation
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
#include <m68k.h>
#include "serialdevice.h"
#include "ttycon.h"
#include "acia.h"
#include "ide.h"
#include "ppide.h"
#include "rtc_bitbang.h"
#include "16x50.h"
#include "w5100.h"
#include "sram_mmu8.h"

static uint8_t ramrom[1024 * 1024];	/* ROM low RAM high */

static uint8_t fast = 0;
static uint8_t wiznet = 0;
static uint8_t bmmu = 0;

static int kernelmode;

static uint16_t tstate_steps = 200;

/* Who is pulling on the interrupt line */

#define IRQ_ACIA	1
#define IRQ_16550A	2

static nic_w5100_t *wiz;
static struct acia *acia;
static struct ppide *ppide;
static struct rtc *rtc;
static struct uart16x50 *uart;
static struct sram_mmu *mmu;

static unsigned acia_narrow;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_IRQ	4
#define TRACE_UNK	8
#define TRACE_RTC	16
#define TRACE_CPU	32
#define TRACE_ACIA	64
#define TRACE_UART	128
#define TRACE_PPIDE	256
#define TRACE_MMU	512

static int trace = 0;
static int irq_mask;

static void add_irq(int n)
{
	if (!(irq_mask & (1 << n)) && (trace & TRACE_IRQ))
		fprintf(stderr, "[IRQ %02X]\n", irq_mask);
	irq_mask |= (1 << n);
	m68k_set_irq(2);
}

static void remove_irq(int n)
{
	if (irq_mask & (1 << n)) {
		irq_mask &= ~(1 << n);
		if (!irq_mask) {
			m68k_set_irq(0);
			if (trace & TRACE_IRQ)
				fprintf(stderr, "[IRQ cleared]\n");
		}
	}
}

int cpu_irq_ack(int level)
{
	return M68K_INT_ACK_AUTOVECTOR;
}

/* We do this in the main loop so no helper needed */
void recalc_interrupts(void)
{
}

static int ide = 0;
struct ide_controller *ide0;

static uint8_t my_ide_read(uint16_t addr)
{
	return ide_read8(ide0, addr);
}

static void my_ide_write(uint16_t addr, uint8_t val)
{
	ide_write8(ide0, addr, val);
}

void uart16x50_signal_change(struct uart16x50 *uart, uint8_t mcr)
{
	/* Modem lines changed - don't care */
}

static uint8_t do_mmio_read_68000(uint16_t addr)
{
	addr &= 0xFF;
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow)
		return acia_read(acia, addr & 1);
	if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		return acia_read(acia, addr & 1);
	if ((addr >= 0x10 && addr <= 0x17) && ide)
		return my_ide_read(addr & 7);
	if ((addr >= 0x20 && addr <= 0x27) && ppide)
		return ppide_read(ppide, addr & 3);
	if (addr >= 0x28 && addr <= 0x2C && wiznet)
		return nic_w5100_read(wiz, addr & 3);
	if (addr == 0x0C && rtc)
		return rtc_read(rtc);
	if (addr >= 0xC0 && addr <= 0xCF && uart)
		return uart16x50_read(uart, addr & 0x0F);
	if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown read from port %04X\n", addr);
	return 0xFF;
}

uint8_t mmio_read_68000(uint16_t addr)
{
	uint8_t r;
	if (trace & TRACE_IO)
		fprintf(stderr, "read %04x <- ", addr);
	r = do_mmio_read_68000(addr);
	if (trace & TRACE_IO)
		fprintf(stderr, "%02x\n", r);
	return r;
}

void mmio_write_68000(uint16_t addr, uint8_t val)
{
	if (trace & TRACE_IO)
		fprintf(stderr, "write %04x <- %02x\n", addr, val);
	addr &= 0xFF;
	if ((addr >= 0x80 && addr <= 0x87) && acia && acia_narrow)
		acia_write(acia, addr & 1, val);
	if ((addr >= 0x80 && addr <= 0xBF) && acia && !acia_narrow)
		acia_write(acia, addr & 1, val);
	else if ((addr >= 0x10 && addr <= 0x17) && ide)
		my_ide_write(addr & 7, val);
	else if ((addr >= 0x20 && addr <= 0x27) && ppide)
		ppide_write(ppide, addr & 3, val);
	else if (addr >= 0x28 && addr <= 0x2C && wiznet)
		nic_w5100_write(wiz, addr & 3, val);
	else if (addr == 0x38)
		sram_mmu_set_latch(mmu, val);
	else if (addr == 0x0C && rtc)
		rtc_write(rtc, val);
	else if (addr >= 0xC0 && addr <= 0xCF && uart)
		uart16x50_write(uart, addr & 0x0F, val);
	else if (addr == 0x00) {
		printf("trace set to %d\n", val);
		trace = val;
#if 0
		if (trace & TRACE_CPU)
		else
#endif
	} else if (trace & TRACE_UNK)
		fprintf(stderr, "Unknown write to port %04X of %02X\n", addr, val);
}

uint8_t *bmmu_translate(unsigned int addr, int wflag, int silent)
{
	unsigned int berr;

	if (bmmu == 0)
		return ramrom + (addr & 0xFFFFF);
	if (!(addr & 0x80000)) {
		if (!kernelmode) {
			fprintf(stderr, "Low fault at 0x%06X\n", addr);
			return NULL;
		}
		/* ROM */
		if (wflag == 1 && addr < 0x10000)
			return NULL;
		/* Linear RAM */
		return ramrom + addr;
	}

	return sram_mmu_translate(mmu, addr & 0x7FFFF, wflag,
					kernelmode, silent, &berr);
	/* We don't emulate bus error yet */
}

unsigned int cpu_read_byte_dasm(unsigned int addr)
{
	uint8_t *ptr = bmmu_translate(addr, 0, 0);
	if (ptr)
		return *ptr;
	return 0xFF;
}

unsigned int cpu_read_word_dasm(unsigned int addr)
{
	return (cpu_read_byte_dasm(addr) << 8) | cpu_read_byte_dasm(addr + 1);
}

unsigned int cpu_read_long_dasm(unsigned int addr)
{
	return (cpu_read_word_dasm(addr) << 16) | cpu_read_word_dasm(addr + 2);
}

static unsigned int do_cpu_read_byte(unsigned int addr)
{
	uint8_t *ptr;
	addr &= 0xFFFFF;

	if ((addr & 0xF0000) == 0x10000)
		return mmio_read_68000(addr);

	ptr = bmmu_translate(addr, 0, 0);
	if (ptr)
		return *ptr;
	return 0xFF;
}

unsigned int cpu_read_byte(unsigned int addr)
{
	unsigned int r;

	if (trace & TRACE_MEM)
		fprintf(stderr, "R %06X = ", addr & 0xFFFFF);
	r = do_cpu_read_byte(addr);
	if (trace & TRACE_MEM)
		fprintf(stderr, "%02X\n", r);
	return r;
}

unsigned int cpu_read_word(unsigned int addr)
{
	return (cpu_read_byte(addr) << 8) | cpu_read_byte(addr + 1);
}

unsigned int cpu_read_long(unsigned int addr)
{
	return (cpu_read_word(addr) << 16) | cpu_read_word(addr + 2);
}

void cpu_write_byte(unsigned int addr, unsigned int value)
{
	uint8_t *ptr;

	addr &= 0xFFFFF;
	if (trace & TRACE_MEM)
		fprintf(stderr, "W %06X = %02X\n",
			addr & 0xFFFFF, value);
	if ((addr & 0xF0000) == 0x10000)
		mmio_write_68000(addr, value);
	else {
		ptr = bmmu_translate(addr, 1, 0);
		if (ptr)
			*ptr = value;
		else
			fprintf(stderr, "Write failed.\n");
	}
}

void cpu_write_word(unsigned int addr, unsigned int value)
{
	cpu_write_byte(addr, value >> 8);
	cpu_write_byte(addr + 1, value);
}

void cpu_write_long(unsigned int addr, unsigned int value)
{
	cpu_write_word(addr, value >> 16);
	cpu_write_word(addr + 2, value);
}

void cpu_write_pd(unsigned int addr, unsigned int value)
{
	cpu_write_word(addr + 2, value);
	cpu_write_word(addr, value >> 16);
}

void cpu_instr_callback(void)
{
	if (trace & TRACE_CPU) {
		char buf[128];
		unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC);
		m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
		fprintf(stderr, ">%06X %s\n", pc, buf);
	}
}

void cpu_pulse_reset(void)
{
}

void cpu_set_fc(int fc)
{
	kernelmode = (fc & 4);
}

void system_process(void)
{
	static int n = 0;
	static struct timespec tc;
	/* 5ms - it's a balance between nice behaviour and simulation
	   smoothness */
	tc.tv_sec = 0;
	tc.tv_nsec = 5000000L;
	if (acia)
		acia_timer(acia);
	if (uart)
		uart16x50_event(uart);
	if (n++ == 100) {
		n = 0;
		if (wiznet)
			w5100_process(wiz);
		/* Do 5ms of I/O and delays */
		if (!fast)
			nanosleep(&tc, NULL);
	}
	if (acia) {
		if (acia_irq_pending(acia))
			add_irq(IRQ_ACIA);
		else
			remove_irq(IRQ_ACIA);
	}
	if (uart) {
		if (uart16x50_irq_pending(uart))
			add_irq(IRQ_16550A);
		else
			remove_irq(IRQ_16550A);
	}
}

static struct termios saved_term, term;

static void cleanup(int sig)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
	exit(1);
}

static void exit_cleanup(void)
{
	tcsetattr(0, TCSADRAIN, &saved_term);
}

static void usage(void)
{
	fprintf(stderr, "rcbus-68008: [-1] [-A] [-a] [-b] [-f] [-R] [-r rompath] [-i disk] [-I disk] [-w] [-d debug]\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	int opt;
	int fd;
	int ppi = 0;
	char *rompath = "rcbus-68000.rom";
	char *idepath;
	int has_rtc = 0;
	int has_acia = 0;
	int has_16550a = 0;

	while ((opt = getopt(argc, argv, "1Aabd:fi:r:I:Rw")) != -1) {
		switch (opt) {
		case '1':
			has_16550a = 1;
			has_acia = 0;
			break;
		case 'a':
			has_acia = 1;
			acia_narrow = 0;
			has_16550a = 0;
			break;
		case 'A':
			has_acia = 1;
			acia_narrow = 1;
			has_16550a = 0;
			break;
		case 'r':
			rompath = optarg;
			break;
		case 'i':
			ide = 1;
			ppi = 0;
			idepath = optarg;
			break;
		case 'I':
			ppi = 1;
			ide = 0;
			idepath = optarg;
			break;
		case 'd':
			trace = atoi(optarg);
			break;
		case 'f':
			fast = 1;
			break;
		case 'R':
			has_rtc = 1;
			break;
		case 'w':
			wiznet = 1;
			break;
		case 'b':
			bmmu = 1;
			break;
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (has_acia == 0 && has_16550a == 0) {
		fprintf(stderr, "rcbus: no UART selected, defaulting to 16550A\n");
		has_16550a = 1;
	}

	fd = open(rompath, O_RDONLY);
	if (fd == -1) {
		perror(rompath);
		exit(EXIT_FAILURE);
	}
	if (read(fd, ramrom, 524288) != 524288) {
		fprintf(stderr, "rcbus: ROM image should be 512K.\n");
		exit(EXIT_FAILURE);
	}
	close(fd);

	if (ide) {
		ide0 = ide_allocate("cf");
		if (ide0) {
			int ide_fd = open(idepath, O_RDWR);
			if (ide_fd == -1) {
				perror(idepath);
				ide = 0;
			}
			if (ide_attach(ide0, 0, ide_fd) == 0) {
				ide = 1;
				ide_reset_begin(ide0);
			}
		}
			else ide = 0;
	} else if (ppi) {
		ppide = ppide_create("ppide");
		int ide_fd = open(idepath, O_RDWR);
		if (ide_fd == -1) {
			perror(idepath);
			ppide = 0;
		} else {
			ppide_attach(ppide, 0, ide_fd);
			ppide_reset(ppide);
		}
		if (trace & TRACE_PPIDE)
			ppide_trace(ppide, 1);
	}
	if (has_rtc) {
		rtc = rtc_create();
		rtc_trace(rtc, trace & TRACE_RTC);
	}
	if (has_16550a) {
		uart = uart16x50_create();
		uart16x50_attach(uart, &console);
		uart16x50_trace(uart, trace & TRACE_UART);
	}
	if (wiznet) {
		wiz = nic_w5100_alloc();
		nic_w5100_reset(wiz);
	}
	if (has_acia) {
		acia = acia_create();
		acia_attach(acia, &console);
		acia_trace(acia, trace & TRACE_ACIA);
	}
	if (bmmu) {
		mmu = sram_mmu_create();
		sram_mmu_trace(mmu, trace & TRACE_MMU);
	}
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
	m68k_init();
	/* Really should be 68008 */
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();
	while(1) {
		m68k_execute(tstate_steps);	/* 4MHz roughly right for 8MHz 68008 */
		system_process();
	}
}
