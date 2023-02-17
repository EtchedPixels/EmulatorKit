/* tmp1 value for ini/inir/outi/otir for [C.1-0][io.1-0] */
const UINT8 irep_tmp1[4][4] = {
	{0,0,1,0},{0,1,0,1},{1,0,1,1},{0,1,1,0}
};

/* tmp1 value for ind/indr/outd/otdr for [C.1-0][io.1-0] */
const UINT8 drep_tmp1[4][4] = {
	{0,1,0,0},{1,0,0,1},{0,0,1,0},{0,1,0,1}
};

/* tmp2 value for all in/out repeated opcodes for B.7-0 */
const UINT8 breg_tmp2[256] = {
	0,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,
	0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,
	1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1
};

const UINT8 cc_op[0x100] = {
/*-0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -a -b -c -d -e -f */
	3, 9, 7, 4, 4, 4, 6, 3, 4, 7, 6, 4, 4, 4, 6, 3,
	7, 9, 7, 4, 4, 4, 6, 3, 8, 7, 6, 4, 4, 4, 6, 3,
	6, 9,16, 4, 4, 4, 6, 4, 6, 7,15, 4, 4, 4, 6, 3,
	6, 9,13, 4,10,10, 9, 3, 6, 7,12, 4, 4, 4, 6, 3,
	4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,
	4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,
	4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,
	7, 7, 7, 7, 7, 7, 3, 7, 4, 4, 4, 4, 4, 4, 6, 4,
	4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,
	4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,
	4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,
	4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,
	5, 9, 6, 9, 6,11, 6,11, 5, 9, 6, 0, 6,16, 6,11,
	5, 9, 6,10, 6,11, 6,11, 5, 3, 6, 9, 6, 0, 6,11,
	5, 9, 6,16, 6,11, 6,11, 5, 3, 6, 3, 6, 0, 6,11,
	5, 9, 6, 3, 6,11, 6,11, 5, 4, 6, 3, 6, 0, 6,11
};

const UINT8 cc_cb[0x100] = {
/*-0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -a -b -c -d -e -f */
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7,
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7,
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7,
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7,
	6, 6, 6, 6, 6, 6, 9, 6, 6, 6, 6, 6, 6, 6, 9, 6,
	6, 6, 6, 6, 6, 6, 9, 6, 6, 6, 6, 6, 6, 6, 9, 6,
	6, 6, 6, 6, 6, 6, 9, 6, 6, 6, 6, 6, 6, 6, 9, 6,
	6, 6, 6, 6, 6, 6, 9, 6, 6, 6, 6, 6, 6, 6, 9, 6,
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7,
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7,
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7,
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7,
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7,
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7,
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7,
	7, 7, 7, 7, 7, 7,13, 7, 7, 7, 7, 7, 7, 7,13, 7
};

const UINT8 cc_ed[0x100] = {
/*-0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -a -b -c -d -e -f */
	12,13, 6, 6, 9, 6, 6, 6,12,13, 6, 6, 9, 6, 6, 6,
	12,13, 6, 6, 9, 6, 6, 6,12,13, 6, 6, 9, 6, 6, 6,
	12,13, 6, 6, 9, 6, 6, 6,12,13, 6, 6,10, 6, 6, 6,
	12,13, 6, 6, 9, 6, 6, 6,12,13, 6, 6, 9, 6, 6, 6,
	9,10,10,19, 6,12, 6, 6, 9,10,10,18,17,12, 6, 6,
	9,10,10,19, 6,12, 6, 6, 9,10,10,18,17,12, 6, 6,
	9,10,10,19, 6,12, 6,16, 9,10,10,18,17,12, 6,16,
	9,10,10,19,12,12, 8, 6, 9,10,10,18,17,12, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	12,12,12,12, 6, 6, 6, 6,12,12,12,12, 6, 6, 6, 6,
	12,12,12,12, 6, 6, 6, 6,12,12,12,12, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6
};

const UINT8 cc_xy[0x100] = {
/*-0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -a -b -c -d -e -f */
	4, 4, 4, 4, 4, 4, 4, 4, 4,10, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4,10, 4, 4, 4, 4, 4, 4,
	4,12,19, 7, 9, 9,15, 4, 4,10,18, 7, 9, 9, 9, 4,
	4, 4, 4, 4,18,18,15, 4, 4,10, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 9, 9,14, 4, 4, 4, 4, 4, 9, 9,14, 4,
	4, 4, 4, 4, 9, 9,14, 4, 4, 4, 4, 4, 9, 9,14, 4,
	9, 9, 9, 9, 9, 9,14, 9, 9, 9, 9, 9, 9, 9,14, 9,
	15,15,15,15,15,15, 4,15, 4, 4, 4, 4, 9, 9,14, 4,
	4, 4, 4, 4, 9, 9,14, 4, 4, 4, 4, 4, 9, 9,14, 4,
	4, 4, 4, 4, 9, 9,14, 4, 4, 4, 4, 4, 9, 9,14, 4,
	4, 4, 4, 4, 9, 9,14, 4, 4, 4, 4, 4, 9, 9,14, 4,
	4, 4, 4, 4, 9, 9,14, 4, 4, 4, 4, 4, 9, 9,14, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4,12, 4,19, 4,14, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4
};

const UINT8 cc_xycb[0x100] = {
/*-0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -a -b -c -d -e -f */
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19
};

const UINT8 cc_dded[0x100] = {
/*-0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -a -b -c -d -e -f */
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19
};

const UINT8 cc_fded[0x100] = {
/*-0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -a -b -c -d -e -f */
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19
};

/* extra cycles if jr/jp/call taken and 'interrupt latency' on rst 0-7 */
const UINT8 cc_ex[0x100] = {
/*-0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -a -b -c -d -e -f */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* DJNZ */
	2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0,   /* JR NZ/JR Z */
	2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0,   /* JR NC/JR C */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,10, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,10, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,10, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,10, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	4, 4, 4, 4, 0, 0, 0, 0, 4, 4, 4, 4, 0, 0, 0, 0,   /* LDIR/CPIR/INIR/OTIR LDDR/CPDR/INDR/OTDR */
	5, 0, 3, 0,10, 0, 0, 2, 5, 0, 3, 0,10, 0, 0, 2,
	5, 0, 3, 0,10, 0, 0, 2, 5, 0, 3, 0,10, 0, 0, 2,
	5, 0, 3, 0,10, 0, 0, 2, 5, 0, 3, 0,10, 0, 0, 2,
	5, 0, 3, 0,10, 0, 0, 2, 5, 0, 3, 0,10, 0, 0, 2
};

const UINT8 *const cc_default[8] = { cc_op, cc_cb, cc_ed, cc_xy, cc_xycb, cc_dded, cc_fded, cc_ex };

// TODO are timings for DD and FD the same?
#define Z280_TABLE_dd    Z280_TABLE_xy
#define Z280_TABLE_fd    Z280_TABLE_xy

int take_interrupt(struct z280_state *cpustate, int irq);

#define PROTOTYPES(tablename,prefix) \
	INLINE void prefix##_00(struct z280_state *cpustate); INLINE void prefix##_01(struct z280_state *cpustate); INLINE void prefix##_02(struct z280_state *cpustate); INLINE void prefix##_03(struct z280_state *cpustate); \
	INLINE void prefix##_04(struct z280_state *cpustate); INLINE void prefix##_05(struct z280_state *cpustate); INLINE void prefix##_06(struct z280_state *cpustate); INLINE void prefix##_07(struct z280_state *cpustate); \
	INLINE void prefix##_08(struct z280_state *cpustate); INLINE void prefix##_09(struct z280_state *cpustate); INLINE void prefix##_0a(struct z280_state *cpustate); INLINE void prefix##_0b(struct z280_state *cpustate); \
	INLINE void prefix##_0c(struct z280_state *cpustate); INLINE void prefix##_0d(struct z280_state *cpustate); INLINE void prefix##_0e(struct z280_state *cpustate); INLINE void prefix##_0f(struct z280_state *cpustate); \
	INLINE void prefix##_10(struct z280_state *cpustate); INLINE void prefix##_11(struct z280_state *cpustate); INLINE void prefix##_12(struct z280_state *cpustate); INLINE void prefix##_13(struct z280_state *cpustate); \
	INLINE void prefix##_14(struct z280_state *cpustate); INLINE void prefix##_15(struct z280_state *cpustate); INLINE void prefix##_16(struct z280_state *cpustate); INLINE void prefix##_17(struct z280_state *cpustate); \
	INLINE void prefix##_18(struct z280_state *cpustate); INLINE void prefix##_19(struct z280_state *cpustate); INLINE void prefix##_1a(struct z280_state *cpustate); INLINE void prefix##_1b(struct z280_state *cpustate); \
	INLINE void prefix##_1c(struct z280_state *cpustate); INLINE void prefix##_1d(struct z280_state *cpustate); INLINE void prefix##_1e(struct z280_state *cpustate); INLINE void prefix##_1f(struct z280_state *cpustate); \
	INLINE void prefix##_20(struct z280_state *cpustate); INLINE void prefix##_21(struct z280_state *cpustate); INLINE void prefix##_22(struct z280_state *cpustate); INLINE void prefix##_23(struct z280_state *cpustate); \
	INLINE void prefix##_24(struct z280_state *cpustate); INLINE void prefix##_25(struct z280_state *cpustate); INLINE void prefix##_26(struct z280_state *cpustate); INLINE void prefix##_27(struct z280_state *cpustate); \
	INLINE void prefix##_28(struct z280_state *cpustate); INLINE void prefix##_29(struct z280_state *cpustate); INLINE void prefix##_2a(struct z280_state *cpustate); INLINE void prefix##_2b(struct z280_state *cpustate); \
	INLINE void prefix##_2c(struct z280_state *cpustate); INLINE void prefix##_2d(struct z280_state *cpustate); INLINE void prefix##_2e(struct z280_state *cpustate); INLINE void prefix##_2f(struct z280_state *cpustate); \
	INLINE void prefix##_30(struct z280_state *cpustate); INLINE void prefix##_31(struct z280_state *cpustate); INLINE void prefix##_32(struct z280_state *cpustate); INLINE void prefix##_33(struct z280_state *cpustate); \
	INLINE void prefix##_34(struct z280_state *cpustate); INLINE void prefix##_35(struct z280_state *cpustate); INLINE void prefix##_36(struct z280_state *cpustate); INLINE void prefix##_37(struct z280_state *cpustate); \
	INLINE void prefix##_38(struct z280_state *cpustate); INLINE void prefix##_39(struct z280_state *cpustate); INLINE void prefix##_3a(struct z280_state *cpustate); INLINE void prefix##_3b(struct z280_state *cpustate); \
	INLINE void prefix##_3c(struct z280_state *cpustate); INLINE void prefix##_3d(struct z280_state *cpustate); INLINE void prefix##_3e(struct z280_state *cpustate); INLINE void prefix##_3f(struct z280_state *cpustate); \
	INLINE void prefix##_40(struct z280_state *cpustate); INLINE void prefix##_41(struct z280_state *cpustate); INLINE void prefix##_42(struct z280_state *cpustate); INLINE void prefix##_43(struct z280_state *cpustate); \
	INLINE void prefix##_44(struct z280_state *cpustate); INLINE void prefix##_45(struct z280_state *cpustate); INLINE void prefix##_46(struct z280_state *cpustate); INLINE void prefix##_47(struct z280_state *cpustate); \
	INLINE void prefix##_48(struct z280_state *cpustate); INLINE void prefix##_49(struct z280_state *cpustate); INLINE void prefix##_4a(struct z280_state *cpustate); INLINE void prefix##_4b(struct z280_state *cpustate); \
	INLINE void prefix##_4c(struct z280_state *cpustate); INLINE void prefix##_4d(struct z280_state *cpustate); INLINE void prefix##_4e(struct z280_state *cpustate); INLINE void prefix##_4f(struct z280_state *cpustate); \
	INLINE void prefix##_50(struct z280_state *cpustate); INLINE void prefix##_51(struct z280_state *cpustate); INLINE void prefix##_52(struct z280_state *cpustate); INLINE void prefix##_53(struct z280_state *cpustate); \
	INLINE void prefix##_54(struct z280_state *cpustate); INLINE void prefix##_55(struct z280_state *cpustate); INLINE void prefix##_56(struct z280_state *cpustate); INLINE void prefix##_57(struct z280_state *cpustate); \
	INLINE void prefix##_58(struct z280_state *cpustate); INLINE void prefix##_59(struct z280_state *cpustate); INLINE void prefix##_5a(struct z280_state *cpustate); INLINE void prefix##_5b(struct z280_state *cpustate); \
	INLINE void prefix##_5c(struct z280_state *cpustate); INLINE void prefix##_5d(struct z280_state *cpustate); INLINE void prefix##_5e(struct z280_state *cpustate); INLINE void prefix##_5f(struct z280_state *cpustate); \
	INLINE void prefix##_60(struct z280_state *cpustate); INLINE void prefix##_61(struct z280_state *cpustate); INLINE void prefix##_62(struct z280_state *cpustate); INLINE void prefix##_63(struct z280_state *cpustate); \
	INLINE void prefix##_64(struct z280_state *cpustate); INLINE void prefix##_65(struct z280_state *cpustate); INLINE void prefix##_66(struct z280_state *cpustate); INLINE void prefix##_67(struct z280_state *cpustate); \
	INLINE void prefix##_68(struct z280_state *cpustate); INLINE void prefix##_69(struct z280_state *cpustate); INLINE void prefix##_6a(struct z280_state *cpustate); INLINE void prefix##_6b(struct z280_state *cpustate); \
	INLINE void prefix##_6c(struct z280_state *cpustate); INLINE void prefix##_6d(struct z280_state *cpustate); INLINE void prefix##_6e(struct z280_state *cpustate); INLINE void prefix##_6f(struct z280_state *cpustate); \
	INLINE void prefix##_70(struct z280_state *cpustate); INLINE void prefix##_71(struct z280_state *cpustate); INLINE void prefix##_72(struct z280_state *cpustate); INLINE void prefix##_73(struct z280_state *cpustate); \
	INLINE void prefix##_74(struct z280_state *cpustate); INLINE void prefix##_75(struct z280_state *cpustate); INLINE void prefix##_76(struct z280_state *cpustate); INLINE void prefix##_77(struct z280_state *cpustate); \
	INLINE void prefix##_78(struct z280_state *cpustate); INLINE void prefix##_79(struct z280_state *cpustate); INLINE void prefix##_7a(struct z280_state *cpustate); INLINE void prefix##_7b(struct z280_state *cpustate); \
	INLINE void prefix##_7c(struct z280_state *cpustate); INLINE void prefix##_7d(struct z280_state *cpustate); INLINE void prefix##_7e(struct z280_state *cpustate); INLINE void prefix##_7f(struct z280_state *cpustate); \
	INLINE void prefix##_80(struct z280_state *cpustate); INLINE void prefix##_81(struct z280_state *cpustate); INLINE void prefix##_82(struct z280_state *cpustate); INLINE void prefix##_83(struct z280_state *cpustate); \
	INLINE void prefix##_84(struct z280_state *cpustate); INLINE void prefix##_85(struct z280_state *cpustate); INLINE void prefix##_86(struct z280_state *cpustate); INLINE void prefix##_87(struct z280_state *cpustate); \
	INLINE void prefix##_88(struct z280_state *cpustate); INLINE void prefix##_89(struct z280_state *cpustate); INLINE void prefix##_8a(struct z280_state *cpustate); INLINE void prefix##_8b(struct z280_state *cpustate); \
	INLINE void prefix##_8c(struct z280_state *cpustate); INLINE void prefix##_8d(struct z280_state *cpustate); INLINE void prefix##_8e(struct z280_state *cpustate); INLINE void prefix##_8f(struct z280_state *cpustate); \
	INLINE void prefix##_90(struct z280_state *cpustate); INLINE void prefix##_91(struct z280_state *cpustate); INLINE void prefix##_92(struct z280_state *cpustate); INLINE void prefix##_93(struct z280_state *cpustate); \
	INLINE void prefix##_94(struct z280_state *cpustate); INLINE void prefix##_95(struct z280_state *cpustate); INLINE void prefix##_96(struct z280_state *cpustate); INLINE void prefix##_97(struct z280_state *cpustate); \
	INLINE void prefix##_98(struct z280_state *cpustate); INLINE void prefix##_99(struct z280_state *cpustate); INLINE void prefix##_9a(struct z280_state *cpustate); INLINE void prefix##_9b(struct z280_state *cpustate); \
	INLINE void prefix##_9c(struct z280_state *cpustate); INLINE void prefix##_9d(struct z280_state *cpustate); INLINE void prefix##_9e(struct z280_state *cpustate); INLINE void prefix##_9f(struct z280_state *cpustate); \
	INLINE void prefix##_a0(struct z280_state *cpustate); INLINE void prefix##_a1(struct z280_state *cpustate); INLINE void prefix##_a2(struct z280_state *cpustate); INLINE void prefix##_a3(struct z280_state *cpustate); \
	INLINE void prefix##_a4(struct z280_state *cpustate); INLINE void prefix##_a5(struct z280_state *cpustate); INLINE void prefix##_a6(struct z280_state *cpustate); INLINE void prefix##_a7(struct z280_state *cpustate); \
	INLINE void prefix##_a8(struct z280_state *cpustate); INLINE void prefix##_a9(struct z280_state *cpustate); INLINE void prefix##_aa(struct z280_state *cpustate); INLINE void prefix##_ab(struct z280_state *cpustate); \
	INLINE void prefix##_ac(struct z280_state *cpustate); INLINE void prefix##_ad(struct z280_state *cpustate); INLINE void prefix##_ae(struct z280_state *cpustate); INLINE void prefix##_af(struct z280_state *cpustate); \
	INLINE void prefix##_b0(struct z280_state *cpustate); INLINE void prefix##_b1(struct z280_state *cpustate); INLINE void prefix##_b2(struct z280_state *cpustate); INLINE void prefix##_b3(struct z280_state *cpustate); \
	INLINE void prefix##_b4(struct z280_state *cpustate); INLINE void prefix##_b5(struct z280_state *cpustate); INLINE void prefix##_b6(struct z280_state *cpustate); INLINE void prefix##_b7(struct z280_state *cpustate); \
	INLINE void prefix##_b8(struct z280_state *cpustate); INLINE void prefix##_b9(struct z280_state *cpustate); INLINE void prefix##_ba(struct z280_state *cpustate); INLINE void prefix##_bb(struct z280_state *cpustate); \
	INLINE void prefix##_bc(struct z280_state *cpustate); INLINE void prefix##_bd(struct z280_state *cpustate); INLINE void prefix##_be(struct z280_state *cpustate); INLINE void prefix##_bf(struct z280_state *cpustate); \
	INLINE void prefix##_c0(struct z280_state *cpustate); INLINE void prefix##_c1(struct z280_state *cpustate); INLINE void prefix##_c2(struct z280_state *cpustate); INLINE void prefix##_c3(struct z280_state *cpustate); \
	INLINE void prefix##_c4(struct z280_state *cpustate); INLINE void prefix##_c5(struct z280_state *cpustate); INLINE void prefix##_c6(struct z280_state *cpustate); INLINE void prefix##_c7(struct z280_state *cpustate); \
	INLINE void prefix##_c8(struct z280_state *cpustate); INLINE void prefix##_c9(struct z280_state *cpustate); INLINE void prefix##_ca(struct z280_state *cpustate); INLINE void prefix##_cb(struct z280_state *cpustate); \
	INLINE void prefix##_cc(struct z280_state *cpustate); INLINE void prefix##_cd(struct z280_state *cpustate); INLINE void prefix##_ce(struct z280_state *cpustate); INLINE void prefix##_cf(struct z280_state *cpustate); \
	INLINE void prefix##_d0(struct z280_state *cpustate); INLINE void prefix##_d1(struct z280_state *cpustate); INLINE void prefix##_d2(struct z280_state *cpustate); INLINE void prefix##_d3(struct z280_state *cpustate); \
	INLINE void prefix##_d4(struct z280_state *cpustate); INLINE void prefix##_d5(struct z280_state *cpustate); INLINE void prefix##_d6(struct z280_state *cpustate); INLINE void prefix##_d7(struct z280_state *cpustate); \
	INLINE void prefix##_d8(struct z280_state *cpustate); INLINE void prefix##_d9(struct z280_state *cpustate); INLINE void prefix##_da(struct z280_state *cpustate); INLINE void prefix##_db(struct z280_state *cpustate); \
	INLINE void prefix##_dc(struct z280_state *cpustate); INLINE void prefix##_dd(struct z280_state *cpustate); INLINE void prefix##_de(struct z280_state *cpustate); INLINE void prefix##_df(struct z280_state *cpustate); \
	INLINE void prefix##_e0(struct z280_state *cpustate); INLINE void prefix##_e1(struct z280_state *cpustate); INLINE void prefix##_e2(struct z280_state *cpustate); INLINE void prefix##_e3(struct z280_state *cpustate); \
	INLINE void prefix##_e4(struct z280_state *cpustate); INLINE void prefix##_e5(struct z280_state *cpustate); INLINE void prefix##_e6(struct z280_state *cpustate); INLINE void prefix##_e7(struct z280_state *cpustate); \
	INLINE void prefix##_e8(struct z280_state *cpustate); INLINE void prefix##_e9(struct z280_state *cpustate); INLINE void prefix##_ea(struct z280_state *cpustate); INLINE void prefix##_eb(struct z280_state *cpustate); \
	INLINE void prefix##_ec(struct z280_state *cpustate); INLINE void prefix##_ed(struct z280_state *cpustate); INLINE void prefix##_ee(struct z280_state *cpustate); INLINE void prefix##_ef(struct z280_state *cpustate); \
	INLINE void prefix##_f0(struct z280_state *cpustate); INLINE void prefix##_f1(struct z280_state *cpustate); INLINE void prefix##_f2(struct z280_state *cpustate); INLINE void prefix##_f3(struct z280_state *cpustate); \
	INLINE void prefix##_f4(struct z280_state *cpustate); INLINE void prefix##_f5(struct z280_state *cpustate); INLINE void prefix##_f6(struct z280_state *cpustate); INLINE void prefix##_f7(struct z280_state *cpustate); \
	INLINE void prefix##_f8(struct z280_state *cpustate); INLINE void prefix##_f9(struct z280_state *cpustate); INLINE void prefix##_fa(struct z280_state *cpustate); INLINE void prefix##_fb(struct z280_state *cpustate); \
	INLINE void prefix##_fc(struct z280_state *cpustate); INLINE void prefix##_fd(struct z280_state *cpustate); INLINE void prefix##_fe(struct z280_state *cpustate); INLINE void prefix##_ff(struct z280_state *cpustate);

#define TABLE(prefix) {\
	prefix##_00,prefix##_01,prefix##_02,prefix##_03,prefix##_04,prefix##_05,prefix##_06,prefix##_07, \
	prefix##_08,prefix##_09,prefix##_0a,prefix##_0b,prefix##_0c,prefix##_0d,prefix##_0e,prefix##_0f, \
	prefix##_10,prefix##_11,prefix##_12,prefix##_13,prefix##_14,prefix##_15,prefix##_16,prefix##_17, \
	prefix##_18,prefix##_19,prefix##_1a,prefix##_1b,prefix##_1c,prefix##_1d,prefix##_1e,prefix##_1f, \
	prefix##_20,prefix##_21,prefix##_22,prefix##_23,prefix##_24,prefix##_25,prefix##_26,prefix##_27, \
	prefix##_28,prefix##_29,prefix##_2a,prefix##_2b,prefix##_2c,prefix##_2d,prefix##_2e,prefix##_2f, \
	prefix##_30,prefix##_31,prefix##_32,prefix##_33,prefix##_34,prefix##_35,prefix##_36,prefix##_37, \
	prefix##_38,prefix##_39,prefix##_3a,prefix##_3b,prefix##_3c,prefix##_3d,prefix##_3e,prefix##_3f, \
	prefix##_40,prefix##_41,prefix##_42,prefix##_43,prefix##_44,prefix##_45,prefix##_46,prefix##_47, \
	prefix##_48,prefix##_49,prefix##_4a,prefix##_4b,prefix##_4c,prefix##_4d,prefix##_4e,prefix##_4f, \
	prefix##_50,prefix##_51,prefix##_52,prefix##_53,prefix##_54,prefix##_55,prefix##_56,prefix##_57, \
	prefix##_58,prefix##_59,prefix##_5a,prefix##_5b,prefix##_5c,prefix##_5d,prefix##_5e,prefix##_5f, \
	prefix##_60,prefix##_61,prefix##_62,prefix##_63,prefix##_64,prefix##_65,prefix##_66,prefix##_67, \
	prefix##_68,prefix##_69,prefix##_6a,prefix##_6b,prefix##_6c,prefix##_6d,prefix##_6e,prefix##_6f, \
	prefix##_70,prefix##_71,prefix##_72,prefix##_73,prefix##_74,prefix##_75,prefix##_76,prefix##_77, \
	prefix##_78,prefix##_79,prefix##_7a,prefix##_7b,prefix##_7c,prefix##_7d,prefix##_7e,prefix##_7f, \
	prefix##_80,prefix##_81,prefix##_82,prefix##_83,prefix##_84,prefix##_85,prefix##_86,prefix##_87, \
	prefix##_88,prefix##_89,prefix##_8a,prefix##_8b,prefix##_8c,prefix##_8d,prefix##_8e,prefix##_8f, \
	prefix##_90,prefix##_91,prefix##_92,prefix##_93,prefix##_94,prefix##_95,prefix##_96,prefix##_97, \
	prefix##_98,prefix##_99,prefix##_9a,prefix##_9b,prefix##_9c,prefix##_9d,prefix##_9e,prefix##_9f, \
	prefix##_a0,prefix##_a1,prefix##_a2,prefix##_a3,prefix##_a4,prefix##_a5,prefix##_a6,prefix##_a7, \
	prefix##_a8,prefix##_a9,prefix##_aa,prefix##_ab,prefix##_ac,prefix##_ad,prefix##_ae,prefix##_af, \
	prefix##_b0,prefix##_b1,prefix##_b2,prefix##_b3,prefix##_b4,prefix##_b5,prefix##_b6,prefix##_b7, \
	prefix##_b8,prefix##_b9,prefix##_ba,prefix##_bb,prefix##_bc,prefix##_bd,prefix##_be,prefix##_bf, \
	prefix##_c0,prefix##_c1,prefix##_c2,prefix##_c3,prefix##_c4,prefix##_c5,prefix##_c6,prefix##_c7, \
	prefix##_c8,prefix##_c9,prefix##_ca,prefix##_cb,prefix##_cc,prefix##_cd,prefix##_ce,prefix##_cf, \
	prefix##_d0,prefix##_d1,prefix##_d2,prefix##_d3,prefix##_d4,prefix##_d5,prefix##_d6,prefix##_d7, \
	prefix##_d8,prefix##_d9,prefix##_da,prefix##_db,prefix##_dc,prefix##_dd,prefix##_de,prefix##_df, \
	prefix##_e0,prefix##_e1,prefix##_e2,prefix##_e3,prefix##_e4,prefix##_e5,prefix##_e6,prefix##_e7, \
	prefix##_e8,prefix##_e9,prefix##_ea,prefix##_eb,prefix##_ec,prefix##_ed,prefix##_ee,prefix##_ef, \
	prefix##_f0,prefix##_f1,prefix##_f2,prefix##_f3,prefix##_f4,prefix##_f5,prefix##_f6,prefix##_f7, \
	prefix##_f8,prefix##_f9,prefix##_fa,prefix##_fb,prefix##_fc,prefix##_fd,prefix##_fe,prefix##_ff  \
}

PROTOTYPES(Z280op,op)
PROTOTYPES(Z280cb,cb)
PROTOTYPES(Z280dd,dd)
PROTOTYPES(Z280ed,ed)
PROTOTYPES(Z280fd,fd)
PROTOTYPES(Z280xycb,xycb)
PROTOTYPES(Z280dded,dded)
PROTOTYPES(Z280fded,fded)

void (*const Z280ops[Z280_PREFIX_COUNT][0x100])(struct z280_state *cpustate) =
{
	TABLE(op),
	TABLE(cb),
	TABLE(dd),
	TABLE(ed),
	TABLE(fd),
	TABLE(xycb),
	TABLE(dded),
	TABLE(fded)
};

/***************************************************************
 * define an opcode function
 ***************************************************************/
#define OP(prefix,opcode)  INLINE void prefix##_##opcode(struct z280_state *cpustate)

/***************************************************************
 * adjust cycle count by n T-states
 ***************************************************************/
#define CC(prefix,opcode) cpustate->extra_cycles += cpustate->cc[Z280_TABLE_##prefix][opcode]

/***************************************************************
 * execute an opcode
 ***************************************************************/

#define EXEC_PROTOTYPE(prefix) \
INLINE int exec##_##prefix(struct z280_state *cpustate, const UINT8 opcode)    \
{                                                                       \
	(*Z280ops[Z280_PREFIX_##prefix][opcode])(cpustate);                                     \
	return cpustate->cc[Z280_TABLE_##prefix][opcode];                   \
}

EXEC_PROTOTYPE(op)
EXEC_PROTOTYPE(cb)
EXEC_PROTOTYPE(dd)
EXEC_PROTOTYPE(ed)
EXEC_PROTOTYPE(fd)
EXEC_PROTOTYPE(xycb)
EXEC_PROTOTYPE(dded)
EXEC_PROTOTYPE(fded)
