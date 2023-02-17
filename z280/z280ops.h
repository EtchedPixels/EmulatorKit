/***************************************************************
 * Enter HALT state; write 1 to fake port on first execution
 ***************************************************************/
#define ENTER_HALT(cs) {                                        \
	(cs)->_PC--;                                                \
	(cs)->HALT = 1;                                             \
}

/***************************************************************
 * Leave HALT state; write 0 to fake port
 ***************************************************************/
#define LEAVE_HALT(cs) {                                        \
	if( (cs)->HALT )                                            \
	{                                                           \
		(cs)->HALT = 0;                                         \
		(cs)->_PC++;                                            \
	}                                                           \
}

/***************************************************************
 * Privileged instruction check
 ***************************************************************/
#define CHECK_PRIV(cs)                                        \
if (is_user(cs))                                              \
{                                                             \
	take_trap(cs, Z280_TRAP_PRIV);			                  \
}															  \
else														  

#define CHECK_PRIV_IO(cs)                                     \
if (is_user(cs) && (cs)->cr[Z280_TCR]&Z280_TCR_I)             \
{                                                             \
	take_trap(cs, Z280_TRAP_PRIV);			                  \
}															  \
else														  

#define CHECK_EPU(cs,t)                                       \
if (!((cs)->cr[Z280_TCR]&Z280_TCR_E))                         \
{                                                             \
	take_trap(cs, t);			                              \
}															  \
else														  

#define CHECK_SSO(cs)                                         \
if ((cs)->cr[Z280_TCR]&Z280_TCR_S &&                          \
  (((cs)->_SSP) & 0xfff0) == SSLR(cs) )                       \
{                                                             \
	take_trap(cs, Z280_TRAP_SSO);			                  \
}															  \


/***************************************************************
 * Internal IO select
 ***************************************************************/
#define is_internal_io(cs,port) ( \
    ((cs)->cr[Z280_IOP] == Z280_UARTIOP && (port & Z280_UARTMASK) == Z280_UARTBASE)|| \
    ((cs)->cr[Z280_IOP] == Z280_CTIOP && (port & Z280_CTMASK) == Z280_CTBASE)|| \
	((cs)->cr[Z280_IOP] == Z280_DMAIOP && (port & Z280_DMAMASK) == Z280_DMABASE)|| \
	((cs)->cr[Z280_IOP] == Z280_MMUIOP && (port & Z280_MMUMASK) == Z280_MMUBASE)|| \
	((cs)->cr[Z280_IOP] == Z280_RRRIOP && (port & Z280_RRRMASK) == Z280_RRR) )

/***************************************************************
 * Input a byte from given I/O port
 ***************************************************************/
#define IN(cs,port)                                             \
	is_internal_io(cs,port)?  \
		z280_readio_byte(cs, ((cs)->cr[Z280_IOP]<<16)|port) : (cs)->iospace->read_byte(((cs)->cr[Z280_IOP]<<16)|port)

/***************************************************************
 * Output a byte to given I/O port
 ***************************************************************/
#define OUT(cs,port,value)                                      \
	if (is_internal_io(cs,port))  \
		z280_writeio_byte(cs,((cs)->cr[Z280_IOP]<<16)|port,value);                       \
	else (cs)->iospace->write_byte(((cs)->cr[Z280_IOP]<<16)|port,value)

/***************************************************************
 * Input a word from given I/O port
 ***************************************************************/
#define IN16(cs,port)                                             \
	is_internal_io(cs,port)?  \
		z280_readio_word(cs, ((cs)->cr[Z280_IOP]<<16)|port) : (cs)->iospace->read_word(((cs)->cr[Z280_IOP]<<16)|port)

/***************************************************************
 * Output a word to given I/O port
 ***************************************************************/
#define OUT16(cs,port,value)                                      \
	if (is_internal_io(cs,port))  \
		z280_writeio_word(cs,((cs)->cr[Z280_IOP]<<16)|port,value);                       \
	else (cs)->iospace->write_word(((cs)->cr[Z280_IOP]<<16)|port,value)

/***************************************************************
 * MMU calculate the memory management lookup table
 ***************************************************************/
INLINE void z280_mmu(struct z280_state *cpustate)
{

}

// translate separate program/data
INLINE offs_t mmu_translate_separate(struct z280_state *cpustate, offs_t addr, int mode, int program) {
	offs_t offset, pfa;
	offset = addr & 0x1fff; // low 13b is offset
	// PC-relative addressing uses high 8 (program) PDRs; other use low 8 PDRs
	int index = ((addr >> 13) & 7) | (program<<3); // high 3b is index
	if (!mode) index += 16; // system (mode=0) -> pdrs 16-31
	cpustate->eapdr = index;
	// extract page frame from the PDR
	pfa = (offs_t)(cpustate->pdr[index] & (Z280_PDR_PFA &~0x0010) ) << 8; // LSB of PFA is set to 0
#ifdef MMU_DEBUG
	LOG("mmu_translate_sep index=%d,pdr=%04X,offs=%04X,pfa=%06X\n", index, cpustate->pdr[index], offset, pfa);
#endif
	return pfa|offset;
}

// translate nonseparate
INLINE offs_t mmu_translate_nonseparate(struct z280_state *cpustate, offs_t addr, int mode) {
	offs_t offset, pfa;
	offset = addr & 0xfff; // low 12bits
	int index = (addr >> 12) & 0xf; // high 4bits = index
	if (!mode) index += 16; // system (mode=0) -> pdrs 16-31
	cpustate->eapdr = index;
	// extract page frame from the PDR
	pfa = (offs_t)(cpustate->pdr[index] & Z280_PDR_PFA) << 8;
#ifdef MMU_DEBUG
	LOG("mmu_translate_nonsep index=%d,pdr=%04X,offs=%04X,pfa=%06X\n", index, cpustate->pdr[index], offset, pfa);
#endif
	return pfa|offset;
}

// translate ea for memory read/write
INLINE offs_t MMU_REMAP_ADDR(struct z280_state *cpustate, offs_t addr, int program, int write)
{
	offs_t res;
	if (is_user(cpustate)) // User mode
	{
		if (MMUMCR(cpustate) & Z280_MMUMCR_UTE) {
			if (MMUMCR(cpustate) & Z280_MMUMCR_UPD)
				res = mmu_translate_separate(cpustate, addr, Z280_MSR_US_USER, program);
			else
				res = mmu_translate_nonseparate(cpustate, addr, Z280_MSR_US_USER);
#ifdef MMU_DEBUG
			LOG("MMU_REMAP_ADDR U %06X\n", res);
#endif
			if (cpustate->pdr[cpustate->eapdr] & Z280_PDR_V)
			{
				if (write)
				{
					if (!(cpustate->pdr[cpustate->eapdr] & Z280_PDR_WP))
					{
						cpustate->pdr[cpustate->eapdr] |= Z280_PDR_M;
					}
					else // attempt to write to a write-protected page
					{
						longjmp(cpustate->abort_handler, 1);
					}
				}
			}
			else // attempt to access an invalid page
			{
				longjmp(cpustate->abort_handler, 1);
			}
		}
		else
		{
			// no translation, zero extend
			res = addr & 0xffff;
		}
	}
	else // System mode
	{
		if (MMUMCR(cpustate) & Z280_MMUMCR_STE) {
			if (MMUMCR(cpustate) & Z280_MMUMCR_SPD)
				res = mmu_translate_separate(cpustate, addr, Z280_MSR_US_SYSTEM, program);
			else
				res = mmu_translate_nonseparate(cpustate, addr, Z280_MSR_US_SYSTEM);
#ifdef MMU_DEBUG
			LOG("MMU_REMAP_ADDR S %06X\n", res);
#endif
			if (cpustate->pdr[cpustate->eapdr] & Z280_PDR_V)
			{
				if (write)
				{
					if (!(cpustate->pdr[cpustate->eapdr] & Z280_PDR_WP))
					{
						cpustate->pdr[cpustate->eapdr] |= Z280_PDR_M;
					}
					else // attempt to write to a write-protected page
					{
						longjmp(cpustate->abort_handler, 1);
					}
				}
			}
			else // attempt to access an invalid page
			{
				longjmp(cpustate->abort_handler, 1);
			}
		}
		else
		{
			// no translation, zero extend
			res = addr & 0xffff;
		}
	}
	return res;
}

// translate ea for LDUD/LDUP instruction
INLINE offs_t MMU_REMAP_ADDR_LDU(struct z280_state *cpustate, offs_t addr, int program, int write)
{
	offs_t res;
	// assume system mode
	if (MMUMCR(cpustate) & Z280_MMUMCR_UTE) {
		if (MMUMCR(cpustate) & Z280_MMUMCR_UPD)
			res = mmu_translate_separate(cpustate, addr, Z280_MSR_US_USER, program);
		else
			res = mmu_translate_nonseparate(cpustate, addr, Z280_MSR_US_USER);
#ifdef MMU_DEBUG
		LOG("MMU_REMAP_ADDR_LDU %06X\n", res);
#endif
		if (cpustate->pdr[cpustate->eapdr] & Z280_PDR_V)
		{
			if (write)
			{
				if (!(cpustate->pdr[cpustate->eapdr] & Z280_PDR_WP))
				{
					cpustate->pdr[cpustate->eapdr] |= Z280_PDR_M;
				}
				else // attempt to write to a write-protected page
				{
					res = MMU_REMAP_ADDR_FAILED;
				}
			}
		}
		else // attempt to access an invalid page
		{
			res = MMU_REMAP_ADDR_FAILED;
		}
	}
	else
	{
		// no translation, zero extend
		res = addr & 0xffff;
	}
	return res;
}

// translate ea for debugger (without any sideeffects nor traps)
INLINE offs_t MMU_REMAP_ADDR_DBG(struct z280_state *cpustate, offs_t addr, int program)
{
	offs_t res;
	if (is_user(cpustate)) // User mode
	{
		if (MMUMCR(cpustate) & Z280_MMUMCR_UTE) {
			if (MMUMCR(cpustate) & Z280_MMUMCR_UPD)
				res = mmu_translate_separate(cpustate, addr, Z280_MSR_US_USER, program);
			else
				res = mmu_translate_nonseparate(cpustate, addr, Z280_MSR_US_USER);
		}
		else
		{
			// no translation, zero extend
			res = addr & 0xffff;
		}
	}
	else // System mode
	{
		if (MMUMCR(cpustate) & Z280_MMUMCR_STE) {
			if (MMUMCR(cpustate) & Z280_MMUMCR_SPD)
				res = mmu_translate_separate(cpustate, addr, Z280_MSR_US_SYSTEM, program);
			else
				res = mmu_translate_nonseparate(cpustate, addr, Z280_MSR_US_SYSTEM);
		}
		else
		{
			// no translation, zero extend
			res = addr & 0xffff;
		}
	}
	return res;
}

/***************************************************************
 * Read a byte from given memory location
 ***************************************************************/
INLINE UINT8 RM(struct z280_state *cpustate, offs_t addr)
{
	offs_t phy = MMU_REMAP_ADDR(cpustate,addr,0,0);
	return cpustate->ram->read_byte(phy);
}

/***************************************************************
 * Write a byte to given memory location
 ***************************************************************/
INLINE void WM(struct z280_state *cpustate, offs_t addr, UINT8 value)
{
	offs_t phy = MMU_REMAP_ADDR(cpustate,addr,0,1);
	cpustate->ram->write_byte(phy,value);
}

/***************************************************************
 * Read a word from given memory location
 ***************************************************************/
INLINE void RM16( struct z280_state *cpustate, offs_t addr, union PAIR *r )
{
	offs_t phy = MMU_REMAP_ADDR(cpustate,addr,0,0);
	if (cpustate->device->m_bus16 && !(addr & 1))
	{
		r->w.l = cpustate->ram->read_word(phy);
	}
	else
	{
		offs_t phy1 = MMU_REMAP_ADDR(cpustate,addr+1,0,0); // p.13-6, enforce ACCV on page boundary
		r->b.l = cpustate->ram->read_byte(phy);
		r->b.h = cpustate->ram->read_byte(phy1);
	}
}

/***************************************************************
 * Write a word to given memory location
 ***************************************************************/
INLINE void WM16( struct z280_state *cpustate, offs_t addr, union PAIR *r )
{
	offs_t phy = MMU_REMAP_ADDR(cpustate,addr,0,1);
	if (cpustate->device->m_bus16 && !(addr & 1))
	{
		cpustate->ram->write_word(phy,r->w.l);
	}
	else
	{
		offs_t phy1 = MMU_REMAP_ADDR(cpustate,addr+1,0,1); // enforce ACCV on page boundary
		cpustate->ram->write_byte(phy,r->b.l);
		cpustate->ram->write_byte(phy1,r->b.h);
	}
}

/***************************************************************
 * Read word from physical memory location (for fetching interrupt vectors)
 ***************************************************************/
INLINE UINT16 RM16PHY(struct z280_state *cpustate, offs_t addr)
{
	if (cpustate->device->m_bus16)
	{
		return cpustate->ram->read_raw_word(addr);
	}
	else
	{
		return cpustate->ram->read_raw_byte(addr)|((UINT32)cpustate->ram->read_raw_byte(addr+1)<<8);
	}
}

/***************************************************************
 * ROP(cpustate) is identical to RM() except it is used for
 * reading opcodes. In case of system with memory mapped I/O,
 * this function can be used to greatly speed up emulation
 ***************************************************************/
INLINE UINT8 ROP(struct z280_state *cpustate)
{
	// TODO word fetch if bus16
	offs_t addr = cpustate->_PCD;
	offs_t phy = MMU_REMAP_ADDR(cpustate,addr,1,0);
	cpustate->_PC++;
	return cpustate->ram->read_raw_byte(phy);
}

/****************************************************************
 * ARG(cpustate) is identical to ROP(cpustate) except it is used
 * for reading opcode arguments. This difference can be used to
 * support systems that use different encoding mechanisms for
 * opcodes and opcode arguments
 ***************************************************************/
INLINE UINT8 ARG(struct z280_state *cpustate)
{
	// TODO word fetch if bus16
	offs_t addr = cpustate->_PCD;
	offs_t phy = MMU_REMAP_ADDR(cpustate,addr,1,0);
	cpustate->_PC++;
	return cpustate->ram->read_raw_byte(phy);
}

INLINE UINT32 ARG16(struct z280_state *cpustate)
{
	// TODO word fetch if bus16
	offs_t addr = cpustate->_PCD;
	offs_t phy = MMU_REMAP_ADDR(cpustate,addr,1,0);
	cpustate->_PC += 2;
	if (cpustate->device->m_bus16 && !(addr & 1))
	{
		return cpustate->ram->read_raw_word(phy);
	}
	else
	{
		offs_t phy1 = MMU_REMAP_ADDR(cpustate,addr+1,1,0);
		return cpustate->ram->read_raw_byte(phy)|((UINT32)cpustate->ram->read_raw_byte(phy1)<<8);
	}
}

/***************************************************************
 * Calculate the effective addess cpustate->ea of an opcode using
 * IX+offset resp. IY+offset addressing.
 ***************************************************************/
#define EAX(cs) (cs)->ea = (UINT32)(UINT16)((cs)->_IX + (INT8)ARG(cs))
#define EAY(cs) (cs)->ea = (UINT32)(UINT16)((cs)->_IY + (INT8)ARG(cs))
// Z280 extended addressing modes
#define EASP16(cs) (cs)->ea = (UINT32)(UINT16)(_SP(cs) + ARG16(cs))
#define EAH16(cs) (cs)->ea = (UINT32)(UINT16)((cs)->_HL + ARG16(cs))
#define EAX16(cs) (cs)->ea = (UINT32)(UINT16)((cs)->_IX + ARG16(cs))
#define EAY16(cs) (cs)->ea = (UINT32)(UINT16)((cs)->_IY + ARG16(cs))
#define EAHX(cs) (cs)->ea = (UINT32)(UINT16)((cs)->_HL + (cs)->_IX)
#define EAHY(cs) (cs)->ea = (UINT32)(UINT16)((cs)->_HL + (cs)->_IY)
#define EAXY(cs) (cs)->ea = (UINT32)(UINT16)((cs)->_IX + (cs)->_IY)
#define EARA(cs) (cs)->ea = (UINT32)(UINT16)((cs)->_PCD + ARG16(cs))

/***************************************************************
 * POP
 ***************************************************************/
#define POP(cs,DR) { RM16(cs, _SPD(cs), &(cs)->DR ); INC2_SP(cs); }

/***************************************************************
 * PUSH
 ***************************************************************/
#define PUSH_R(cs,SR) { WM16(cs, _SPD(cs)-2, &(cs)->SR); DEC2_SP(cs); }
#define PUSH(cs,SR) { PUSH_R(cs,SR); if(is_system(cs)) CHECK_SSO(cs); }

/***************************************************************
 * JP
 ***************************************************************/
#define JP {                                                    \
	cpustate->_PCD = ARG16(cpustate);                                           \
}

#define JP_RA {                                                    \
	EARA(cpustate); cpustate->_PCD = cpustate->ea;                         \
}

/***************************************************************
 * JP_COND
 ***************************************************************/

#define JP_COND(cond)                                           \
	if( cond )                                                  \
	{                                                           \
		cpustate->_PCD = ARG16(cpustate);                                       \
	}                                                           \
	else                                                        \
	{                                                           \
		cpustate->_PC += 2;                                             \
	}

#define JP_HL_COND(cond)                                           \
	if( cond )                                                  \
	{                                                           \
		cpustate->_PCD = cpustate->_HL;                         \
	}                                                           \
	else                                                        \
	{                                                           \
		cpustate->_PC += 2;                                             \
	}

#define JP_RA_COND(cond)                                           \
	if( cond )                                                  \
	{                                                           \
		EARA(cpustate); cpustate->_PCD = cpustate->ea;           \
	}                                                           \
	else                                                        \
	{                                                           \
		cpustate->_PC += 2;                                             \
	}

/***************************************************************
 * JR
 ***************************************************************/
#define JR()                                                    \
{                                                               \
	INT8 arg = (INT8)ARG(cpustate); /* ARG(cpustate) also increments cpustate->_PC */   \
	cpustate->_PC += arg;           /* so don't do cpustate->_PC += ARG(cpustate) */      \
}

/***************************************************************
 * JR_COND
 ***************************************************************/
#define JR_COND(cond,opcode)                                    \
	if( cond )                                                  \
	{                                                           \
		INT8 arg = (INT8)ARG(cpustate); /* ARG(cpustate) also increments cpustate->_PC */ \
		cpustate->_PC += arg;           /* so don't do cpustate->_PC += ARG(cpustate) */  \
		CC(ex,opcode);                                          \
	}                                                           \
	else cpustate->_PC++;
/***************************************************************
 * CALL
 ***************************************************************/
#define CALL()                                                  \
	cpustate->ea = ARG16(cpustate);                                             \
	PUSH_R(cpustate,  PC );                                               \
	cpustate->_PCD = cpustate->ea;								\
	if(is_system(cpustate)) CHECK_SSO(cpustate);

/***************************************************************
 * CALL_COND
 ***************************************************************/
#define CALL_COND(cond,opcode)                                  \
	if( cond )                                                  \
	{                                                           \
		CALL();													\
		CC(ex,opcode);                                          \
	}                                                           \
	else                                                        \
	{                                                           \
		cpustate->_PC+=2;                                               \
	}

#define CALL_HL_COND(cond,opcode)                                  \
	if( cond )                                                  \
	{                                                           \
		PUSH_R(cpustate,  PC );                                               \
		cpustate->_PCD = cpustate->_HL;                                              \
		if(is_system(cpustate)) CHECK_SSO(cpustate);            \
		CC(ex,opcode);                                          \
	}                                                           \
	else                                                        \
	{                                                           \
		cpustate->_PC+=2;                                               \
	}

#define CALL_RA_COND(cond,opcode)                                  \
	if( cond )                                                  \
	{                                                           \
		PUSH_R(cpustate,  PC );                                               \
		EARA(cpustate); cpustate->_PCD = cpustate->ea;            \
		if(is_system(cpustate)) CHECK_SSO(cpustate);            \
		CC(ex,opcode);                                          \
	}                                                           \
	else                                                        \
	{                                                           \
		cpustate->_PC+=2;                                               \
	}

/***************************************************************
 * RET_COND
 ***************************************************************/
#define RET_COND(cond,opcode)                                   \
	if( cond )                                                  \
	{                                                           \
		POP(cpustate, PC);                                              \
		CC(ex,opcode);                                          \
	}

/***************************************************************
 * RETN
 ***************************************************************/
#define RETN    {                                               \
	CHECK_PRIV(cpustate)												  \
	{															\
		LOG("Z280 '%s' RETN MSR:%d IFF2:%d\n", cpustate->device->m_tag, \
			cpustate->cr[Z280_MSR]&Z280_MSR_IREMASK, cpustate->IFF2); \
		POP(cpustate, PC);                                                  \
		cpustate->cr[Z280_MSR] = (cpustate->cr[Z280_MSR] & ~Z280_MSR_IREMASK) | \
			cpustate->IFF2;                                                \
	}															\
}

/***************************************************************
 * RETI
 ***************************************************************/
#define RETI    {                                               \
	CHECK_PRIV(cpustate)												  \
	{															\
		POP(cpustate, PC);                                                  \
/* according to http://www.msxnet.org/tech/Z80/z80undoc.txt */  \
/*  	cpustate->IFF1 = cpustate->IFF2;  */                                            \
		if (cpustate->daisy != NULL)						\
			z80_daisy_chain_call_reti_device(cpustate->daisy);                 \
	}															\
}

#define RETIL    {                                               \
	union PAIR tmp, tmp2;                                              \
	CHECK_PRIV(cpustate)												  \
	{															\
		/* atomic double pop */					                \
		RM16(cpustate, _SPD(cpustate), &tmp);                    \
		RM16(cpustate, _SPD(cpustate)+2, &tmp2);                    \
		if(is_system(cpustate)) (cpustate)->_SSP += 4; else (cpustate)->_USP += 4;   \
		MSR(cpustate) = tmp.w.l;                   \
		cpustate->_PC = tmp2.w.l;                   \
	}															\
}

/***************************************************************
 * PCACHE
 ***************************************************************/
#define PCACHE { \
				 \
}

/***************************************************************
 * LDUP, LDUD
 ***************************************************************/
#define LDU_A_M(program) {	 \
	CHECK_PRIV(cpustate)												  \
	{															\
		offs_t phy = MMU_REMAP_ADDR_LDU(cpustate,cpustate->ea,program,0);		   \
		if (phy != MMU_REMAP_ADDR_FAILED)						   \
		{														   \
			cpustate->_A = (cpustate)->ram->read_byte(phy);		   \
			cpustate->_F &= ~CF;								   \
		}														   \
		else													   \
		{														   \
			cpustate->_F &= ~(ZF|VF);							   \
			UINT16 pdrv = cpustate->pdr[cpustate->eapdr];          \
			cpustate->_F |= CF | (pdrv & Z280_PDR_V?VF:0) | (pdrv & Z280_PDR_WP?ZF:0);\
		}														   \
	}															\
}

#define LDU_M_A(program) {	 \
	CHECK_PRIV(cpustate)												  \
	{															\
		offs_t phy = MMU_REMAP_ADDR_LDU(cpustate,cpustate->ea,program,1);		   \
		if (phy != MMU_REMAP_ADDR_FAILED)						   \
		{														   \
			(cpustate)->ram->write_byte(phy, cpustate->_A);		   \
			cpustate->_F &= ~CF;								   \
		}														   \
		else													   \
		{														   \
			cpustate->_F &= ~(ZF|VF);							   \
			UINT16 pdrv = cpustate->pdr[cpustate->eapdr];          \
			cpustate->_F |= CF | (pdrv & Z280_PDR_V?VF:0) | (pdrv & Z280_PDR_WP?ZF:0);\
		}														   \
	}															\
}

/***************************************************************
 * LD   R,A
 ***************************************************************/
#define LD_R_A {                                                \
	CHECK_PRIV(cpustate)												  \
	{															\
		cpustate->R = cpustate->_A;                                                 \
	}															\
}

/***************************************************************
 * LD   A,R
 ***************************************************************/
#define LD_A_R {                                                \
	CHECK_PRIV(cpustate)												  \
	{															\
		cpustate->_A = cpustate->R;                                     \
		cpustate->_F = (cpustate->_F & CF) | SZ[cpustate->_A] | ( (MSR(cpustate)&1) << 2 );                    \
	}															\
}

/***************************************************************
 * LD   I,A
 ***************************************************************/
#define LD_I_A {                                                \
	CHECK_PRIV(cpustate)												  \
	{															\
		cpustate->I = cpustate->_A;                                                 \
	}															\
}

/***************************************************************
 * LD   A,I
 ***************************************************************/
#define LD_A_I {                                                \
	CHECK_PRIV(cpustate)												  \
	{															\
		cpustate->_A = cpustate->I;                                                 \
		cpustate->_F = (cpustate->_F & CF) | SZ[cpustate->_A] | ( (MSR(cpustate)&1) << 2 );                    \
	}															\
}

/***************************************************************
 * IM   n
 ***************************************************************/
#define IM(cs,n) {                                                \
	CHECK_PRIV(cs)												  \
	{															\
		cs->IM = n;												\
	}															\
}

/***************************************************************
 * RST
 ***************************************************************/
#define RST(addr)                                               \
	PUSH_R(cpustate,  PC );                                            \
	cpustate->_PCD = addr;										 \
	if(is_system(cpustate)) CHECK_SSO(cpustate);


/***************************************************************
 * LDCTL
 ***************************************************************/
#define LD_CTL_REG(SR) {	 \
	CHECK_PRIV(cpustate)												  \
	{															\
		z280_writecontrol(cpustate, cpustate->_C, cpustate->SR.d);			 \
	}															\
}

#define LD_REG_CTL(DR) {	 \
	CHECK_PRIV(cpustate)												  \
	{															\
		cpustate->DR.d = z280_readcontrol(cpustate, cpustate->_C);			 \
	}															\
}


/***************************************************************
 * INC  r8
 ***************************************************************/
INLINE UINT8 INC(struct z280_state *cpustate, UINT8 value)
{
	UINT8 res = value + 1;
	cpustate->_F = (cpustate->_F & CF) | SZHV_inc[res];
	return (UINT8)res;
}

/***************************************************************
 * DEC  r8
 ***************************************************************/
INLINE UINT8 DEC(struct z280_state *cpustate, UINT8 value)
{
	UINT8 res = value - 1;
	cpustate->_F = (cpustate->_F & CF) | SZHV_dec[res];
	return res;
}

/***************************************************************
 * RLCA
 ***************************************************************/
#define RLCA                                                    \
	cpustate->_A = (cpustate->_A << 1) | (cpustate->_A >> 7);                               \
	cpustate->_F = (cpustate->_F & (SF | ZF | PF)) | (cpustate->_A & (YF | XF | CF))

/***************************************************************
 * RRCA
 ***************************************************************/
#define RRCA                                                    \
	cpustate->_F = (cpustate->_F & (SF | ZF | PF)) | (cpustate->_A & (YF | XF | CF));       \
	cpustate->_A = (cpustate->_A >> 1) | (cpustate->_A << 7)

/***************************************************************
 * RLA
 ***************************************************************/
#define RLA {                                                   \
	UINT8 res = (cpustate->_A << 1) | (cpustate->_F & CF);                          \
	UINT8 c = (cpustate->_A & 0x80) ? CF : 0;                           \
	cpustate->_F = (cpustate->_F & (SF | ZF | PF)) | c | (res & (YF | XF));         \
	cpustate->_A = res;                                                 \
}

/***************************************************************
 * RRA
 ***************************************************************/
#define RRA {                                                   \
	UINT8 res = (cpustate->_A >> 1) | (cpustate->_F << 7);                          \
	UINT8 c = (cpustate->_A & 0x01) ? CF : 0;                           \
	cpustate->_F = (cpustate->_F & (SF | ZF | PF)) | c | (res & (YF | XF));         \
	cpustate->_A = res;                                                 \
}

/***************************************************************
 * RRD
 ***************************************************************/
#define RRD {                                                   \
	UINT8 n = RM(cpustate, cpustate->_HL);                                          \
	WM(cpustate,  cpustate->_HL, (n >> 4) | (cpustate->_A << 4) );                          \
	cpustate->_A = (cpustate->_A & 0xf0) | (n & 0x0f);                              \
	cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_A];                                 \
}

/***************************************************************
 * RLD
 ***************************************************************/
#define RLD {                                                   \
	UINT8 n = RM(cpustate, cpustate->_HL);                                          \
	WM(cpustate,  cpustate->_HL, (n << 4) | (cpustate->_A & 0x0f) );                            \
	cpustate->_A = (cpustate->_A & 0xf0) | (n >> 4);                                \
	cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_A];                                 \
}

/***************************************************************
 * EXTS
 ***************************************************************/
#define EXTS { \
	cpustate->_L = cpustate->_A;		   \
	cpustate->_H = cpustate->_A&0x80?0xff:0;		   \
}

#define EXTS_HL {  \
	cpustate->_DE = cpustate->_H&0x80?0xffff:0;		   \
}

/***************************************************************
 * ADD  A,n
 ***************************************************************/
#define ADD(value)                                              \
{                                                               \
	UINT32 ah = cpustate->_AFD & 0xff00;                                    \
	UINT32 res = (UINT8)((ah >> 8) + value);                    \
	cpustate->_F = SZHVC_add[ah | res];                                 \
	cpustate->_A = res;                                                 \
}

/***************************************************************
 * ADC  A,n
 ***************************************************************/
#define ADC(value)                                              \
{                                                               \
	UINT32 ah = cpustate->_AFD & 0xff00, c = cpustate->_AFD & 1;                    \
	UINT32 res = (UINT8)((ah >> 8) + value + c);                \
	cpustate->_F = SZHVC_add[(c << 16) | ah | res];                     \
	cpustate->_A = res;                                                 \
}

/***************************************************************
 * SUB  n
 ***************************************************************/
#define SUB(value)                                              \
{                                                               \
	UINT32 ah = cpustate->_AFD & 0xff00;                                    \
	UINT32 res = (UINT8)((ah >> 8) - value);                    \
	cpustate->_F = SZHVC_sub[ah | res];                                 \
	cpustate->_A = res;                                                 \
}

/***************************************************************
 * CP   n
 ***************************************************************/
#define CP(value)                                               \
{                                                               \
	UINT32 ah = cpustate->_AFD & 0xff00;                                    \
	UINT32 res = (UINT8)((ah >> 8) - value);                    \
	cpustate->_F = SZHVC_sub[ah | res];                                 \
}

/***************************************************************
 * SBC  A,n
 ***************************************************************/
#define SBC(value)                                              \
{                                                               \
	UINT32 ah = cpustate->_AFD & 0xff00, c = cpustate->_AFD & 1;                    \
	UINT32 res = (UINT8)((ah >> 8) - value - c);                \
	cpustate->_F = SZHVC_sub[(c<<16) | ah | res];                       \
	cpustate->_A = res;                                                 \
}

/***************************************************************
 * NEG
 ***************************************************************/
#define NEG {                                                   \
	UINT8 value = cpustate->_A;                                         \
	cpustate->_A = 0;                                                   \
	SUB(value);                                                 \
}

#define NEG16 {                                                   \
	UINT32 res = - cpustate->_HLD;                 \
	cpustate->_F = (((cpustate->_HLD ^ res) >> 8) & HF) | NF |         \
		((res >> 16) & CF) |                                    \
		((res >> 8) & SF) |                                     \
		((res & 0xffff) ? 0 : ZF) |                             \
		((cpustate->_HLD & res &0x8000) >> 13);   \
	cpustate->_HL = (UINT16)res;                                            \
}

/***************************************************************
 * DAA
 ***************************************************************/
#define DAA {                                                   \
	UINT8 r = cpustate->_A;                                         \
	if (cpustate->_F&NF) {                                          \
		if ((cpustate->_F&HF)|((cpustate->_A&0xf)>9)) r-=6;                     \
		if ((cpustate->_F&CF)|(cpustate->_A>0x99)) r-=0x60;                     \
	}                                                   \
	else {                                                  \
		if ((cpustate->_F&HF)|((cpustate->_A&0xf)>9)) r+=6;                     \
		if ((cpustate->_F&CF)|(cpustate->_A>0x99)) r+=0x60;                     \
	}                                                   \
	cpustate->_F=(cpustate->_F&3)|(cpustate->_A>0x99)|((cpustate->_A^r)&HF)|SZP[r];             \
	cpustate->_A=r;                                             \
}

/***************************************************************
 * AND  n
 ***************************************************************/
#define AND(value)                                              \
	cpustate->_A &= value;                                              \
	cpustate->_F = SZP[cpustate->_A] | HF

/***************************************************************
 * OR   n
 ***************************************************************/
#define OR(value)                                               \
	cpustate->_A |= value;                                              \
	cpustate->_F = SZP[cpustate->_A]

/***************************************************************
 * XOR  n
 ***************************************************************/
#define XOR(value)                                              \
	cpustate->_A ^= value;                                              \
	cpustate->_F = SZP[cpustate->_A]

/***************************************************************
 * EX   AF,AF'
 ***************************************************************/
#define EX_AF {                                                 \
	union PAIR tmp;                                                   \
	tmp = cpustate->AF; cpustate->AF = cpustate->AF2; cpustate->AF2 = tmp;          \
	cpustate->AF2inuse = !!cpustate->AF2inuse;                  \
}

/***************************************************************
 * EX   DE,HL
 ***************************************************************/
#define EX_DE_HL {                                              \
	union PAIR tmp;                                                   \
	tmp = cpustate->DE; cpustate->DE = cpustate->HL; cpustate->HL = tmp;            \
}

/***************************************************************
 * EXX
 ***************************************************************/
#define EXX {                                                   \
	union PAIR tmp;                                                   \
	tmp = cpustate->BC; cpustate->BC = cpustate->BC2; cpustate->BC2 = tmp;          \
	tmp = cpustate->DE; cpustate->DE = cpustate->DE2; cpustate->DE2 = tmp;          \
	tmp = cpustate->HL; cpustate->HL = cpustate->HL2; cpustate->HL2 = tmp;          \
	cpustate->BC2inuse = !!cpustate->BC2inuse;                  \
}

/***************************************************************
 * EX   (SP),r16
 ***************************************************************/
#define EXSP(DR)                                                \
{                                                               \
	union PAIR tmp = { { 0, 0, 0, 0 } };                              \
	RM16(cpustate,  _SPD(cpustate), &tmp );                                         \
	WM16(cpustate,  _SPD(cpustate), &cpustate->DR );                                    \
	cpustate->DR = tmp;                                             \
}

/***************************************************************
 * ADD16
 ***************************************************************/
// old Z80 ADD16
#define ADD16(DR,val)                                            \
{                                                               \
	UINT32 res = cpustate->DR.d + val;                       \
	cpustate->_F = (cpustate->_F & (SF | ZF | VF)) |                                \
		(((cpustate->DR.d ^ res ^ val) >> 8) & HF) |         \
		((res >> 16) & CF);                                     \
	cpustate->DR.w.l = (UINT16)res;                                 \
}

// fixed ADD16 with correct flags
#define ADD16F(val)                                           \
{                                                               \
	UINT16 uvalue = val;                                       \
	UINT32 res = cpustate->_HLD + uvalue;                       \
	cpustate->_F = (((cpustate->_HLD ^ res ^ uvalue) >> 8) & HF) |              \
		((res >> 16) & CF) |                                    \
		((res >> 8) & SF) |                                     \
		((res & 0xffff) ? 0 : ZF) |                             \
		(((uvalue ^ cpustate->_HLD ^ 0x8000) & (uvalue ^ res) & 0x8000) >> 13); \
	cpustate->_HL = (UINT16)res;                                 \
}

/***************************************************************
 * ADC  r16,r16
 ***************************************************************/
#define ADC16(DR,val)                                               \
{                                                               \
	UINT32 res = cpustate->DR.d + val + (cpustate->_F & CF);                 \
	cpustate->_F = (((cpustate->DR.d ^ res ^ val) >> 8) & HF) |              \
		((res >> 16) & CF) |                                    \
		((res >> 8) & SF) |                                     \
		((res & 0xffff) ? 0 : ZF) |                             \
		(((val ^ cpustate->DR.d ^ 0x8000) & (val ^ res) & 0x8000) >> 13); \
	cpustate->DR.w.l = (UINT16)res;                                            \
}

/***************************************************************
 * SUB  r16,r16
 ***************************************************************/
#define SUB16(val)                                               \
{                                                               \
	UINT16 uvalue = val;                                       \
	UINT32 res = cpustate->_HLD - uvalue;                 \
	cpustate->_F = (((cpustate->_HLD ^ res ^ uvalue) >> 8) & HF) | NF |         \
		((res >> 16) & CF) |                                    \
		((res >> 8) & SF) |                                     \
		((res & 0xffff) ? 0 : ZF) |                             \
		(((uvalue ^ cpustate->_HLD) & (cpustate->_HLD ^ res) &0x8000) >> 13);   \
	cpustate->_HL = (UINT16)res;                                            \
}

/***************************************************************
 * CP  r16,r16
 ***************************************************************/
#define CP16(val)                                               \
{                                                               \
	UINT16 uvalue = val;                                       \
	UINT32 res = cpustate->_HLD - uvalue;                 \
	cpustate->_F = (((cpustate->_HLD ^ res ^ uvalue) >> 8) & HF) | NF |         \
		((res >> 16) & CF) |                                    \
		((res >> 8) & SF) |                                     \
		((res & 0xffff) ? 0 : ZF) |                             \
		(((uvalue ^ cpustate->_HLD) & (cpustate->_HLD ^ res) &0x8000) >> 13);   \
}

/***************************************************************
 * SBC  r16,r16
 ***************************************************************/
#define SBC16(DR,val)                                               \
{                                                               \
	UINT32 res = cpustate->DR.d - val - (cpustate->_F & CF);                 \
	cpustate->_F = (((cpustate->DR.d ^ res ^ val) >> 8) & HF) | NF |         \
		((res >> 16) & CF) |                                    \
		((res >> 8) & SF) |                                     \
		((res & 0xffff) ? 0 : ZF) |                             \
		(((val ^ cpustate->DR.d) & (cpustate->DR.d ^ res) &0x8000) >> 13);   \
	cpustate->DR.w.l = (UINT16)res;                                            \
}

/***************************************************************
 * ADD  r16,A
 ***************************************************************/
#define ADD16_A(DR)                                            \
{                                                               \
    UINT32 i = cpustate->_A;								   \
	if (i&0x80) i |= 0xffffff00;								   \
	UINT32 res = cpustate->DR.d + i;                       \
	cpustate->_F = (cpustate->_F & (SF | ZF | VF)) |                                \
		(((cpustate->DR.d ^ res ^ i) >> 8) & HF) |         \
		((res >> 16) & CF);                                     \
	cpustate->DR.w.l = (UINT16)res;                                 \
}

/***************************************************************
 * MULT  A,r
 ***************************************************************/
#define MULT(value)                                              \
{                                                               \
	int16_t res = (int16_t)(INT8)cpustate->_A * (int16_t)(INT8)value;      \
	cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	     (res<0?SF:0) | (res==0?ZF:0) | (res<-128||res>=128?CF:0);      \
	cpustate->_HL = res;                                                 \
}

/***************************************************************
 * MULTU  A,r
 ***************************************************************/
#define MULTU(value)                                              \
{                                                               \
	UINT16 res = (UINT16)cpustate->_A * (UINT16)value;                    \
	cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	     (res==0?ZF:0) | (res&0xff00?CF:0);                     \
	cpustate->_HL = res;                                                 \
}

/***************************************************************
 * MULTW  HL,r
 ***************************************************************/
#define MULTW(value)                                              \
{                                                               \
	INT32 res = (INT32)(int16_t)cpustate->_HL * (INT32)(int16_t)value;                    \
	cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	     (res<0?SF:0) | (res==0?ZF:0) | (res<-32768||res>=32768?CF:0);      \
	cpustate->_DE = (res>>16);                                           \
	cpustate->_HL = res;                                           \
}

/***************************************************************
 * MULTUW  HL,r
 ***************************************************************/
#define MULTUW(value)                                              \
{                                                               \
	UINT32 res = (UINT32)cpustate->_HL * (UINT32)value;                    \
	cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	     (res==0?ZF:0) | (res&0xffff0000?CF:0);                     \
	cpustate->_DE = (res>>16);                                           \
	cpustate->_HL = res;                                           \
}

/***************************************************************
 * DIV  HL,r
 ***************************************************************/
#define DIV(value)                                              \
{                                                               \
    int16_t ivalue = (int16_t)value;                             \
    if (ivalue == 0)												\
	{															\
	   cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	      (SF | ZF);                                      \
	   take_trap(cpustate, Z280_TRAP_DIV);						\
	}															\
	else                                                        \
	{                                                           \
	   int16_t quot = (int16_t)cpustate->_HL / ivalue;      \
	   if (quot>=-128&&quot<128)							\
	   {													\
	      /* remainder has same sign as dividend, this is compliant to C99  */ 		  \
		  int16_t rem = (int16_t)cpustate->_HL % ivalue;      \
	      cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	         (quot<0?SF:0) | (quot==0?ZF:0);      \
	      cpustate->_A = quot;                               \
	      cpustate->_L = rem;                                  \
	   }														\
	   else														\
	   {														\
     	   cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	         (VF);                                              \
     	   take_trap(cpustate, Z280_TRAP_DIV);				\
	   }														\
    }															\
}

/***************************************************************
 * DIVU  HL,r
 ***************************************************************/
#define DIVU(value)                                              \
{                                                               \
    UINT16 uvalue = (UINT16)value;                             \
    if (uvalue == 0)												\
	{															\
	   cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	      (SF | ZF);                                      \
	   take_trap(cpustate, Z280_TRAP_DIV);						\
	}															\
	else                                                        \
	{                                                           \
	   UINT16 quot = cpustate->_HL / uvalue;         \
	   if (quot<128)							\
	   {													\
	      UINT16 rem = cpustate->_HL % uvalue;      \
	      cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	         (quot==0?ZF:0);      \
	      cpustate->_A = quot;                               \
	      cpustate->_L = rem;                                  \
	   }														\
	   else														\
	   {														\
     	   cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	         (VF);                                              \
     	   take_trap(cpustate, Z280_TRAP_DIV);					\
	   }														\
    }															\
}

/***************************************************************
 * DIVW  DEHL,r
 ***************************************************************/
#define DIVW(value)                                              \
{                                                               \
    INT32 ivalue = (INT32)value;                                 \
	if (ivalue == 0)												\
	{															\
	   cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	      (SF | ZF);                                      \
	   take_trap(cpustate, Z280_TRAP_DIV);							\
	}															\
	else                                                        \
	{                                                           \
	   UINT32 src = (cpustate->_DE << 16) | cpustate->_HL;   \
	   INT32 quot = (INT32)src / ivalue;                      \
	   if (quot>=-32768&&quot<32768)							\
	   {													\
	      /* remainder has same sign as dividend, this is compliant to C99  */ 		  \
		  INT32 rem = (INT32)src % ivalue;                  \
	      cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	         (quot<0?SF:0) | (quot==0?ZF:0);      \
	      cpustate->_HL = quot;                               \
	      cpustate->_DE = rem;                                  \
	   }														\
	   else														\
	   {														\
     	   cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	         (VF);                                              \
     	   take_trap(cpustate, Z280_TRAP_DIV);						\
	   }														\
    }															\
}

/***************************************************************
 * DIVUW  DEHL,r
 ***************************************************************/
#define DIVUW(value)                                              \
{                                                               \
    UINT32 uvalue = (UINT32)value;                                \
    if (uvalue == 0)												\
	{															\
	   cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	      (SF | ZF);                                      \
	   take_trap(cpustate, Z280_TRAP_DIV);						\
	}															\
	else                                                        \
	{                                                           \
	   UINT32 src = (cpustate->_DE << 16) | cpustate->_HL;   \
	   UINT32 quot = src / uvalue;                           \
	   if (quot<32768)							\
	   {													\
	      UINT32 rem = src % uvalue;                          \
	      cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	         (quot==0?ZF:0);      \
	      cpustate->_HL = quot;                               \
	      cpustate->_DE = rem;                                  \
	   }														\
	   else														\
	   {														\
     	   cpustate->_F = (cpustate->_F & (HF | NF)) |        \
	         (VF);                                              \
     	   take_trap(cpustate, Z280_TRAP_DIV);					\
	   }														\
    }															\
}

/***************************************************************
 * RLC  r8
 ***************************************************************/
INLINE UINT8 RLC(struct z280_state *cpustate, UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = ((res << 1) | (res >> 7)) & 0xff;
	cpustate->_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * RRC  r8
 ***************************************************************/
INLINE UINT8 RRC(struct z280_state *cpustate, UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = ((res >> 1) | (res << 7)) & 0xff;
	cpustate->_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * RL   r8
 ***************************************************************/
INLINE UINT8 RL(struct z280_state *cpustate, UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = ((res << 1) | (cpustate->_F & CF)) & 0xff;
	cpustate->_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * RR   r8
 ***************************************************************/
INLINE UINT8 RR(struct z280_state *cpustate, UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = ((res >> 1) | (cpustate->_F << 7)) & 0xff;
	cpustate->_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SLA  r8
 ***************************************************************/
INLINE UINT8 SLA(struct z280_state *cpustate, UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = (res << 1) & 0xff;
	cpustate->_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SRA  r8
 ***************************************************************/
INLINE UINT8 SRA(struct z280_state *cpustate, UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = ((res >> 1) | (res & 0x80)) & 0xff;
	cpustate->_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SLL  r8
 ***************************************************************/
INLINE UINT8 SLL(struct z280_state *cpustate, UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = ((res << 1) | 0x01) & 0xff;
	cpustate->_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SRL  r8
 ***************************************************************/
INLINE UINT8 SRL(struct z280_state *cpustate, UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = (res >> 1) & 0xff;
	cpustate->_F = SZP[res] | c;
	return res;
}

/***************************************************************
 * TSET   r8
 ***************************************************************/
INLINE UINT8 TSET(struct z280_state *cpustate, UINT8 value)                                               \
{
	cpustate->_F = (cpustate->_F & 0x7f) | (value & 0x80);
	return 0xff;
}

/***************************************************************
 * BIT  bit,r8
 ***************************************************************/
#undef BIT
#define BIT(bit,reg)                                            \
	cpustate->_F = (cpustate->_F & CF) | HF | SZ_BIT[reg & (1<<bit)]

/***************************************************************
 * BIT  bit,(IX/Y+o)
 ***************************************************************/
#define BIT_XY(bit,reg)                                         \
	cpustate->_F = (cpustate->_F & CF) | HF | (SZ_BIT[reg & (1<<bit)] & ~(YF|XF)) | ((cpustate->ea>>8) & (YF|XF))

/***************************************************************
 * RES  bit,r8
 ***************************************************************/
INLINE UINT8 RES(UINT8 bit, UINT8 value)
{
	return value & ~(1<<bit);
}

/***************************************************************
 * SET  bit,r8
 ***************************************************************/
INLINE UINT8 SET(UINT8 bit, UINT8 value)
{
	return value | (1<<bit);
}

/***************************************************************
 * LDI
 ***************************************************************/
#define LDI {                                                   \
	UINT8 val = RM(cpustate, cpustate->_HL);                                         \
	WM(cpustate,  cpustate->_DE, val );                                              \
	cpustate->_F &= SF | ZF | CF;                                       \
	if( (cpustate->_A + val) & 0x02 ) cpustate->_F |= YF; /* bit 1 -> flag 5 */      \
	if( (cpustate->_A + val) & 0x08 ) cpustate->_F |= XF; /* bit 3 -> flag 3 */      \
	cpustate->_HL++; cpustate->_DE++; cpustate->_BC--;                                      \
	if( cpustate->_BC ) cpustate->_F |= VF;                                         \
}

/***************************************************************
 * CPI
 ***************************************************************/
#define CPI {                                                   \
	UINT8 val = RM(cpustate, cpustate->_HL);                                        \
	UINT8 res = cpustate->_A - val;                                     \
	cpustate->_HL++; cpustate->_BC--;                                               \
	cpustate->_F = (cpustate->_F & CF) | (SZ[res] & ~(YF|XF)) | ((cpustate->_A ^ val ^ res) & HF) | NF;  \
	if( cpustate->_F & HF ) res -= 1;                                   \
	if( res & 0x02 ) cpustate->_F |= YF; /* bit 1 -> flag 5 */          \
	if( res & 0x08 ) cpustate->_F |= XF; /* bit 3 -> flag 3 */          \
	if( cpustate->_BC ) cpustate->_F |= VF;                                         \
}

/***************************************************************
 * INI
 ***************************************************************/
#define INI {                                                   \
	UINT8 io = IN(cpustate, cpustate->_BC);                                         \
	WM(cpustate,  cpustate->_HL, io );                                              \
	cpustate->_B--;                                                     \
	cpustate->_HL++;                                                        \
	cpustate->_F = SZ[cpustate->_B];                                                \
	if( io & SF ) cpustate->_F |= NF;                                   \
	if( (cpustate->_C + io + 1) & 0x100 ) cpustate->_F |= HF | CF;                  \
	if( (irep_tmp1[cpustate->_C & 3][io & 3] ^                          \
			breg_tmp2[cpustate->_B] ^                                       \
			(cpustate->_C >> 2) ^                                           \
			(io >> 2)) & 1 )                                        \
		cpustate->_F |= PF;                                             \
}

#define INIW {                                                   \
	UINT16 io = IN16(cpustate, cpustate->_BC);                                         \
	union PAIR tmp; tmp.w.l = io;                                            \
	WM16(cpustate,  cpustate->_HL, &tmp );                                              \
	cpustate->_B--;                                                     \
	cpustate->_HL += 2;                                                        \
	cpustate->_F = SZ[cpustate->_B];                                                \
	if( io & SF ) cpustate->_F |= NF;                                   \
	if( (cpustate->_C + io + 1) & 0x100 ) cpustate->_F |= HF | CF;                  \
	if( (irep_tmp1[cpustate->_C & 3][io & 3] ^                          \
			breg_tmp2[cpustate->_B] ^                                       \
			(cpustate->_C >> 2) ^                                           \
			(io >> 2)) & 1 )                                        \
		cpustate->_F |= PF;                                             \
}

/***************************************************************
 * OUTI
 ***************************************************************/
#define OUTI {                                                  \
	UINT8 io = RM(cpustate, cpustate->_HL);                                         \
	OUT(cpustate,  cpustate->_BC, io );                                             \
	cpustate->_B--;                                                     \
	cpustate->_HL++;                                                        \
	cpustate->_F = SZ[cpustate->_B];                                                \
	if( io & SF ) cpustate->_F |= NF;                                   \
	if( (cpustate->_C + io + 1) & 0x100 ) cpustate->_F |= HF | CF;                  \
	if( (irep_tmp1[cpustate->_C & 3][io & 3] ^                          \
			breg_tmp2[cpustate->_B] ^                                       \
			(cpustate->_C >> 2) ^                                           \
			(io >> 2)) & 1 )                                        \
		cpustate->_F |= PF;                                             \
}

#define OUTIW {                                                  \
	union PAIR tmp;                                                 \
	RM16(cpustate, cpustate->_HL, &tmp);                                  \
	UINT16 io = tmp.w.l;												\
	OUT16(cpustate,  cpustate->_BC, io );                                             \
	cpustate->_B--;                                                     \
	cpustate->_HL += 2;                                                        \
	cpustate->_F = SZ[cpustate->_B];                                                \
	if( io & SF ) cpustate->_F |= NF;                                   \
	if( (cpustate->_C + io + 1) & 0x100 ) cpustate->_F |= HF | CF;                  \
	if( (irep_tmp1[cpustate->_C & 3][io & 3] ^                          \
			breg_tmp2[cpustate->_B] ^                                       \
			(cpustate->_C >> 2) ^                                           \
			(io >> 2)) & 1 )                                        \
		cpustate->_F |= PF;                                             \
}

/***************************************************************
 * LDD
 ***************************************************************/
#define LDD {                                                   \
	UINT8 val = RM(cpustate, cpustate->_HL);                                         \
	WM(cpustate,  cpustate->_DE, val );                                              \
	cpustate->_F &= SF | ZF | CF;                                       \
	if( (cpustate->_A + val) & 0x02 ) cpustate->_F |= YF; /* bit 1 -> flag 5 */      \
	if( (cpustate->_A + val) & 0x08 ) cpustate->_F |= XF; /* bit 3 -> flag 3 */      \
	cpustate->_HL--; cpustate->_DE--; cpustate->_BC--;                                      \
	if( cpustate->_BC ) cpustate->_F |= VF;                                         \
}

/***************************************************************
 * CPD
 ***************************************************************/
#define CPD {                                                   \
	UINT8 val = RM(cpustate, cpustate->_HL);                                        \
	UINT8 res = cpustate->_A - val;                                     \
	cpustate->_HL--; cpustate->_BC--;                                               \
	cpustate->_F = (cpustate->_F & CF) | (SZ[res] & ~(YF|XF)) | ((cpustate->_A ^ val ^ res) & HF) | NF;  \
	if( cpustate->_F & HF ) res -= 1;                                   \
	if( res & 0x02 ) cpustate->_F |= YF; /* bit 1 -> flag 5 */          \
	if( res & 0x08 ) cpustate->_F |= XF; /* bit 3 -> flag 3 */          \
	if( cpustate->_BC ) cpustate->_F |= VF;                                         \
}

/***************************************************************
 * IND
 ***************************************************************/
#define IND {                                                   \
	UINT8 io = IN(cpustate, cpustate->_BC);                                         \
	WM(cpustate,  cpustate->_HL, io );                                              \
	cpustate->_B--;                                                     \
	cpustate->_HL--;                                                        \
	cpustate->_F = SZ[cpustate->_B];                                                \
	if( io & SF ) cpustate->_F |= NF;                                   \
	if( (cpustate->_C + io - 1) & 0x100 ) cpustate->_F |= HF | CF;                  \
	if( (drep_tmp1[cpustate->_C & 3][io & 3] ^                          \
			breg_tmp2[cpustate->_B] ^                                       \
			(cpustate->_C >> 2) ^                                           \
			(io >> 2)) & 1 )                                        \
		cpustate->_F |= PF;                                             \
}

#define INDW {                                                   \
	UINT16 io = IN16(cpustate, cpustate->_BC);                                         \
	union PAIR tmp; tmp.w.l = io;												\
	WM16(cpustate,  cpustate->_HL, &tmp );                                              \
	cpustate->_B--;                                                     \
	cpustate->_HL -= 2;                                                        \
	cpustate->_F = SZ[cpustate->_B];                                                \
	if( io & SF ) cpustate->_F |= NF;                                   \
	if( (cpustate->_C + io - 1) & 0x100 ) cpustate->_F |= HF | CF;                  \
	if( (drep_tmp1[cpustate->_C & 3][io & 3] ^                          \
			breg_tmp2[cpustate->_B] ^                                       \
			(cpustate->_C >> 2) ^                                           \
			(io >> 2)) & 1 )                                        \
		cpustate->_F |= PF;                                             \
}

/***************************************************************
 * OUTD
 ***************************************************************/
#define OUTD {                                                  \
	UINT8 io = RM(cpustate, cpustate->_HL);                                         \
	OUT(cpustate,  cpustate->_BC, io );                                             \
	cpustate->_B--;                                                     \
	cpustate->_HL--;                                                        \
	cpustate->_F = SZ[cpustate->_B];                                                \
	if( io & SF ) cpustate->_F |= NF;                                   \
	if( (cpustate->_C + io - 1) & 0x100 ) cpustate->_F |= HF | CF;                  \
	if( (drep_tmp1[cpustate->_C & 3][io & 3] ^                          \
			breg_tmp2[cpustate->_B] ^                                       \
			(cpustate->_C >> 2) ^                                           \
			(io >> 2)) & 1 )                                        \
		cpustate->_F |= PF;                                             \
}

#define OUTDW {                                                  \
	union PAIR tmp;                                                 \
	RM16(cpustate, cpustate->_HL, &tmp);                                  \
	UINT16 io = tmp.w.l;												\
	OUT16(cpustate,  cpustate->_BC, io );                                             \
	cpustate->_B--;                                                     \
	cpustate->_HL--;                                                        \
	cpustate->_F = SZ[cpustate->_B];                                                \
	if( io & SF ) cpustate->_F |= NF;                                   \
	if( (cpustate->_C + io - 1) & 0x100 ) cpustate->_F |= HF | CF;                  \
	if( (drep_tmp1[cpustate->_C & 3][io & 3] ^                          \
			breg_tmp2[cpustate->_B] ^                                       \
			(cpustate->_C >> 2) ^                                           \
			(io >> 2)) & 1 )                                        \
		cpustate->_F |= PF;                                             \
}

/***************************************************************
 * LDIR
 ***************************************************************/
#define LDIR                                                    \
	LDI;                                                        \
	if( cpustate->_BC )                                                 \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0xb0);                                            \
	}

/***************************************************************
 * CPIR
 ***************************************************************/
#define CPIR                                                    \
	CPI;                                                        \
	if( cpustate->_BC && !(cpustate->_F & ZF) )                                     \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0xb1);                                            \
	}

/***************************************************************
 * INIR
 ***************************************************************/
#define INIR                                                    \
	INI;                                                        \
	if( cpustate->_B )                                                  \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0xb2);                                            \
	}

#define INIRW                                                    \
	INIW;                                                        \
	if( cpustate->_B )                                                  \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0x92);                                            \
	}

/***************************************************************
 * OTIR
 ***************************************************************/
#define OTIR                                                    \
	OUTI;                                                       \
	if( cpustate->_B )                                                  \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0xb3);                                            \
	}

#define OTIRW                                                    \
	OUTIW;                                                       \
	if( cpustate->_B )                                                  \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0x93);                                            \
	}

/***************************************************************
 * LDDR
 ***************************************************************/
#define LDDR                                                    \
	LDD;                                                        \
	if( cpustate->_BC )                                                 \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0xb8);                                            \
	}

/***************************************************************
 * CPDR
 ***************************************************************/
#define CPDR                                                    \
	CPD;                                                        \
	if( cpustate->_BC && !(cpustate->_F & ZF) )                                     \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0xb9);                                            \
	}

/***************************************************************
 * INDR
 ***************************************************************/
#define INDR                                                    \
	IND;                                                        \
	if( cpustate->_B )                                                  \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0xba);                                            \
	}

#define INDRW                                                    \
	INDW;                                                        \
	if( cpustate->_B )                                                  \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0x9a);                                            \
	}

/***************************************************************
 * OTDR
 ***************************************************************/
#define OTDR                                                    \
	OUTD;                                                       \
	if( cpustate->_B )                                                  \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0xbb);                                            \
	}

#define OTDRW                                                    \
	OUTDW;                                                       \
	if( cpustate->_B )                                                  \
	{                                                           \
		cpustate->_PC -= 2;                                             \
		MSR(cpustate) &= ~Z280_MSR_SSP;                         \
		CC(ex,0x9b);                                            \
	}

/***************************************************************
 * EI
 ***************************************************************/
#define EI(val) {                                                    \
	CHECK_PRIV(cpustate)												  \
	{															\
		MSR(cpustate) |= val&0x7f;						\
		cpustate->after_EI = 1;                                         \
	}															\
}

#define DI(val) {  \
	CHECK_PRIV(cpustate)												  \
	{															\
		MSR(cpustate) &= ~(val&0x7f);						\
	}															\
}
