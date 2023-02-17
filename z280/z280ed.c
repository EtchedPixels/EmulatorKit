/*OP(illegal,2)
{
	logerror("Z280 '%s' ill. opcode $%02x $%02x\n",
			cpustate->device->m_tag, cpustate->ram->read_raw_byte((cpustate->_PCD-2)&0xffff), cpustate->ram->read_raw_byte((cpustate->_PCD-1)&0xffff));
	//cpustate->int_pending[Z280_INT_TRAP] = 1;
	//cpustate->IO_ITC |= Z280_ITC_TRAP;
	//cpustate->IO_ITC |= Z280_ITC_UFO;
}*/

/**********************************************************
 * special opcodes (ED prefix)
 **********************************************************/
OP(ed,00) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,01) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,02) { EASP16(cpustate); cpustate->_HL = cpustate->ea;                 } /* LDA HL,(SP+w)    */
OP(ed,03) { EASP16(cpustate); WM(cpustate,  cpustate->ea, cpustate->_A );   } /* LD (SP+w),A      */
OP(ed,04) { EASP16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->HL);  } /* LD HL,(SP+w)     */
OP(ed,05) { EASP16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->HL);  } /* LD (SP+w),HL     */
OP(ed,06) { RM16(cpustate, cpustate->_HL, &cpustate->BC);                   } /* LD BC,(HL)       */
OP(ed,07) { union PAIR tmp; tmp.b.l = cpustate->_A; cpustate->_A = cpustate->_B; cpustate->_B = tmp.b.l; } /* EX A,B           */

OP(ed,08) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,09) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,0a) { EAHX(cpustate); cpustate->_HL = cpustate->ea;                   } /* LDA HL,(HL+IX)   */
OP(ed,0b) { EAHX(cpustate); WM(cpustate,  cpustate->ea, cpustate->_A );     } /* LD (HL+IX),A     */
OP(ed,0c) { EAHX(cpustate); RM16(cpustate, cpustate->ea, &cpustate->HL);    } /* LD HL,(HL+IX)    */
OP(ed,0d) { EAHX(cpustate); WM16(cpustate, cpustate->ea, &cpustate->HL);    } /* LD (HL+IX),HL    */
OP(ed,0e) { WM16(cpustate, cpustate->_HL, &cpustate->BC);                   } /* LD (HL),BC       */
OP(ed,0f) { union PAIR tmp; tmp.b.l = cpustate->_A; cpustate->_A = cpustate->_C; cpustate->_C = tmp.b.l; } /* EX A,C           */

OP(ed,10) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,11) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,12) { EAHY(cpustate); cpustate->_HL = cpustate->ea;                   } /* LDA HL,(HL+IY)   */
OP(ed,13) { EAHY(cpustate); WM(cpustate,  cpustate->ea, cpustate->_A );     } /* LD (HL+IY),A     */
OP(ed,14) { EAHY(cpustate); RM16(cpustate, cpustate->ea, &cpustate->HL);    } /* LD HL,(HL+IY)    */
OP(ed,15) { EAHY(cpustate); WM16(cpustate, cpustate->ea, &cpustate->HL);    } /* LD (HL+IY),HL    */
OP(ed,16) { RM16(cpustate, cpustate->_HL, &cpustate->DE);                   } /* LD DE,(HL)       */
OP(ed,17) { union PAIR tmp; tmp.b.l = cpustate->_A; cpustate->_A = cpustate->_D; cpustate->_D = tmp.b.l; } /* EX A,D           */

OP(ed,18) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,19) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,1a) { EAXY(cpustate); cpustate->_HL = cpustate->ea;                   } /* LDA HL,(IX+IY)   */
OP(ed,1b) { EAXY(cpustate); WM(cpustate,  cpustate->ea, cpustate->_A );     } /* LD (IX+IY),A     */
OP(ed,1c) { EAXY(cpustate); RM16(cpustate, cpustate->ea, &cpustate->HL);    } /* LD HL,(IX+IY)    */
OP(ed,1d) { EAXY(cpustate); WM16(cpustate, cpustate->ea, &cpustate->HL);    } /* LD (IX+IY),HL    */
OP(ed,1e) { WM16(cpustate, cpustate->_HL, &cpustate->DE);                   } /* LD (HL),DE       */
OP(ed,1f) { union PAIR tmp; tmp.b.l = cpustate->_A; cpustate->_A = cpustate->_E; cpustate->_E = tmp.b.l; } /* EX A,E           */

OP(ed,20) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,21) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,22) { EARA(cpustate); cpustate->_HL = cpustate->ea;                   } /* LDA HL,(ra)      */
OP(ed,23) { EARA(cpustate); WM(cpustate,  cpustate->ea, cpustate->_A );     } /* LD (ra),A        */
OP(ed,24) { EARA(cpustate); RM16(cpustate, cpustate->ea, &cpustate->HL);    } /* LD HL,(ra)       */
OP(ed,25) { EARA(cpustate); WM16(cpustate, cpustate->ea, &cpustate->HL);    } /* LD (ra),HL       */
OP(ed,26) { RM16(cpustate, cpustate->_HL, &cpustate->HL);                   } /* LD HL,(HL)       */
OP(ed,27) { union PAIR tmp; tmp.b.l = cpustate->_A; cpustate->_A = cpustate->_H; cpustate->_H = tmp.b.l; } /* EX A,H           */

OP(ed,28) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,29) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,2a) { EAX16(cpustate); cpustate->_HL = cpustate->ea;                  } /* LDA HL,(IX+w)    */
OP(ed,2b) { EAX16(cpustate); WM(cpustate,  cpustate->ea, cpustate->_A );    } /* LD (IX+w),A      */
OP(ed,2c) { EAX16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->HL);   } /* LD HL,(IX+w)     */
OP(ed,2d) { EAX16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->HL);   } /* LD (IX+w),HL     */
OP(ed,2e) { WM16(cpustate, cpustate->_HL, &cpustate->BC);                   } /* LD (HL),HL       */
OP(ed,2f) { union PAIR tmp; tmp.b.l = cpustate->_A; cpustate->_A = cpustate->_L; cpustate->_L = tmp.b.l; } /* EX A,L           */

OP(ed,30) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,31) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,32) { EAY16(cpustate); cpustate->_HL = cpustate->ea;                  } /* LDA HL,(IY+w)    */
OP(ed,33) { EAY16(cpustate); WM(cpustate,  cpustate->ea, cpustate->_A );    } /* LD (IY+w),A      */
OP(ed,34) { EAY16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->HL);   } /* LD HL,(IY+w)     */
OP(ed,35) { EAY16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->HL);   } /* LD (IY+w),HL     */
OP(ed,36) { union PAIR tmp; RM16(cpustate, cpustate->_HL, &tmp); SET_SP(cpustate, tmp.w.l); } /* LD SP,(HL)       */
OP(ed,37) { union PAIR tmp; tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->_HL); WM(cpustate, cpustate->_HL, tmp.b.l); } /* EX A,(HL)        */

OP(ed,38) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,39) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,3a) { EAH16(cpustate); cpustate->_HL = cpustate->ea;                  } /* LDA HL,(HL+w)    */
OP(ed,3b) { EAH16(cpustate); WM(cpustate,  cpustate->ea, cpustate->_A );    } /* LD (HL+w),A      */
OP(ed,3c) { EAH16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->HL);   } /* LD HL,(HL+w)     */
OP(ed,3d) { EAH16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->HL);   } /* LD (HL+w),HL     */
OP(ed,3e) { union PAIR tmp; tmp.w.l = _SP(cpustate); WM16(cpustate, cpustate->_HL, &tmp); } /* LD (HL),SP       */
OP(ed,3f) {                                                                 } /* EX A,A           */

OP(ed,40) { CHECK_PRIV_IO(cpustate) { cpustate->_B = IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_B]; } } /* IN   B,(C)       */
OP(ed,41) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC,cpustate->_B); } } /* OUT  (C),B       */
OP(ed,42) { SBC16( HL, cpustate->_BC );                                            } /* SBC  HL,BC       */
OP(ed,43) { cpustate->ea = ARG16(cpustate); WM16(cpustate,  cpustate->ea, &cpustate->BC );                  } /* LD   (w),BC      */
OP(ed,44) { NEG;                                                    } /* NEG A            */
OP(ed,45) { RETN;                                                   } /* RETN             */
OP(ed,46) { IM(cpustate, 0);                                               } /* IM   0           */
OP(ed,47) { LD_I_A;                                                 } /* LD   I,A         */

OP(ed,48) { CHECK_PRIV_IO(cpustate) { cpustate->_C = IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_C]; } } /* IN   C,(C)       */
OP(ed,49) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC,cpustate->_C); } } /* OUT  (C),C       */
OP(ed,4a) { ADC16( HL, cpustate->_BC );                                            } /* ADC  HL,BC       */
OP(ed,4b) { cpustate->ea = ARG16(cpustate); RM16(cpustate,  cpustate->ea, &cpustate->BC );                  } /* LD   BC,(w)      */
OP(ed,4c) { NEG16;                                                  } /* NEG HL           */
OP(ed,4d) { RETI;                                                   } /* RETI             */
OP(ed,4e) { IM(cpustate, 3);                                               } /* IM   3           */
OP(ed,4f) { LD_R_A;                                                 } /* LD   R,A         */

OP(ed,50) { CHECK_PRIV_IO(cpustate) { cpustate->_D = IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_D]; } } /* IN   D,(C)       */
OP(ed,51) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC,cpustate->_D); } } /* OUT  (C),D       */
OP(ed,52) { SBC16( HL, cpustate->_DE );                                            } /* SBC  HL,DE       */
OP(ed,53) { cpustate->ea = ARG16(cpustate); WM16(cpustate,  cpustate->ea, &cpustate->DE );                  } /* LD   (w),DE      */
OP(ed,54) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,55) { RETIL;                                                  } /* RETIL            */
OP(ed,56) { IM(cpustate, 1);                                               } /* IM   1           */
OP(ed,57) { LD_A_I;                                                 } /* LD   A,I         */

OP(ed,58) { CHECK_PRIV_IO(cpustate) { cpustate->_E = IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_E]; } } /* IN   E,(C)       */
OP(ed,59) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC,cpustate->_E); } } /* OUT  (C),E       */
OP(ed,5a) { ADC16( HL, cpustate->_DE );                                            } /* ADC  HL,DE       */
OP(ed,5b) { cpustate->ea = ARG16(cpustate); RM16(cpustate,  cpustate->ea, &cpustate->DE );                  } /* LD   DE,(w)      */
OP(ed,5c) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,5d) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,5e) { IM(cpustate, 2);                                               } /* IM   2           */
OP(ed,5f) { LD_A_R;                                                 } /* LD   A,R         */

OP(ed,60) { CHECK_PRIV_IO(cpustate) { cpustate->_H = IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_H]; } } /* IN   H,(C)       */
OP(ed,61) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC,cpustate->_H); } } /* OUT  (C),H       */
OP(ed,62) { SBC16( HL, cpustate->_HL );                                            } /* SBC  HL,HL       */
OP(ed,63) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,64) { EXTS;                                                           } /* EXTS A           */
OP(ed,65) { PCACHE;                                                         } /* PCACHE           */
OP(ed,66) { LD_REG_CTL( HL );                                         } /* LDCTL HL,(C)     */
OP(ed,67) { RRD;                                                    } /* RRD  (HL)        */

OP(ed,68) { CHECK_PRIV_IO(cpustate) { cpustate->_L = IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_L]; } } /* IN   L,(C)       */
OP(ed,69) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC,cpustate->_L); } } /* OUT  (C),L       */
OP(ed,6a) { ADC16( HL, cpustate->_HL );                                            } /* ADC  HL,HL       */
OP(ed,6b) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,6c) { EXTS_HL;                                                } /* EXTS HL          */
OP(ed,6d) { ADD16_A( HL );                                          } /* ADD HL,A         */
OP(ed,6e) { LD_CTL_REG( HL );                                          } /* LDCTL (C),HL     */
OP(ed,6f) { RLD;                                                    } /* RLD  (HL)        */

OP(ed,70) { CHECK_PRIV_IO(cpustate) { IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_L]; } } /* TSTI (C)       */
OP(ed,71) { cpustate->extra_cycles += take_trap(cpustate, Z280_TRAP_SC);    } /* SC w             */
OP(ed,72) { SBC16( HL, _SP(cpustate) );                                            } /* SBC  HL,SP       */
OP(ed,73) { union PAIR tmp; tmp.w.l = _SP(cpustate); cpustate->ea = ARG16(cpustate); WM16(cpustate,  cpustate->ea, &tmp );                  } /* LD   (w),SP      */
OP(ed,74) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,75) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,76) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,77) { DI(ARG(cpustate));                                              } /* DI n             */

OP(ed,78) { CHECK_PRIV_IO(cpustate) { cpustate->_A = IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_A]; } } /* IN   A,(C)       */
OP(ed,79) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC,cpustate->_A); } } /* OUT  (C),A       */
OP(ed,7a) { ADC16( HL, _SP(cpustate) );                                            } /* ADC  HL,SP       */
OP(ed,7b) { union PAIR tmp; cpustate->ea = ARG16(cpustate); RM16(cpustate,  cpustate->ea, &tmp ); SET_SP(cpustate, tmp.w.l); } /* LD   SP,(w)      */
OP(ed,7c) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,7d) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,7e) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,7f) { EI(ARG(cpustate));                                              } /* EI n             */

OP(ed,80) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,81) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,82) { CHECK_PRIV_IO(cpustate) { INIW; }                               } /* INIW             */
OP(ed,83) { CHECK_PRIV_IO(cpustate) { OUTIW; }                              } /* OUTIW            */
OP(ed,84) { EASP16(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_EPUM) { LOG("unimplemented EPU opcode\n"); } } /* EPUM (SP+w)      */
OP(ed,85) { EASP16(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_MEPU) { LOG("unimplemented EPU opcode\n"); } } /* MEPU (SP+w)      */
OP(ed,86) { cpustate->ea = cpustate->_HL; LDU_A_M(0);                       } /* LDUD A,(HL)      */
OP(ed,87) { cpustate->_HL = cpustate->_USP;                                 } /* LDCTL HL,USP     */

OP(ed,88) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,89) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,8a) { CHECK_PRIV_IO(cpustate) { INDW; }                               } /* INDW             */
OP(ed,8b) { CHECK_PRIV_IO(cpustate) { OUTDW; }                              } /* OUTDW            */
OP(ed,8c) { EAHX(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_EPUM) { LOG("unimplemented EPU opcode\n"); } } /* EPUM (HL+IX)     */
OP(ed,8d) { EAHX(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_MEPU) { LOG("unimplemented EPU opcode\n"); } } /* MEPU (HL+IX)     */
OP(ed,8e) { cpustate->ea = cpustate->_HL; LDU_M_A(0);                       } /* LDUD (HL),A      */
OP(ed,8f) { cpustate->_USP = cpustate->_HL;                                 } /* LDCTL USP,HL     */

OP(ed,90) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,91) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,92) { CHECK_PRIV_IO(cpustate) { INIRW; }                              } /* INIRW            */
OP(ed,93) { CHECK_PRIV_IO(cpustate) { OTIRW; }                              } /* OTIRW            */
OP(ed,94) { EAHY(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_EPUM) { LOG("unimplemented EPU opcode\n"); } } /* EPUM (HL+IY)     */
OP(ed,95) { EAHY(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_MEPU) { LOG("unimplemented EPU opcode\n"); } } /* MEPU (HL+IY)     */
OP(ed,96) { cpustate->ea = cpustate->_HL; LDU_A_M(1);                       } /* LDUP A,(HL)      */
OP(ed,97) { cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_EPUF) { LOG("unimplemented EPU opcode\n"); } } /* EPUF             */

OP(ed,98) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,99) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,9a) { CHECK_PRIV_IO(cpustate) { INDRW; }                              } /* INDRW            */
OP(ed,9b) { CHECK_PRIV_IO(cpustate) { OTDRW; }                              } /* OTDRW            */
OP(ed,9c) { EAXY(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_EPUM) { LOG("unimplemented EPU opcode\n"); } } /* EPUM (IX+IY)     */
OP(ed,9d) { EAXY(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_MEPU) { LOG("unimplemented EPU opcode\n"); } } /* MEPU (IX+IY)     */
OP(ed,9e) { cpustate->ea = cpustate->_HL; LDU_M_A(1);                       } /* LDUP (HL),A      */
OP(ed,9f) { cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_EPUI) { LOG("unimplemented EPU opcode\n"); } } /* EPUI             */

OP(ed,a0) { LDI;                                                    } /* LDI              */
OP(ed,a1) { CPI;                                                    } /* CPI              */
OP(ed,a2) { CHECK_PRIV_IO(cpustate) { INI; }                        } /* INI              */
OP(ed,a3) { CHECK_PRIV_IO(cpustate) { OUTI; }                       } /* OUTI             */
OP(ed,a4) { EARA(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_EPUM) { LOG("unimplemented EPU opcode\n"); } } /* EPUM (ra)        */
OP(ed,a5) { EARA(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_MEPU) { LOG("unimplemented EPU opcode\n"); } } /* MEPU (ra)        */
OP(ed,a6) { cpustate->ea = cpustate->_HL;cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_EPUM) { LOG("unimplemented EPU opcode\n"); } } /* EPUM (HL)        */
OP(ed,a7) { cpustate->ea = ARG16(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_EPUM) { LOG("unimplemented EPU opcode\n"); } } /* EPUM (w)         */

OP(ed,a8) { LDD;                                                    } /* LDD              */
OP(ed,a9) { CPD;                                                    } /* CPD              */
OP(ed,aa) { CHECK_PRIV_IO(cpustate) { IND; }                        } /* IND              */
OP(ed,ab) { CHECK_PRIV_IO(cpustate) { OUTD; }                       } /* OUTD             */
OP(ed,ac) { EAX16(cpustate);cpustate->_PC+=4;CHECK_EPU(cpustate,Z280_TRAP_EPUM) { LOG("unimplemented EPU opcode\n"); } } /* EPUM (IX+w)      */
OP(ed,ad) { EAX16(cpustate);cpustate->_PC+=4;CHECK_EPU(cpustate,Z280_TRAP_MEPU) { LOG("unimplemented EPU opcode\n"); } } /* MEPU (IX+w)      */
OP(ed,ae) { cpustate->ea = cpustate->_HL;cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_MEPU) { LOG("unimplemented EPU opcode\n"); } } /* MEPU (HL)        */
OP(ed,af) { cpustate->ea = ARG16(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_MEPU) { LOG("unimplemented EPU opcode\n"); } } /* MEPU (w)         */

OP(ed,b0) { LDIR;                                                   } /* LDIR             */
OP(ed,b1) { CPIR;                                                   } /* CPIR             */
OP(ed,b2) { CHECK_PRIV_IO(cpustate) { INIR; }                       } /* INIR             */
OP(ed,b3) { CHECK_PRIV_IO(cpustate) { OTIR; }                       } /* OTIR             */
OP(ed,b4) { EAY16(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_EPUM) { LOG("unimplemented EPU opcode\n"); } } /* EPUM (IY+w)      */
OP(ed,b5) { EAY16(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_MEPU) { LOG("unimplemented EPU opcode\n"); } } /* MEPU (IY+w)      */
OP(ed,b6) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,b7) { CHECK_PRIV_IO(cpustate) { cpustate->_HL = IN16(cpustate, cpustate->_BC); } } /* INW HL,(C)        */

OP(ed,b8) { LDDR;                                                   } /* LDDR             */
OP(ed,b9) { CPDR;                                                   } /* CPDR             */
OP(ed,ba) { CHECK_PRIV_IO(cpustate) { INDR; }                       } /* INDR             */
OP(ed,bb) { CHECK_PRIV_IO(cpustate) { OTDR; }                       } /* OTDR             */
OP(ed,bc) { EAH16(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_EPUM) { LOG("unimplemented EPU opcode\n"); } } /* EPUM (HL+w)      */
OP(ed,bd) { EAH16(cpustate);cpustate->_PC+=4; CHECK_EPU(cpustate,Z280_TRAP_MEPU) { LOG("unimplemented EPU opcode\n"); } } /* MEPU (HL+w)      */
OP(ed,be) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(ed,bf) { CHECK_PRIV_IO(cpustate) { OUT16(cpustate, cpustate->_BC,cpustate->_HL); } } /* OUTW (C),HL      */

OP(ed,c0) { MULT( cpustate->_B );                                           } /* MULT A,B         */
OP(ed,c1) { MULTU( cpustate->_B );                                          } /* MULTU A,B        */
OP(ed,c2) { MULTW( cpustate->_BC );                                         } /* MULTW HL,BC      */
OP(ed,c3) { MULTUW( cpustate->_BC );                                        } /* MULTUW HL,BC     */
OP(ed,c4) { DIV( cpustate->_B );                                            } /* DIV HL,B         */
OP(ed,c5) { DIVU( cpustate->_B );                                           } /* DIVU HL,B        */
OP(ed,c6) { ADD16F(cpustate->_BC);                                          } /* ADDW HL,BC       */
OP(ed,c7) { CP16(cpustate->_BC);                                            } /* CPW HL,BC        */

OP(ed,c8) { MULT( cpustate->_C );                                           } /* MULT A,C         */
OP(ed,c9) { MULTU( cpustate->_C );                                          } /* MULTU A,C        */
OP(ed,ca) { DIVW( cpustate->_BC );                                          } /* DIVW DEHL,BC      */
OP(ed,cb) { DIVUW( cpustate->_BC );                                         } /* DIVUW DEHL,BC     */
OP(ed,cc) { DIV( cpustate->_C );                                            } /* DIV HL,C         */
OP(ed,cd) { DIVU( cpustate->_C );                                           } /* DIVU HL,C        */
OP(ed,ce) { SUB16(cpustate->_BC);                                           } /* SUBW HL,BC       */
OP(ed,cf) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */

OP(ed,d0) { MULT( cpustate->_D );                                           } /* MULT A,D         */
OP(ed,d1) { MULTU( cpustate->_D );                                          } /* MULTU A,D        */
OP(ed,d2) { MULTW( cpustate->_DE );                                         } /* MULTW HL,DE      */
OP(ed,d3) { MULTUW( cpustate->_DE );                                        } /* MULTUW HL,DE     */
OP(ed,d4) { DIV( cpustate->_D );                                            } /* DIV HL,D         */
OP(ed,d5) { DIVU( cpustate->_D );                                           } /* DIVU HL,D        */
OP(ed,d6) { ADD16F(cpustate->_DE);                                          } /* ADDW HL,DE       */
OP(ed,d7) { CP16(cpustate->_DE);                                            } /* CPW HL,DE        */

OP(ed,d8) { MULT( cpustate->_E );                                           } /* MULT A,E         */
OP(ed,d9) { MULTU( cpustate->_E );                                          } /* MULTU A,E        */
OP(ed,da) { DIVW( cpustate->_DE );                                          } /* DIVW DEHL,DE     */
OP(ed,db) { DIVUW( cpustate->_DE );                                         } /* DIVUW DEHL,DE    */
OP(ed,dc) { DIV( cpustate->_E );                                            } /* DIV HL,E         */
OP(ed,dd) { DIVU( cpustate->_E );                                           } /* DIVU HL,E        */
OP(ed,de) { SUB16(cpustate->_DE);                                           } /* SUBW HL,DE       */
OP(ed,df) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */

OP(ed,e0) { MULT( cpustate->_H );                                           } /* MULT A,H         */
OP(ed,e1) { MULTU( cpustate->_H );                                          } /* MULTU A,H        */
OP(ed,e2) { MULTW( cpustate->_HL );                                         } /* MULTW HL,HL      */
OP(ed,e3) { MULTUW( cpustate->_HL );                                        } /* MULTUW HL,HL     */
OP(ed,e4) { DIV( cpustate->_H );                                            } /* DIV HL,H         */
OP(ed,e5) { DIVU( cpustate->_H );                                           } /* DIVU HL,H        */
OP(ed,e6) { ADD16F(cpustate->_HL);                                          } /* ADDW HL,HL       */
OP(ed,e7) { CP16(cpustate->_HL);                                            } /* CPW HL,HL        */

OP(ed,e8) { MULT( cpustate->_L );                                           } /* MULT A,L         */
OP(ed,e9) { MULTU( cpustate->_L );                                          } /* MULTU A,L        */
OP(ed,ea) { DIVW( cpustate->_HL );                                          } /* DIVW DEHL,HL     */
OP(ed,eb) { DIVUW( cpustate->_HL );                                         } /* DIVUW DEHL,HL    */
OP(ed,ec) { DIV( cpustate->_L );                                            } /* DIV HL,L         */
OP(ed,ed) { DIVU( cpustate->_L );                                           } /* DIVU HL,L        */
OP(ed,ee) { SUB16(cpustate->_HL);                                           } /* SUBW HL,HL       */
OP(ed,ef) { union PAIR tmp; tmp.b.l = cpustate->_H; cpustate->_H = cpustate->_L; cpustate->_L = tmp.b.l; } /* EX H,L           */

OP(ed,f0) { MULT( RM(cpustate, cpustate->_HL) );                            } /* MULT A,(HL)      */
OP(ed,f1) { MULTU( RM(cpustate, cpustate->_HL) );                           } /* MULTU A,(HL)     */
OP(ed,f2) { MULTW( _SP(cpustate) );                                         } /* MULTW HL,SP      */
OP(ed,f3) { MULTUW( _SP(cpustate) );                                        } /* MULTUW HL,SP     */
OP(ed,f4) { DIV( RM(cpustate, cpustate->_HL) );                             } /* DIV HL,(HL)      */
OP(ed,f5) { DIVU( RM(cpustate, cpustate->_HL) );                            } /* DIVU HL,(HL)     */
OP(ed,f6) { ADD16F(_SP(cpustate));                                          } /* ADDW HL,SP       */
OP(ed,f7) { CP16(_SP(cpustate));                                            } /* CPW HL,SP        */

OP(ed,f8) { MULT( cpustate->_A );                                           } /* MULT A,A         */
OP(ed,f9) { MULTU( cpustate->_A );                                          } /* MULTU A,A        */
OP(ed,fa) { DIVW( _SP(cpustate) );                                          } /* DIVW DEHL,SP     */
OP(ed,fb) { DIVUW( _SP(cpustate) );                                         } /* DIVUW DEHL,SP    */
OP(ed,fc) { DIV( cpustate->_A );                                            } /* DIV HL,A         */
OP(ed,fd) { DIVU( cpustate->_A );                                           } /* DIVU HL,A        */
OP(ed,fe) { SUB16(_SP(cpustate));                                           } /* SUBW HL,SP       */
OP(ed,ff) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
