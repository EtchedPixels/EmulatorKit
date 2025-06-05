/* ============================== *
 * GDB Server Backend for RiscV32 *
 * ============================== */

#define GDB_BACKEND_RV32
#include "gdb-server.h"

/* quiet a compile error */
#define MINIRV32WARN(...)
#define MINIRV32_DECORATE

#include "riscv/mini-rv32ima.h"

#define MULTILINE_STRING(...) #__VA_ARGS__

/* target description xml */
static const char *rv32_target_description = MULTILINE_STRING(
	<?xml version="1.0"?>
	<!DOCTYPE target SYSTEM "gdb-target.dtd">
	<target version="1.0">
		<architecture>riscv:rv32</architecture>
		<osabi>none</osabi>
	</target>
);

/* get the address of the next instruction */
static unsigned long rv32_get_pc(void *vctx)
{
	struct gdb_context_rv32 *ctx = vctx;
	return ctx->cpu->pc;
}

/* set the address of the next instruction */
static void rv32_set_pc(void *vctx, unsigned long addr)
{
	struct gdb_context_rv32 *ctx = vctx;
	ctx->cpu->pc = addr;
}

/* GDB expects registers in this order */
enum rv32_regs {
	R_Z,	R_RA,	R_SP,	R_GP,
	R_TP,	R_T0,	R_T1,	R_T2,
	R_FP,	R_S1,	R_A0,	R_A1,
	R_A2,	R_A3,	R_A4,	R_A5,

	R_A6,	R_A7,	R_S2,	R_S3,
	R_S4,	R_S5,	R_S6,	R_S7,
	R_S8,	R_S9,	R_S10,	R_S11,
	R_T3,	R_T4,	R_T5,	R_T6,

	R_PC,

	R_REGISTER_MAX,
};

/* write a register value to the output buffer */
static void rv32_get_reg(void *vctx, struct gdb_server *gdb, unsigned int reg)
{
	struct gdb_context_rv32 *ctx = vctx;

	if (reg < 32) {
		gdb_writef(gdb, "%08x", gdb_swap_u32(ctx->cpu->regs[reg]));
	} else if (reg == R_PC) {
		gdb_writef(gdb, "%08x", gdb_swap_u32(ctx->cpu->pc));
	}
}

/* read a register value from the given packet */
static bool rv32_set_reg(void *vctx, struct gdb_packet *p, unsigned int reg)
{
	struct gdb_context_rv32 *ctx = vctx;
	unsigned int val;
	int end;

	if (!gdb_packet_scanf(p, &end, "%08x", &val)) {
		return false;
	}

	val = gdb_swap_u32(val);

	if (reg < 32) {
		ctx->cpu->regs[reg] = val;
	} else if (reg == R_PC) {
		ctx->cpu->pc = val;
	} else {
		return false;
	}

	return true;
}

/* read a byte of memory from addr */
static uint8_t rv32_read_mem(void *vctx, unsigned long addr)
{
	struct gdb_context_rv32 *ctx = vctx;
	uint32_t trap = 0;
	uint32_t rval;

	return ctx->read8(addr, &trap, &rval);
}

/* write a byte of memory to addr */
static void rv32_write_mem(void *vctx, unsigned long addr, uint8_t val)
{
	struct gdb_context_rv32 *ctx = vctx;
	uint32_t trap = 0;
	uint32_t rval;

	ctx->write8(addr, val, &trap, &rval);
}

/* construct the rv32 backend */
struct gdb_backend *gdb_backend_rv32(struct gdb_context_rv32 *ctx)
{
	struct gdb_backend *backend = calloc(1, sizeof(struct gdb_backend));
	if (!backend) {
		return NULL;
	}

	struct gdb_context_rv32 *ctx_copy = calloc(1, sizeof(*ctx));
	if (!ctx_copy) {
		free(backend);
		return NULL;
	}
	*ctx_copy = *ctx;

	backend->ctx = ctx_copy;
	backend->free = free;

	backend->target_description = rv32_target_description;

	backend->get_pc = rv32_get_pc;
	backend->set_pc = rv32_set_pc;

	backend->register_max = R_REGISTER_MAX;
	backend->get_reg = rv32_get_reg;
	backend->set_reg = rv32_set_reg;

	backend->read_mem = ctx->read8 ? rv32_read_mem : NULL;
	backend->write_mem = ctx->write8 ? rv32_write_mem : NULL;

	return backend;
}
