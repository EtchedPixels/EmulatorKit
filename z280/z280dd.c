void illegal_1(struct z280_state *cpustate, const char *opcode_name) {
	logerror("Z280 '%s' ill. opcode %s\n",
			cpustate->device->m_tag, opcode_name);
	//cpustate->int_pending[Z280_INT_TRAP] = 1;
	//cpustate->IO_ITC |= Z280_ITC_TRAP;
	//cpustate->IO_ITC &= ~Z280_ITC_UFO;
}

/**********************************************************
 * IX register related opcodes (DD prefix)
 **********************************************************/
OP(dd,00) { illegal_1(cpustate, __func__); op_00(cpustate);                                   } /* DB   DD          */
OP(dd,01) { union PAIR tmp; tmp.w.l = ARG16(cpustate); WM16(cpustate, cpustate->_HL, &tmp); } /* LD   (HL),w      */
OP(dd,02) { illegal_1(cpustate, __func__); op_02(cpustate);                                   } /* DB   DD          */
OP(dd,03) { union PAIR tmp; RM16(cpustate, cpustate->_HL, &tmp); tmp.w.l++; WM16(cpustate, cpustate->_HL, &tmp); } /* INCW (HL)        */
OP(dd,04) { EASP16(cpustate); WM(cpustate, cpustate->ea, INC(cpustate, RM(cpustate, cpustate->ea)) ); } /* INC (SP+w)       */
OP(dd,05) { EASP16(cpustate); WM(cpustate, cpustate->ea, DEC(cpustate, RM(cpustate, cpustate->ea)) ); } /* DEC (SP+w)       */
OP(dd,06) { EASP16(cpustate); WM(cpustate, cpustate->ea, ARG(cpustate) );           } /* LD (SP+w),n      */
OP(dd,07) { illegal_1(cpustate, __func__); op_07(cpustate);                                   } /* DB   DD          */

OP(dd,08) { illegal_1(cpustate, __func__); op_08(cpustate);                                   } /* DB   DD          */
OP(dd,09) { ADD16(IX,cpustate->_BC);                                    } /* ADD  IX,BC       */
OP(dd,0a) { illegal_1(cpustate, __func__); op_0a(cpustate);                                   } /* DB   DD          */
OP(dd,0b) { union PAIR tmp; RM16(cpustate, cpustate->_HL, &tmp); tmp.w.l--; WM16(cpustate, cpustate->_HL, &tmp); } /* DECW (HL)        */
OP(dd,0c) { EAHX(cpustate); WM(cpustate, cpustate->ea, INC(cpustate, RM(cpustate, cpustate->ea)) ); } /* INC (HL+IX)      */
OP(dd,0d) { EAHX(cpustate); WM(cpustate, cpustate->ea, DEC(cpustate, RM(cpustate, cpustate->ea)) ); } /* DEC (HL+IX)      */
OP(dd,0e) { EAHX(cpustate); WM(cpustate, cpustate->ea, ARG(cpustate) );             } /* LD (HL+IX),n     */
OP(dd,0f) { illegal_1(cpustate, __func__); op_0f(cpustate);                                   } /* DB   DD          */

OP(dd,10) { illegal_1(cpustate, __func__); op_10(cpustate);                                   } /* DB   DD          */
OP(dd,11) { union PAIR tmp; cpustate->ea = ARG16(cpustate); tmp.w.l = ARG16(cpustate); WM16(cpustate, cpustate->ea, &tmp); } /* LD (w),w         */
OP(dd,12) { illegal_1(cpustate, __func__); op_12(cpustate);                                   } /* DB   DD          */
OP(dd,13) { union PAIR tmp; cpustate->ea = ARG16(cpustate); RM16(cpustate, cpustate->ea, &tmp); tmp.w.l++; WM16(cpustate, cpustate->ea, &tmp); } /* INCW (w)         */
OP(dd,14) { EAHY(cpustate); WM(cpustate, cpustate->ea, INC(cpustate, RM(cpustate, cpustate->ea)) ); } /* INC (HL+IY)      */
OP(dd,15) { EAHY(cpustate); WM(cpustate, cpustate->ea, DEC(cpustate, RM(cpustate, cpustate->ea)) ); } /* DEC (HL+IY)      */
OP(dd,16) { EAHY(cpustate); WM(cpustate, cpustate->ea, ARG(cpustate) );             } /* LD (HL+IY),n     */
OP(dd,17) { illegal_1(cpustate, __func__); op_17(cpustate);                                   } /* DB   DD          */

OP(dd,18) { illegal_1(cpustate, __func__); op_18(cpustate);                                   } /* DB   DD          */
OP(dd,19) { ADD16(IX,cpustate->_DE);                                    } /* ADD  IX,DE       */
OP(dd,1a) { illegal_1(cpustate, __func__); op_1a(cpustate);                                   } /* DB   DD          */
OP(dd,1b) { union PAIR tmp; cpustate->ea = ARG16(cpustate); RM16(cpustate, cpustate->ea, &tmp); tmp.w.l--; WM16(cpustate, cpustate->ea, &tmp); } /* DECW (w)         */
OP(dd,1c) { EAXY(cpustate); WM(cpustate, cpustate->ea, INC(cpustate, RM(cpustate, cpustate->ea)) ); } /* INC (IX+IY)      */
OP(dd,1d) { EAXY(cpustate); WM(cpustate, cpustate->ea, DEC(cpustate, RM(cpustate, cpustate->ea)) ); } /* DEC (IX+IY)      */
OP(dd,1e) { EAXY(cpustate); WM(cpustate, cpustate->ea, ARG(cpustate) );             } /* LD (IX+IY),n     */
OP(dd,1f) { illegal_1(cpustate, __func__); op_1f(cpustate);                                   } /* DB   DD          */

OP(dd,20) { JR_COND(cpustate->BC2inuse, 0x20);                                      } /* JAR  o           */
OP(dd,21) { cpustate->_IX = ARG16(cpustate);                                 } /* LD   IX,w        */
OP(dd,22) { cpustate->ea = ARG16(cpustate); WM16(cpustate,  cpustate->ea, &cpustate->IX );               } /* LD   (w),IX      */
OP(dd,23) { cpustate->_IX++;                                         } /* INC  IX          */
OP(dd,24) { cpustate->_HX = INC(cpustate, cpustate->_HX);                                    } /* INC  HX          */
OP(dd,25) { cpustate->_HX = DEC(cpustate, cpustate->_HX);                                    } /* DEC  HX          */
OP(dd,26) { cpustate->_HX = ARG(cpustate);                                       } /* LD   HX,n        */
OP(dd,27) { illegal_1(cpustate, __func__); op_27(cpustate);                                   } /* DB   DD          */

OP(dd,28) { JR_COND(cpustate->AF2inuse, 0x28);                                      } /* JAF  o           */
OP(dd,29) { ADD16(IX,cpustate->_IX);                                    } /* ADD  IX,IX       */
OP(dd,2a) { cpustate->ea = ARG16(cpustate); RM16(cpustate,  cpustate->ea, &cpustate->IX );               } /* LD   IX,(w)      */
OP(dd,2b) { cpustate->_IX--;                                         } /* DEC  IX          */
OP(dd,2c) { cpustate->_LX = INC(cpustate, cpustate->_LX);                                    } /* INC  LX          */
OP(dd,2d) { cpustate->_LX = DEC(cpustate, cpustate->_LX);                                    } /* DEC  LX          */
OP(dd,2e) { cpustate->_LX = ARG(cpustate);                                       } /* LD   LX,n        */
OP(dd,2f) { illegal_1(cpustate, __func__); op_2f(cpustate);                                   } /* DB   DD          */

OP(dd,30) { illegal_1(cpustate, __func__); op_30(cpustate);                                   } /* DB   DD          */
OP(dd,31) { union PAIR tmp; EARA(cpustate); tmp.w.l = ARG16(cpustate); WM16(cpustate, cpustate->ea, &tmp); } /* LD (ra),w        */
OP(dd,32) { illegal_1(cpustate, __func__); op_32(cpustate);                                   } /* DB   DD          */
OP(dd,33) { union PAIR tmp; EARA(cpustate); RM16(cpustate, cpustate->ea, &tmp); tmp.w.l++; WM16(cpustate, cpustate->ea, &tmp); } /* INCW (ra)        */
OP(dd,34) { EAX(cpustate); WM(cpustate,  cpustate->ea, INC(cpustate, RM(cpustate, cpustate->ea)) );                      } /* INC  (IX+o)      */
OP(dd,35) { EAX(cpustate); WM(cpustate,  cpustate->ea, DEC(cpustate, RM(cpustate, cpustate->ea)) );                      } /* DEC  (IX+o)      */
OP(dd,36) { EAX(cpustate); WM(cpustate,  cpustate->ea, ARG(cpustate) );                          } /* LD   (IX+o),n    */
OP(dd,37) { illegal_1(cpustate, __func__); op_37(cpustate);                                   } /* DB   DD          */

OP(dd,38) { illegal_1(cpustate, __func__); op_38(cpustate);                                   } /* DB   DD          */
OP(dd,39) { ADD16(IX,_SP(cpustate));                                    } /* ADD  IX,SP       */
OP(dd,3a) { illegal_1(cpustate, __func__); op_3a(cpustate);                                   } /* DB   DD          */
OP(dd,3b) { union PAIR tmp; EARA(cpustate); RM16(cpustate, cpustate->ea, &tmp); tmp.w.l--; WM16(cpustate, cpustate->ea, &tmp); } /* DECW (ra)        */
OP(dd,3c) { cpustate->ea = ARG16(cpustate); WM(cpustate, cpustate->ea, INC(cpustate, RM(cpustate, cpustate->ea)) ); } /* INC (w)          */
OP(dd,3d) { cpustate->ea = ARG16(cpustate); WM(cpustate, cpustate->ea, DEC(cpustate, RM(cpustate, cpustate->ea)) ); } /* DEC (w)          */
OP(dd,3e) { cpustate->ea = ARG16(cpustate); WM(cpustate, cpustate->ea, ARG(cpustate) ); } /* LD (w),n         */
OP(dd,3f) { illegal_1(cpustate, __func__); op_3f(cpustate);                                   } /* DB   DD          */

OP(dd,40) { illegal_1(cpustate, __func__); op_40(cpustate);                                   } /* DB   DD          */
OP(dd,41) { illegal_1(cpustate, __func__); op_41(cpustate);                                   } /* DB   DD          */
OP(dd,42) { illegal_1(cpustate, __func__); op_42(cpustate);                                   } /* DB   DD          */
OP(dd,43) { illegal_1(cpustate, __func__); op_43(cpustate);                                   } /* DB   DD          */
OP(dd,44) { cpustate->_B = cpustate->_HX;                                        } /* LD   B,HX        */
OP(dd,45) { cpustate->_B = cpustate->_LX;                                        } /* LD   B,LX        */
OP(dd,46) { EAX(cpustate); cpustate->_B = RM(cpustate, cpustate->ea);                                } /* LD   B,(IX+o)    */
OP(dd,47) { illegal_1(cpustate, __func__); op_47(cpustate);                                   } /* DB   DD          */

OP(dd,48) { illegal_1(cpustate, __func__); op_48(cpustate);                                   } /* DB   DD          */
OP(dd,49) { illegal_1(cpustate, __func__); op_49(cpustate);                                   } /* DB   DD          */
OP(dd,4a) { illegal_1(cpustate, __func__); op_4a(cpustate);                                   } /* DB   DD          */
OP(dd,4b) { illegal_1(cpustate, __func__); op_4b(cpustate);                                   } /* DB   DD          */
OP(dd,4c) { cpustate->_C = cpustate->_HX;                                        } /* LD   C,HX        */
OP(dd,4d) { cpustate->_C = cpustate->_LX;                                        } /* LD   C,LX        */
OP(dd,4e) { EAX(cpustate); cpustate->_C = RM(cpustate, cpustate->ea);                                } /* LD   C,(IX+o)    */
OP(dd,4f) { illegal_1(cpustate, __func__); op_4f(cpustate);                                   } /* DB   DD          */

OP(dd,50) { illegal_1(cpustate, __func__); op_50(cpustate);                                   } /* DB   DD          */
OP(dd,51) { illegal_1(cpustate, __func__); op_51(cpustate);                                   } /* DB   DD          */
OP(dd,52) { illegal_1(cpustate, __func__); op_52(cpustate);                                   } /* DB   DD          */
OP(dd,53) { illegal_1(cpustate, __func__); op_53(cpustate);                                   } /* DB   DD          */
OP(dd,54) { cpustate->_D = cpustate->_HX;                                        } /* LD   D,HX        */
OP(dd,55) { cpustate->_D = cpustate->_LX;                                        } /* LD   D,LX        */
OP(dd,56) { EAX(cpustate); cpustate->_D = RM(cpustate, cpustate->ea);                                } /* LD   D,(IX+o)    */
OP(dd,57) { illegal_1(cpustate, __func__); op_57(cpustate);                                   } /* DB   DD          */

OP(dd,58) { illegal_1(cpustate, __func__); op_58(cpustate);                                   } /* DB   DD          */
OP(dd,59) { illegal_1(cpustate, __func__); op_59(cpustate);                                   } /* DB   DD          */
OP(dd,5a) { illegal_1(cpustate, __func__); op_5a(cpustate);                                   } /* DB   DD          */
OP(dd,5b) { illegal_1(cpustate, __func__); op_5b(cpustate);                                   } /* DB   DD          */
OP(dd,5c) { cpustate->_E = cpustate->_HX;                                        } /* LD   E,HX        */
OP(dd,5d) { cpustate->_E = cpustate->_LX;                                        } /* LD   E,LX        */
OP(dd,5e) { EAX(cpustate); cpustate->_E = RM(cpustate, cpustate->ea);                                } /* LD   E,(IX+o)    */
OP(dd,5f) { illegal_1(cpustate, __func__); op_5f(cpustate);                                   } /* DB   DD          */

OP(dd,60) { cpustate->_HX = cpustate->_B;                                        } /* LD   HX,B        */
OP(dd,61) { cpustate->_HX = cpustate->_C;                                        } /* LD   HX,C        */
OP(dd,62) { cpustate->_HX = cpustate->_D;                                        } /* LD   HX,D        */
OP(dd,63) { cpustate->_HX = cpustate->_E;                                        } /* LD   HX,E        */
OP(dd,64) {                                                         } /* LD   HX,HX       */
OP(dd,65) { cpustate->_HX = cpustate->_LX;                                       } /* LD   HX,LX       */
OP(dd,66) { EAX(cpustate); cpustate->_H = RM(cpustate, cpustate->ea);                                } /* LD   H,(IX+o)    */
OP(dd,67) { cpustate->_HX = cpustate->_A;                                        } /* LD   HX,A        */

OP(dd,68) { cpustate->_LX = cpustate->_B;                                        } /* LD   LX,B        */
OP(dd,69) { cpustate->_LX = cpustate->_C;                                        } /* LD   LX,C        */
OP(dd,6a) { cpustate->_LX = cpustate->_D;                                        } /* LD   LX,D        */
OP(dd,6b) { cpustate->_LX = cpustate->_E;                                        } /* LD   LX,E        */
OP(dd,6c) { cpustate->_LX = cpustate->_HX;                                       } /* LD   LX,HX       */
OP(dd,6d) {                                                         } /* LD   LX,LX       */
OP(dd,6e) { EAX(cpustate); cpustate->_L = RM(cpustate, cpustate->ea);                                } /* LD   L,(IX+o)    */
OP(dd,6f) { cpustate->_LX = cpustate->_A;                                        } /* LD   LX,A        */

OP(dd,70) { EAX(cpustate); WM(cpustate,  cpustate->ea, cpustate->_B );                               } /* LD   (IX+o),B    */
OP(dd,71) { EAX(cpustate); WM(cpustate,  cpustate->ea, cpustate->_C );                               } /* LD   (IX+o),C    */
OP(dd,72) { EAX(cpustate); WM(cpustate,  cpustate->ea, cpustate->_D );                               } /* LD   (IX+o),D    */
OP(dd,73) { EAX(cpustate); WM(cpustate,  cpustate->ea, cpustate->_E );                               } /* LD   (IX+o),E    */
OP(dd,74) { EAX(cpustate); WM(cpustate,  cpustate->ea, cpustate->_H );                               } /* LD   (IX+o),H    */
OP(dd,75) { EAX(cpustate); WM(cpustate,  cpustate->ea, cpustate->_L );                               } /* LD   (IX+o),L    */
OP(dd,76) { illegal_1(cpustate, __func__); op_76(cpustate);                                   }         /* DB   DD          */
OP(dd,77) { EAX(cpustate); WM(cpustate,  cpustate->ea, cpustate->_A );                               } /* LD   (IX+o),A    */

OP(dd,78) { EASP16(cpustate); cpustate->_A = RM(cpustate, cpustate->ea);          } /* LD A,(SP+w)      */
OP(dd,79) { EAHX(cpustate); cpustate->_A = RM(cpustate, cpustate->ea);            } /* LD A,(HL+IX)     */
OP(dd,7a) { EAHY(cpustate); cpustate->_A = RM(cpustate, cpustate->ea);            } /* LD A,(HL+IY)     */
OP(dd,7b) { EAXY(cpustate); cpustate->_A = RM(cpustate, cpustate->ea);            } /* LD A,(IX+IY)     */
OP(dd,7c) { cpustate->_A = cpustate->_HX;                                        } /* LD   A,HX        */
OP(dd,7d) { cpustate->_A = cpustate->_LX;                                        } /* LD   A,LX        */
OP(dd,7e) { EAX(cpustate); cpustate->_A = RM(cpustate, cpustate->ea);                                } /* LD   A,(IX+o)    */
OP(dd,7f) { illegal_1(cpustate, __func__); op_7f(cpustate);                                   } /* DB   DD          */

OP(dd,80) { EASP16(cpustate); ADD(RM(cpustate, cpustate->ea));                    } /* ADD A,(SP+w)     */
OP(dd,81) { EAHX(cpustate); ADD(RM(cpustate, cpustate->ea));                      } /* ADD A,(HL+IX)    */
OP(dd,82) { EAHY(cpustate); ADD(RM(cpustate, cpustate->ea));                      } /* ADD A,(HL+IY)    */
OP(dd,83) { EAXY(cpustate); ADD(RM(cpustate, cpustate->ea));                      } /* ADD A,(IX+IY)    */
OP(dd,84) { ADD(cpustate->_HX);                                      } /* ADD  A,HX        */
OP(dd,85) { ADD(cpustate->_LX);                                      } /* ADD  A,LX        */
OP(dd,86) { EAX(cpustate); ADD(RM(cpustate, cpustate->ea));                              } /* ADD  A,(IX+o)    */
OP(dd,87) { cpustate->ea = ARG16(cpustate); ADD(RM(cpustate, cpustate->ea));      } /* ADD A,(w)        */

OP(dd,88) { EASP16(cpustate); ADC(RM(cpustate, cpustate->ea));                    } /* ADC A,(SP+w)     */
OP(dd,89) { EAHX(cpustate); ADC(RM(cpustate, cpustate->ea));                      } /* ADC A,(HL+IX)    */
OP(dd,8a) { EAHY(cpustate); ADC(RM(cpustate, cpustate->ea));                      } /* ADC A,(HL+IY)    */
OP(dd,8b) { EAXY(cpustate); ADC(RM(cpustate, cpustate->ea));                      } /* ADC A,(IX+IY)    */
OP(dd,8c) { ADC(cpustate->_HX);                                      } /* ADC  A,HX        */
OP(dd,8d) { ADC(cpustate->_LX);                                      } /* ADC  A,LX        */
OP(dd,8e) { EAX(cpustate); ADC(RM(cpustate, cpustate->ea));                              } /* ADC  A,(IX+o)    */
OP(dd,8f) { cpustate->ea = ARG16(cpustate); ADC(RM(cpustate, cpustate->ea));      } /* ADC A,(w)        */

OP(dd,90) { EASP16(cpustate); SUB(RM(cpustate, cpustate->ea));                    } /* SUB A,(SP+w)     */
OP(dd,91) { EAHX(cpustate); SUB(RM(cpustate, cpustate->ea));                      } /* SUB A,(HL+IX)    */
OP(dd,92) { EAHY(cpustate); SUB(RM(cpustate, cpustate->ea));                      } /* SUB A,(HL+IY)    */
OP(dd,93) { EAXY(cpustate); SUB(RM(cpustate, cpustate->ea));                      } /* SUB A,(IX+IY)    */
OP(dd,94) { SUB(cpustate->_HX);                                      } /* SUB  HX          */
OP(dd,95) { SUB(cpustate->_LX);                                      } /* SUB  LX          */
OP(dd,96) { EAX(cpustate); SUB(RM(cpustate, cpustate->ea));                              } /* SUB  (IX+o)      */
OP(dd,97) { cpustate->ea = ARG16(cpustate); SUB(RM(cpustate, cpustate->ea));      } /* SUB A,(w)        */

OP(dd,98) { EASP16(cpustate); SBC(RM(cpustate, cpustate->ea));                    } /* SBC A,(SP+w)     */
OP(dd,99) { EAHX(cpustate); SBC(RM(cpustate, cpustate->ea));                      } /* SBC A,(HL+IX)    */
OP(dd,9a) { EAHY(cpustate); SBC(RM(cpustate, cpustate->ea));                      } /* SBC A,(HL+IY)    */
OP(dd,9b) { EAXY(cpustate); SBC(RM(cpustate, cpustate->ea));                      } /* SBC A,(IX+IY)    */
OP(dd,9c) { SBC(cpustate->_HX);                                      } /* SBC  A,HX        */
OP(dd,9d) { SBC(cpustate->_LX);                                      } /* SBC  A,LX        */
OP(dd,9e) { EAX(cpustate); SBC(RM(cpustate, cpustate->ea));                              } /* SBC  A,(IX+o)    */
OP(dd,9f) { cpustate->ea = ARG16(cpustate); SBC(RM(cpustate, cpustate->ea));      } /* SBC A,(w)        */

OP(dd,a0) { EASP16(cpustate); AND(RM(cpustate, cpustate->ea));                    } /* AND A,(SP+w)     */
OP(dd,a1) { EAHX(cpustate); AND(RM(cpustate, cpustate->ea));                      } /* AND A,(HL+IX)    */
OP(dd,a2) { EAHY(cpustate); AND(RM(cpustate, cpustate->ea));                      } /* AND A,(HL+IY)    */
OP(dd,a3) { EAXY(cpustate); AND(RM(cpustate, cpustate->ea));                      } /* AND A,(IX+IY)    */
OP(dd,a4) { AND(cpustate->_HX);                                      } /* AND  HX          */
OP(dd,a5) { AND(cpustate->_LX);                                      } /* AND  LX          */
OP(dd,a6) { EAX(cpustate); AND(RM(cpustate, cpustate->ea));                              } /* AND  (IX+o)      */
OP(dd,a7) { cpustate->ea = ARG16(cpustate); AND(RM(cpustate, cpustate->ea));      } /* AND A,(w)        */

OP(dd,a8) { EASP16(cpustate); XOR(RM(cpustate, cpustate->ea));                    } /* XOR A,(SP+w)     */
OP(dd,a9) { EAHX(cpustate); XOR(RM(cpustate, cpustate->ea));                      } /* XOR A,(HL+IX)    */
OP(dd,aa) { EAHY(cpustate); XOR(RM(cpustate, cpustate->ea));                      } /* XOR A,(HL+IY)    */
OP(dd,ab) { EAXY(cpustate); XOR(RM(cpustate, cpustate->ea));                      } /* XOR A,(IX+IY)    */
OP(dd,ac) { XOR(cpustate->_HX);                                      } /* XOR  HX          */
OP(dd,ad) { XOR(cpustate->_LX);                                      } /* XOR  LX          */
OP(dd,ae) { EAX(cpustate); XOR(RM(cpustate, cpustate->ea));                              } /* XOR  (IX+o)      */
OP(dd,af) { cpustate->ea = ARG16(cpustate); XOR(RM(cpustate, cpustate->ea));      } /* XOR A,(w)        */

OP(dd,b0) { EASP16(cpustate); OR(RM(cpustate, cpustate->ea));                    } /* OR A,(SP+w)      */
OP(dd,b1) { EAHX(cpustate); OR(RM(cpustate, cpustate->ea));                      } /* OR A,(HL+IX)     */
OP(dd,b2) { EAHY(cpustate); OR(RM(cpustate, cpustate->ea));                      } /* OR A,(HL+IY)     */
OP(dd,b3) { EAXY(cpustate); OR(RM(cpustate, cpustate->ea));                      } /* OR A,(IX+IY)     */
OP(dd,b4) { OR(cpustate->_HX);                                           } /* OR   HX          */
OP(dd,b5) { OR(cpustate->_LX);                                           } /* OR   LX          */
OP(dd,b6) { EAX(cpustate); OR(RM(cpustate, cpustate->ea));                                   } /* OR   (IX+o)      */
OP(dd,b7) { cpustate->ea = ARG16(cpustate); OR(RM(cpustate, cpustate->ea));      } /* OR A,(w)         */

OP(dd,b8) { EASP16(cpustate); CP(RM(cpustate, cpustate->ea));                    } /* CP A,(SP+w)      */
OP(dd,b9) { EAHX(cpustate); CP(RM(cpustate, cpustate->ea));                      } /* CP A,(HL+IX)     */
OP(dd,ba) { EAHY(cpustate); CP(RM(cpustate, cpustate->ea));                      } /* CP A,(HL+IY)     */
OP(dd,bb) { EAXY(cpustate); CP(RM(cpustate, cpustate->ea));                      } /* CP A,(IX+IY)     */
OP(dd,bc) { CP(cpustate->_HX);                                           } /* CP   HX          */
OP(dd,bd) { CP(cpustate->_LX);                                           } /* CP   LX          */
OP(dd,be) { EAX(cpustate); CP(RM(cpustate, cpustate->ea));                                   } /* CP   (IX+o)      */
OP(dd,bf) { cpustate->ea = ARG16(cpustate); CP(RM(cpustate, cpustate->ea));      } /* CP A,(w)         */

OP(dd,c0) { illegal_1(cpustate, __func__); op_c0(cpustate);                                   } /* DB   DD          */
OP(dd,c1) { union PAIR tmp; RM16(cpustate, _SPD(cpustate), &tmp ); WM16(cpustate, (cpustate)->_HL, &tmp); INC2_SP(cpustate);  } /* POP (HL)         */
OP(dd,c2) { JP_HL_COND( !(cpustate->_F & ZF) );                                     } /* JP NZ, (HL)      */
OP(dd,c3) { illegal_1(cpustate, __func__); op_c3(cpustate);                                   } /* DB   DD          */
OP(dd,c4) { CALL_HL_COND( !(cpustate->_F & ZF), 0xc4);                              } /* CALL NZ, (HL)    */
OP(dd,c5) { union PAIR tmp; RM16(cpustate, (cpustate)->_HL, &tmp ); WM16(cpustate, _SPD(cpustate)-2, &tmp); DEC2_SP(cpustate); if(is_system(cpustate)) CHECK_SSO(cpustate); } /* PUSH (HL)        */
OP(dd,c6) { illegal_1(cpustate, __func__); op_c6(cpustate);                                   } /* DB   DD          */
OP(dd,c7) { illegal_1(cpustate, __func__); op_c7(cpustate);                                   }         /* DB   DD          */

OP(dd,c8) { illegal_1(cpustate, __func__); op_c8(cpustate);                                   } /* DB   DD          */
OP(dd,c9) { illegal_1(cpustate, __func__); op_c9(cpustate);                                   } /* DB   DD          */
OP(dd,ca) { JP_HL_COND( cpustate->_F & ZF );                                        } /* JP Z, (HL)       */
OP(dd,cb) { EAX(cpustate); cpustate->extra_cycles += exec_xycb(cpustate,ARG(cpustate));                          } /* **   DD CB xx    */
OP(dd,cc) { CALL_HL_COND( cpustate->_F & ZF, 0xcc);                                 } /* CALL Z, (HL)     */
OP(dd,cd) { PUSH_R(cpustate,  PC ); cpustate->_PCD = cpustate->_HL; if(is_system(cpustate)) CHECK_SSO(cpustate); } /* CALL (HL)        */
OP(dd,ce) { illegal_1(cpustate, __func__); op_ce(cpustate);                                   } /* DB   DD          */
OP(dd,cf) { illegal_1(cpustate, __func__); op_cf(cpustate);                                   } /* DB   DD          */

OP(dd,d0) { illegal_1(cpustate, __func__); op_d0(cpustate);                                   } /* DB   DD          */
OP(dd,d1) { union PAIR tmp; cpustate->ea = ARG16(cpustate); RM16(cpustate, _SPD(cpustate), &tmp ); WM16(cpustate, (cpustate)->ea, &tmp); INC2_SP(cpustate); } /* POP (w)          */
OP(dd,d2) { JP_HL_COND( !(cpustate->_F & CF) );                                     } /* JP NC, (HL)      */
OP(dd,d3) { illegal_1(cpustate, __func__); op_d3(cpustate);                                   } /* DB   DD          */
OP(dd,d4) { CALL_HL_COND( !(cpustate->_F & CF), 0xd4);                              } /* CALL NC, (HL)    */
OP(dd,d5) { union PAIR tmp; cpustate->ea = ARG16(cpustate); RM16(cpustate, (cpustate)->ea, &tmp ); WM16(cpustate, _SPD(cpustate)-2, &tmp); DEC2_SP(cpustate); if(is_system(cpustate)) CHECK_SSO(cpustate); } /* PUSH (w)         */
OP(dd,d6) { illegal_1(cpustate, __func__); op_d6(cpustate);                                   } /* DB   DD          */
OP(dd,d7) { illegal_1(cpustate, __func__); op_d7(cpustate);                                   } /* DB   DD          */

OP(dd,d8) { illegal_1(cpustate, __func__); op_d8(cpustate);                                   } /* DB   DD          */
OP(dd,d9) { illegal_1(cpustate, __func__); op_d9(cpustate);                                   } /* DB   DD          */
OP(dd,da) { JP_HL_COND( cpustate->_F & CF );                                        } /* JP C, (HL)       */
OP(dd,db) { illegal_1(cpustate, __func__); op_db(cpustate);                                   } /* DB   DD          */
OP(dd,dc) { CALL_HL_COND( cpustate->_F & CF, 0xdc);                                 } /* CALL C, (HL)     */
OP(dd,dd) { illegal_1(cpustate, __func__); op_dd(cpustate);                                   } /* DB   DD          */
OP(dd,de) { illegal_1(cpustate, __func__); op_de(cpustate);                                   } /* DB   DD          */
OP(dd,df) { illegal_1(cpustate, __func__); op_df(cpustate);                                   } /* DB   DD          */

OP(dd,e0) { illegal_1(cpustate, __func__); op_e0(cpustate);                                   } /* DB   DD          */
OP(dd,e1) { POP(cpustate, IX);                                           } /* POP  IX          */
OP(dd,e2) { JP_HL_COND( !(cpustate->_F & PF) );                                     } /* JP PO, (HL)      */
OP(dd,e3) { EXSP(IX);                                        } /* EX   (SP),IX     */
OP(dd,e4) { CALL_HL_COND( !(cpustate->_F & PF), 0xe4);                              } /* CALL PO, (HL)    */
OP(dd,e5) { PUSH(cpustate, IX );                                        } /* PUSH IX          */
OP(dd,e6) { illegal_1(cpustate, __func__); op_e6(cpustate);                                   } /* DB   DD          */
OP(dd,e7) { illegal_1(cpustate, __func__); op_e7(cpustate);                                   } /* DB   DD          */

OP(dd,e8) { illegal_1(cpustate, __func__); op_e8(cpustate);                                   } /* DB   DD          */
OP(dd,e9) { cpustate->_PC = cpustate->_IX;                                          } /* JP   (IX)        */
OP(dd,ea) { JP_HL_COND( cpustate->_F & PF );                                        } /* JP PE, (HL)      */
OP(dd,eb) { union PAIR tmp; tmp = cpustate->IX; cpustate->IX = cpustate->HL; cpustate->HL = tmp; } /* EX IX,HL         */
OP(dd,ec) { CALL_HL_COND( cpustate->_F & PF, 0xec);                                 } /* CALL PE, (HL)    */
OP(dd,ed) { cpustate->extra_cycles += exec_dded(cpustate,ROP(cpustate));            } /* **** DD ED xx    */
OP(dd,ee) { illegal_1(cpustate, __func__); op_ee(cpustate);                                   } /* DB   DD          */
OP(dd,ef) { illegal_1(cpustate, __func__); op_ef(cpustate);                                   } /* DB   DD          */

OP(dd,f0) { illegal_1(cpustate, __func__); op_f0(cpustate);                                   } /* DB   DD          */
OP(dd,f1) { union PAIR tmp; EARA(cpustate); RM16(cpustate, _SPD(cpustate), &tmp ); WM16(cpustate, (cpustate)->ea, &tmp); INC2_SP(cpustate); } /* POP (ra)         */
OP(dd,f2) { JP_HL_COND( !(cpustate->_F & SF) );                                     } /* JP P, (HL)       */
OP(dd,f3) { illegal_1(cpustate, __func__); op_f3(cpustate);                                   } /* DB   DD          */
OP(dd,f4) { CALL_HL_COND( !(cpustate->_F & SF), 0xf4);                              } /* CALL P, (HL)     */
OP(dd,f5) { union PAIR tmp; EARA(cpustate); RM16(cpustate, (cpustate)->ea, &tmp ); WM16(cpustate, _SPD(cpustate)-2, &tmp); DEC2_SP(cpustate); if(is_system(cpustate)) CHECK_SSO(cpustate); } /* PUSH (ra)        */
OP(dd,f6) { illegal_1(cpustate, __func__); op_f6(cpustate);                                   } /* DB   DD          */
OP(dd,f7) { illegal_1(cpustate, __func__); op_f7(cpustate);                                   } /* DB   DD          */

OP(dd,f8) { illegal_1(cpustate, __func__); op_f8(cpustate);                                   } /* DB   DD          */
OP(dd,f9) { SET_SP(cpustate, cpustate->_IX);                                        } /* LD   SP,IX       */
OP(dd,fa) { JP_HL_COND( cpustate->_F & SF );                                        } /* JP M, (HL)      */
OP(dd,fb) { illegal_1(cpustate, __func__); op_fb(cpustate);                                   } /* DB   DD          */
OP(dd,fc) { CALL_HL_COND( cpustate->_F & SF, 0xfc);                                 } /* CALL M, (HL)     */
OP(dd,fd) { illegal_1(cpustate, __func__); op_fd(cpustate);                                   } /* DB   DD          */
OP(dd,fe) { illegal_1(cpustate, __func__); op_fe(cpustate);                                   } /* DB   DD          */
OP(dd,ff) { illegal_1(cpustate, __func__); op_ff(cpustate);                                   } /* DB   DD          */
