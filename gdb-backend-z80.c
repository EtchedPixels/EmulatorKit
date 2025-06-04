/* ========================== *
 * GDB Server Backend for Z80 *
 * ========================== */

#define GDB_BACKEND_Z80
#include "gdb-server.h"

#include "libz80/z80.h"

#define MULTILINE_STRING(...) #__VA_ARGS__

/* target description xml */
static const char *z80_target_description = MULTILINE_STRING(
	<?xml version="1.0"?>
	<!DOCTYPE target SYSTEM "gdb-target.dtd">
	<target version="1.0">
		<architecture>z80</architecture>
		<osabi>none</osabi>
	</target>
);

/* get the address of the next instruction */
static unsigned long z80_get_pc(void *vctx)
{
	Z80Context *ctx = vctx;
	return ctx->PC;
}

/* set the address of the next instruction */
static void z80_set_pc(void *vctx, unsigned long addr)
{
	Z80Context *ctx = vctx;
	ctx->PC = addr;
}

/* GDB expects registers in this order */
enum z80_regs {
	R_AF,	R_BC,	R_DE,	R_HL,
	R_SP,	R_PC,	R_IX,	R_IY,
	R_AFP,	R_BCP,	R_DEP,	R_HLP,

	R_IR,

	R_REGISTER_MAX,
};

/* write a register value to the output buffer */
static void z80_get_reg(void *vctx, struct gdb_server *gdb, unsigned int reg)
{
	Z80Context *ctx = vctx;
	uint16_t val;

	switch (reg) {
	case R_AF:
		val = ctx->R1.wr.AF;
		break;
	case R_BC:
		val = ctx->R1.wr.BC;
		break;
	case R_DE:
		val = ctx->R1.wr.DE;
		break;
	case R_HL:
		val = ctx->R1.wr.HL;
		break;

	case R_SP:
		val = ctx->R1.wr.SP;
		break;
	case R_PC:
		val = ctx->PC;
		break;
	case R_IX:
		val = ctx->R1.wr.IX;
		break;
	case R_IY:
		val = ctx->R1.wr.IY;
		break;

	case R_AFP:
		val = ctx->R2.wr.AF;
		break;
	case R_BCP:
		val = ctx->R2.wr.BC;
		break;
	case R_DEP:
		val = ctx->R2.wr.DE;
		break;
	case R_HLP:
		val = ctx->R2.wr.HL;
		break;

	case R_IR:
		/* special */
		gdb_writef(gdb, "%02x", ctx->R);
		gdb_writef(gdb, "%02x", ctx->I);
		return;
	}

	gdb_writef(gdb, "%04x", gdb_swap_u16(val));
}

/* read a register value from the given packet */
static bool z80_set_reg(void *vctx, struct gdb_packet *p, unsigned int reg)
{
	Z80Context *ctx = vctx;
	uint16_t *dest = NULL;
	int end;

	switch (reg) {
	case R_AF:
		dest = &(ctx->R1.wr.AF);
		break;
	case R_BC:
		dest = &(ctx->R1.wr.BC);
		break;
	case R_DE:
		dest = &(ctx->R1.wr.DE);
		break;
	case R_HL:
		dest = &(ctx->R1.wr.HL);
		break;

	case R_SP:
		dest = &(ctx->R1.wr.SP);
		break;
	case R_PC:
		dest = &(ctx->PC);
		break;
	case R_IX:
		dest = &(ctx->R1.wr.IX);
		break;
	case R_IY:
		dest = &(ctx->R1.wr.IY);
		break;

	case R_AFP:
		dest = &(ctx->R2.wr.AF);
		break;
	case R_BCP:
		dest = &(ctx->R2.wr.BC);
		break;
	case R_DEP:
		dest = &(ctx->R2.wr.DE);
		break;
	case R_HLP:
		dest = &(ctx->R2.wr.HL);
		break;

	case R_IR:
		/* special */
		return gdb_packet_scanf(p, &end, "%02hhx", &(ctx->R)) &&
			gdb_packet_scanf(p, &end, "%02hhx", &(ctx->I));
	}

	if (dest) {
		if (!gdb_packet_scanf(p, &end, "%04hx", dest)) {
			return false;
		}
		*dest = gdb_swap_u16(*dest);
		return true;
	}

	return false;
}

/* read a byte of memory from addr */
static uint8_t z80_read_mem(void *vctx, unsigned long addr)
{
	Z80Context *ctx = vctx;
	return ctx->memRead(ctx->memParam, addr);
}

/* write a byte of memory to addr */
static void z80_write_mem(void *vctx, unsigned long addr, uint8_t val)
{
	Z80Context *ctx = vctx;
	ctx->memWrite(ctx->memParam, addr, val);
}

/* construct the z80 backend */
struct gdb_backend *gdb_backend_z80(Z80Context *ctx)
{
	struct gdb_backend *backend = calloc(1, sizeof(struct gdb_backend));
	if (!backend) {
		return NULL;
	}

	backend->ctx = ctx;

	backend->target_description = z80_target_description;

	backend->get_pc = z80_get_pc;
	backend->set_pc = z80_set_pc;

	backend->register_max = R_REGISTER_MAX;
	backend->get_reg = z80_get_reg;
	backend->set_reg = z80_set_reg;

	backend->read_mem = z80_read_mem;
	backend->write_mem = z80_write_mem;

	return backend;
}
