/**********************************************************
 * IY register related opcodes (FD prefix)
 **********************************************************/
OP(fd,00) { illegal_1(cpustate, __func__); op_00(cpustate);                                   } /* DB   FD          */
OP(fd,01) { illegal_1(cpustate, __func__); op_01(cpustate);                                   } /* DB   FD          */
OP(fd,02) { illegal_1(cpustate, __func__); op_02(cpustate);                                   } /* DB   FD          */
OP(fd,03) { union PAIR tmp; EAX16(cpustate); RM16(cpustate, cpustate->ea, &tmp); tmp.w.l++; WM16(cpustate, cpustate->ea, &tmp); } /* INCW (IX+w)       */
OP(fd,04) { EARA(cpustate); WM(cpustate, cpustate->ea, INC(cpustate, RM(cpustate, cpustate->ea)) ); } /* INC (ra)         */
OP(fd,05) { EARA(cpustate); WM(cpustate, cpustate->ea, DEC(cpustate, RM(cpustate, cpustate->ea)) ); } /* DEC (ra)         */
OP(fd,06) { EARA(cpustate); WM(cpustate, cpustate->ea, ARG(cpustate) );             } /* LD (ra),n      */
OP(fd,07) { illegal_1(cpustate, __func__); op_07(cpustate);                                   } /* DB   FD          */

OP(fd,08) { illegal_1(cpustate, __func__); op_08(cpustate);                                   } /* DB   FD          */
OP(fd,09) { ADD16(IY,cpustate->_BC);                                    } /* ADD  IY,BC       */
OP(fd,0a) { illegal_1(cpustate, __func__); op_0a(cpustate);                                   } /* DB   FD          */
OP(fd,0b) { union PAIR tmp; EAX16(cpustate); RM16(cpustate, cpustate->ea, &tmp); tmp.w.l--; WM16(cpustate, cpustate->ea, &tmp); } /* DECW (IX+w)      */
OP(fd,0c) { EAX16(cpustate); WM(cpustate, cpustate->ea, INC(cpustate, RM(cpustate, cpustate->ea)) ); } /* INC (IX+w)      */
OP(fd,0d) { EAX16(cpustate); WM(cpustate, cpustate->ea, DEC(cpustate, RM(cpustate, cpustate->ea)) ); } /* DEC (IX+w)      */
OP(fd,0e) { EAX16(cpustate); WM(cpustate, cpustate->ea, ARG(cpustate) );            } /* LD (IX+w),n      */
OP(fd,0f) { illegal_1(cpustate, __func__); op_0f(cpustate);                                   } /* DB   FD          */

OP(fd,10) { illegal_1(cpustate, __func__); op_10(cpustate);                                   } /* DB   FD          */
OP(fd,11) { illegal_1(cpustate, __func__); op_11(cpustate);                                   } /* DB   FD          */
OP(fd,12) { illegal_1(cpustate, __func__); op_12(cpustate);                                   } /* DB   FD          */
OP(fd,13) { union PAIR tmp; EAY16(cpustate); RM16(cpustate, cpustate->ea, &tmp); tmp.w.l++; WM16(cpustate, cpustate->ea, &tmp); } /* INCW (IY+w)       */
OP(fd,14) { EAY16(cpustate); WM(cpustate, cpustate->ea, INC(cpustate, RM(cpustate, cpustate->ea)) ); } /* INC (IY+w)       */
OP(fd,15) { EAY16(cpustate); WM(cpustate, cpustate->ea, DEC(cpustate, RM(cpustate, cpustate->ea)) ); } /* DEC (IY+w)       */
OP(fd,16) { EAY16(cpustate); WM(cpustate, cpustate->ea, ARG(cpustate) );            } /* LD (IY+w),n      */
OP(fd,17) { illegal_1(cpustate, __func__); op_17(cpustate);                                   } /* DB   FD          */

OP(fd,18) { illegal_1(cpustate, __func__); op_18(cpustate);                                   } /* DB   FD          */
OP(fd,19) { ADD16(IY,cpustate->_DE);                                    } /* ADD  IY,DE       */
OP(fd,1a) { illegal_1(cpustate, __func__); op_1a(cpustate);                                   } /* DB   FD          */
OP(fd,1b) { union PAIR tmp; EAY16(cpustate); RM16(cpustate, cpustate->ea, &tmp); tmp.w.l--; WM16(cpustate, cpustate->ea, &tmp); } /* DECW (IY+w)      */
OP(fd,1c) { EAH16(cpustate); WM(cpustate, cpustate->ea, INC(cpustate, RM(cpustate, cpustate->ea)) ); } /* INC (HL+w)       */
OP(fd,1d) { EAH16(cpustate); WM(cpustate, cpustate->ea, DEC(cpustate, RM(cpustate, cpustate->ea)) ); } /* DEC (HL+w)       */
OP(fd,1e) { EAH16(cpustate); WM(cpustate, cpustate->ea, ARG(cpustate) );            } /* LD (HL+w),n      */
OP(fd,1f) { illegal_1(cpustate, __func__); op_1f(cpustate);                                   } /* DB   FD          */

OP(fd,20) { illegal_1(cpustate, __func__); op_20(cpustate);                                   } /* DB   FD          */
OP(fd,21) { cpustate->_IY = ARG16(cpustate);                                 } /* LD   IY,w        */
OP(fd,22) { cpustate->ea = ARG16(cpustate); WM16(cpustate,  cpustate->ea, &cpustate->IY );               } /* LD   (w),IY      */
OP(fd,23) { cpustate->_IY++;                                         } /* INC  IY          */
OP(fd,24) { cpustate->_HY = INC(cpustate, cpustate->_HY);                                    } /* INC  HY          */
OP(fd,25) { cpustate->_HY = DEC(cpustate, cpustate->_HY);                                    } /* DEC  HY          */
OP(fd,26) { cpustate->_HY = ARG(cpustate);                                       } /* LD   HY,n        */
OP(fd,27) { illegal_1(cpustate, __func__); op_27(cpustate);                                   } /* DB   FD          */

OP(fd,28) { illegal_1(cpustate, __func__); op_28(cpustate);                                   } /* DB   FD          */
OP(fd,29) { ADD16(IY,cpustate->_IY);                                    } /* ADD  IY,IY       */
OP(fd,2a) { cpustate->ea = ARG16(cpustate); RM16(cpustate,  cpustate->ea, &cpustate->IY );               } /* LD   IY,(w)      */
OP(fd,2b) { cpustate->_IY--;                                         } /* DEC  IY          */
OP(fd,2c) { cpustate->_LY = INC(cpustate, cpustate->_LY);                                    } /* INC  LY          */
OP(fd,2d) { cpustate->_LY = DEC(cpustate, cpustate->_LY);                                    } /* DEC  LY          */
OP(fd,2e) { cpustate->_LY = ARG(cpustate);                                       } /* LD   LY,n        */
OP(fd,2f) { illegal_1(cpustate, __func__); op_2f(cpustate);                                   } /* DB   FD          */

OP(fd,30) { illegal_1(cpustate, __func__); op_30(cpustate);                                   } /* DB   FD          */
OP(fd,31) { illegal_1(cpustate, __func__); op_31(cpustate);                                   } /* DB   FD          */
OP(fd,32) { illegal_1(cpustate, __func__); op_32(cpustate);                                   } /* DB   FD          */
OP(fd,33) { illegal_1(cpustate, __func__); op_33(cpustate);                                   } /* DB   FD          */
OP(fd,34) { EAY(cpustate); WM(cpustate,  cpustate->ea, INC(cpustate, RM(cpustate, cpustate->ea)) );                      } /* INC  (IY+o)      */
OP(fd,35) { EAY(cpustate); WM(cpustate,  cpustate->ea, DEC(cpustate, RM(cpustate, cpustate->ea)) );                      } /* DEC  (IY+o)      */
OP(fd,36) { EAY(cpustate); WM(cpustate,  cpustate->ea, ARG(cpustate) );                          } /* LD   (IY+o),n    */
OP(fd,37) { illegal_1(cpustate, __func__); op_37(cpustate);                                   } /* DB   FD          */

OP(fd,38) { illegal_1(cpustate, __func__); op_38(cpustate);                                   } /* DB   FD          */
OP(fd,39) { ADD16(IY,_SP(cpustate));                                    } /* ADD  IY,SP       */
OP(fd,3a) { illegal_1(cpustate, __func__); op_3a(cpustate);                                   } /* DB   FD          */
OP(fd,3b) { illegal_1(cpustate, __func__); op_3b(cpustate);                                   } /* DB   FD          */
OP(fd,3c) { illegal_1(cpustate, __func__); op_3c(cpustate);                                   } /* DB   FD          */
OP(fd,3d) { illegal_1(cpustate, __func__); op_3d(cpustate);                                   } /* DB   FD          */
OP(fd,3e) { illegal_1(cpustate, __func__); op_3e(cpustate);                                   } /* DB   FD          */
OP(fd,3f) { illegal_1(cpustate, __func__); op_3f(cpustate);                                   } /* DB   FD          */

OP(fd,40) { illegal_1(cpustate, __func__); op_40(cpustate);                                   } /* DB   FD          */
OP(fd,41) { illegal_1(cpustate, __func__); op_41(cpustate);                                   } /* DB   FD          */
OP(fd,42) { illegal_1(cpustate, __func__); op_42(cpustate);                                   } /* DB   FD          */
OP(fd,43) { illegal_1(cpustate, __func__); op_43(cpustate);                                   } /* DB   FD          */
OP(fd,44) { cpustate->_B = cpustate->_HY;                                        } /* LD   B,HY        */
OP(fd,45) { cpustate->_B = cpustate->_LY;                                        } /* LD   B,LY        */
OP(fd,46) { EAY(cpustate); cpustate->_B = RM(cpustate, cpustate->ea);                                } /* LD   B,(IY+o)    */
OP(fd,47) { illegal_1(cpustate, __func__); op_47(cpustate);                                   } /* DB   FD          */

OP(fd,48) { illegal_1(cpustate, __func__); op_48(cpustate);                                   } /* DB   FD          */
OP(fd,49) { illegal_1(cpustate, __func__); op_49(cpustate);                                   } /* DB   FD          */
OP(fd,4a) { illegal_1(cpustate, __func__); op_4a(cpustate);                                   } /* DB   FD          */
OP(fd,4b) { illegal_1(cpustate, __func__); op_4b(cpustate);                                   } /* DB   FD          */
OP(fd,4c) { cpustate->_C = cpustate->_HY;                                        } /* LD   C,HY        */
OP(fd,4d) { cpustate->_C = cpustate->_LY;                                        } /* LD   C,LY        */
OP(fd,4e) { EAY(cpustate); cpustate->_C = RM(cpustate, cpustate->ea);                                } /* LD   C,(IY+o)    */
OP(fd,4f) { illegal_1(cpustate, __func__); op_4f(cpustate);                                   } /* DB   FD          */

OP(fd,50) { illegal_1(cpustate, __func__); op_50(cpustate);                                   } /* DB   FD          */
OP(fd,51) { illegal_1(cpustate, __func__); op_51(cpustate);                                   } /* DB   FD          */
OP(fd,52) { illegal_1(cpustate, __func__); op_52(cpustate);                                   } /* DB   FD          */
OP(fd,53) { illegal_1(cpustate, __func__); op_53(cpustate);                                   } /* DB   FD          */
OP(fd,54) { cpustate->_D = cpustate->_HY;                                        } /* LD   D,HY        */
OP(fd,55) { cpustate->_D = cpustate->_LY;                                        } /* LD   D,LY        */
OP(fd,56) { EAY(cpustate); cpustate->_D = RM(cpustate, cpustate->ea);                                } /* LD   D,(IY+o)    */
OP(fd,57) { illegal_1(cpustate, __func__); op_57(cpustate);                                   } /* DB   FD          */

OP(fd,58) { illegal_1(cpustate, __func__); op_58(cpustate);                                   } /* DB   FD          */
OP(fd,59) { illegal_1(cpustate, __func__); op_59(cpustate);                                   } /* DB   FD          */
OP(fd,5a) { illegal_1(cpustate, __func__); op_5a(cpustate);                                   } /* DB   FD          */
OP(fd,5b) { illegal_1(cpustate, __func__); op_5b(cpustate);                                   } /* DB   FD          */
OP(fd,5c) { cpustate->_E = cpustate->_HY;                                        } /* LD   E,HY        */
OP(fd,5d) { cpustate->_E = cpustate->_LY;                                        } /* LD   E,LY        */
OP(fd,5e) { EAY(cpustate); cpustate->_E = RM(cpustate, cpustate->ea);                                } /* LD   E,(IY+o)    */
OP(fd,5f) { illegal_1(cpustate, __func__); op_5f(cpustate);                                   } /* DB   FD          */

OP(fd,60) { cpustate->_HY = cpustate->_B;                                        } /* LD   HY,B        */
OP(fd,61) { cpustate->_HY = cpustate->_C;                                        } /* LD   HY,C        */
OP(fd,62) { cpustate->_HY = cpustate->_D;                                        } /* LD   HY,D        */
OP(fd,63) { cpustate->_HY = cpustate->_E;                                        } /* LD   HY,E        */
OP(fd,64) {                                                         } /* LD   HY,HY       */
OP(fd,65) { cpustate->_HY = cpustate->_LY;                                       } /* LD   HY,LY       */
OP(fd,66) { EAY(cpustate); cpustate->_H = RM(cpustate, cpustate->ea);                                } /* LD   H,(IY+o)    */
OP(fd,67) { cpustate->_HY = cpustate->_A;                                        } /* LD   HY,A        */

OP(fd,68) { cpustate->_LY = cpustate->_B;                                        } /* LD   LY,B        */
OP(fd,69) { cpustate->_LY = cpustate->_C;                                        } /* LD   LY,C        */
OP(fd,6a) { cpustate->_LY = cpustate->_D;                                        } /* LD   LY,D        */
OP(fd,6b) { cpustate->_LY = cpustate->_E;                                        } /* LD   LY,E        */
OP(fd,6c) { cpustate->_LY = cpustate->_HY;                                       } /* LD   LY,HY       */
OP(fd,6d) {                                                         } /* LD   LY,LY       */
OP(fd,6e) { EAY(cpustate); cpustate->_L = RM(cpustate, cpustate->ea);                                } /* LD   L,(IY+o)    */
OP(fd,6f) { cpustate->_LY = cpustate->_A;                                        } /* LD   LY,A        */

OP(fd,70) { EAY(cpustate); WM(cpustate,  cpustate->ea, cpustate->_B );                               } /* LD   (IY+o),B    */
OP(fd,71) { EAY(cpustate); WM(cpustate,  cpustate->ea, cpustate->_C );                               } /* LD   (IY+o),C    */
OP(fd,72) { EAY(cpustate); WM(cpustate,  cpustate->ea, cpustate->_D );                               } /* LD   (IY+o),D    */
OP(fd,73) { EAY(cpustate); WM(cpustate,  cpustate->ea, cpustate->_E );                               } /* LD   (IY+o),E    */
OP(fd,74) { EAY(cpustate); WM(cpustate,  cpustate->ea, cpustate->_H );                               } /* LD   (IY+o),H    */
OP(fd,75) { EAY(cpustate); WM(cpustate,  cpustate->ea, cpustate->_L );                               } /* LD   (IY+o),L    */
OP(fd,76) { illegal_1(cpustate, __func__); op_76(cpustate);                                   }         /* DB   FD          */
OP(fd,77) { EAY(cpustate); WM(cpustate,  cpustate->ea, cpustate->_A );                               } /* LD   (IY+o),A    */

OP(fd,78) { EARA(cpustate); cpustate->_A = RM(cpustate, cpustate->ea);            } /* LD A,(ra)      */
OP(fd,79) { EAX16(cpustate); cpustate->_A = RM(cpustate, cpustate->ea);            } /* LD A,(IX+w)     */
OP(fd,7a) { EAY16(cpustate); cpustate->_A = RM(cpustate, cpustate->ea);            } /* LD A,(IY+w)     */
OP(fd,7b) { EAH16(cpustate); cpustate->_A = RM(cpustate, cpustate->ea);            } /* LD A,(HL+w)     */
OP(fd,7c) { cpustate->_A = cpustate->_HY;                                        } /* LD   A,HY        */
OP(fd,7d) { cpustate->_A = cpustate->_LY;                                        } /* LD   A,LY        */
OP(fd,7e) { EAY(cpustate); cpustate->_A = RM(cpustate, cpustate->ea);                                } /* LD   A,(IY+o)    */
OP(fd,7f) { illegal_1(cpustate, __func__); op_7f(cpustate);                                   } /* DB   FD          */

OP(fd,80) { EARA(cpustate); ADD(RM(cpustate, cpustate->ea));                    } /* ADD A,(ra)     */
OP(fd,81) { EAX16(cpustate); ADD(RM(cpustate, cpustate->ea));                      } /* ADD A,(IX+w)    */
OP(fd,82) { EAY16(cpustate); ADD(RM(cpustate, cpustate->ea));                      } /* ADD A,(IY+w)    */
OP(fd,83) { EAH16(cpustate); ADD(RM(cpustate, cpustate->ea));                      } /* ADD A,(HL+w)    */
OP(fd,84) { ADD(cpustate->_HY);                                      } /* ADD  A,HY        */
OP(fd,85) { ADD(cpustate->_LY);                                      } /* ADD  A,LY        */
OP(fd,86) { EAY(cpustate); ADD(RM(cpustate, cpustate->ea));                              } /* ADD  A,(IY+o)    */
OP(fd,87) { illegal_1(cpustate, __func__); op_87(cpustate);                                   } /* DB   FD          */

OP(fd,88) { EARA(cpustate); ADC(RM(cpustate, cpustate->ea));                    } /* ADC A,(ra)     */
OP(fd,89) { EAX16(cpustate); ADC(RM(cpustate, cpustate->ea));                      } /* ADC A,(IX+w)    */
OP(fd,8a) { EAY16(cpustate); ADC(RM(cpustate, cpustate->ea));                      } /* ADC A,(IY+w)    */
OP(fd,8b) { EAH16(cpustate); ADC(RM(cpustate, cpustate->ea));                      } /* ADC A,(HL+w)    */
OP(fd,8c) { ADC(cpustate->_HY);                                      } /* ADC  A,HY        */
OP(fd,8d) { ADC(cpustate->_LY);                                      } /* ADC  A,LY        */
OP(fd,8e) { EAY(cpustate); ADC(RM(cpustate, cpustate->ea));                              } /* ADC  A,(IY+o)    */
OP(fd,8f) { illegal_1(cpustate, __func__); op_8f(cpustate);                                   } /* DB   FD          */

OP(fd,90) { EARA(cpustate); SUB(RM(cpustate, cpustate->ea));                    } /* SUB A,(ra)     */
OP(fd,91) { EAX16(cpustate); SUB(RM(cpustate, cpustate->ea));                      } /* SUB A,(IX+w)    */
OP(fd,92) { EAY16(cpustate); SUB(RM(cpustate, cpustate->ea));                      } /* SUB A,(IY+w)    */
OP(fd,93) { EAH16(cpustate); SUB(RM(cpustate, cpustate->ea));                      } /* SUB A,(HL+w)    */
OP(fd,94) { SUB(cpustate->_HY);                                      } /* SUB  HY          */
OP(fd,95) { SUB(cpustate->_LY);                                      } /* SUB  LY          */
OP(fd,96) { EAY(cpustate); SUB(RM(cpustate, cpustate->ea));                              } /* SUB  (IY+o)      */
OP(fd,97) { illegal_1(cpustate, __func__); op_97(cpustate);                                   } /* DB   FD          */

OP(fd,98) { EARA(cpustate); SBC(RM(cpustate, cpustate->ea));                    } /* SBC A,(ra)     */
OP(fd,99) { EAX16(cpustate); SBC(RM(cpustate, cpustate->ea));                      } /* SBC A,(IX+w)    */
OP(fd,9a) { EAY16(cpustate); SBC(RM(cpustate, cpustate->ea));                      } /* SBC A,(IY+w)    */
OP(fd,9b) { EAH16(cpustate); SBC(RM(cpustate, cpustate->ea));                      } /* SBC A,(HL+w)    */
OP(fd,9c) { SBC(cpustate->_HY);                                      } /* SBC  A,HY        */
OP(fd,9d) { SBC(cpustate->_LY);                                      } /* SBC  A,LY        */
OP(fd,9e) { EAY(cpustate); SBC(RM(cpustate, cpustate->ea));                              } /* SBC  A,(IY+o)    */
OP(fd,9f) { illegal_1(cpustate, __func__); op_9f(cpustate);                                   } /* DB   FD          */

OP(fd,a0) { EARA(cpustate); AND(RM(cpustate, cpustate->ea));                    } /* AND A,(ra)     */
OP(fd,a1) { EAX16(cpustate); AND(RM(cpustate, cpustate->ea));                      } /* AND A,(IX+w)    */
OP(fd,a2) { EAY16(cpustate); AND(RM(cpustate, cpustate->ea));                      } /* AND A,(IY+w)    */
OP(fd,a3) { EAH16(cpustate); AND(RM(cpustate, cpustate->ea));                      } /* AND A,(HL+w)    */
OP(fd,a4) { AND(cpustate->_HY);                                      } /* AND  HY          */
OP(fd,a5) { AND(cpustate->_LY);                                      } /* AND  LY          */
OP(fd,a6) { EAY(cpustate); AND(RM(cpustate, cpustate->ea));                              } /* AND  (IY+o)      */
OP(fd,a7) { illegal_1(cpustate, __func__); op_a7(cpustate);                                   } /* DB   FD          */

OP(fd,a8) { EARA(cpustate); XOR(RM(cpustate, cpustate->ea));                    } /* XOR A,(ra)     */
OP(fd,a9) { EAX16(cpustate); XOR(RM(cpustate, cpustate->ea));                      } /* XOR A,(IX+w)    */
OP(fd,aa) { EAY16(cpustate); XOR(RM(cpustate, cpustate->ea));                      } /* XOR A,(IY+w)    */
OP(fd,ab) { EAH16(cpustate); XOR(RM(cpustate, cpustate->ea));                      } /* XOR A,(HL+w)    */
OP(fd,ac) { XOR(cpustate->_HY);                                      } /* XOR  HY          */
OP(fd,ad) { XOR(cpustate->_LY);                                      } /* XOR  LY          */
OP(fd,ae) { EAY(cpustate); XOR(RM(cpustate, cpustate->ea));                              } /* XOR  (IY+o)      */
OP(fd,af) { illegal_1(cpustate, __func__); op_af(cpustate);                                   } /* DB   FD          */

OP(fd,b0) { EARA(cpustate); OR(RM(cpustate, cpustate->ea));                    } /* OR A,(ra)      */
OP(fd,b1) { EAX16(cpustate); OR(RM(cpustate, cpustate->ea));                      } /* OR A,(IX+w)     */
OP(fd,b2) { EAY16(cpustate); OR(RM(cpustate, cpustate->ea));                      } /* OR A,(IY+w)     */
OP(fd,b3) { EAH16(cpustate); OR(RM(cpustate, cpustate->ea));                      } /* OR A,(HL+w)     */
OP(fd,b4) { OR(cpustate->_HY);                                           } /* OR   HY          */
OP(fd,b5) { OR(cpustate->_LY);                                           } /* OR   LY          */
OP(fd,b6) { EAY(cpustate); OR(RM(cpustate, cpustate->ea));                                   } /* OR   (IY+o)      */
OP(fd,b7) { illegal_1(cpustate, __func__); op_b7(cpustate);                                   } /* DB   FD          */

OP(fd,b8) { EARA(cpustate); CP(RM(cpustate, cpustate->ea));                    } /* CP A,(ra)      */
OP(fd,b9) { EAX16(cpustate); CP(RM(cpustate, cpustate->ea));                      } /* CP A,(IX+w)     */
OP(fd,ba) { EAY16(cpustate); CP(RM(cpustate, cpustate->ea));                      } /* CP A,(IY+w)     */
OP(fd,bb) { EAH16(cpustate); CP(RM(cpustate, cpustate->ea));                      } /* CP A,(HL+w)     */
OP(fd,bc) { CP(cpustate->_HY);                                           } /* CP   HY          */
OP(fd,bd) { CP(cpustate->_LY);                                           } /* CP   LY          */
OP(fd,be) { EAY(cpustate); CP(RM(cpustate, cpustate->ea));                                   } /* CP   (IY+o)      */
OP(fd,bf) { illegal_1(cpustate, __func__); op_bf(cpustate);                                   } /* DB   FD          */

OP(fd,c0) { illegal_1(cpustate, __func__); op_c0(cpustate);                                   } /* DB   FD          */
OP(fd,c1) { illegal_1(cpustate, __func__); op_c1(cpustate);                                   } /* DB   FD          */
OP(fd,c2) { JP_RA_COND( !(cpustate->_F & ZF) );                                     } /* JP NZ, (ra)      */
OP(fd,c3) { JP_RA;                                                                  } /* JP (ra)          */
OP(fd,c4) { CALL_RA_COND( !(cpustate->_F & ZF), 0xc4);                              } /* CALL NZ, (ra)    */
OP(fd,c5) { illegal_1(cpustate, __func__); op_c5(cpustate);                                   } /* DB   FD          */
OP(fd,c6) { illegal_1(cpustate, __func__); op_c6(cpustate);                                   } /* DB   FD          */
OP(fd,c7) { illegal_1(cpustate, __func__); op_c7(cpustate);                                   }         /* DB   FD          */

OP(fd,c8) { illegal_1(cpustate, __func__); op_c8(cpustate);                                   } /* DB   FD          */
OP(fd,c9) { illegal_1(cpustate, __func__); op_c9(cpustate);                                   } /* DB   FD          */
OP(fd,ca) { JP_RA_COND( cpustate->_F & ZF );                                        } /* JP Z, (ra)       */
OP(fd,cb) { EAY(cpustate); cpustate->extra_cycles += exec_xycb(cpustate,ARG(cpustate));                          } /* **   FD CB xx    */
OP(fd,cc) { CALL_RA_COND( cpustate->_F & ZF, 0xcc);                                 } /* CALL Z, (ra)     */
OP(fd,cd) { PUSH_R(cpustate, PC); EARA(cpustate); cpustate->_PCD = cpustate->ea; if(is_system(cpustate)) CHECK_SSO(cpustate); } /* CALL (ra)        */
OP(fd,ce) { illegal_1(cpustate, __func__); op_ce(cpustate);                                   } /* DB   FD          */
OP(fd,cf) { illegal_1(cpustate, __func__); op_cf(cpustate);                                   } /* DB   FD          */

OP(fd,d0) { illegal_1(cpustate, __func__); op_d0(cpustate);                                   } /* DB   FD          */
OP(fd,d1) { illegal_1(cpustate, __func__); op_d1(cpustate);                                   } /* DB   FD          */
OP(fd,d2) { JP_RA_COND( !(cpustate->_F & CF) );                                     } /* JP NC, (ra)      */
OP(fd,d3) { illegal_1(cpustate, __func__); op_d3(cpustate);                                   } /* DB   FD          */
OP(fd,d4) { CALL_RA_COND( !(cpustate->_F & CF), 0xd4);                              } /* CALL NC, (ra)    */
OP(fd,d5) { illegal_1(cpustate, __func__); op_d5(cpustate);                                   } /* DB   FD          */
OP(fd,d6) { illegal_1(cpustate, __func__); op_d6(cpustate);                                   } /* DB   FD          */
OP(fd,d7) { illegal_1(cpustate, __func__); op_d7(cpustate);                                   } /* DB   FD          */

OP(fd,d8) { illegal_1(cpustate, __func__); op_d8(cpustate);                                   } /* DB   FD          */
OP(fd,d9) { illegal_1(cpustate, __func__); op_d9(cpustate);                                   } /* DB   FD          */
OP(fd,da) { JP_RA_COND( cpustate->_F & CF );                                        } /* JP C, (ra)       */
OP(fd,db) { illegal_1(cpustate, __func__); op_db(cpustate);                                   } /* DB   FD          */
OP(fd,dc) { CALL_RA_COND( cpustate->_F & CF, 0xdc);                                 } /* CALL C, (ra)     */
OP(fd,dd) { illegal_1(cpustate, __func__); op_dd(cpustate);                                   } /* DB   FD          */
OP(fd,de) { illegal_1(cpustate, __func__); op_de(cpustate);                                   } /* DB   FD          */
OP(fd,df) { illegal_1(cpustate, __func__); op_df(cpustate);                                   } /* DB   FD          */

OP(fd,e0) { illegal_1(cpustate, __func__); op_e0(cpustate);                                   } /* DB   FD          */
OP(fd,e1) { POP(cpustate, IY);                                           } /* POP  IY          */
OP(fd,e2) { JP_RA_COND( !(cpustate->_F & PF) );                                     } /* JP PO, (ra)      */
OP(fd,e3) { EXSP(IY);                                        } /* EX   (SP),IY     */
OP(fd,e4) { CALL_RA_COND( !(cpustate->_F & PF), 0xe4);                              } /* CALL PO, (ra)    */
OP(fd,e5) { PUSH(cpustate,  IY );                                        } /* PUSH IY          */
OP(fd,e6) { illegal_1(cpustate, __func__); op_e6(cpustate);                                   } /* DB   FD          */
OP(fd,e7) { illegal_1(cpustate, __func__); op_e7(cpustate);                                   } /* DB   FD          */

OP(fd,e8) { illegal_1(cpustate, __func__); op_e8(cpustate);                                   } /* DB   FD          */
OP(fd,e9) { cpustate->_PC = cpustate->_IY;                                       } /* JP   (IY)        */
OP(fd,ea) { JP_RA_COND( cpustate->_F & PF );                                        } /* JP PE, (ra)      */
OP(fd,eb) { union PAIR tmp; tmp = cpustate->IY; cpustate->IY = cpustate->HL; cpustate->HL = tmp; } /* EX IY,HL         */
OP(fd,ec) { CALL_RA_COND( cpustate->_F & PF, 0xec);                                 } /* CALL PE, (ra)    */
OP(fd,ed) { cpustate->extra_cycles += exec_fded(cpustate,ROP(cpustate));            } /* **** FD ED xx    */
OP(fd,ee) { illegal_1(cpustate, __func__); op_ee(cpustate);                                   } /* DB   FD          */
OP(fd,ef) { illegal_1(cpustate, __func__); op_ef(cpustate);                                   } /* DB   FD          */

OP(fd,f0) { illegal_1(cpustate, __func__); op_f0(cpustate);                                   } /* DB   FD          */
OP(fd,f1) { illegal_1(cpustate, __func__); op_f1(cpustate);                                   } /* DB   FD          */
OP(fd,f2) { JP_RA_COND( !(cpustate->_F & SF) );                                     } /* JP P, (ra)       */
OP(fd,f3) { illegal_1(cpustate, __func__); op_f3(cpustate);                                   } /* DB   FD          */
OP(fd,f4) { CALL_RA_COND( !(cpustate->_F & SF), 0xf4);                              } /* CALL P, (ra)     */
OP(fd,f5) { union PAIR tmp; tmp.w.l = ARG16(cpustate); WM16(cpustate, _SPD(cpustate)-2, &tmp); DEC2_SP(cpustate); if(is_system(cpustate)) CHECK_SSO(cpustate); } /* PUSH w           */
OP(fd,f6) { illegal_1(cpustate, __func__); op_f6(cpustate);                                   } /* DB   FD          */
OP(fd,f7) { illegal_1(cpustate, __func__); op_f7(cpustate);                                   } /* DB   FD          */

OP(fd,f8) { illegal_1(cpustate, __func__); op_f8(cpustate);                                   } /* DB   FD          */
OP(fd,f9) { SET_SP(cpustate, cpustate->_IY);                                       } /* LD   SP,IY       */
OP(fd,fa) { JP_RA_COND( cpustate->_F & SF );                                        } /* JP M, (ra)      */
OP(fd,fb) { illegal_1(cpustate, __func__); op_fb(cpustate);                                   } /* DB   FD          */
OP(fd,fc) { CALL_RA_COND( cpustate->_F & SF, 0xfc);                                 } /* CALL M, (ra)     */
OP(fd,fd) { illegal_1(cpustate, __func__); op_fd(cpustate);                                   } /* DB   FD          */
OP(fd,fe) { illegal_1(cpustate, __func__); op_fe(cpustate);                                   } /* DB   FD          */
OP(fd,ff) { illegal_1(cpustate, __func__); op_ff(cpustate);                                   } /* DB   FD          */
