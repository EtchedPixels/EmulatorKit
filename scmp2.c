/*
 *	A minimal Ins8060 machine
 */

/* * HOW TO RUN: ====
 *
 * 1) Get NIBL BASIC ROM: NIBL.bin
 *     URL https://github.com/iruka-/SCMP2Emulator/tree/main/scmp2sim/
 *    or
 *     URL https://github.com/ekuester/SCMP-INS8060-NIBL-FloatingPoint-TinyBASIC-Interpreter/tree/main/EMULATOR
 *
 * 2) ./build
 *     $ make scmp2
 *
 * 3) ./run
 *     $ ./scmp2
 *
 * . Have Fun!
 *
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
#include <sys/select.h>
#include "ns806x.h"


static uint8_t ramrom[65536];
static uint8_t fast;
static volatile unsigned done;
static unsigned trace;
struct ns8060 *cpu;

#define TRACE_MEM	1
#define TRACE_IO	2
#define TRACE_CPU	4

int check_chario(void)
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

uint8_t ns8060_read_op(struct ns8060 *cpu, uint16_t addr, int debug)
{
	return ramrom[addr];
}

uint8_t mem_debug_read(struct ns8060 *cpu, uint16_t addr)
{
	return ns8060_read_op(cpu, addr, 1);
}

uint8_t mem_read(struct ns8060 *cpu, uint16_t addr)
{
	return ns8060_read_op(cpu, addr, 0);
}

void mem_write(struct ns8060 *cpu, uint16_t addr, uint8_t val)
{
#if 1 // ROM Modify Check.
	/* BASIC and spare ROM */
	if (addr < 0x1000) {
		printf("mem_write(%04x,%02x) %x\n",addr,val,ramrom[addr]);
		printf("WRITE VIOLATION!\n");
		exit(1);
		return;
	}
#endif
	ramrom[addr] = val;
}

void flag_change(struct ns8060 *cpu, uint8_t fbits)
{
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

void terminal_init(void)
{
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
}

static void usage(void)
{
	fprintf(stderr,
		"scmp2: [-f] [-r rompath] [-d debug]\n");
	exit(EXIT_FAILURE);
}


/*----------------------------------------------------------------------------
   Memory Dump
  ----------------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
/*	static struct timespec tc; */
	int opt;
	int fd;
	int rom = 1;
	char *rompath = "NIBL.bin";
/*	unsigned int cycles = 0; */

	while ((opt = getopt(argc, argv, "d:fi:r:")) != -1) {
		switch (opt) {
		case 'r':
			rompath = optarg;
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

	if (rom) {
		fd = open(rompath, O_RDONLY);
		if (fd == -1) {
			perror(rompath);
			exit(EXIT_FAILURE);
		}
		int n;
		if ( (n=read(fd, ramrom, 0x2000)) < 1) {
			fprintf(stderr, "nybbles: short rom '%s'.\n",
				rompath);
			exit(EXIT_FAILURE);
		}
		close(fd);
	}

	/* Patch in our I/O hooks */
	cpu = ns8060_create();  // ramrom);
	ns8060_reset(cpu);
	ns8060_trace(cpu, trace & TRACE_CPU);
	ns8060_set_b(cpu, 1);

	/* 5ms - it's a balance between nice behaviour and simulation
	   smoothness */
/*	tc.tv_sec = 0;
	tc.tv_nsec = 5000000L; */

//	terminal_init();

	/* This is the wrong way to do it but it's easier for the moment. We
	   should track how much real time has occurred and try to keep cycle
	   matched with that. The scheme here works fine except when the host
	   is loaded though */

	while (!done) {
		/* TODO: timing, ints etc */
		ns8060_execute_one(cpu);
#if 0
		static uint8_t pendc;			/* Pending char */
		if (check_chario() & 1) {
			pendc  = next_char();
			/* The TinyBASIC expects break type conditions
			   that persist for a while - this is a fudge */
			if (pendc == 3)
				ns8060_set_a(cpu, 0);
			else
				ns8060_set_a(cpu, 1);
		}
#endif
	}
	exit(0);
}

void ser_output(struct ns8060 *cpu, uint8_t bit)
{
	/* TODO: emulate bitbang serial */
}
uint8_t ser_input(struct ns8060 *cpu)
{
	/* TODO: emulate bitbang serial */
	return 0;
}

int  ns8060_emu_getch(void)
{
	int c = getchar();
	if( (c>='a')&&(c<='z') ) c=c-0x20;
	if( c == 0x0a ) c=0x0d;

/*	LogPrint("\nGETC:%02x:%c\n",c,c & 0x7f); */
	return c;
}
void ns8060_emu_putch(int ch)
{
	putchar(ch & 0x7f);
/*	printf("PUTC:%02x:%c\n",ch,ch & 0x7f);
	LogPrint("\nPUTC:%02x:%c\n",ch,ch & 0x7f); */
}
