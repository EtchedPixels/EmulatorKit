/*****************************************************************************
 *
 *   z280dasm.c
 *   Portable Z8x280 disassembler
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License
 *     as published by the Free Software Foundation; either version 2
 *     of the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *
 *****************************************************************************/

/*   Copyright Juergen Buchmueller, all rights reserved. */
/*   Copyright Michal Tomek 2021 z280emu */

#include <stdio.h>
//#include "emu.h"
//#include "debugger.h"
#include "z280.h"

enum e_mnemonics {
	zADC   ,zADD   ,zADDW  ,zAND   ,zBIT   ,zCALL  ,zCCF   ,zCP    ,zCPD   ,
	zCPDR  ,zCPI   ,zCPIR  ,zCPL   ,zCPW   ,zDAA   ,zDB    ,zDEC   ,zDECW  ,zDI    ,
	zDIV   ,zDIVU  ,zDIVUW ,zDIVW  ,
	zDJNZ  ,zEI    ,zEPUF  ,zEPUI  ,zEPUM  ,zEX    ,zEXTS  ,zEXX   ,zHLT   ,zIM    ,zIN    ,zINW   ,
	zINC   ,zINCW  ,zIND   ,zINDR  ,zINDRW ,zINDW  ,zINI   ,zINIR  ,zINIRW ,zINIW  ,
	zJAR   ,zJAF   ,zJP    ,zJR    ,zLD    ,zLDA   ,zLDCTL ,
	zLDD   ,zLDDR  ,zLDI   ,zLDIR  ,zLDUD  ,zLDUP  ,
	zMEPU  ,zMULT  ,zMULTU ,zMULTUW,zMULTW ,
	zNEG   ,zNOP   ,zOR    ,
	zOTDR  ,zOTDRW ,zOTIR  ,zOTIRW ,
	zOUT   ,zOUTD  ,zOUTDW ,zOUTI  ,zOUTIW ,zOUTW  ,
	zPCACHE,zPOP   ,zPUSH  ,zRES   ,
	zRET   ,zRETI  ,zRETIL ,zRETN  ,
	zRL    ,zRLA   ,zRLC   ,zRLCA  ,zRLD   ,zRR    ,zRRA   ,zRRC   ,
	zRRCA  ,zRRD   ,zRST   ,zSBC   ,zSC    ,zSCF   ,zSET   ,zSLA   ,
	zSRA   ,zSRL   ,zSUB   ,zSUBW  ,zTSET  ,zTSTI  ,zXOR
};

static const char *const s_mnemonic[] = {
	"adc"  ,"add"  ,"addw" ,"and"  ,"bit"  ,"call" ,"ccf"  ,"cp"   ,"cpd"  ,
	"cpdr" ,"cpi"  ,"cpir" ,"cpl"  ,"cpw"  ,"daa"  ,"db"   ,"dec"  ,"decw" ,"di"   ,
	"div"  ,"divu" ,"divuw","divw" ,
	"djnz" ,"ei"   ,"epuf" ,"epui" ,"epum" ,"ex"   ,"exts" ,"exx"  ,"halt" ,"im"   ,"in"   ,"inw"  ,
	"inc"  ,"incw" ,"ind"  ,"indr" ,"indrw","indw" ,"ini"  ,"inir" ,"inirw","iniw" ,
	"jar"  ,"jaf"  ,"jp"   ,"jr"   ,"ld"   ,"lda"  ,"ldctl",
	"ldd"  ,"lddr" ,"ldi"  ,"ldir" ,"ldud" ,"ldup" ,
	"mepu" ,"mult" ,"multu","multuw","multw",
	"neg"  ,"nop"  ,"or"   ,
	"otdr" ,"otdrw","otir" ,"otirw",
	"out"  ,"outd" ,"outdw","outi" ,"outiw","outw" ,
	"pcache","pop" ,"push" ,"res"  ,
	"ret"  ,"reti" ,"retil","retn" ,
	"rl"   ,"rla"  ,"rlc"  ,"rlca" ,"rld"  ,"rr"   ,"rra"  ,"rrc"  ,
	"rrca" ,"rrd"  ,"rst"  ,"sbc"  ,"sc"   ,"scf"  ,"set"  ,"sla"  ,
	"sra"  ,"srl"  ,"sub"  ,"subw", "tset" ,"tsti" ,"xor"
};

struct z80dasm {
	UINT8 mnemonic;
	const char *arguments;
};

static const struct z80dasm mnemonic_xx_cb[256]= {
	{zRLC,"'Y"},   {zRLC,"'Y"},   {zRLC,"'Y"},   {zRLC,"'Y"},
	{zRLC,"'Y"},   {zRLC,"'Y"},   {zRLC,"Y"},     {zRLC,"'Y"},
	{zRRC,"'Y"},   {zRRC,"'Y"},   {zRRC,"'Y"},   {zRRC,"'Y"},
	{zRRC,"'Y"},   {zRRC,"'Y"},   {zRRC,"Y"},     {zRRC,"'Y"},
	{zRL,"'Y"},    {zRL,"'Y"},    {zRL,"'Y"},    {zRL,"'Y"},
	{zRL,"'Y"},    {zRL,"'Y"},    {zRL,"Y"},      {zRL,"'Y"},
	{zRR,"'Y"},    {zRR,"'Y"},    {zRR,"'Y"},    {zRR,"'Y"},
	{zRR,"'Y"},    {zRR,"'Y"},    {zRR,"Y"},      {zRR,"'Y"},
	{zSLA,"'Y"},   {zSLA,"'Y"},   {zSLA,"'Y"},   {zSLA,"'Y"},
	{zSLA,"'Y"},   {zSLA,"'Y"},   {zSLA,"Y"},     {zSLA,"'Y"},
	{zSRA,"'Y"},   {zSRA,"'Y"},   {zSRA,"'Y"},   {zSRA,"'Y"},
	{zSRA,"'Y"},   {zSRA,"'Y"},   {zSRA,"Y"},     {zSRA,"'Y"},
	{zTSET,"'Y"},  {zTSET,"'Y"},  {zTSET,"'Y"},  {zTSET,"'Y"},
	{zTSET,"'Y"},  {zTSET,"'Y"},  {zTSET,"Y"},    {zTSET,"'Y"},
	{zSRL,"'Y"},   {zSRL,"'Y"},   {zSRL,"'Y"},   {zSRL,"'Y"},
	{zSRL,"'Y"},   {zSRL,"'Y"},   {zSRL,"Y"},     {zSRL,"'Y"},
	{zBIT,"'0,Y"}, {zBIT,"'0,Y"}, {zBIT,"'0,Y"}, {zBIT,"'0,Y"},
	{zBIT,"'0,Y"}, {zBIT,"'0,Y"}, {zBIT,"0,Y"},   {zBIT,"'0,Y"},
	{zBIT,"'1,Y"}, {zBIT,"'1,Y"}, {zBIT,"'1,Y"}, {zBIT,"'1,Y"},
	{zBIT,"'1,Y"}, {zBIT,"'1,Y"}, {zBIT,"1,Y"},   {zBIT,"'1,Y"},
	{zBIT,"'2,Y"}, {zBIT,"'2,Y"}, {zBIT,"'2,Y"}, {zBIT,"'2,Y"},
	{zBIT,"'2,Y"}, {zBIT,"'2,Y"}, {zBIT,"2,Y"},   {zBIT,"'2,Y"},
	{zBIT,"'3,Y"}, {zBIT,"'3,Y"}, {zBIT,"'3,Y"}, {zBIT,"'3,Y"},
	{zBIT,"'3,Y"}, {zBIT,"'3,Y"}, {zBIT,"3,Y"},   {zBIT,"'3,Y"},
	{zBIT,"'4,Y"}, {zBIT,"'4,Y"}, {zBIT,"'4,Y"}, {zBIT,"'4,Y"},
	{zBIT,"'4,Y"}, {zBIT,"'4,Y"}, {zBIT,"4,Y"},   {zBIT,"'4,Y"},
	{zBIT,"'5,Y"}, {zBIT,"'5,Y"}, {zBIT,"'5,Y"}, {zBIT,"'5,Y"},
	{zBIT,"'5,Y"}, {zBIT,"'5,Y"}, {zBIT,"5,Y"},   {zBIT,"'5,Y"},
	{zBIT,"'6,Y"}, {zBIT,"'6,Y"}, {zBIT,"'6,Y"}, {zBIT,"'6,Y"},
	{zBIT,"'6,Y"}, {zBIT,"'6,Y"}, {zBIT,"6,Y"},   {zBIT,"'6,Y"},
	{zBIT,"'7,Y"}, {zBIT,"'7,Y"}, {zBIT,"'7,Y"}, {zBIT,"'7,Y"},
	{zBIT,"'7,Y"}, {zBIT,"'7,Y"}, {zBIT,"7,Y"},   {zBIT,"'7,Y"},
	{zRES,"'0,Y"}, {zRES,"'0,Y"}, {zRES,"'0,Y"}, {zRES,"'0,Y"},
	{zRES,"'0,Y"}, {zRES,"'0,Y"}, {zRES,"0,Y"},   {zRES,"'0,Y"},
	{zRES,"'1,Y"}, {zRES,"'1,Y"}, {zRES,"'1,Y"}, {zRES,"'1,Y"},
	{zRES,"'1,Y"}, {zRES,"'1,Y"}, {zRES,"1,Y"},   {zRES,"'1,Y"},
	{zRES,"'2,Y"}, {zRES,"'2,Y"}, {zRES,"'2,Y"}, {zRES,"'2,Y"},
	{zRES,"'2,Y"}, {zRES,"'2,Y"}, {zRES,"2,Y"},   {zRES,"'2,Y"},
	{zRES,"'3,Y"}, {zRES,"'3,Y"}, {zRES,"'3,Y"}, {zRES,"'3,Y"},
	{zRES,"'3,Y"}, {zRES,"'3,Y"}, {zRES,"3,Y"},   {zRES,"'3,Y"},
	{zRES,"'4,Y"}, {zRES,"'4,Y"}, {zRES,"'4,Y"}, {zRES,"'4,Y"},
	{zRES,"'4,Y"}, {zRES,"'4,Y"}, {zRES,"4,Y"},   {zRES,"'4,Y"},
	{zRES,"'5,Y"}, {zRES,"'5,Y"}, {zRES,"'5,Y"}, {zRES,"'5,Y"},
	{zRES,"'5,Y"}, {zRES,"'5,Y"}, {zRES,"5,Y"},   {zRES,"'5,Y"},
	{zRES,"'6,Y"}, {zRES,"'6,Y"}, {zRES,"'6,Y"}, {zRES,"'6,Y"},
	{zRES,"'6,Y"}, {zRES,"'6,Y"}, {zRES,"6,Y"},   {zRES,"'6,Y"},
	{zRES,"'7,Y"}, {zRES,"'7,Y"}, {zRES,"'7,Y"}, {zRES,"'7,Y"},
	{zRES,"'7,Y"}, {zRES,"'7,Y"}, {zRES,"7,Y"},   {zRES,"'7,Y"},
	{zSET,"'0,Y"}, {zSET,"'0,Y"}, {zSET,"'0,Y"}, {zSET,"'0,Y"},
	{zSET,"'0,Y"}, {zSET,"'0,Y"}, {zSET,"0,Y"},   {zSET,"'0,Y"},
	{zSET,"'1,Y"}, {zSET,"'1,Y"}, {zSET,"'1,Y"}, {zSET,"'1,Y"},
	{zSET,"'1,Y"}, {zSET,"'1,Y"}, {zSET,"1,Y"},   {zSET,"'1,Y"},
	{zSET,"'2,Y"}, {zSET,"'2,Y"}, {zSET,"'2,Y"}, {zSET,"'2,Y"},
	{zSET,"'2,Y"}, {zSET,"'2,Y"}, {zSET,"2,Y"},   {zSET,"'2,Y"},
	{zSET,"'3,Y"}, {zSET,"'3,Y"}, {zSET,"'3,Y"}, {zSET,"'3,Y"},
	{zSET,"'3,Y"}, {zSET,"'3,Y"}, {zSET,"3,Y"},   {zSET,"'3,Y"},
	{zSET,"'4,Y"}, {zSET,"'4,Y"}, {zSET,"'4,Y"}, {zSET,"'4,Y"},
	{zSET,"'4,Y"}, {zSET,"'4,Y"}, {zSET,"4,Y"},   {zSET,"'4,Y"},
	{zSET,"'5,Y"}, {zSET,"'5,Y"}, {zSET,"'5,Y"}, {zSET,"'5,Y"},
	{zSET,"'5,Y"}, {zSET,"'5,Y"}, {zSET,"5,Y"},   {zSET,"'5,Y"},
	{zSET,"'6,Y"}, {zSET,"'6,Y"}, {zSET,"'6,Y"}, {zSET,"'6,Y"},
	{zSET,"'6,Y"}, {zSET,"'6,Y"}, {zSET,"6,Y"},   {zSET,"'6,Y"},
	{zSET,"'7,Y"}, {zSET,"'7,Y"}, {zSET,"'7,Y"}, {zSET,"'7,Y"},
	{zSET,"'7,Y"}, {zSET,"'7,Y"}, {zSET,"7,Y"},   {zSET,"'7,Y"}
};

static const struct z80dasm mnemonic_cb[256] = {
	{zRLC,"b"},     {zRLC,"c"},     {zRLC,"d"},     {zRLC,"e"},
	{zRLC,"h"},     {zRLC,"l"},     {zRLC,"(hl)"},  {zRLC,"a"},
	{zRRC,"b"},     {zRRC,"c"},     {zRRC,"d"},     {zRRC,"e"},
	{zRRC,"h"},     {zRRC,"l"},     {zRRC,"(hl)"},  {zRRC,"a"},
	{zRL,"b"},      {zRL,"c"},      {zRL,"d"},      {zRL,"e"},
	{zRL,"h"},      {zRL,"l"},      {zRL,"(hl)"},   {zRL,"a"},
	{zRR,"b"},      {zRR,"c"},      {zRR,"d"},      {zRR,"e"},
	{zRR,"h"},      {zRR,"l"},      {zRR,"(hl)"},   {zRR,"a"},
	{zSLA,"b"},     {zSLA,"c"},     {zSLA,"d"},     {zSLA,"e"},
	{zSLA,"h"},     {zSLA,"l"},     {zSLA,"(hl)"},  {zSLA,"a"},
	{zSRA,"b"},     {zSRA,"c"},     {zSRA,"d"},     {zSRA,"e"},
	{zSRA,"h"},     {zSRA,"l"},     {zSRA,"(hl)"},  {zSRA,"a"},
	{zTSET,"b"},    {zTSET,"c"},    {zTSET,"d"},    {zTSET,"e"},
	{zTSET,"h"},    {zTSET,"l"},    {zTSET,"(hl)"}, {zTSET,"a"},
	{zSRL,"b"},     {zSRL,"c"},     {zSRL,"d"},     {zSRL,"e"},
	{zSRL,"h"},     {zSRL,"l"},     {zSRL,"(hl)"},  {zSRL,"a"},
	{zBIT,"0,b"},   {zBIT,"0,c"},   {zBIT,"0,d"},   {zBIT,"0,e"},
	{zBIT,"0,h"},   {zBIT,"0,l"},   {zBIT,"0,(hl)"},{zBIT,"0,a"},
	{zBIT,"1,b"},   {zBIT,"1,c"},   {zBIT,"1,d"},   {zBIT,"1,e"},
	{zBIT,"1,h"},   {zBIT,"1,l"},   {zBIT,"1,(hl)"},{zBIT,"1,a"},
	{zBIT,"2,b"},   {zBIT,"2,c"},   {zBIT,"2,d"},   {zBIT,"2,e"},
	{zBIT,"2,h"},   {zBIT,"2,l"},   {zBIT,"2,(hl)"},{zBIT,"2,a"},
	{zBIT,"3,b"},   {zBIT,"3,c"},   {zBIT,"3,d"},   {zBIT,"3,e"},
	{zBIT,"3,h"},   {zBIT,"3,l"},   {zBIT,"3,(hl)"},{zBIT,"3,a"},
	{zBIT,"4,b"},   {zBIT,"4,c"},   {zBIT,"4,d"},   {zBIT,"4,e"},
	{zBIT,"4,h"},   {zBIT,"4,l"},   {zBIT,"4,(hl)"},{zBIT,"4,a"},
	{zBIT,"5,b"},   {zBIT,"5,c"},   {zBIT,"5,d"},   {zBIT,"5,e"},
	{zBIT,"5,h"},   {zBIT,"5,l"},   {zBIT,"5,(hl)"},{zBIT,"5,a"},
	{zBIT,"6,b"},   {zBIT,"6,c"},   {zBIT,"6,d"},   {zBIT,"6,e"},
	{zBIT,"6,h"},   {zBIT,"6,l"},   {zBIT,"6,(hl)"},{zBIT,"6,a"},
	{zBIT,"7,b"},   {zBIT,"7,c"},   {zBIT,"7,d"},   {zBIT,"7,e"},
	{zBIT,"7,h"},   {zBIT,"7,l"},   {zBIT,"7,(hl)"},{zBIT,"7,a"},
	{zRES,"0,b"},   {zRES,"0,c"},   {zRES,"0,d"},   {zRES,"0,e"},
	{zRES,"0,h"},   {zRES,"0,l"},   {zRES,"0,(hl)"},{zRES,"0,a"},
	{zRES,"1,b"},   {zRES,"1,c"},   {zRES,"1,d"},   {zRES,"1,e"},
	{zRES,"1,h"},   {zRES,"1,l"},   {zRES,"1,(hl)"},{zRES,"1,a"},
	{zRES,"2,b"},   {zRES,"2,c"},   {zRES,"2,d"},   {zRES,"2,e"},
	{zRES,"2,h"},   {zRES,"2,l"},   {zRES,"2,(hl)"},{zRES,"2,a"},
	{zRES,"3,b"},   {zRES,"3,c"},   {zRES,"3,d"},   {zRES,"3,e"},
	{zRES,"3,h"},   {zRES,"3,l"},   {zRES,"3,(hl)"},{zRES,"3,a"},
	{zRES,"4,b"},   {zRES,"4,c"},   {zRES,"4,d"},   {zRES,"4,e"},
	{zRES,"4,h"},   {zRES,"4,l"},   {zRES,"4,(hl)"},{zRES,"4,a"},
	{zRES,"5,b"},   {zRES,"5,c"},   {zRES,"5,d"},   {zRES,"5,e"},
	{zRES,"5,h"},   {zRES,"5,l"},   {zRES,"5,(hl)"},{zRES,"5,a"},
	{zRES,"6,b"},   {zRES,"6,c"},   {zRES,"6,d"},   {zRES,"6,e"},
	{zRES,"6,h"},   {zRES,"6,l"},   {zRES,"6,(hl)"},{zRES,"6,a"},
	{zRES,"7,b"},   {zRES,"7,c"},   {zRES,"7,d"},   {zRES,"7,e"},
	{zRES,"7,h"},   {zRES,"7,l"},   {zRES,"7,(hl)"},{zRES,"7,a"},
	{zSET,"0,b"},   {zSET,"0,c"},   {zSET,"0,d"},   {zSET,"0,e"},
	{zSET,"0,h"},   {zSET,"0,l"},   {zSET,"0,(hl)"},{zSET,"0,a"},
	{zSET,"1,b"},   {zSET,"1,c"},   {zSET,"1,d"},   {zSET,"1,e"},
	{zSET,"1,h"},   {zSET,"1,l"},   {zSET,"1,(hl)"},{zSET,"1,a"},
	{zSET,"2,b"},   {zSET,"2,c"},   {zSET,"2,d"},   {zSET,"2,e"},
	{zSET,"2,h"},   {zSET,"2,l"},   {zSET,"2,(hl)"},{zSET,"2,a"},
	{zSET,"3,b"},   {zSET,"3,c"},   {zSET,"3,d"},   {zSET,"3,e"},
	{zSET,"3,h"},   {zSET,"3,l"},   {zSET,"3,(hl)"},{zSET,"3,a"},
	{zSET,"4,b"},   {zSET,"4,c"},   {zSET,"4,d"},   {zSET,"4,e"},
	{zSET,"4,h"},   {zSET,"4,l"},   {zSET,"4,(hl)"},{zSET,"4,a"},
	{zSET,"5,b"},   {zSET,"5,c"},   {zSET,"5,d"},   {zSET,"5,e"},
	{zSET,"5,h"},   {zSET,"5,l"},   {zSET,"5,(hl)"},{zSET,"5,a"},
	{zSET,"6,b"},   {zSET,"6,c"},   {zSET,"6,d"},   {zSET,"6,e"},
	{zSET,"6,h"},   {zSET,"6,l"},   {zSET,"6,(hl)"},{zSET,"6,a"},
	{zSET,"7,b"},   {zSET,"7,c"},   {zSET,"7,d"},   {zSET,"7,e"},
	{zSET,"7,h"},   {zSET,"7,l"},   {zSET,"7,(hl)"},{zSET,"7,a"}
};

static const struct z80dasm mnemonic_ed[256]= {
	{zDB,"?"},      {zDB,"?"},      {zLDA,"hl,(spD)"},  {zLD,"(spD),a"},
	{zLD,"hl,(spD)"},{zLD,"(spD),hl"},   {zLD,"bc,(hl)"},      {zEX,"a,b"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"hl,(hl+ix)"},  {zLD,"(hl+ix),a"},
	{zLD,"hl,(hl+ix)"},{zLD,"(hl+ix),hl"},   {zLD,"(hl),bc"},      {zEX,"a,c"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"hl,(hl+iy)"},  {zLD,"(hl+iy),a"},
	{zLD,"hl,(hl+iy)"},{zLD,"(hl+iy),hl"},   {zLD,"de,(hl)"},      {zEX,"a,d"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"hl,(ix+iy)"},  {zLD,"(ix+iy),a"},
	{zLD,"hl,(ix+iy)"},{zLD,"(ix+iy),hl"},   {zLD,"(hl),de"},      {zEX,"a,e"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"hl,(Q)"},  {zLD,"(Q),a"},
	{zLD,"hl,(Q)"},{zLD,"(Q),hl"},   {zLD,"hl,(hl)"},      {zEX,"a,h"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"hl,(ixD)"},  {zLD,"(ixD),a"},
	{zLD,"hl,(ixD)"},{zLD,"(ixD),hl"},   {zLD,"(hl),hl"},      {zEX,"a,l"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"hl,(iyD)"},  {zLD,"(iyD),a"},
	{zLD,"hl,(iyD)"},{zLD,"(iyD),hl"},   {zLD,"sp,(hl)"},      {zEX,"a,(hl)"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"hl,(hlD)"},  {zLD,"(hlD),a"},
	{zLD,"hl,(hlD)"},{zLD,"(hlD),hl"},   {zLD,"(hl),sp"},      {zEX,"a,a"},

	{zIN,"b,(c)"},  {zOUT,"(c),b"}, {zSBC,"hl,bc"}, {zLD,"(W),bc"},
	{zNEG,0},       {zRETN,0},      {zIM,"0"},      {zLD,"i,a"},
	{zIN,"c,(c)"},  {zOUT,"(c),c"}, {zADC,"hl,bc"}, {zLD,"bc,(W)"},
	{zNEG,"hl"},    {zRETI,0},      {zIM,"3"},      {zLD,"r,a"},
	{zIN,"d,(c)"},  {zOUT,"(c),d"}, {zSBC,"hl,de"}, {zLD,"(W),de"},
	{zDB,"?"},      {zRETIL,0},     {zIM,"1"},      {zLD,"a,i"},
	{zIN,"e,(c)"},  {zOUT,"(c),e"}, {zADC,"hl,de"}, {zLD,"de,(W)"},
	{zDB,"?"},      {zDB,"?"},      {zIM,"2"},      {zLD,"a,r"},
	{zIN,"h,(c)"},  {zOUT,"(c),h"}, {zSBC,"hl,hl"}, {zDB,"?"},
	{zEXTS,"a"},    {zPCACHE,0},    {zLDCTL,"hl,(c)"},{zRRD,"(hl)"},
	{zIN,"l,(c)"},  {zOUT,"(c),l"}, {zADC,"hl,hl"}, {zDB,"?"},
	{zEXTS,"hl"},   {zADD,"hl,a"},  {zLDCTL,"(c),hl"},{zRLD,"(hl)"},
	{zTSTI,"(c)"},  {zSC,"N"},      {zSBC,"hl,sp"}, {zLD,"(W),sp"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDI,"B"},
	{zIN,"a,(c)"},  {zOUT,"(c),a"}, {zADC,"hl,sp"}, {zLD,"sp,(W)"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zEI,"B"},

	{zDB,"?"},      {zDB,"?"},      {zINIW,0},      {zOUTIW,0},
	{zEPUM,"(spD)"},{zMEPU,"(spD)"},{zLDUD,"a,(hl)"},{zLDCTL,"hl,usp"},
	{zDB,"?"},      {zDB,"?"},      {zINDW,0},      {zOUTDW,0},
	{zEPUM,"(hl+ix)"},{zMEPU,"(hl+ix)"},{zLDUD,"(hl),a"},{zLDCTL,"usp,hl"},
	{zDB,"?"},      {zDB,"?"},      {zINIRW,0},     {zOTIRW,0},
	{zEPUM,"(hl+iy)"},{zMEPU,"(hl+iy)"},{zLDUP,"a,(hl)"},{zEPUF,0},
	{zDB,"?"},      {zDB,"?"},      {zINDRW,0},     {zOTDRW,0},
	{zEPUM,"(ix+iy)"},{zMEPU,"(ix+iy)"},{zLDUP,"(hl),a"},{zEPUI,0},
	{zLDI,0},       {zCPI,0},       {zINI,0},       {zOUTI,0},
	{zEPUM,"(Q)"},  {zMEPU,"(Q)"},  {zEPUM,"(hl)"}, {zEPUM,"(W)"},
	{zLDD,0},       {zCPD,0},       {zIND,0},       {zOUTD,0},
	{zEPUM,"(ixD)"},{zMEPU,"(ixD)"},{zMEPU,"(hl)"}, {zMEPU,"(W)"},
	{zLDIR,0},      {zCPIR,0},      {zINIR,0},      {zOTIR,0},
	{zEPUM,"(iyD)"},{zMEPU,"(iyD)"},{zDB,"?"},      {zINW,"hl,(c)"},
	{zLDDR,0},      {zCPDR,0},      {zINDR,0},      {zOTDR,0},
	{zEPUM,"(hlD)"},{zMEPU,"(hlD)"},{zDB,"?"},      {zOUTW,"(c),hl"},

	{zMULT,"a,b"},  {zMULTU,"a,b"}, {zMULTW,"hl,bc"},{zMULTUW,"hl,bc"},
	{zDIV,"hl,b"},  {zDIVU,"hl,b"}, {zADDW,"hl,bc"},{zCPW,"hl,bc"},
	{zMULT,"a,c"},  {zMULTU,"a,c"}, {zDIVW,"dehl,bc"},{zDIVUW,"dehl,bc"},
	{zDIV,"hl,c"},  {zDIVU,"hl,c"}, {zSUBW,"hl,bc"},{zDB,"?"},
	{zMULT,"a,d"},  {zMULTU,"a,d"}, {zMULTW,"hl,de"},{zMULTUW,"hl,de"},
	{zDIV,"hl,d"},  {zDIVU,"hl,d"}, {zADDW,"hl,de"},{zCPW,"hl,de"},
	{zMULT,"a,e"},  {zMULTU,"a,e"}, {zDIVW,"dehl,de"},{zDIVUW,"dehl,de"},
	{zDIV,"hl,e"},  {zDIVU,"hl,e"}, {zSUBW,"hl,de"},{zDB,"?"},
	{zMULT,"a,h"},  {zMULTU,"a,h"}, {zMULTW,"hl,hl"},{zMULTUW,"hl,hl"},
	{zDIV,"hl,h"},  {zDIVU,"hl,h"}, {zADDW,"hl,hl"},{zCPW,"hl,hl"},
	{zMULT,"a,l"},  {zMULTU,"a,l"}, {zDIVW,"dehl,hl"},{zDIVUW,"dehl,hl"},
	{zDIV,"hl,l"},  {zDIVU,"hl,l"}, {zSUBW,"hl,hl"},{zEX,"h,l"},
	{zMULT,"a,(hl)"},{zMULTU,"a,(hl)"},{zMULTW,"hl,sp"},{zMULTUW,"hl,sp"},
	{zDIV,"hl,(hl)"},{zDIVU,"hl,(hl)"},{zADDW,"hl,sp"},{zCPW,"hl,sp"},
	{zMULT,"a,a"},  {zMULTU,"a,a"}, {zDIVW,"dehl,sp"},{zDIVUW,"dehl,sp"},
	{zDIV,"hl,a"},  {zDIVU,"hl,a"}, {zSUBW,"hl,sp"},{zDB,"?"}
};

static const struct z80dasm mnemonic_dd[256]= {
	{zDB,"?"},      {zLD,"(hl),N"}, {zDB,"?"},      {zINCW,"(hl)"},
	{zINC,"(spD)"},{zDEC,"(spD)"},{zLD,"(spD),B"},{zDB,"?"},
	{zDB,"?"},      {zADD,"I,bc"},  {zDB,"?"},      {zDECW,"(hl)"},
	{zINC,"(hl+ix)"},{zDEC,"(hl+ix)"},{zLD,"(hl+ix),B"},{zDB,"?"},
	{zDB,"?"},      {zLD,"(W),N"},  {zDB,"?"},      {zINCW,"(W)"},
	{zINC,"(hl+iy)"},{zDEC,"(hl+iy)"},{zLD,"(hl+iy),B"}, {zDB,"?"},
	{zDB,"?"},      {zADD,"I,de"},  {zDB,"?"},      {zDECW,"(W)"},
	{zINC,"(ix+iy)"},{zDEC,"(ix+iy)"},{zLD,"(ix+iy),B"}, {zDB,"?"},
	{zJAR,"O"},     {zLD,"I,N"},    {zLD,"(W),I"},  {zINC,"I"},
	{zINC,"Ih"},    {zDEC,"Ih"},    {zLD,"Ih,B"},   {zDB,"?"},
	{zJAF,"O"},     {zADD,"I,I"},   {zLD,"I,(W)"},  {zDEC,"I"},
	{zINC,"Il"},    {zDEC,"Il"},    {zLD,"Il,B"},   {zDB,"?"},
	{zDB,"?"},      {zLD,"(Q),N"},  {zDB,"?"},      {zINCW,"(Q)"},
	{zINC,"X"},     {zDEC,"X"},     {zLD,"X,B"},    {zDB,"?"},
	{zDB,"?"},      {zADD,"I,sp"},  {zDB,"?"},      {zDECW,"(Q)"},
	{zINC,"(W)"},   {zDEC,"(W)"},   {zLD,"(W),B"},  {zDB,"?"},

	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDB,"?"},
	{zLD,"b,Ih"},   {zLD,"b,Il"},   {zLD,"b,X"},    {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDB,"?"},
	{zLD,"c,Ih"},   {zLD,"c,Il"},   {zLD,"c,X"},    {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDB,"?"},
	{zLD,"d,Ih"},   {zLD,"d,Il"},   {zLD,"d,X"},    {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDB,"?"},
	{zLD,"e,Ih"},   {zLD,"e,Il"},   {zLD,"e,X"},    {zDB,"?"},
	{zLD,"Ih,b"},   {zLD,"Ih,c"},   {zLD,"Ih,d"},   {zLD,"Ih,e"},
	{zLD,"Ih,Ih"},  {zLD,"Ih,Il"},  {zLD,"h,X"},    {zLD,"Ih,a"},
	{zLD,"Il,b"},   {zLD,"Il,c"},   {zLD,"Il,d"},   {zLD,"Il,e"},
	{zLD,"Il,Ih"},  {zLD,"Il,Il"},  {zLD,"l,X"},    {zLD,"Il,a"},
	{zLD,"X,b"},    {zLD,"X,c"},    {zLD,"X,d"},    {zLD,"X,e"},
	{zLD,"X,h"},    {zLD,"X,l"},    {zDB,"?"},      {zLD,"X,a"},
	{zLD,"a,(spD)"},{zLD,"a,(hl+ix)"},{zLD,"a,(hl+iy)"},{zLD,"a,(ix+iy)"},
	{zLD,"a,Ih"},   {zLD,"a,Il"},   {zLD,"a,X"},    {zDB,"?"},

	{zADD,"a,(spD)"},{zADD,"a,(hl+ix)"},{zADD,"a,(hl+iy)"},{zADD,"a,(ix+iy)"},
	{zADD,"a,Ih"},  {zADD,"a,Il"},  {zADD,"a,X"},   {zADD,"a,(W)"},
	{zADC,"a,(spD)"},{zADC,"a,(hl+ix)"},{zADC,"a,(hl+iy)"},{zADC,"a,(ix+iy)"},
	{zADC,"a,Ih"},  {zADC,"a,Il"},  {zADC,"a,X"},   {zADC,"a,(W)"},
	{zSUB,"a,(spD)"},{zSUB,"a,(hl+ix)"},{zSUB,"a,(hl+iy)"},{zSUB,"a,(ix+iy)"},
	{zSUB,"Ih"},    {zSUB,"Il"},    {zSUB,"X"},     {zSUB,"a,(W)"},
	{zSBC,"a,(spD)"},{zSBC,"a,(hl+ix)"},{zSBC,"a,(hl+iy)"},{zSBC,"a,(ix+iy)"},
	{zSBC,"a,Ih"},  {zSBC,"a,Il"},  {zSBC,"a,X"},   {zSBC,"a,(W)"},
	{zAND,"a,(spD)"},{zAND,"a,(hl+ix)"},{zAND,"a,(hl+iy)"},{zAND,"a,(ix+iy)"},
	{zAND,"Ih"},    {zAND,"Il"},    {zAND,"X"},     {zAND,"a,(W)"},
	{zXOR,"a,(spD)"},{zXOR,"a,(hl+ix)"},{zXOR,"a,(hl+iy)"},{zXOR,"a,(ix+iy)"},
	{zXOR,"Ih"},    {zXOR,"Il"},    {zXOR,"X"},     {zXOR,"a,(W)"},
	{zOR,"a,(spD)"},{zOR,"a,(hl+ix)"},{zOR,"a,(hl+iy)"},{zOR,"a,(ix+iy)"},
	{zOR,"Ih"},     {zOR,"Il"},     {zOR,"X"},      {zOR,"a,(W)"},
	{zCP,"a,(spD)"},{zCP,"a,(hl+ix)"},{zCP,"a,(hl+iy)"},{zCP,"a,(ix+iy)"},
	{zCP,"Ih"},     {zCP,"Il"},     {zCP,"X"},      {zCP,"a,(W)"},

	{zDB,"?"},      {zPOP,"(hl)"},  {zJP,"nz,(hl)"},{zDB,"?"},
	{zCALL,"nz,(hl)"},{zPUSH,"(hl)"},{zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zJP,"z,(hl)"}, {zDB,"cb"},
	{zCALL,"z,(hl)"},{zCALL,"(hl)"},{zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zPOP,"(W)"},   {zJP,"nc,(hl)"},{zDB,"?"},
	{zCALL,"nc,(hl)"},{zPUSH,"(W)"},{zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zJP,"c,(hl)"}, {zDB,"?"},
	{zCALL,"c,(hl)"},{zDB,"?"},     {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zPOP,"I"},     {zJP,"po,(hl)"},{zEX,"(sp),I"},
	{zCALL,"po,(hl)"},{zPUSH,"I"},  {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zJP,"(I)"},    {zJP,"pe,(hl)"},{zEX,"I,hl"},
	{zCALL,"pe,(hl)"},{zDB,"ed"},   {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zPOP,"(Q)"},   {zJP,"p,(hl)"}, {zDB,"?"},
	{zCALL,"p,(hl)"},{zPUSH,"(Q)"}, {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zLD,"sp,I"},   {zJP,"m,(hl)"}, {zDB,"?"},
	{zCALL,"m,(hl)"},{zDB,"?"},     {zDB,"?"},      {zDB,"?"}
};

static const struct z80dasm mnemonic_fd[256]= {
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zINCW,"(ixD)"},
	{zINC,"(Q)"},   {zDEC,"(Q)"},   {zLD,"(Q),B"},  {zDB,"?"},
	{zDB,"?"},      {zADD,"I,bc"},  {zDB,"?"},      {zDECW,"(ixD)"},
	{zINC,"(ixD)"},{zDEC,"(ixD)"},{zLD,"(ixD),B"},{zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zINCW,"(iyD)"},
	{zINC,"(iyD)"},{zDEC,"(iyD)"},{zLD,"(iyD),B"},{zDB,"?"},
	{zDB,"?"},      {zADD,"I,de"},  {zDB,"?"},      {zDECW,"(iyD)"},
	{zINC,"(hlD)"},{zDEC,"(hlD)"},{zLD,"(hlD),B"},{zDB,"?"},
	{zDB,"?"},      {zLD,"I,N"},    {zLD,"(W),I"},  {zINC,"I"},
	{zINC,"Ih"},    {zDEC,"Ih"},    {zLD,"Ih,B"},   {zDB,"?"},
	{zDB,"?"},      {zADD,"I,I"},   {zLD,"I,(W)"},  {zDEC,"I"},
	{zINC,"Il"},    {zDEC,"Il"},    {zLD,"Il,B"},   {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDB,"?"},
	{zINC,"X"},     {zDEC,"X"},     {zLD,"X,B"},    {zDB,"?"},
	{zDB,"?"},      {zADD,"I,sp"},  {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDB,"?"},

	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDB,"?"},
	{zLD,"b,Ih"},   {zLD,"b,Il"},   {zLD,"b,X"},    {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDB,"?"},
	{zLD,"c,Ih"},   {zLD,"c,Il"},   {zLD,"c,X"},    {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDB,"?"},
	{zLD,"d,Ih"},   {zLD,"d,Il"},   {zLD,"d,X"},    {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDB,"?"},
	{zLD,"e,Ih"},   {zLD,"e,Il"},   {zLD,"e,X"},    {zDB,"?"},
	{zLD,"Ih,b"},   {zLD,"Ih,c"},   {zLD,"Ih,d"},   {zLD,"Ih,e"},
	{zLD,"Ih,Ih"},  {zLD,"Ih,Il"},  {zLD,"h,X"},    {zLD,"Ih,a"},
	{zLD,"Il,b"},   {zLD,"Il,c"},   {zLD,"Il,d"},   {zLD,"Il,e"},
	{zLD,"Il,Ih"},  {zLD,"Il,Il"},  {zLD,"l,X"},    {zLD,"Il,a"},
	{zLD,"X,b"},    {zLD,"X,c"},    {zLD,"X,d"},    {zLD,"X,e"},
	{zLD,"X,h"},    {zLD,"X,l"},    {zDB,"?"},      {zLD,"X,a"},
	{zLD,"a,(Q)"},  {zLD,"a,(ixD)"},{zLD,"a,(iyD)"},{zLD,"a,(hlD)"},
	{zLD,"a,Ih"},   {zLD,"a,Il"},   {zLD,"a,X"},    {zDB,"?"},

	{zADD,"a,(Q)"}, {zADD,"a,(ixD)"},{zADD,"a,(iyD)"},{zADD,"a,(HLD)"},
	{zADD,"a,Ih"},  {zADD,"a,Il"},  {zADD,"a,X"},   {zDB,"?"},
	{zADC,"a,(Q)"}, {zADC,"a,(ixD)"},{zADC,"a,(iyD)"},{zADC,"a,(HLD)"},
	{zADC,"a,Ih"},  {zADC,"a,Il"},  {zADC,"a,X"},   {zDB,"?"},
	{zSUB,"a,(Q)"}, {zSUB,"a,(ixD)"},{zSUB,"a,(iyD)"},{zSUB,"a,(HLD)"},
	{zSUB,"Ih"},    {zSUB,"Il"},    {zSUB,"X"},     {zDB,"?"},
	{zSBC,"a,(Q)"}, {zSBC,"a,(ixD)"},{zSBC,"a,(iyD)"},{zSBC,"a,(HLD)"},
	{zSBC,"a,Ih"},  {zSBC,"a,Il"},  {zSBC,"a,X"},   {zDB,"?"},
	{zAND,"a,(Q)"}, {zAND,"a,(ixD)"},{zAND,"a,(iyD)"},{zAND,"a,(HLD)"},
	{zAND,"Ih"},    {zAND,"Il"},    {zAND,"X"},     {zDB,"?"},
	{zXOR,"a,(Q)"}, {zXOR,"a,(ixD)"},{zXOR,"a,(iyD)"},{zXOR,"a,(HLD)"},
	{zXOR,"Ih"},    {zXOR,"Il"},    {zXOR,"X"},     {zDB,"?"},
	{zOR,"a,(Q)"},  {zOR,"a,(ixD)"},{zOR,"a,(iyD)"},{zOR,"a,(HLD)"},
	{zOR,"Ih"},     {zOR,"Il"},     {zOR,"X"},      {zDB,"?"},
	{zCP,"a,(Q)"},  {zCP,"a,(ixD)"},{zCP,"a,(iyD)"},{zCP,"a,(HLD)"},
	{zCP,"Ih"},     {zCP,"Il"},     {zCP,"X"},      {zDB,"?"},

	{zDB,"?"},      {zDB,"?"},      {zJP,"nz,(Q)"}, {zJP,"(Q)"},
	{zCALL,"nz,(Q)"},{zDB,"?"},     {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zJP,"z,(Q)"},  {zDB,"cb"},
	{zCALL,"z,(Q)"},{zCALL,"(Q)"},  {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zJP,"nc,(Q)"}, {zDB,"?"},
	{zCALL,"nc,(Q)"},{zDB,"?"},     {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zJP,"c,(Q)"},  {zDB,"?"},
	{zCALL,"c,(Q)"},{zDB,"?"},      {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zPOP,"I"},     {zJP,"po,(Q)"}, {zEX,"(sp),I"},
	{zCALL,"po,(Q)"},{zPUSH,"I"},   {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zJP,"(I)"},    {zJP,"pe,(Q)"}, {zEX,"I,hl"},
	{zCALL,"pe,(Q)"},{zDB,"ed"},    {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zDB,"?"},      {zJP,"p,(Q)"},  {zDB,"?"},
	{zCALL,"p,(Q)"},{zPUSH,"N"},    {zDB,"?"},      {zDB,"?"},
	{zDB,"?"},      {zLD,"sp,I"},   {zJP,"m,(Q)"},  {zDB,"?"},
	{zCALL,"m,(Q)"},{zDB,"?"},      {zDB,"?"},      {zDB,"?"}
};

static const struct z80dasm mnemonic_dd_ed[256]= {
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(spD)"},  {zLD,"'(spD),a"},
	{zLD,"I,(spD)"},{zLD,"(spD),I"},   {zLD,"bc,X"},      {zEX,"a,(spD)"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(hl+ix)"},  {zLD,"'(hl+ix),a"},
	{zLD,"I,(hl+ix)"},{zLD,"(hl+ix),I"},   {zLD,"X,bc"},      {zEX,"a,(hl+ix)"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(hl+iy)"},  {zLD,"'(hl+iy),a"},
	{zLD,"I,(hl+iy)"},{zLD,"(hl+iy),I"},   {zLD,"de,X"},      {zEX,"a,(hl+iy)"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(ix+iy)"},  {zLD,"'(ix+iy),a"},
	{zLD,"I,(ix+iy)"},{zLD,"(ix+iy),I"},   {zLD,"X,de"},      {zEX,"a,(ix+iy)"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(Q)"},  {zLD,"'(Q),a"},
	{zLD,"I,(Q)"},  {zLD,"(Q),I"},  {zLD,"hl,X"},      {zEX,"a,Ih"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(ixD)"},  {zLD,"'(ixD),a"},
	{zLD,"I,(ixD)"},{zLD,"(ixD),I"},   {zLD,"X,hl"},      {zEX,"a,Il"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(iyD)"},  {zLD,"'(iyD),a"},
	{zLD,"I,(iyD)"},{zLD,"(iyD),I"},   {zLD,"sp,X"},      {zEX,"a,X"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(hlD)"},  {zLD,"'(hlD),a"},
	{zLD,"I,(hlD)"},{zLD,"(hlD),I"},   {zLD,"X,sp"},      {zEX,"a,(W)"},

	{zIN,"(spD),(c)"},{zOUT,"(c),(spD)"},{zSBC,"I,bc"}, {zLD,"'(W),bc"},
	{zNEG,"'"},       {zRETN,"'"},      {zIM,"'0"},      {zLD,"'i,a"},
	{zIN,"(hl+ix),(c)"},  {zOUT,"(c),(hl+ix)"}, {zADC,"I,bc"}, {zLD,"'bc,(W)"},
	{zNEG,"'hl"},    {zRETI,"'"},      {zIM,"'3"},      {zLD,"'r,a"},
	{zIN,"(hl+iy),(c)"},  {zOUT,"(c),(hl+iy)"}, {zSBC,"I,de"}, {zLD,"'(W),de"},
	{zDB,"?"},      {zRETIL,"'"},     {zIM,"'1"},      {zLD,"'a,i"},
	{zIN,"(ix+iy),(c)"},  {zOUT,"(c),(ix+iy)"}, {zADC,"I,de"}, {zLD,"'de,(W)"},
	{zDB,"?"},      {zDB,"?"},      {zIM,"'2"},      {zLD,"'a,r"},
	{zIN,"Ih,(c)"}, {zOUT,"(c),Ih"},{zSBC,"I,I"},  {zDB,"?"},
	{zEXTS,"'a"},    {zPCACHE,"'"},   {zLDCTL,"I,(c)"},{zRRD,"'(hl)"},
	{zIN,"Il,(c)"}, {zOUT,"(c),Il"},{zADC,"I,I"},  {zDB,"?"},
	{zEXTS,"'hl"},    {zADD,"I,a"},   {zLDCTL,"(c),I"},{zRLD,"'(hl)"},
	{zTSTI,"'(c)"},  {zSC,"'N"},        {zSBC,"I,sp"},  {zLD,"'(W),sp"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zDI,"'B"},
	{zIN,"(W),(c)"},{zOUT,"(c),(W)"},{zADC,"I,sp"}, {zLD,"'sp,(W)"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},      {zEI,"'B"},

	{zDB,"?"},      {zDB,"?"},      {zINIW,"'"},      {zOUTIW,"'"},
	{zEPUM,"'(spD)"},{zMEPU,"'(spD)"},{zLDUD,"a,X"},{zLDCTL,"I,usp"},
	{zDB,"?"},      {zDB,"?"},      {zINDW,"'"},      {zOUTDW,"'"},
	{zEPUM,"'(hl+ix)"},{zMEPU,"'(hl+ix)"},{zLDUD,"X,a"},{zLDCTL,"usp,I"},
	{zDB,"?"},      {zDB,"?"},      {zINIRW,"'"},     {zOTIRW,"'"},
	{zEPUM,"'(hl+iy)"},{zMEPU,"'(hl+iy)"},{zLDUP,"a,X"},{zEPUF,"'"},
	{zDB,"?"},      {zDB,"?"},      {zINDRW,"'"},     {zOTDRW,"'"},
	{zEPUM,"'(ix+iy)"},{zMEPU,"'(ix+iy)"},{zLDUP,"X,a"},{zEPUI,"'"},
	{zLDI,"'"},       {zCPI,"'"},       {zINI,"'"},       {zOUTI,"'"},
	{zEPUM,"'(Q)"},  {zMEPU,"'(Q)"},  {zEPUM,"'(hl)"}, {zEPUM,"'(W)"},
	{zLDD,"'"},       {zCPD,"'"},       {zIND,"'"},       {zOUTD,"'"},
	{zEPUM,"'(ixD)"},{zMEPU,"'(ixD)"},{zMEPU,"'(hl)"}, {zMEPU,"'(W)"},
	{zLDIR,"'"},      {zCPIR,"'"},      {zINIR,"'"},      {zOTIR,"'"},
	{zEPUM,"'(iyD)"},{zMEPU,"'(iyD)"},{zDB,"?"},      {zINW,"'hl,(c)"},
	{zLDDR,"'"},      {zCPDR,"'"},      {zINDR,"'"},      {zOTDR,"'"},
	{zEPUM,"'(hlD)"},{zMEPU,"'(hlD)"},{zDB,"?"},      {zOUTW,"'(c),hl"},

	{zMULT,"a,(spD)"},  {zMULTU,"a,(spD)"}, {zMULTW,"hl,(hl)"},{zMULTUW,"hl,(hl)"},
	{zDIV,"hl,(spD)"},  {zDIVU,"hl,(spD)"}, {zADDW,"hl,(hl)"},{zCPW,"hl,(hl)"},
	{zMULT,"a,(hl+ix)"},  {zMULTU,"a,(hl+ix)"}, {zDIVW,"dehl,(hl)"},{zDIVUW,"dehl,(hl)"},
	{zDIV,"hl,(hl+ix)"},  {zDIVU,"hl,(hl+ix)"}, {zSUBW,"hl,(hl)"},{zDB,"?"},
	{zMULT,"a,(hl+iy)"},  {zMULTU,"a,(hl+iy)"}, {zMULTW,"hl,(W)"},{zMULTUW,"hl,(W)"},
	{zDIV,"hl,(hl+iy)"},  {zDIVU,"hl,(hl+iy)"}, {zADDW,"hl,(W)"},{zCPW,"hl,(W)"},
	{zMULT,"a,(ix+iy)"},  {zMULTU,"a,(ix+iy)"}, {zDIVW,"dehl,(W)"},{zDIVUW,"dehl,(W)"},
	{zDIV,"hl,(ix+iy)"},  {zDIVU,"hl,(ix+iy)"}, {zSUBW,"hl,(W)"},{zDB,"?"},
	{zMULT,"a,Ih"},  {zMULTU,"a,Ih"}, {zMULTW,"hl,I"},{zMULTUW,"hl,I"},
	{zDIV,"hl,Ih"},  {zDIVU,"hl,Ih"}, {zADDW,"hl,I"},{zCPW,"hl,I"},
	{zMULT,"a,Il"},  {zMULTU,"a,Il"}, {zDIVW,"dehl,I"},{zDIVUW,"dehl,I"},
	{zDIV,"hl,Il"},  {zDIVU,"hl,Il"}, {zSUBW,"hl,I"},{zEX,"'h,l"},
	{zMULT,"a,X"},{zMULTU,"a,X"},{zMULTW,"hl,(Q)"},{zMULTUW,"hl,(Q)"},
	{zDIV,"hl,X"},{zDIVU,"hl,X"},{zADDW,"hl,(Q)"},{zCPW,"hl,(Q)"},
	{zMULT,"a,(W)"},  {zMULTU,"a,(W)"}, {zDIVW,"dehl,(Q)"},{zDIVUW,"dehl,(Q)"},
	{zDIV,"hl,(W)"},  {zDIVU,"hl,(W)"}, {zSUBW,"hl,(Q)"},{zDB,"?"}
};

static const struct z80dasm mnemonic_fd_ed[256]= {
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(spD)"},  {zLD,"'(spD),a"},
	{zLD,"I,(spD)"},{zLD,"(spD),I"},   {zLD,"bc,X"},      {zEX,"a,(Q)"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(hl+ix)"},  {zLD,"'(hl+ix),a"},
	{zLD,"I,(hl+ix)"},{zLD,"(hl+ix),I"},   {zLD,"X,bc"},      {zEX,"a,(ixD)"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(hl+iy)"},  {zLD,"'(hl+iy),a"},
	{zLD,"I,(hl+iy)"},{zLD,"(hl+iy),I"},   {zLD,"de,X"},      {zEX,"a,(iyD)"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(ix+iy)"},  {zLD,"'(ix+iy),a"},
	{zLD,"I,(ix+iy)"},{zLD,"(ix+iy),I"},   {zLD,"X,de"},      {zEX,"a,(hlD)"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(Q)"},  {zLD,"'(Q),a"},
	{zLD,"I,(Q)"},  {zLD,"(Q),I"},  {zLD,"hl,X"},      {zEX,"a,Ih"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(ixD)"},  {zLD,"'(ixD),a"},
	{zLD,"I,(ixD)"},{zLD,"(ixD),I"},   {zLD,"X,hl"},      {zEX,"a,Il"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(iyD)"},  {zLD,"'(iyD),a"},
	{zLD,"I,(iyD)"},{zLD,"(iyD),I"},   {zLD,"sp,X"},      {zEX,"a,X"},
	{zDB,"?"},      {zDB,"?"},      {zLDA,"I,(hlD)"},  {zLD,"'(hlD),a"},
	{zLD,"I,(hlD)"},{zLD,"(hlD),I"},   {zLD,"X,sp"},      {zEX,"'a,a"},

	{zIN,"(Q),(c)"},{zOUT,"(c),(Q)"},{zSBC,"I,bc"}, {zLD,"'(W),bc"},
	{zNEG,"'"},       {zRETN,"'"},      {zIM,"'0"},      {zLD,"'i,a"},
	{zIN,"(ixD),(c)"},  {zOUT,"(c),(ixD)"}, {zADC,"I,bc"}, {zLD,"'bc,(W)"},
	{zNEG,"'hl"},    {zRETI,"'"},      {zIM,"'3"},      {zLD,"'r,a"},
	{zIN,"(iyD),(c)"},  {zOUT,"(c),(iyD)"}, {zSBC,"I,de"}, {zLD,"'(W),de"},
	{zDB,"?"},      {zRETIL,"'"},     {zIM,"'1"},      {zLD,"'a,i"},
	{zIN,"(hlD),(c)"},  {zOUT,"(c),(hlD)"}, {zADC,"I,de"}, {zLD,"'de,(W)"},
	{zDB,"?"},      {zDB,"?"},      {zIM,"'2"},      {zLD,"'a,r"},
	{zIN,"Ih,(c)"}, {zOUT,"(c),Ih"},{zSBC,"I,I"},  {zDB,"?"},
	{zEXTS,"'a"},    {zPCACHE,"'"}, {zLDCTL,"I,(c)"},{zRRD,"'(hl)"},
	{zIN,"Il,(c)"}, {zOUT,"(c),Il"},{zADC,"I,I"},  {zDB,"?"},
	{zEXTS,"'hl"},  {zADD,"I,a"},   {zLDCTL,"(c),I"},{zRLD,"'(hl)"},
	{zTSTI,"'(c)"},  {zSC,"'N"},     {zSBC,"I,sp"},   {zLD,"'(W),sp"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},     {zDI,"'B"},
	{zDB,"?"},      {zDB,"?"},      {zADC,"I,sp"}, {zLD,"'sp,(W)"},
	{zDB,"?"},      {zDB,"?"},      {zDB,"?"},     {zEI,"'B"},

	{zDB,"?"},      {zDB,"?"},      {zINIW,"'"},      {zOUTIW,"'"},
	{zEPUM,"'(spD)"},{zMEPU,"'(spD)"},{zLDUD,"a,X"},{zLDCTL,"I,usp"},
	{zDB,"?"},      {zDB,"?"},      {zINDW,"'"},      {zOUTDW,"'"},
	{zEPUM,"'(hl+ix)"},{zMEPU,"'(hl+ix)"},{zLDUD,"X,a"},{zLDCTL,"usp,I"},
	{zDB,"?"},      {zDB,"?"},      {zINIRW,"'"},     {zOTIRW,"'"},
	{zEPUM,"'(hl+iy)"},{zMEPU,"'(hl+iy)"},{zLDUP,"a,X"},{zEPUF,"'"},
	{zDB,"?"},      {zDB,"?"},      {zINDRW,"'"},     {zOTDRW,"'"},
	{zEPUM,"'(ix+iy)"},{zMEPU,"'(ix+iy)"},{zLDUP,"X,a"},{zEPUI,"'"},
	{zLDI,"'"},       {zCPI,"'"},       {zINI,"'"},       {zOUTI,"'"},
	{zEPUM,"'(Q)"},  {zMEPU,"'(Q)"},  {zEPUM,"'(hl)"}, {zEPUM,"'(W)"},
	{zLDD,"'"},       {zCPD,"'"},       {zIND,"'"},       {zOUTD,"'"},
	{zEPUM,"'(ixD)"},{zMEPU,"'(ixD)"},{zMEPU,"'(hl)"}, {zMEPU,"'(W)"},
	{zLDIR,"'"},      {zCPIR,"'"},      {zINIR,"'"},      {zOTIR,"'"},
	{zEPUM,"'(iyD)"},{zMEPU,"'(iyD)"},{zDB,"?"},      {zINW,"'hl,(c)"},
	{zLDDR,"'"},      {zCPDR,"'"},      {zINDR,"'"},      {zOTDR,"'"},
	{zEPUM,"'(hlD)"},{zMEPU,"'(hlD)"},{zDB,"?"},      {zOUTW,"'(c),hl"},

	{zMULT,"a,(Q)"},  {zMULTU,"a,(Q)"}, {zMULTW,"hl,(ixD)"},{zMULTUW,"hl,(ixD)"},
	{zDIV,"hl,(Q)"},  {zDIVU,"hl,(Q)"}, {zADDW,"hl,(ixD)"},{zCPW,"hl,(ixD)"},
	{zMULT,"a,(ixD)"},  {zMULTU,"a,(ixD)"}, {zDIVW,"dehl,(ixD)"},{zDIVUW,"dehl,(ixD)"},
	{zDIV,"hl,(ixD)"},  {zDIVU,"hl,(ixD)"}, {zSUBW,"hl,(ixD)"},{zDB,"?"},
	{zMULT,"a,(iyD)"},  {zMULTU,"a,(iyD)"}, {zMULTW,"hl,(iyD)"},{zMULTUW,"hl,(iyD)"},
	{zDIV,"hl,(iyD)"},  {zDIVU,"hl,(iyD)"}, {zADDW,"hl,(iyD)"},{zCPW,"hl,(iyD)"},
	{zMULT,"a,(hlD)"},  {zMULTU,"a,(hlD)"}, {zDIVW,"dehl,(iyD)"},{zDIVUW,"dehl,(iyD)"},
	{zDIV,"hl,(hlD)"},  {zDIVU,"hl,(hlD)"}, {zSUBW,"hl,(iyD)"},{zDB,"?"},
	{zMULT,"a,Ih"},  {zMULTU,"a,Ih"}, {zMULTW,"hl,I"},{zMULTUW,"hl,I"},
	{zDIV,"hl,Ih"},  {zDIVU,"hl,Ih"}, {zADDW,"hl,I"},{zCPW,"hl,I"},
	{zMULT,"a,Il"},  {zMULTU,"a,Il"}, {zDIVW,"dehl,I"},{zDIVUW,"dehl,I"},
	{zDIV,"hl,Il"},  {zDIVU,"hl,Il"}, {zSUBW,"hl,I"},{zEX,"'h,l"},
	{zMULT,"a,X"},{zMULTU,"a,X"},{zMULTW,"hl,N"},{zMULTUW,"hl,N"},
	{zDIV,"hl,X"},{zDIVU,"hl,X"},{zADDW,"hl,N"},{zCPW,"hl,N"},
	{zMULT,"a,B"},  {zMULTU,"a,B"}, {zDIVW,"dehl,N"},{zDIVUW,"dehl,N"},
	{zDIV,"hl,B"},  {zDIVU,"hl,B"}, {zSUBW,"hl,N"},{zDB,"?"}
};

static const struct z80dasm mnemonic_main[256]= {
	{zNOP,0},       {zLD,"bc,N"},   {zLD,"(bc),a"}, {zINC,"bc"},
	{zINC,"b"},     {zDEC,"b"},     {zLD,"b,B"},    {zRLCA,0},
	{zEX,"af,af'"}, {zADD,"hl,bc"}, {zLD,"a,(bc)"}, {zDEC,"bc"},
	{zINC,"c"},     {zDEC,"c"},     {zLD,"c,B"},    {zRRCA,0},
	{zDJNZ,"O"},    {zLD,"de,N"},   {zLD,"(de),a"}, {zINC,"de"},
	{zINC,"d"},     {zDEC,"d"},     {zLD,"d,B"},    {zRLA,0},
	{zJR,"O"},      {zADD,"hl,de"}, {zLD,"a,(de)"}, {zDEC,"de"},
	{zINC,"e"},     {zDEC,"e"},     {zLD,"e,B"},    {zRRA,0},
	{zJR,"nz,O"},   {zLD,"hl,N"},   {zLD,"(W),hl"}, {zINC,"hl"},
	{zINC,"h"},     {zDEC,"h"},     {zLD,"h,B"},    {zDAA,0},
	{zJR,"z,O"},    {zADD,"hl,hl"}, {zLD,"hl,(W)"}, {zDEC,"hl"},
	{zINC,"l"},     {zDEC,"l"},     {zLD,"l,B"},    {zCPL,0},
	{zJR,"nc,O"},   {zLD,"sp,N"},   {zLD,"(W),a"},  {zINC,"sp"},
	{zINC,"(hl)"},  {zDEC,"(hl)"},  {zLD,"(hl),B"}, {zSCF,0},
	{zJR,"c,O"},    {zADD,"hl,sp"}, {zLD,"a,(W)"},  {zDEC,"sp"},
	{zINC,"a"},     {zDEC,"a"},     {zLD,"a,B"},    {zCCF,0},

	{zLD,"b,b"},    {zLD,"b,c"},    {zLD,"b,d"},    {zLD,"b,e"},
	{zLD,"b,h"},    {zLD,"b,l"},    {zLD,"b,(hl)"}, {zLD,"b,a"},
	{zLD,"c,b"},    {zLD,"c,c"},    {zLD,"c,d"},    {zLD,"c,e"},
	{zLD,"c,h"},    {zLD,"c,l"},    {zLD,"c,(hl)"}, {zLD,"c,a"},
	{zLD,"d,b"},    {zLD,"d,c"},    {zLD,"d,d"},    {zLD,"d,e"},
	{zLD,"d,h"},    {zLD,"d,l"},    {zLD,"d,(hl)"}, {zLD,"d,a"},
	{zLD,"e,b"},    {zLD,"e,c"},    {zLD,"e,d"},    {zLD,"e,e"},
	{zLD,"e,h"},    {zLD,"e,l"},    {zLD,"e,(hl)"}, {zLD,"e,a"},
	{zLD,"h,b"},    {zLD,"h,c"},    {zLD,"h,d"},    {zLD,"h,e"},
	{zLD,"h,h"},    {zLD,"h,l"},    {zLD,"h,(hl)"}, {zLD,"h,a"},
	{zLD,"l,b"},    {zLD,"l,c"},    {zLD,"l,d"},    {zLD,"l,e"},
	{zLD,"l,h"},    {zLD,"l,l"},    {zLD,"l,(hl)"}, {zLD,"l,a"},
	{zLD,"(hl),b"}, {zLD,"(hl),c"}, {zLD,"(hl),d"}, {zLD,"(hl),e"},
	{zLD,"(hl),h"}, {zLD,"(hl),l"}, {zHLT,0},       {zLD,"(hl),a"},
	{zLD,"a,b"},    {zLD,"a,c"},    {zLD,"a,d"},    {zLD,"a,e"},
	{zLD,"a,h"},    {zLD,"a,l"},    {zLD,"a,(hl)"}, {zLD,"a,a"},

	{zADD,"a,b"},   {zADD,"a,c"},   {zADD,"a,d"},   {zADD,"a,e"},
	{zADD,"a,h"},   {zADD,"a,l"},   {zADD,"a,(hl)"},{zADD,"a,a"},
	{zADC,"a,b"},   {zADC,"a,c"},   {zADC,"a,d"},   {zADC,"a,e"},
	{zADC,"a,h"},   {zADC,"a,l"},   {zADC,"a,(hl)"},{zADC,"a,a"},
	{zSUB,"b"},     {zSUB,"c"},     {zSUB,"d"},     {zSUB,"e"},
	{zSUB,"h"},     {zSUB,"l"},     {zSUB,"(hl)"},  {zSUB,"a"},
	{zSBC,"a,b"},   {zSBC,"a,c"},   {zSBC,"a,d"},   {zSBC,"a,e"},
	{zSBC,"a,h"},   {zSBC,"a,l"},   {zSBC,"a,(hl)"},{zSBC,"a,a"},
	{zAND,"b"},     {zAND,"c"},     {zAND,"d"},     {zAND,"e"},
	{zAND,"h"},     {zAND,"l"},     {zAND,"(hl)"},  {zAND,"a"},
	{zXOR,"b"},     {zXOR,"c"},     {zXOR,"d"},     {zXOR,"e"},
	{zXOR,"h"},     {zXOR,"l"},     {zXOR,"(hl)"},  {zXOR,"a"},
	{zOR,"b"},      {zOR,"c"},      {zOR,"d"},      {zOR,"e"},
	{zOR,"h"},      {zOR,"l"},      {zOR,"(hl)"},   {zOR,"a"},
	{zCP,"b"},      {zCP,"c"},      {zCP,"d"},      {zCP,"e"},
	{zCP,"h"},      {zCP,"l"},      {zCP,"(hl)"},   {zCP,"a"},

	{zRET,"nz"},    {zPOP,"bc"},    {zJP,"nz,A"},   {zJP,"A"},
	{zCALL,"nz,A"}, {zPUSH,"bc"},   {zADD,"a,B"},   {zRST,"V"},
	{zRET,"z"},     {zRET,0},       {zJP,"z,A"},    {zDB,"cb"},
	{zCALL,"z,A"},  {zCALL,"A"},    {zADC,"a,B"},   {zRST,"V"},
	{zRET,"nc"},    {zPOP,"de"},    {zJP,"nc,A"},   {zOUT,"(P),a"},
	{zCALL,"nc,A"}, {zPUSH,"de"},   {zSUB,"B"},     {zRST,"V"},
	{zRET,"c"},     {zEXX,0},       {zJP,"c,A"},    {zIN,"a,(P)"},
	{zCALL,"c,A"},  {zDB,"dd"},     {zSBC,"a,B"},   {zRST,"V"},
	{zRET,"po"},    {zPOP,"hl"},    {zJP,"po,A"},   {zEX,"(sp),hl"},
	{zCALL,"po,A"}, {zPUSH,"hl"},   {zAND,"B"},     {zRST,"V"},
	{zRET,"pe"},    {zJP,"(hl)"},   {zJP,"pe,A"},   {zEX,"de,hl"},
	{zCALL,"pe,A"}, {zDB,"ed"},     {zXOR,"B"},     {zRST,"V"},
	{zRET,"p"},     {zPOP,"af"},    {zJP,"p,A"},    {zDI,0},
	{zCALL,"p,A"},  {zPUSH,"af"},   {zOR,"B"},      {zRST,"V"},
	{zRET,"m"},     {zLD,"sp,hl"},  {zJP,"m,A"},    {zEI,0},
	{zCALL,"m,A"},  {zDB,"fd"},     {zCP,"B"},      {zRST,"V"}
};

static char sign(int16_t offset)
{
	return (offset < 0)? '-':'+';
}

static int offs(int16_t offset)
{
	if (offset < 0) return -offset;
	return offset;
}

/****************************************************************************
 * Disassemble opcode at PC and return number of bytes it takes
 ****************************************************************************/
offs_t cpu_disassemble_z280(device_t *device, char *buffer, offs_t pc, const UINT8 *opram, int options)
{
	const struct z80dasm *d;
	const char *src, *ixy;
	char *dst;
	unsigned PC = pc;
	int16_t offset = 0;
	UINT8 op, op1 = 0;
	UINT16 ea = 0;
	int pos = 0;
	UINT32 flags = 0;

	ixy = "oops!!";
	dst = buffer;

	op = opram[pos++];

	switch (op)
	{
	case 0xcb:
		op = opram[pos++];
		d = &mnemonic_cb[op];
		break;
	case 0xed:
		op1 = opram[pos++];
		d = &mnemonic_ed[op1];
		break;
	case 0xdd:
		ixy = "ix";
		op1 = opram[pos++];
		if( op1 == 0xcb )
		{
			offset = (INT8) opram[pos++];
			op1 = opram[pos++];
			d = &mnemonic_xx_cb[op1];
		}
		else if (op1 == 0xed)
		{
			op1 = opram[pos++];
			d = &mnemonic_dd_ed[op1];
		}
		else d = &mnemonic_dd[op1];
		break;
	case 0xfd:
		ixy = "iy";
		op1 = opram[pos++];
		if( op1 == 0xcb )
		{
			offset = (INT8) opram[pos++];
			op1 = opram[pos++];
			d = &mnemonic_xx_cb[op1];
		}
		else if (op1 == 0xed)
		{
			op1 = opram[pos++];
			d = &mnemonic_fd_ed[op1];
		}
		else d = &mnemonic_fd[op1];
		break;
	default:
		d = &mnemonic_main[op];
		break;
	}

	if( d->arguments )
	{
		dst += sprintf(dst, "%-5s ", s_mnemonic[d->mnemonic]);
		src = d->arguments;
		while( *src )
		{
			switch( *src )
			{
			case '?':   /* illegal opcode */
				dst += sprintf( dst, "$%02x,$%02x", op, op1);
				break;
			case 'A':   /* Absolute word address */
				ea = opram[pos] + ( opram[pos+1] << 8);
				pos += 2;
				dst += sprintf( dst, "$%04X", ea );
				break;
			case 'B':   /* Byte op arg */
				ea = opram[pos++];
				dst += sprintf( dst, "$%02X", ea );
				break;
			case 'D':   /* Word displacement */
				offset = opram[pos] + ( opram[pos+1] << 8 );
				pos += 2;
				dst += sprintf( dst, "%c$%04X", sign(offset), offs(offset) );
				break;
			case 'N':   /* Immediate 16 bit */
				ea = opram[pos] + ( opram[pos+1] << 8 );
				pos += 2;
				dst += sprintf( dst, "$%04X", ea );
				break;
			case 'O':   /* Byte Offset relative to PC */
				offset = (INT8) opram[pos++];
				dst += sprintf( dst, "$%06X", PC + offset + 2 );
				break;
			case 'Q':   /* Word offset relative to PC, program space */
				offset = opram[pos] + ( opram[pos+1] << 8 );
				pos += 2;
				dst += sprintf( dst, "$%06X", PC + offset + 2 );
				break;
			case 'P':   /* Port number */
				ea = opram[pos++];
				dst += sprintf( dst, "$%02X", ea );
				break;
			case 'V':   /* Restart vector */
				ea = op & 0x38;
				dst += sprintf( dst, "$%02X", ea );
				break;
			case 'W':   /* Memory address word (used for indirect) */
				ea = opram[pos] + ( opram[pos+1] << 8);
				pos += 2;
				dst += sprintf( dst, "$%04X", ea );
				break;
			case 'X':	/* (ix+nn) (iy+nn) */
				offset = (INT8) opram[pos++];
			case 'Y':
				dst += sprintf( dst,"(%s%c$%02x)", ixy, sign(offset), offs(offset) );
				break;
			case 'I':
				dst += sprintf( dst, "%s", ixy);
				break;
			default:
				*dst++ = *src;
			}
			src++;
		}
		*dst = '\0';
	}
	else
	{
		dst += sprintf(dst, "%s", s_mnemonic[d->mnemonic]);
	}

	if (d->mnemonic == zCALL || d->mnemonic == zCPDR || d->mnemonic == zCPIR || d->mnemonic == zDJNZ ||
		d->mnemonic == zHLT  || d->mnemonic == zINDR || d->mnemonic == zINDRW || d->mnemonic == zINIR || d->mnemonic == zINIRW || d->mnemonic == zLDDR ||
		d->mnemonic == zLDIR || d->mnemonic == zOTDR || d->mnemonic == zOTDRW || d->mnemonic == zOTIR || d->mnemonic == zOTIRW || d->mnemonic == zRST)
		flags = DASMFLAG_STEP_OVER;
	else if (d->mnemonic == zRETN || d->mnemonic == zRET || d->mnemonic == zRETI)
		flags = DASMFLAG_STEP_OUT;
	else if (d->mnemonic == zEPUM || d->mnemonic == zMEPU || d->mnemonic == zEPUF || d->mnemonic == zEPUI)
	    pos += 4;

	return pos | flags | DASMFLAG_SUPPORTED;
}
