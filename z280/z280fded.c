/**********************************************************
 * special opcodes (FD ED prefix)
 **********************************************************/
OP(fded,00) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,01) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,02) { EASP16(cpustate); cpustate->_IY = cpustate->ea;                 } /* LDA IY,(SP+w)    */
OP(fded,03) { illegal_1(cpustate, __func__); ed_03(cpustate);                           } /* LD '(SP+w),A     */
OP(fded,04) { EASP16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IY);  } /* LD IY,(SP+w)     */
OP(fded,05) { EASP16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IY);  } /* LD (SP+w),IY     */
OP(fded,06) { EAY(cpustate); RM16(cpustate, cpustate->ea, &cpustate->BC);     } /* LD BC,(IY+o)     */
OP(fded,07) { union PAIR tmp; EARA(cpustate); tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->ea); WM(cpustate, cpustate->ea, tmp.b.l); } /* EX A,(ra)        */

OP(fded,08) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,09) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,0a) { EAHX(cpustate); cpustate->_IY = cpustate->ea;                   } /* LDA IY,(HL+IX)   */
OP(fded,0b) { illegal_1(cpustate, __func__); ed_0b(cpustate);                            } /* LD '(HL+IX),A    */
OP(fded,0c) { EAHX(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IY);    } /* LD IY,(HL+IX)    */
OP(fded,0d) { EAHX(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IY);    } /* LD (HL+IX),IY    */
OP(fded,0e) { EAY(cpustate); WM16(cpustate, cpustate->ea, &cpustate->BC);     } /* LD (IY+o),BC     */
OP(fded,0f) { union PAIR tmp; EAX16(cpustate); tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->ea); WM(cpustate, cpustate->ea, tmp.b.l); } /* EX A,(IX+w)      */

OP(fded,10) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,11) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,12) { EAHY(cpustate); cpustate->_IY = cpustate->ea;                   } /* LDA IY,(HL+IY)   */
OP(fded,13) { illegal_1(cpustate, __func__); ed_13(cpustate);                            } /* LD '(HL+IY),A    */
OP(fded,14) { EAHY(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IY);    } /* LD IY,(HL+IY)    */
OP(fded,15) { EAHY(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IY);    } /* LD (HL+IY),IY    */
OP(fded,16) { EAY(cpustate); RM16(cpustate, cpustate->ea, &cpustate->DE);     } /* LD DE,(IY+o)     */
OP(fded,17) { union PAIR tmp; EAY16(cpustate); tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->ea); WM(cpustate, cpustate->ea, tmp.b.l); } /* EX A,(IY+w)      */

OP(fded,18) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,19) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,1a) { EAXY(cpustate); cpustate->_IY = cpustate->ea;                   } /* LDA IY,(IX+IY)   */
OP(fded,1b) { illegal_1(cpustate, __func__); ed_1b(cpustate);                            } /* LD '(IX+IY),A    */
OP(fded,1c) { EAXY(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IY);    } /* LD IY,(IX+IY)    */
OP(fded,1d) { EAXY(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IY);    } /* LD (IX+IY),IY    */
OP(fded,1e) { EAY(cpustate); WM16(cpustate, cpustate->ea, &cpustate->DE);     } /* LD (IY+o),DE     */
OP(fded,1f) { union PAIR tmp; EAH16(cpustate); tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->ea); WM(cpustate, cpustate->ea, tmp.b.l); } /* EX A,(HL+w)      */

OP(fded,20) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,21) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,22) { EARA(cpustate); cpustate->_IY = cpustate->ea;                   } /* LDA IY,(ra)      */
OP(fded,23) { illegal_1(cpustate, __func__); ed_23(cpustate);                            } /* LD '(ra),A       */
OP(fded,24) { EARA(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IY);    } /* LD IY,(ra)       */
OP(fded,25) { EARA(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IY);    } /* LD (ra),IY       */
OP(fded,26) { EAY(cpustate); RM16(cpustate, cpustate->ea, &cpustate->HL);     } /* LD HL,(IY+o)     */
OP(fded,27) { union PAIR tmp; tmp.b.l = cpustate->_A; cpustate->_A = cpustate->_HY; cpustate->_HY = tmp.b.l; } /* EX A,HY          */

OP(fded,28) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,29) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,2a) { EAX16(cpustate); cpustate->_IY = cpustate->ea;                  } /* LDA IY,(IX+w)    */
OP(fded,2b) { illegal_1(cpustate, __func__); ed_2b(cpustate);                            } /* LD '(IX+w),A     */
OP(fded,2c) { EAX16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IY);   } /* LD IY,(IX+w)     */
OP(fded,2d) { EAX16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IY);   } /* LD (IX+w),IY     */
OP(fded,2e) { EAY(cpustate); WM16(cpustate, cpustate->ea, &cpustate->HL);     } /* LD (IY+o),HL     */
OP(fded,2f) { union PAIR tmp; tmp.b.l = cpustate->_A; cpustate->_A = cpustate->_LY; cpustate->_LY = tmp.b.l; } /* EX A,LY          */

OP(fded,30) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,31) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,32) { EAY16(cpustate); cpustate->_IY = cpustate->ea;                  } /* LDA IY,(IY+w)    */
OP(fded,33) { illegal_1(cpustate, __func__); ed_33(cpustate);                            } /* LD '(IY+w),A     */
OP(fded,34) { EAY16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IY);   } /* LD IY,(IY+w)     */
OP(fded,35) { EAY16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IY);   } /* LD (IY+w),IY     */
OP(fded,36) { union PAIR tmp; EAY(cpustate); RM16(cpustate, cpustate->ea, &tmp); SET_SP(cpustate, tmp.w.l); } /* LD SP,(IY+o)     */
OP(fded,37) { union PAIR tmp; EAY(cpustate); tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->ea); WM(cpustate, cpustate->ea, tmp.b.l); } /* EX A,(IY+o)      */

OP(fded,38) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,39) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,3a) { EAH16(cpustate); cpustate->_IY = cpustate->ea;                  } /* LDA IY,(HL+w)    */
OP(fded,3b) { illegal_1(cpustate, __func__); ed_3b(cpustate);                            } /* LD '(HL+w),A     */
OP(fded,3c) { EAH16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IY);   } /* LD IY,(HL+w)     */
OP(fded,3d) { EAH16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IY);   } /* LD (HL+w),IY     */
OP(fded,3e) { union PAIR tmp; tmp.w.l = _SP(cpustate); EAY(cpustate); WM16(cpustate, cpustate->ea, &tmp); } /* LD (IY+o),SP     */
OP(fded,3f) { illegal_1(cpustate, __func__);                                            } /* EX 'A,A         */

OP(fded,40) { union PAIR tmp; CHECK_PRIV_IO(cpustate) { EARA(cpustate); tmp.b.l = IN(cpustate, cpustate->_BC); WM(cpustate, cpustate->ea, tmp.b.l); cpustate->_F = (cpustate->_F & CF) | SZP[tmp.b.l]; } } /* IN (ra),(C)    */
OP(fded,41) { CHECK_PRIV_IO(cpustate) { EARA(cpustate); OUT(cpustate, cpustate->_BC, RM(cpustate, cpustate->ea)); } } /* OUT (C),(ra)   */
OP(fded,42) { SBC16( IY, cpustate->_BC );                                            } /* SBC  IY,BC       */
OP(fded,43) { illegal_1(cpustate, __func__); ed_43(cpustate);                            } /* LD   '(w),BC     */
OP(fded,44) { illegal_1(cpustate, __func__); ed_44(cpustate);                            } /* NEG  'A          */
OP(fded,45) { illegal_1(cpustate, __func__); ed_45(cpustate);                            } /* RETN '           */
OP(fded,46) { illegal_1(cpustate, __func__); ed_46(cpustate);                            } /* IM   '0          */
OP(fded,47) { illegal_1(cpustate, __func__); ed_47(cpustate);                            } /* LD   'I,A        */

OP(fded,48) { union PAIR tmp; CHECK_PRIV_IO(cpustate) { EAX16(cpustate); tmp.b.l = IN(cpustate, cpustate->_BC); WM(cpustate, cpustate->ea, tmp.b.l); cpustate->_F = (cpustate->_F & CF) | SZP[tmp.b.l]; } } /* IN (IX+w),(C)    */
OP(fded,49) { CHECK_PRIV_IO(cpustate) { EAX16(cpustate); OUT(cpustate, cpustate->_BC, RM(cpustate, cpustate->ea)); } } /* OUT (C),(IX+w)   */
OP(fded,4a) { ADC16( IY, cpustate->_BC );                                            } /* ADC  IY,BC       */
OP(fded,4b) { illegal_1(cpustate, __func__); ed_4b(cpustate);                            } /* LD   'BC,(w)     */
OP(fded,4c) { illegal_1(cpustate, __func__); ed_4c(cpustate);                            } /* NEG  'HL         */
OP(fded,4d) { illegal_1(cpustate, __func__); ed_4d(cpustate);                            } /* RETI '           */
OP(fded,4e) { illegal_1(cpustate, __func__); ed_4e(cpustate);                            } /* IM   '3          */
OP(fded,4f) { illegal_1(cpustate, __func__); ed_4f(cpustate);                            } /* LD   'R,A        */

OP(fded,50) { union PAIR tmp; CHECK_PRIV_IO(cpustate) { EAY16(cpustate); tmp.b.l = IN(cpustate, cpustate->_BC); WM(cpustate, cpustate->ea, tmp.b.l); cpustate->_F = (cpustate->_F & CF) | SZP[tmp.b.l]; } } /* IN (IY+w),(C)    */
OP(fded,51) { CHECK_PRIV_IO(cpustate) { EAY16(cpustate); OUT(cpustate, cpustate->_BC, RM(cpustate, cpustate->ea)); } } /* OUT (C),(IY+w)   */
OP(fded,52) { SBC16( IY, cpustate->_DE );                                            } /* SBC  IY,DE       */
OP(fded,53) { illegal_1(cpustate, __func__); ed_53(cpustate);                            } /* LD  '(w),DE      */
OP(fded,54) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,55) { illegal_1(cpustate, __func__); ed_55(cpustate);                            } /* RETIL '          */
OP(fded,56) { illegal_1(cpustate, __func__); ed_56(cpustate);                            } /* IM   '1          */
OP(fded,57) { illegal_1(cpustate, __func__); ed_57(cpustate);                            } /* LD   'A,I        */

OP(fded,58) { union PAIR tmp; CHECK_PRIV_IO(cpustate) { EAH16(cpustate); tmp.b.l = IN(cpustate, cpustate->_BC); WM(cpustate, cpustate->ea, tmp.b.l); cpustate->_F = (cpustate->_F & CF) | SZP[tmp.b.l]; } } /* IN (HL+w),(C)    */
OP(fded,59) { CHECK_PRIV_IO(cpustate) { EAH16(cpustate); OUT(cpustate, cpustate->_BC, RM(cpustate, cpustate->ea)); } } /* OUT (C),(HL+w)   */
OP(fded,5a) { ADC16( IY, cpustate->_DE );                                            } /* ADC  IY,DE       */
OP(fded,5b) { illegal_1(cpustate, __func__); ed_5b(cpustate);                            } /* LD   'DE,(w)     */
OP(fded,5c) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,5d) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,5e) { illegal_1(cpustate, __func__); ed_5e(cpustate);                            } /* IM   '2          */
OP(fded,5f) { illegal_1(cpustate, __func__); ed_5f(cpustate);                            } /* LD   'A,R        */

OP(fded,60) { CHECK_PRIV_IO(cpustate) { cpustate->_HY = IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_HY]; } } /* IN   HY,(C)      */
OP(fded,61) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC,cpustate->_HY); } } /* OUT  (C),HY       */
OP(fded,62) { SBC16( IY, cpustate->_IY );                                            } /* SBC  IY,IY       */
OP(fded,63) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,64) { illegal_1(cpustate, __func__); ed_64(cpustate);                            } /* EXTS 'A          */
OP(fded,65) { illegal_1(cpustate, __func__); ed_65(cpustate);                            } /* PCACHE '         */
OP(fded,66) { LD_REG_CTL( IY );                                               } /* LDCTL IY,(C)     */
OP(fded,67) { illegal_1(cpustate, __func__); ed_67(cpustate);                            } /* RRD  '          */

OP(fded,68) { CHECK_PRIV_IO(cpustate) { cpustate->_LY = IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_LY]; } } /* IN   LY,(C)       */
OP(fded,69) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC,cpustate->_LY); } } /* OUT  (C),LY       */
OP(fded,6a) { ADC16( IY, cpustate->_IY );                                            } /* ADC  IY,IY       */
OP(fded,6b) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,6c) { illegal_1(cpustate, __func__); ed_6c(cpustate);                            } /* EXTS 'HL          */
OP(fded,6d) { ADD16_A( IY );                                                  } /* ADD IY,A         */
OP(fded,6e) { LD_CTL_REG( IY );                                               } /* LDCTL (C),IY     */
OP(fded,6f) { illegal_1(cpustate, __func__); ed_6f(cpustate);                            } /* RLD  '         */

OP(fded,70) { illegal_1(cpustate, __func__); ed_70(cpustate);                            } /* TSTI '(C)         */
OP(fded,71) { illegal_1(cpustate, __func__); ed_71(cpustate);                            } /* SC  'w         */
OP(fded,72) { SBC16( IY, _SP(cpustate) );                                     } /* SBC  IY,SP       */
OP(fded,73) { illegal_1(cpustate, __func__); ed_73(cpustate);                            } /* LD  '(w),SP      */
OP(fded,74) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,75) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,76) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,77) { illegal_1(cpustate, __func__); ed_77(cpustate);                            } /* DI  'n           */

OP(fded,78) { illegal_1(cpustate, __func__); ed_78(cpustate);                            } /* IN 'A,(C)       */
OP(fded,79) { illegal_1(cpustate, __func__); ed_79(cpustate);                            } /* OUT '(C),A      */
OP(fded,7a) { ADC16( IY, _SP(cpustate) );                                     } /* ADC  IY,SP       */
OP(fded,7b) { illegal_1(cpustate, __func__); ed_7b(cpustate);                            } /* LD  'SP,(w)      */
OP(fded,7c) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,7d) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,7e) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,7f) { illegal_1(cpustate, __func__); ed_7f(cpustate);                            } /* EI   'n          */

OP(fded,80) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,81) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,82) { illegal_1(cpustate, __func__); ed_82(cpustate);                            } /* INIW '           */
OP(fded,83) { illegal_1(cpustate, __func__); ed_83(cpustate);                            } /* OUTIW '          */
OP(fded,84) { illegal_1(cpustate, __func__); ed_84(cpustate);                            } /* EPUM '(SP+w)     */
OP(fded,85) { illegal_1(cpustate, __func__); ed_85(cpustate);                            } /* MEPU '(SP+w)     */
OP(fded,86) { EAY(cpustate); LDU_A_M(0);                                       } /* LDUD A,(IY+o)    */
OP(fded,87) { cpustate->_IY = cpustate->_USP;                                 } /* LDCTL IY,USP     */

OP(fded,88) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,89) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,8a) { illegal_1(cpustate, __func__); ed_8a(cpustate);                            } /* INDW '         */
OP(fded,8b) { illegal_1(cpustate, __func__); ed_8b(cpustate);                            } /* OUTDW '         */
OP(fded,8c) { illegal_1(cpustate, __func__); ed_8c(cpustate);                            } /* EPUM '(HL+IX)   */
OP(fded,8d) { illegal_1(cpustate, __func__); ed_8d(cpustate);                            } /* MEPU '(HL+IX)   */
OP(fded,8e) { EAY(cpustate); LDU_M_A(0);                                       } /* LDUD (IY+o),A    */
OP(fded,8f) { cpustate->_USP = cpustate->_IY;                                 } /* LDCTL USP,IY     */

OP(fded,90) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,91) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,92) { illegal_1(cpustate, __func__); ed_92(cpustate);                            } /* INIRW '          */
OP(fded,93) { illegal_1(cpustate, __func__); ed_93(cpustate);                            } /* OTIRW '          */
OP(fded,94) { illegal_1(cpustate, __func__); ed_94(cpustate);                            } /* EPUM '(HL+IY)     */
OP(fded,95) { illegal_1(cpustate, __func__); ed_95(cpustate);                            } /* MEPU '(HL+IY)     */
OP(fded,96) { EAY(cpustate); LDU_A_M(1);                                       } /* LDUP A,(IY+o)    */
OP(fded,97) { illegal_1(cpustate, __func__); ed_97(cpustate);                            } /* EPUF '          */

OP(fded,98) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,99) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,9a) { illegal_1(cpustate, __func__); ed_9a(cpustate);                            } /* INDRW '         */
OP(fded,9b) { illegal_1(cpustate, __func__); ed_9b(cpustate);                            } /* OTDRW '          */
OP(fded,9c) { illegal_1(cpustate, __func__); ed_9c(cpustate);                            } /* EPUM '(IX+IY)     */
OP(fded,9d) { illegal_1(cpustate, __func__); ed_9d(cpustate);                            } /* EPUM '(IX+IY)     */
OP(fded,9e) { EAY(cpustate); LDU_M_A(1);                                       } /* LDUP (IY+o),A    */
OP(fded,9f) { illegal_1(cpustate, __func__); ed_9f(cpustate);                            } /* EPUI '          */

OP(fded,a0) { illegal_1(cpustate, __func__); ed_a0(cpustate);                            } /* LDI '          */
OP(fded,a1) { illegal_1(cpustate, __func__); ed_a1(cpustate);                            } /* CPI '          */
OP(fded,a2) { illegal_1(cpustate, __func__); ed_a2(cpustate);                            } /* INI '          */
OP(fded,a3) { illegal_1(cpustate, __func__); ed_a3(cpustate);                            } /* OUTI '          */
OP(fded,a4) { illegal_1(cpustate, __func__); ed_a4(cpustate);                            } /* EPUM '(ra)     */
OP(fded,a5) { illegal_1(cpustate, __func__); ed_a5(cpustate);                            } /* MEPU '(ra)     */
OP(fded,a6) { illegal_1(cpustate, __func__); ed_a6(cpustate);                            } /* EPUM '(HL)     */
OP(fded,a7) { illegal_1(cpustate, __func__); ed_a7(cpustate);                            } /* EPUM '(w)     */

OP(fded,a8) { illegal_1(cpustate, __func__); ed_a8(cpustate);                            } /* LDD '          */
OP(fded,a9) { illegal_1(cpustate, __func__); ed_a9(cpustate);                            } /* CPD '          */
OP(fded,aa) { illegal_1(cpustate, __func__); ed_aa(cpustate);                            } /* IND '          */
OP(fded,ab) { illegal_1(cpustate, __func__); ed_ab(cpustate);                            } /* OUTD '          */
OP(fded,ac) { illegal_1(cpustate, __func__); ed_ac(cpustate);                            } /* EPUM '(IX+w)    */
OP(fded,ad) { illegal_1(cpustate, __func__); ed_ad(cpustate);                            } /* MEPU '(IX+w)     */
OP(fded,ae) { illegal_1(cpustate, __func__); ed_ae(cpustate);                            } /* MEPU '(HL)     */
OP(fded,af) { illegal_1(cpustate, __func__); ed_af(cpustate);                            } /* MEPU '(w)       */

OP(fded,b0) { illegal_1(cpustate, __func__); ed_b0(cpustate);                            } /* LDIR '          */
OP(fded,b1) { illegal_1(cpustate, __func__); ed_b1(cpustate);                            } /* CPIR '          */
OP(fded,b2) { illegal_1(cpustate, __func__); ed_b2(cpustate);                            } /* INIR '          */
OP(fded,b3) { illegal_1(cpustate, __func__); ed_b3(cpustate);                            } /* OTIR '          */
OP(fded,b4) { illegal_1(cpustate, __func__); ed_b4(cpustate);                            } /* EPUM '(IY+w)     */
OP(fded,b5) { illegal_1(cpustate, __func__); ed_b5(cpustate);                            } /* MEPU '(IY+w)     */
OP(fded,b6) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,b7) { illegal_1(cpustate, __func__); ed_b7(cpustate);                            } /* INW 'HL,(C)      */

OP(fded,b8) { illegal_1(cpustate, __func__); ed_b8(cpustate);                            } /* LDDR '          */
OP(fded,b9) { illegal_1(cpustate, __func__); ed_b9(cpustate);                            } /* CPDR '          */
OP(fded,ba) { illegal_1(cpustate, __func__); ed_ba(cpustate);                            } /* INDR '          */
OP(fded,bb) { illegal_1(cpustate, __func__); ed_bb(cpustate);                            } /* OTDR '          */
OP(fded,bc) { illegal_1(cpustate, __func__); ed_bc(cpustate);                            } /* EPUM '(HL+w)     */
OP(fded,bd) { illegal_1(cpustate, __func__); ed_bd(cpustate);                            } /* MEPU '(HL+w)     */
OP(fded,be) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(fded,bf) { illegal_1(cpustate, __func__); ed_bf(cpustate);                            } /* OUTW '(C),HL     */

OP(fded,c0) { EARA(cpustate); MULT( RM(cpustate, cpustate->ea) );           } /* MULT A,(ra)    */
OP(fded,c1) { EARA(cpustate); MULTU( RM(cpustate, cpustate->ea) );          } /* MULTU A,(ra)   */
OP(fded,c2) { union PAIR tmp; EAX16(cpustate); RM16(cpustate, cpustate->ea, &tmp); MULTW( tmp.w.l ); } /* MULTW HL,(IX+w)    */
OP(fded,c3) { union PAIR tmp; EAX16(cpustate); RM16(cpustate, cpustate->ea, &tmp); MULTUW( tmp.w.l ); } /* MULTUW HL,(IX+w)   */
OP(fded,c4) { EARA(cpustate); DIV( RM(cpustate, cpustate->ea) );            } /* DIV HL,(ra)    */
OP(fded,c5) { EARA(cpustate); DIVU( RM(cpustate, cpustate->ea) );           } /* DIVU HL,(ra)   */
OP(fded,c6) { union PAIR tmp; EAX16(cpustate); RM16(cpustate, cpustate->ea, &tmp); ADD16F(tmp.w.l); } /* ADDW HL,(IX+w)     */
OP(fded,c7) { union PAIR tmp; EAX16(cpustate); RM16(cpustate, cpustate->ea, &tmp); CP16(tmp.w.l); } /* CPW HL,(IX+w)      */

OP(fded,c8) { EAX16(cpustate); MULT( RM(cpustate, cpustate->ea) );             } /* MULT A,(IX+w)   */
OP(fded,c9) { EAX16(cpustate); MULTU( RM(cpustate, cpustate->ea) );            } /* MULTU A,(IX+w)  */
OP(fded,ca) { union PAIR tmp; EAX16(cpustate); RM16(cpustate, cpustate->ea, &tmp); DIVW( tmp.w.l ); } /* DIVW DEHL,(IX+w)   */
OP(fded,cb) { union PAIR tmp; EAX16(cpustate); RM16(cpustate, cpustate->ea, &tmp); DIVUW( tmp.w.l ); } /* DIVUW DEHL,(IX+w)  */
OP(fded,cc) { EAX16(cpustate); DIV( RM(cpustate, cpustate->ea) );              } /* DIV HL,(IX+w)   */
OP(fded,cd) { EAX16(cpustate); DIVU( RM(cpustate, cpustate->ea) );             } /* DIVU HL,(IX+w)  */
OP(fded,ce) { union PAIR tmp; EAX16(cpustate); RM16(cpustate, cpustate->ea, &tmp); SUB16(tmp.w.l); } /* SUBW HL,(IX+w)     */
OP(fded,cf) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */

OP(fded,d0) { EAY16(cpustate); MULT( RM(cpustate, cpustate->ea) );           } /* MULT A,(IY+w)    */
OP(fded,d1) { EAY16(cpustate); MULTU( RM(cpustate, cpustate->ea) );          } /* MULTU A,(IY+w)   */
OP(fded,d2) { union PAIR tmp; EAY16(cpustate); RM16(cpustate, cpustate->ea, &tmp); MULTW( tmp.w.l ); } /* MULTW (IY+w)    */
OP(fded,d3) { union PAIR tmp; EAY16(cpustate); RM16(cpustate, cpustate->ea, &tmp); MULTUW( tmp.w.l ); } /* MULTUW (IY+w)   */
OP(fded,d4) { EAY16(cpustate); DIV( RM(cpustate, cpustate->ea) );            } /* DIV HL,(IY+w)    */
OP(fded,d5) { EAY16(cpustate); DIVU( RM(cpustate, cpustate->ea) );           } /* DIVU HL,(IY+w)   */
OP(fded,d6) { union PAIR tmp; EAY16(cpustate); RM16(cpustate, cpustate->ea, &tmp); ADD16F(tmp.w.l); } /* ADDW HL,(IY+w)     */
OP(fded,d7) { union PAIR tmp; EAY16(cpustate); RM16(cpustate, cpustate->ea, &tmp); CP16(tmp.w.l); } /* CPW HL,(IY+w)      */

OP(fded,d8) { EAH16(cpustate); MULT( RM(cpustate, cpustate->ea) );             } /* MULT A,(HL+w)   */
OP(fded,d9) { EAH16(cpustate); MULTU( RM(cpustate, cpustate->ea) );            } /* MULTU A,(HL+w)  */
OP(fded,da) { union PAIR tmp; EAY16(cpustate); RM16(cpustate, cpustate->ea, &tmp); DIVW( tmp.w.l ); } /* DIVW DEHL,(IY+w)   */
OP(fded,db) { union PAIR tmp; EAY16(cpustate); RM16(cpustate, cpustate->ea, &tmp); DIVUW( tmp.w.l ); } /* DIVUW DEHL,(IY+w)  */
OP(fded,dc) { EAH16(cpustate); DIV( RM(cpustate, cpustate->ea) );              } /* DIV HL,(HL+w)   */
OP(fded,dd) { EAH16(cpustate); DIVU( RM(cpustate, cpustate->ea) );             } /* DIVU HL,(HL+w)  */
OP(fded,de) { union PAIR tmp; EAY16(cpustate); RM16(cpustate, cpustate->ea, &tmp); SUB16(tmp.w.l); } /* SUBW HL,(IY+w)     */
OP(fded,df) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */

OP(fded,e0) { MULT( cpustate->_HY );                                          } /* MULT A,HY        */
OP(fded,e1) { MULTU( cpustate->_HY );                                         } /* MULTU A,HY       */
OP(fded,e2) { MULTW( cpustate->_IY );                                         } /* MULTW HL,IY      */
OP(fded,e3) { MULTUW( cpustate->_IY );                                        } /* MULTUW HL,IY     */
OP(fded,e4) { DIV( cpustate->_HY );                                           } /* DIV HL,HY        */
OP(fded,e5) { DIVU( cpustate->_HY );                                          } /* DIVU HL,HY       */
OP(fded,e6) { ADD16F(cpustate->_IY);                                          } /* ADDW HL,IY       */
OP(fded,e7) { CP16(cpustate->_IY);                                            } /* CPW HL,IY        */

OP(fded,e8) { MULT( cpustate->_LY );                                          } /* MULT A,LY        */
OP(fded,e9) { MULTU( cpustate->_LY );                                         } /* MULTU A,LY       */
OP(fded,ea) { DIVW( cpustate->_IY );                                          } /* DIVW DEHL,IY     */
OP(fded,eb) { DIVUW( cpustate->_IY );                                         } /* DIVUW DEHL,IY    */
OP(fded,ec) { DIV( cpustate->_LY );                                           } /* DIV HL,LY        */
OP(fded,ed) { DIVU( cpustate->_LY );                                          } /* DIVU HL,LY       */
OP(fded,ee) { SUB16(cpustate->_IY);                                           } /* SUBW HL,IY       */
OP(fded,ef) { illegal_1(cpustate, __func__); ed_ef(cpustate);                           } /* EX 'H,L          */

OP(fded,f0) { EAY(cpustate); MULT( RM(cpustate, cpustate->ea) );              } /* MULT A,(IY+o)    */
OP(fded,f1) { EAY(cpustate); MULTU( RM(cpustate, cpustate->ea) );             } /* MULTU A,(IY+o)   */
OP(fded,f2) { MULTW( ARG16(cpustate) );                                       } /* MULTW HL,w       */
OP(fded,f3) { MULTUW( ARG16(cpustate) );                                      } /* MULTUW HL,w      */
OP(fded,f4) { EAY(cpustate); DIV( RM(cpustate, cpustate->ea) );               } /* DIV HL,(IY+o)    */
OP(fded,f5) { EAY(cpustate); DIVU( RM(cpustate, cpustate->ea) );              } /* DIVU HL,(IY+o)   */
OP(fded,f6) { ADD16F( ARG16(cpustate) );                                      } /* ADDW HL,w        */
OP(fded,f7) { CP16( ARG16(cpustate));                                         } /* CPW HL,w         */
																								   
OP(fded,f8) { MULT( ARG(cpustate) );                                          } /* MULT A,n         */
OP(fded,f9) { MULTU( ARG(cpustate) );                                         } /* MULTU A,n        */
OP(fded,fa) { DIVW( ARG16(cpustate) );                                        } /* DIVW DEHL,w      */
OP(fded,fb) { DIVUW( ARG16(cpustate) );                                       } /* DIVUW DEHL,w     */
OP(fded,fc) { DIV( ARG(cpustate) );                                           } /* DIV HL,n         */
OP(fded,fd) { DIVU( ARG(cpustate) );                                          } /* DIVU HL,n        */
OP(fded,fe) { SUB16( ARG16(cpustate) );                                       } /* SUBW HL,w        */
OP(fded,ff) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
