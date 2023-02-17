/**********************************************************
 * special opcodes (DD ED prefix)
 **********************************************************/
OP(dded,00) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,01) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,02) { EASP16(cpustate); cpustate->_IX = cpustate->ea;                 } /* LDA IX,(SP+w)    */
OP(dded,03) { illegal_1(cpustate, __func__); ed_03(cpustate);                           } /* LD '(SP+w),A     */
OP(dded,04) { EASP16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IX);  } /* LD IX,(SP+w)     */
OP(dded,05) { EASP16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IX);  } /* LD (SP+w),IX     */
OP(dded,06) { EAX(cpustate); RM16(cpustate, cpustate->ea, &cpustate->BC);     } /* LD BC,(IX+o)     */
OP(dded,07) { union PAIR tmp; EASP16(cpustate); tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->ea); WM(cpustate, cpustate->ea, tmp.b.l); } /* EX A,(SP+w)      */

OP(dded,08) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,09) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,0a) { EAHX(cpustate); cpustate->_IX = cpustate->ea;                   } /* LDA IX,(HL+IX)   */
OP(dded,0b) { illegal_1(cpustate, __func__); ed_0b(cpustate);                            } /* LD '(HL+IX),A    */
OP(dded,0c) { EAHX(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IX);    } /* LD IX,(HL+IX)    */
OP(dded,0d) { EAHX(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IX);    } /* LD (HL+IX),IX    */
OP(dded,0e) { EAX(cpustate); WM16(cpustate, cpustate->ea, &cpustate->BC);     } /* LD (IX+o),BC     */
OP(dded,0f) { union PAIR tmp; EAHX(cpustate); tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->ea); WM(cpustate, cpustate->ea, tmp.b.l); } /* EX A,(HL+IX)      */

OP(dded,10) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,11) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,12) { EAHY(cpustate); cpustate->_IX = cpustate->ea;                   } /* LDA IX,(HL+IY)   */
OP(dded,13) { illegal_1(cpustate, __func__); ed_13(cpustate);                            } /* LD '(HL+IY),A    */
OP(dded,14) { EAHY(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IX);    } /* LD IX,(HL+IY)    */
OP(dded,15) { EAHY(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IX);    } /* LD (HL+IY),IX    */
OP(dded,16) { EAX(cpustate); RM16(cpustate, cpustate->ea, &cpustate->DE);     } /* LD DE,(IX+o)     */
OP(dded,17) { union PAIR tmp; EAHY(cpustate); tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->ea); WM(cpustate, cpustate->ea, tmp.b.l); } /* EX A,(HL+IY)      */

OP(dded,18) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,19) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,1a) { EAXY(cpustate); cpustate->_IX = cpustate->ea;                   } /* LDA IX,(IX+IY)   */
OP(dded,1b) { illegal_1(cpustate, __func__); ed_1b(cpustate);                            } /* LD '(IX+IY),A    */
OP(dded,1c) { EAXY(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IX);    } /* LD IX,(IX+IY)    */
OP(dded,1d) { EAXY(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IX);    } /* LD (IX+IY),IX    */
OP(dded,1e) { EAX(cpustate); WM16(cpustate, cpustate->ea, &cpustate->DE);     } /* LD (IX+o),DE     */
OP(dded,1f) { union PAIR tmp; EAXY(cpustate); tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->ea); WM(cpustate, cpustate->ea, tmp.b.l); } /* EX A,(IX+IY)      */

OP(dded,20) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,21) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,22) { EARA(cpustate); cpustate->_IX = cpustate->ea;                   } /* LDA IX,(ra)      */
OP(dded,23) { illegal_1(cpustate, __func__); ed_23(cpustate);                            } /* LD '(ra),A       */
OP(dded,24) { EARA(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IX);    } /* LD IX,(ra)       */
OP(dded,25) { EARA(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IX);    } /* LD (ra),IX       */
OP(dded,26) { EAX(cpustate); RM16(cpustate, cpustate->ea, &cpustate->HL);     } /* LD HL,(IX+o)     */
OP(dded,27) { union PAIR tmp; tmp.b.l = cpustate->_A; cpustate->_A = cpustate->_HX; cpustate->_HX = tmp.b.l; } /* EX A,HX          */

OP(dded,28) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,29) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,2a) { EAX16(cpustate); cpustate->_IX = cpustate->ea;                  } /* LDA IX,(IX+w)    */
OP(dded,2b) { illegal_1(cpustate, __func__); ed_2b(cpustate);                            } /* LD '(IX+w),A     */
OP(dded,2c) { EAX16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IX);   } /* LD IX,(IX+w)     */
OP(dded,2d) { EAX16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IX);   } /* LD (IX+w),IX     */
OP(dded,2e) { EAX(cpustate); WM16(cpustate, cpustate->ea, &cpustate->HL);     } /* LD (IX+o),HL     */
OP(dded,2f) { union PAIR tmp; tmp.b.l = cpustate->_A; cpustate->_A = cpustate->_LX; cpustate->_LX = tmp.b.l; } /* EX A,LX          */

OP(dded,30) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,31) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,32) { EAY16(cpustate); cpustate->_IX = cpustate->ea;                  } /* LDA IX,(IY+w)    */
OP(dded,33) { illegal_1(cpustate, __func__); ed_33(cpustate);                            } /* LD '(IY+w),A     */
OP(dded,34) { EAY16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IX);   } /* LD IX,(IY+w)     */
OP(dded,35) { EAY16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IX);   } /* LD (IY+w),IX     */
OP(dded,36) { union PAIR tmp; EAX(cpustate); RM16(cpustate, cpustate->ea, &tmp); SET_SP(cpustate, tmp.w.l); } /* LD SP,(IX+o)     */
OP(dded,37) { union PAIR tmp; EAX(cpustate); tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->ea); WM(cpustate, cpustate->ea, tmp.b.l); } /* EX A,(IX+o)      */

OP(dded,38) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,39) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,3a) { EAH16(cpustate); cpustate->_IX = cpustate->ea;                  } /* LDA IX,(HL+w)    */
OP(dded,3b) { illegal_1(cpustate, __func__); ed_3b(cpustate);                            } /* LD '(HL+w),A     */
OP(dded,3c) { EAH16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->IX);   } /* LD IX,(HL+w)     */
OP(dded,3d) { EAH16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->IX);   } /* LD (HL+w),IX     */
OP(dded,3e) { union PAIR tmp; tmp.w.l = _SP(cpustate); EAX(cpustate); WM16(cpustate, cpustate->ea, &tmp); } /* LD (IX+o),SP     */
OP(dded,3f) { union PAIR tmp; cpustate->ea = ARG16(cpustate); tmp.b.l = cpustate->_A; cpustate->_A = RM(cpustate, cpustate->ea); WM(cpustate, cpustate->ea, tmp.b.l); } /* EX A,(w)      */

OP(dded,40) { union PAIR tmp; CHECK_PRIV_IO(cpustate) { EASP16(cpustate); tmp.b.l = IN(cpustate, cpustate->_BC); WM(cpustate, cpustate->ea, tmp.b.l); cpustate->_F = (cpustate->_F & CF) | SZP[tmp.b.l]; } } /* IN (SP+w),(C)    */
OP(dded,41) { CHECK_PRIV_IO(cpustate) { EASP16(cpustate); OUT(cpustate, cpustate->_BC, RM(cpustate, cpustate->ea)); } } /* OUT (C),(SP+w)   */
OP(dded,42) { SBC16( IX, cpustate->_BC );                                            } /* SBC  IX,BC       */
OP(dded,43) { illegal_1(cpustate, __func__); ed_43(cpustate);                            } /* LD   '(w),BC     */
OP(dded,44) { illegal_1(cpustate, __func__); ed_44(cpustate);                            } /* NEG  'A          */
OP(dded,45) { illegal_1(cpustate, __func__); ed_45(cpustate);                            } /* RETN '           */
OP(dded,46) { illegal_1(cpustate, __func__); ed_46(cpustate);                            } /* IM   '0          */
OP(dded,47) { illegal_1(cpustate, __func__); ed_47(cpustate);                            } /* LD   'I,A        */

OP(dded,48) { union PAIR tmp; CHECK_PRIV_IO(cpustate) { EAHX(cpustate); tmp.b.l = IN(cpustate, cpustate->_BC); WM(cpustate, cpustate->ea, tmp.b.l); cpustate->_F = (cpustate->_F & CF) | SZP[tmp.b.l]; } } /* IN (HL+IX),(C)    */
OP(dded,49) { CHECK_PRIV_IO(cpustate) { EAHX(cpustate); OUT(cpustate, cpustate->_BC, RM(cpustate, cpustate->ea)); } } /* OUT (C),(HL+IX)   */
OP(dded,4a) { ADC16( IX, cpustate->_BC );                                            } /* ADC  IX,BC       */
OP(dded,4b) { illegal_1(cpustate, __func__); ed_4b(cpustate);                            } /* LD   'BC,(w)     */
OP(dded,4c) { illegal_1(cpustate, __func__); ed_4c(cpustate);                            } /* NEG  'HL         */
OP(dded,4d) { illegal_1(cpustate, __func__); ed_4d(cpustate);                            } /* RETI '           */
OP(dded,4e) { illegal_1(cpustate, __func__); ed_4e(cpustate);                            } /* IM   '3          */
OP(dded,4f) { illegal_1(cpustate, __func__); ed_4f(cpustate);                            } /* LD   'R,A        */

OP(dded,50) { union PAIR tmp; CHECK_PRIV_IO(cpustate) { EAHY(cpustate); tmp.b.l = IN(cpustate, cpustate->_BC); WM(cpustate, cpustate->ea, tmp.b.l); cpustate->_F = (cpustate->_F & CF) | SZP[tmp.b.l]; } } /* IN (HL+IY),(C)    */
OP(dded,51) { CHECK_PRIV_IO(cpustate) { EAHY(cpustate); OUT(cpustate, cpustate->_BC, RM(cpustate, cpustate->ea)); } } /* OUT (C),(HL+IY)   */
OP(dded,52) { SBC16( IX, cpustate->_DE );                                            } /* SBC  IX,DE       */
OP(dded,53) { illegal_1(cpustate, __func__); ed_53(cpustate);                            } /* LD  '(w),DE      */
OP(dded,54) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,55) { illegal_1(cpustate, __func__); ed_55(cpustate);                            } /* RETIL '          */
OP(dded,56) { illegal_1(cpustate, __func__); ed_56(cpustate);                            } /* IM   '1          */
OP(dded,57) { illegal_1(cpustate, __func__); ed_57(cpustate);                            } /* LD   'A,I        */

OP(dded,58) { union PAIR tmp; CHECK_PRIV_IO(cpustate) { EAXY(cpustate); tmp.b.l = IN(cpustate, cpustate->_BC); WM(cpustate, cpustate->ea, tmp.b.l); cpustate->_F = (cpustate->_F & CF) | SZP[tmp.b.l]; } } /* IN (IX+IY),(C)    */
OP(dded,59) { CHECK_PRIV_IO(cpustate) { EAXY(cpustate); OUT(cpustate, cpustate->_BC, RM(cpustate, cpustate->ea)); } } /* OUT (C),(IX+IY)   */
OP(dded,5a) { ADC16( IX, cpustate->_DE );                                            } /* ADC  IX,DE       */
OP(dded,5b) { illegal_1(cpustate, __func__); ed_5b(cpustate);                            } /* LD   'DE,(w)     */
OP(dded,5c) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,5d) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,5e) { illegal_1(cpustate, __func__); ed_5e(cpustate);                            } /* IM   '2          */
OP(dded,5f) { illegal_1(cpustate, __func__); ed_5f(cpustate);                            } /* LD   'A,R        */

OP(dded,60) { CHECK_PRIV_IO(cpustate) { cpustate->_HX = IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_HX]; } } /* IN   HX,(C)      */
OP(dded,61) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC,cpustate->_HX); } } /* OUT  (C),HX       */
OP(dded,62) { SBC16( IX, cpustate->_IX );                                            } /* SBC  IX,IX       */
OP(dded,63) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,64) { illegal_1(cpustate, __func__); ed_64(cpustate);                            } /* EXTS 'A          */
OP(dded,65) { illegal_1(cpustate, __func__); ed_65(cpustate);                            } /* PCACHE '         */
OP(dded,66) { LD_REG_CTL( IX );                                         } /* LDCTL IX,(C)     */
OP(dded,67) { illegal_1(cpustate, __func__); ed_67(cpustate);                            } /* RRD  '          */

OP(dded,68) { CHECK_PRIV_IO(cpustate) { cpustate->_LX = IN(cpustate, cpustate->_BC); cpustate->_F = (cpustate->_F & CF) | SZP[cpustate->_LX]; } } /* IN   LX,(C)       */
OP(dded,69) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC,cpustate->_LX); } } /* OUT  (C),LX       */
OP(dded,6a) { ADC16( IX, cpustate->_IX );                                            } /* ADC  IX,IX       */
OP(dded,6b) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,6c) { illegal_1(cpustate, __func__); ed_6c(cpustate);                            } /* EXTS 'HL          */
OP(dded,6d) { ADD16_A( IX );                                          } /* ADD IX,A         */
OP(dded,6e) { LD_CTL_REG( IX );                                          } /* LDCTL (C),IX     */
OP(dded,6f) { illegal_1(cpustate, __func__); ed_6f(cpustate);                            } /* RLD  '         */

OP(dded,70) { illegal_1(cpustate, __func__); ed_70(cpustate);                            } /* TSTI '(C)         */
OP(dded,71) { illegal_1(cpustate, __func__); ed_71(cpustate);                            } /* SC  'w         */
OP(dded,72) { SBC16( IX, _SP(cpustate) );                                            } /* SBC  IX,SP       */
OP(dded,73) { illegal_1(cpustate, __func__); ed_73(cpustate);                            } /* LD  '(w),SP      */
OP(dded,74) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,75) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,76) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,77) { illegal_1(cpustate, __func__); ed_77(cpustate);                            } /* DI  'n           */

OP(dded,78) { union PAIR tmp; CHECK_PRIV_IO(cpustate) { tmp.b.l = IN(cpustate, cpustate->_BC); WM(cpustate, ARG16(cpustate), tmp.b.l); cpustate->_F = (cpustate->_F & CF) | SZP[tmp.b.l]; } } /* IN (w),(C)    */
OP(dded,79) { CHECK_PRIV_IO(cpustate) { OUT(cpustate, cpustate->_BC, RM(cpustate, ARG16(cpustate))); } } /* OUT (C),(w)   */
OP(dded,7a) { ADC16( IX, _SP(cpustate) );                                            } /* ADC  IX,SP       */
OP(dded,7b) { illegal_1(cpustate, __func__); ed_7b(cpustate);                            } /* LD  'SP,(w)      */
OP(dded,7c) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,7d) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,7e) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,7f) { illegal_1(cpustate, __func__); ed_7f(cpustate);                            } /* EI   'n          */

OP(dded,80) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,81) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,82) { illegal_1(cpustate, __func__); ed_82(cpustate);                            } /* INIW '           */
OP(dded,83) { illegal_1(cpustate, __func__); ed_83(cpustate);                            } /* OUTIW '          */
OP(dded,84) { illegal_1(cpustate, __func__); ed_84(cpustate);                            } /* EPUM '(SP+w)     */
OP(dded,85) { illegal_1(cpustate, __func__); ed_85(cpustate);                            } /* MEPU '(SP+w)     */
OP(dded,86) { EAX(cpustate); LDU_A_M(0);                                       } /* LDUD A,(IX+o)    */
OP(dded,87) { cpustate->_IX = cpustate->_USP;                                 } /* LDCTL IX,USP     */

OP(dded,88) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,89) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,8a) { illegal_1(cpustate, __func__); ed_8a(cpustate);                            } /* INDW '         */
OP(dded,8b) { illegal_1(cpustate, __func__); ed_8b(cpustate);                            } /* OUTDW '         */
OP(dded,8c) { illegal_1(cpustate, __func__); ed_8c(cpustate);                            } /* EPUM '(HL+IX)   */
OP(dded,8d) { illegal_1(cpustate, __func__); ed_8d(cpustate);                            } /* MEPU '(HL+IX)   */
OP(dded,8e) { EAX(cpustate); LDU_M_A(0);                                       } /* LDUD (IX+o),A    */
OP(dded,8f) { cpustate->_USP = cpustate->_IX;                                 } /* LDCTL USP,IX     */

OP(dded,90) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,91) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,92) { illegal_1(cpustate, __func__); ed_92(cpustate);                            } /* INIRW '          */
OP(dded,93) { illegal_1(cpustate, __func__); ed_93(cpustate);                            } /* OTIRW '          */
OP(dded,94) { illegal_1(cpustate, __func__); ed_94(cpustate);                            } /* EPUM '(HL+IY)     */
OP(dded,95) { illegal_1(cpustate, __func__); ed_95(cpustate);                            } /* MEPU '(HL+IY)     */
OP(dded,96) { EAX(cpustate); LDU_A_M(1);                                       } /* LDUP A,(IX+o)    */
OP(dded,97) { illegal_1(cpustate, __func__); ed_97(cpustate);                            } /* EPUF '          */

OP(dded,98) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,99) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,9a) { illegal_1(cpustate, __func__); ed_9a(cpustate);                            } /* INDRW '         */
OP(dded,9b) { illegal_1(cpustate, __func__); ed_9b(cpustate);                            } /* OTDRW '          */
OP(dded,9c) { illegal_1(cpustate, __func__); ed_9c(cpustate);                            } /* EPUM '(IX+IY)     */
OP(dded,9d) { illegal_1(cpustate, __func__); ed_9d(cpustate);                            } /* EPUM '(IX+IY)     */
OP(dded,9e) { EAX(cpustate); LDU_M_A(1);                                       } /* LDUP (IX+o),A    */
OP(dded,9f) { illegal_1(cpustate, __func__); ed_9f(cpustate);                            } /* EPUI '          */

OP(dded,a0) { illegal_1(cpustate, __func__); ed_a0(cpustate);                            } /* LDI '          */
OP(dded,a1) { illegal_1(cpustate, __func__); ed_a1(cpustate);                            } /* CPI '          */
OP(dded,a2) { illegal_1(cpustate, __func__); ed_a2(cpustate);                            } /* INI '          */
OP(dded,a3) { illegal_1(cpustate, __func__); ed_a3(cpustate);                            } /* OUTI '          */
OP(dded,a4) { illegal_1(cpustate, __func__); ed_a4(cpustate);                            } /* EPUM '(ra)     */
OP(dded,a5) { illegal_1(cpustate, __func__); ed_a5(cpustate);                            } /* MEPU '(ra)     */
OP(dded,a6) { illegal_1(cpustate, __func__); ed_a6(cpustate);                            } /* EPUM '(HL)     */
OP(dded,a7) { illegal_1(cpustate, __func__); ed_a7(cpustate);                            } /* EPUM '(w)     */

OP(dded,a8) { illegal_1(cpustate, __func__); ed_a8(cpustate);                            } /* LDD '          */
OP(dded,a9) { illegal_1(cpustate, __func__); ed_a9(cpustate);                            } /* CPD '          */
OP(dded,aa) { illegal_1(cpustate, __func__); ed_aa(cpustate);                            } /* IND '          */
OP(dded,ab) { illegal_1(cpustate, __func__); ed_ab(cpustate);                            } /* OUTD '          */
OP(dded,ac) { illegal_1(cpustate, __func__); ed_ac(cpustate);                            } /* EPUM '(IX+w)    */
OP(dded,ad) { illegal_1(cpustate, __func__); ed_ad(cpustate);                            } /* MEPU '(IX+w)     */
OP(dded,ae) { illegal_1(cpustate, __func__); ed_ae(cpustate);                            } /* MEPU '(HL)     */
OP(dded,af) { illegal_1(cpustate, __func__); ed_af(cpustate);                            } /* MEPU '(w)       */

OP(dded,b0) { illegal_1(cpustate, __func__); ed_b0(cpustate);                            } /* LDIR '          */
OP(dded,b1) { illegal_1(cpustate, __func__); ed_b1(cpustate);                            } /* CPIR '          */
OP(dded,b2) { illegal_1(cpustate, __func__); ed_b2(cpustate);                            } /* INIR '          */
OP(dded,b3) { illegal_1(cpustate, __func__); ed_b3(cpustate);                            } /* OTIR '          */
OP(dded,b4) { illegal_1(cpustate, __func__); ed_b4(cpustate);                            } /* EPUM '(IY+w)     */
OP(dded,b5) { illegal_1(cpustate, __func__); ed_b5(cpustate);                            } /* MEPU '(IY+w)     */
OP(dded,b6) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,b7) { illegal_1(cpustate, __func__); ed_b7(cpustate);                            } /* INW 'HL,(C)      */

OP(dded,b8) { illegal_1(cpustate, __func__); ed_b8(cpustate);                            } /* LDDR '          */
OP(dded,b9) { illegal_1(cpustate, __func__); ed_b9(cpustate);                            } /* CPDR '          */
OP(dded,ba) { illegal_1(cpustate, __func__); ed_ba(cpustate);                            } /* INDR '          */
OP(dded,bb) { illegal_1(cpustate, __func__); ed_bb(cpustate);                            } /* OTDR '          */
OP(dded,bc) { illegal_1(cpustate, __func__); ed_bc(cpustate);                            } /* EPUM '(HL+w)     */
OP(dded,bd) { illegal_1(cpustate, __func__); ed_bd(cpustate);                            } /* MEPU '(HL+w)     */
OP(dded,be) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
OP(dded,bf) { illegal_1(cpustate, __func__); ed_bf(cpustate);                            } /* OUTW '(C),HL     */

OP(dded,c0) { EASP16(cpustate); MULT( RM(cpustate, cpustate->ea) );           } /* MULT A,(SP+w)    */
OP(dded,c1) { EASP16(cpustate); MULTU( RM(cpustate, cpustate->ea) );          } /* MULTU A,(SP+w)   */
OP(dded,c2) { union PAIR tmp; RM16(cpustate, cpustate->_HL, &tmp); MULTW( tmp.w.l ); } /* MULTW HL,(HL)    */
OP(dded,c3) { union PAIR tmp; RM16(cpustate, cpustate->_HL, &tmp); MULTUW( tmp.w.l ); } /* MULTUW HL,(HL)   */
OP(dded,c4) { EASP16(cpustate); DIV( RM(cpustate, cpustate->ea) );            } /* DIV HL,(SP+w)    */
OP(dded,c5) { EASP16(cpustate); DIVU( RM(cpustate, cpustate->ea) );           } /* DIVU HL,(SP+w)   */
OP(dded,c6) { union PAIR tmp; RM16(cpustate, cpustate->_HL, &tmp); ADD16F(tmp.w.l); } /* ADDW HL,(HL)     */
OP(dded,c7) { union PAIR tmp; RM16(cpustate, cpustate->_HL, &tmp); CP16(tmp.w.l); } /* CPW HL,(HL)      */

OP(dded,c8) { EAHX(cpustate); MULT( RM(cpustate, cpustate->ea) );             } /* MULT A,(HL+IX)   */
OP(dded,c9) { EAHX(cpustate); MULTU( RM(cpustate, cpustate->ea) );            } /* MULTU A,(HL+IX)  */
OP(dded,ca) { union PAIR tmp; RM16(cpustate, cpustate->_HL, &tmp); DIVW( tmp.w.l ); } /* DIVW DEHL,(HL)   */
OP(dded,cb) { union PAIR tmp; RM16(cpustate, cpustate->_HL, &tmp); DIVUW( tmp.w.l ); } /* DIVUW DEHL,(HL)  */
OP(dded,cc) { EAHX(cpustate); DIV( RM(cpustate, cpustate->ea) );              } /* DIV HL,(HL+IX)   */
OP(dded,cd) { EAHX(cpustate); DIVU( RM(cpustate, cpustate->ea) );             } /* DIVU HL,(HL+IX)  */
OP(dded,ce) { union PAIR tmp; RM16(cpustate, cpustate->_HL, &tmp); SUB16(tmp.w.l); } /* SUBW HL,(HL)     */
OP(dded,cf) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */

OP(dded,d0) { EAHY(cpustate); MULT( RM(cpustate, cpustate->ea) );           } /* MULT A,(HL+IY)    */
OP(dded,d1) { EAHY(cpustate); MULTU( RM(cpustate, cpustate->ea) );          } /* MULTU A,(HL+IY)   */
OP(dded,d2) { union PAIR tmp; RM16(cpustate, ARG16(cpustate), &tmp); MULTW( tmp.w.l ); } /* MULTW (w)    */
OP(dded,d3) { union PAIR tmp; RM16(cpustate, ARG16(cpustate), &tmp); MULTUW( tmp.w.l ); } /* MULTUW (w)   */
OP(dded,d4) { EAHY(cpustate); DIV( RM(cpustate, cpustate->ea) );            } /* DIV HL,(HL+IY)    */
OP(dded,d5) { EAHY(cpustate); DIVU( RM(cpustate, cpustate->ea) );           } /* DIVU HL,(HL+IY)   */
OP(dded,d6) { union PAIR tmp; RM16(cpustate, ARG16(cpustate), &tmp); ADD16F(tmp.w.l); } /* ADDW HL,(w)     */
OP(dded,d7) { union PAIR tmp; RM16(cpustate, ARG16(cpustate), &tmp); CP16(tmp.w.l); } /* CPW HL,(w)      */

OP(dded,d8) { EAXY(cpustate); MULT( RM(cpustate, cpustate->ea) );             } /* MULT A,(IX+IY)   */
OP(dded,d9) { EAXY(cpustate); MULTU( RM(cpustate, cpustate->ea) );            } /* MULTU A,(IX+IY)  */
OP(dded,da) { union PAIR tmp; RM16(cpustate, ARG16(cpustate), &tmp); DIVW( tmp.w.l ); } /* DIVW DEHL,(w)   */
OP(dded,db) { union PAIR tmp; RM16(cpustate, ARG16(cpustate), &tmp); DIVUW( tmp.w.l ); } /* DIVUW DEHL,(w)  */
OP(dded,dc) { EAXY(cpustate); DIV( RM(cpustate, cpustate->ea) );              } /* DIV HL,(IX+IY)   */
OP(dded,dd) { EAXY(cpustate); DIVU( RM(cpustate, cpustate->ea) );             } /* DIVU HL,(IX+IY)  */
OP(dded,de) { union PAIR tmp; RM16(cpustate, ARG16(cpustate), &tmp); SUB16(tmp.w.l); } /* SUBW HL,(w)     */
OP(dded,df) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */

OP(dded,e0) { MULT( cpustate->_HX );                                          } /* MULT A,HX        */
OP(dded,e1) { MULTU( cpustate->_HX );                                         } /* MULTU A,HX       */
OP(dded,e2) { MULTW( cpustate->_IX );                                         } /* MULTW HL,IX      */
OP(dded,e3) { MULTUW( cpustate->_IX );                                        } /* MULTUW HL,IX     */
OP(dded,e4) { DIV( cpustate->_HX );                                           } /* DIV HL,HX        */
OP(dded,e5) { DIVU( cpustate->_HX );                                          } /* DIVU HL,HX       */
OP(dded,e6) { ADD16F(cpustate->_IX);                                          } /* ADDW HL,IX       */
OP(dded,e7) { CP16(cpustate->_IX);                                            } /* CPW HL,IX        */

OP(dded,e8) { MULT( cpustate->_LX );                                          } /* MULT A,LX        */
OP(dded,e9) { MULTU( cpustate->_LX );                                         } /* MULTU A,LX       */
OP(dded,ea) { DIVW( cpustate->_IX );                                          } /* DIVW DEHL,IX     */
OP(dded,eb) { DIVUW( cpustate->_IX );                                         } /* DIVUW DEHL,IX    */
OP(dded,ec) { DIV( cpustate->_LX );                                           } /* DIV HL,LX        */
OP(dded,ed) { DIVU( cpustate->_LX );                                          } /* DIVU HL,LX       */
OP(dded,ee) { SUB16(cpustate->_IX);                                           } /* SUBW HL,IX       */
OP(dded,ef) { illegal_1(cpustate, __func__); ed_ef(cpustate);                           } /* EX 'H,L          */

OP(dded,f0) { EAX(cpustate); MULT( RM(cpustate, cpustate->ea) );           } /* MULT A,(IX+o)    */
OP(dded,f1) { EAX(cpustate); MULTU( RM(cpustate, cpustate->ea) );          } /* MULTU A,(IX+o)   */
OP(dded,f2) { union PAIR tmp; EARA(cpustate); RM16(cpustate, cpustate->ea, &tmp); MULTW( tmp.w.l ); } /* MULTW HL,(ra)    */
OP(dded,f3) { union PAIR tmp; EARA(cpustate); RM16(cpustate, cpustate->ea, &tmp); MULTUW( tmp.w.l ); } /* MULTUW HL,(ra)   */
OP(dded,f4) { EAX(cpustate); DIV( RM(cpustate, cpustate->ea) );            } /* DIV HL,(IX+o)    */
OP(dded,f5) { EAX(cpustate); DIVU( RM(cpustate, cpustate->ea) );           } /* DIVU HL,(IX+o)   */
OP(dded,f6) { union PAIR tmp; EARA(cpustate); RM16(cpustate, cpustate->ea, &tmp); ADD16F(tmp.w.l); } /* ADDW HL,(ra)     */
OP(dded,f7) { union PAIR tmp; EARA(cpustate); RM16(cpustate, cpustate->ea, &tmp); CP16(tmp.w.l); } /* CPW HL,(ra)      */

OP(dded,f8) { MULT( RM(cpustate, ARG16(cpustate)) );                       } /* MULT A,(w)   */
OP(dded,f9) { MULTU( RM(cpustate, ARG16(cpustate)) );                      } /* MULTU A,(w)  */
OP(dded,fa) { union PAIR tmp; EARA(cpustate); RM16(cpustate, cpustate->ea, &tmp); DIVW( tmp.w.l ); } /* DIVW DEHL,(ra)   */
OP(dded,fb) { union PAIR tmp; EARA(cpustate); RM16(cpustate, cpustate->ea, &tmp); DIVUW( tmp.w.l ); } /* DIVUW DEHL,(ra)  */
OP(dded,fc) { DIV( RM(cpustate, ARG16(cpustate)) );                        } /* DIV HL,(w)   */
OP(dded,fd) { DIVU( RM(cpustate, ARG16(cpustate)) );                       } /* DIVU HL,(w)  */
OP(dded,fe) { union PAIR tmp; EARA(cpustate); RM16(cpustate, cpustate->ea, &tmp); SUB16(tmp.w.l); } /* SUBW HL,(ra)     */
OP(dded,ff) { illegal_1(cpustate, __func__);                                            } /* DB   ED          */
