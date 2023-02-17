
/**********************************************************
 * main opcodes
 **********************************************************/
OP(op,00) {                                                         } /* NOP              */
OP(op,01) { cpustate->_BC = ARG16(cpustate);                                            } /* LD   BC,w        */
OP(op,02) { WM(cpustate,  cpustate->_BC, cpustate->_A );                                            } /* LD   (BC),A      */
OP(op,03) { cpustate->_BC++;                                                    } /* INC  BC          */
OP(op,04) { cpustate->_B = INC(cpustate, cpustate->_B);                                         } /* INC  B           */
OP(op,05) { cpustate->_B = DEC(cpustate, cpustate->_B);                                         } /* DEC  B           */
OP(op,06) { cpustate->_B = ARG(cpustate);                                           } /* LD   B,n         */
OP(op,07) { RLCA;                                                   } /* RLCA             */

OP(op,08) { EX_AF;                                                  } /* EX   AF,AF'      */
OP(op,09) { ADD16(HL, cpustate->_BC);                                           } /* ADD  HL,BC       */
OP(op,0a) { cpustate->_A = RM(cpustate, cpustate->_BC);                                         } /* LD   A,(BC)      */
OP(op,0b) { cpustate->_BC--;                                                    } /* DEC  BC          */
OP(op,0c) { cpustate->_C = INC(cpustate, cpustate->_C);                                         } /* INC  C           */
OP(op,0d) { cpustate->_C = DEC(cpustate, cpustate->_C);                                         } /* DEC  C           */
OP(op,0e) { cpustate->_C = ARG(cpustate);                                           } /* LD   C,n         */
OP(op,0f) { RRCA;                                                   } /* RRCA             */

OP(op,10) { cpustate->_B--; JR_COND( cpustate->_B, 0x10 );                              } /* DJNZ o           */
OP(op,11) { cpustate->_DE = ARG16(cpustate);                                            } /* LD   DE,w        */
OP(op,12) { WM(cpustate,  cpustate->_DE, cpustate->_A );                                            } /* LD   (DE),A      */
OP(op,13) { cpustate->_DE++;                                                    } /* INC  DE          */
OP(op,14) { cpustate->_D = INC(cpustate, cpustate->_D);                                         } /* INC  D           */
OP(op,15) { cpustate->_D = DEC(cpustate, cpustate->_D);                                         } /* DEC  D           */
OP(op,16) { cpustate->_D = ARG(cpustate);                                           } /* LD   D,n         */
OP(op,17) { RLA;                                                    } /* RLA              */

OP(op,18) { JR();                                                   } /* JR   o           */
OP(op,19) { ADD16(HL, cpustate->_DE);                                           } /* ADD  HL,DE       */
OP(op,1a) { cpustate->_A = RM(cpustate, cpustate->_DE);                                         } /* LD   A,(DE)      */
OP(op,1b) { cpustate->_DE--;                ;                                   } /* DEC  DE          */
OP(op,1c) { cpustate->_E = INC(cpustate, cpustate->_E);                                         } /* INC  E           */
OP(op,1d) { cpustate->_E = DEC(cpustate, cpustate->_E);                                         } /* DEC  E           */
OP(op,1e) { cpustate->_E = ARG(cpustate);                                           } /* LD   E,n         */
OP(op,1f) { RRA;                                                    } /* RRA              */

OP(op,20) { JR_COND( !(cpustate->_F & ZF), 0x20 );                          } /* JR   NZ,o        */
OP(op,21) { cpustate->_HL = ARG16(cpustate);                                            } /* LD   HL,w        */
OP(op,22) { cpustate->ea = ARG16(cpustate); WM16(cpustate, cpustate->ea, &cpustate->HL );                   } /* LD   (w),HL      */
OP(op,23) { cpustate->_HL++;                                                    } /* INC  HL          */
OP(op,24) { cpustate->_H = INC(cpustate, cpustate->_H);                                         } /* INC  H           */
OP(op,25) { cpustate->_H = DEC(cpustate, cpustate->_H);                                         } /* DEC  H           */
OP(op,26) { cpustate->_H = ARG(cpustate);                                           } /* LD   H,n         */
OP(op,27) { DAA;                                                    } /* DAA              */

OP(op,28) { JR_COND( cpustate->_F & ZF, 0x28 );                             } /* JR   Z,o         */
OP(op,29) { ADD16(HL, cpustate->_HL);                                           } /* ADD  HL,HL       */
OP(op,2a) { cpustate->ea = ARG16(cpustate); RM16(cpustate, cpustate->ea, &cpustate->HL );                   } /* LD   HL,(w)      */
OP(op,2b) { cpustate->_HL--;                                                    } /* DEC  HL          */
OP(op,2c) { cpustate->_L = INC(cpustate, cpustate->_L);                                         } /* INC  L           */
OP(op,2d) { cpustate->_L = DEC(cpustate, cpustate->_L);                                         } /* DEC  L           */
OP(op,2e) { cpustate->_L = ARG(cpustate);                                           } /* LD   L,n         */
OP(op,2f) { cpustate->_A ^= 0xff; cpustate->_F = (cpustate->_F&(SF|ZF|PF|CF))|HF|NF|(cpustate->_A&(YF|XF)); } /* CPL              */

OP(op,30) { JR_COND( !(cpustate->_F & CF), 0x30 );                          } /* JR   NC,o        */
OP(op,31) { SET_SP(cpustate, ARG16(cpustate));                                            } /* LD   SP,w        */
OP(op,32) { cpustate->ea = ARG16(cpustate); WM(cpustate,  cpustate->ea, cpustate->_A );                             } /* LD   (w),A       */
OP(op,33) { INC_SP(cpustate);                                                          } /* INC  SP          */
OP(op,34) { WM(cpustate,  cpustate->_HL, INC(cpustate, RM(cpustate, cpustate->_HL)) );                              } /* INC  (HL)        */
OP(op,35) { WM(cpustate,  cpustate->_HL, DEC(cpustate, RM(cpustate, cpustate->_HL)) );                              } /* DEC  (HL)        */
OP(op,36) { WM(cpustate,  cpustate->_HL, ARG(cpustate) );                                       } /* LD   (HL),n      */
OP(op,37) { cpustate->_F = (cpustate->_F & (SF|ZF|PF)) | CF | (cpustate->_A & (YF|XF));         } /* SCF              */

OP(op,38) { JR_COND( cpustate->_F & CF, 0x38 );                             } /* JR   C,o         */
OP(op,39) { ADD16(HL, _SP(cpustate));                                           } /* ADD  HL,SP       */
OP(op,3a) { cpustate->ea = ARG16(cpustate); cpustate->_A = RM(cpustate,  cpustate->ea );                            } /* LD   A,(w)       */
OP(op,3b) { DEC_SP(cpustate);                                                       } /* DEC  SP          */
OP(op,3c) { cpustate->_A = INC(cpustate, cpustate->_A);                                         } /* INC  A           */
OP(op,3d) { cpustate->_A = DEC(cpustate, cpustate->_A);                                         } /* DEC  A           */
OP(op,3e) { cpustate->_A = ARG(cpustate);                                           } /* LD   A,n         */
OP(op,3f) { cpustate->_F = ((cpustate->_F&(SF|ZF|PF|CF))|((cpustate->_F&CF)<<4)|(cpustate->_A&(YF|XF)))^CF; } /* CCF              */
//OP(op,3f) { cpustate->_F = ((cpustate->_F & ~(HF|NF)) | ((cpustate->_F & CF)<<4)) ^ CF;           } /* CCF              */

OP(op,40) {                                                         } /* LD   B,B         */
OP(op,41) { cpustate->_B = cpustate->_C;                                                } /* LD   B,C         */
OP(op,42) { cpustate->_B = cpustate->_D;                                                } /* LD   B,D         */
OP(op,43) { cpustate->_B = cpustate->_E;                                                } /* LD   B,E         */
OP(op,44) { cpustate->_B = cpustate->_H;                                                } /* LD   B,H         */
OP(op,45) { cpustate->_B = cpustate->_L;                                                } /* LD   B,L         */
OP(op,46) { cpustate->_B = RM(cpustate, cpustate->_HL);                                         } /* LD   B,(HL)      */
OP(op,47) { cpustate->_B = cpustate->_A;                                                } /* LD   B,A         */

OP(op,48) { cpustate->_C = cpustate->_B;                                                } /* LD   C,B         */
OP(op,49) {                                                         } /* LD   C,C         */
OP(op,4a) { cpustate->_C = cpustate->_D;                                                } /* LD   C,D         */
OP(op,4b) { cpustate->_C = cpustate->_E;                                                } /* LD   C,E         */
OP(op,4c) { cpustate->_C = cpustate->_H;                                                } /* LD   C,H         */
OP(op,4d) { cpustate->_C = cpustate->_L;                                                } /* LD   C,L         */
OP(op,4e) { cpustate->_C = RM(cpustate, cpustate->_HL);                                         } /* LD   C,(HL)      */
OP(op,4f) { cpustate->_C = cpustate->_A;                                                } /* LD   C,A         */

OP(op,50) { cpustate->_D = cpustate->_B;                                                } /* LD   D,B         */
OP(op,51) { cpustate->_D = cpustate->_C;                                                } /* LD   D,C         */
OP(op,52) {                                                         } /* LD   D,D         */
OP(op,53) { cpustate->_D = cpustate->_E;                                                } /* LD   D,E         */
OP(op,54) { cpustate->_D = cpustate->_H;                                                } /* LD   D,H         */
OP(op,55) { cpustate->_D = cpustate->_L;                                                } /* LD   D,L         */
OP(op,56) { cpustate->_D = RM(cpustate, cpustate->_HL);                                         } /* LD   D,(HL)      */
OP(op,57) { cpustate->_D = cpustate->_A;                                                } /* LD   D,A         */

OP(op,58) { cpustate->_E = cpustate->_B;                                                } /* LD   E,B         */
OP(op,59) { cpustate->_E = cpustate->_C;                                                } /* LD   E,C         */
OP(op,5a) { cpustate->_E = cpustate->_D;                                                } /* LD   E,D         */
OP(op,5b) {                                                         } /* LD   E,E         */
OP(op,5c) { cpustate->_E = cpustate->_H;                                                } /* LD   E,H         */
OP(op,5d) { cpustate->_E = cpustate->_L;                                                } /* LD   E,L         */
OP(op,5e) { cpustate->_E = RM(cpustate, cpustate->_HL);                                         } /* LD   E,(HL)      */
OP(op,5f) { cpustate->_E = cpustate->_A;                                                } /* LD   E,A         */

OP(op,60) { cpustate->_H = cpustate->_B;                                                } /* LD   H,B         */
OP(op,61) { cpustate->_H = cpustate->_C;                                                } /* LD   H,C         */
OP(op,62) { cpustate->_H = cpustate->_D;                                                } /* LD   H,D         */
OP(op,63) { cpustate->_H = cpustate->_E;                                                } /* LD   H,E         */
OP(op,64) {                                                         } /* LD   H,H         */
OP(op,65) { cpustate->_H = cpustate->_L;                                                } /* LD   H,L         */
OP(op,66) { cpustate->_H = RM(cpustate, cpustate->_HL);                                         } /* LD   H,(HL)      */
OP(op,67) { cpustate->_H = cpustate->_A;                                                } /* LD   H,A         */

OP(op,68) { cpustate->_L = cpustate->_B;                                                } /* LD   L,B         */
OP(op,69) { cpustate->_L = cpustate->_C;                                                } /* LD   L,C         */
OP(op,6a) { cpustate->_L = cpustate->_D;                                                } /* LD   L,D         */
OP(op,6b) { cpustate->_L = cpustate->_E;                                                } /* LD   L,E         */
OP(op,6c) { cpustate->_L = cpustate->_H;                                                } /* LD   L,H         */
OP(op,6d) {                                                         } /* LD   L,L         */
OP(op,6e) { cpustate->_L = RM(cpustate, cpustate->_HL);                                         } /* LD   L,(HL)      */
OP(op,6f) { cpustate->_L = cpustate->_A;                                                } /* LD   L,A         */

OP(op,70) { WM(cpustate,  cpustate->_HL, cpustate->_B );                                            } /* LD   (HL),B      */
OP(op,71) { WM(cpustate,  cpustate->_HL, cpustate->_C );                                            } /* LD   (HL),C      */
OP(op,72) { WM(cpustate,  cpustate->_HL, cpustate->_D );                                            } /* LD   (HL),D      */
OP(op,73) { WM(cpustate,  cpustate->_HL, cpustate->_E );                                            } /* LD   (HL),E      */
OP(op,74) { WM(cpustate,  cpustate->_HL, cpustate->_H );                                            } /* LD   (HL),H      */
OP(op,75) { WM(cpustate,  cpustate->_HL, cpustate->_L );                                            } /* LD   (HL),L      */
OP(op,76) { if (MSR(cpustate)&Z280_MSR_BH) { cpustate->extra_cycles += take_trap(cpustate, Z280_TRAP_BP); } else CHECK_PRIV(cpustate) { ENTER_HALT(cpustate); } } /* HALT             */
OP(op,77) { WM(cpustate,  cpustate->_HL, cpustate->_A );                                            } /* LD   (HL),A      */

OP(op,78) { cpustate->_A = cpustate->_B;                                                } /* LD   A,B         */
OP(op,79) { cpustate->_A = cpustate->_C;                                                } /* LD   A,C         */
OP(op,7a) { cpustate->_A = cpustate->_D;                                                } /* LD   A,D         */
OP(op,7b) { cpustate->_A = cpustate->_E;                                                } /* LD   A,E         */
OP(op,7c) { cpustate->_A = cpustate->_H;                                                } /* LD   A,H         */
OP(op,7d) { cpustate->_A = cpustate->_L;                                                } /* LD   A,L         */
OP(op,7e) { cpustate->_A = RM(cpustate, cpustate->_HL);                                         } /* LD   A,(HL)      */
OP(op,7f) {                                                         } /* LD   A,A         */

OP(op,80) { ADD(cpustate->_B);                                              } /* ADD  A,B         */
OP(op,81) { ADD(cpustate->_C);                                              } /* ADD  A,C         */
OP(op,82) { ADD(cpustate->_D);                                              } /* ADD  A,D         */
OP(op,83) { ADD(cpustate->_E);                                              } /* ADD  A,E         */
OP(op,84) { ADD(cpustate->_H);                                              } /* ADD  A,H         */
OP(op,85) { ADD(cpustate->_L);                                              } /* ADD  A,L         */
OP(op,86) { ADD(RM(cpustate, cpustate->_HL));                                           } /* ADD  A,(HL)      */
OP(op,87) { ADD(cpustate->_A);                                              } /* ADD  A,A         */

OP(op,88) { ADC(cpustate->_B);                                              } /* ADC  A,B         */
OP(op,89) { ADC(cpustate->_C);                                              } /* ADC  A,C         */
OP(op,8a) { ADC(cpustate->_D);                                              } /* ADC  A,D         */
OP(op,8b) { ADC(cpustate->_E);                                              } /* ADC  A,E         */
OP(op,8c) { ADC(cpustate->_H);                                              } /* ADC  A,H         */
OP(op,8d) { ADC(cpustate->_L);                                              } /* ADC  A,L         */
OP(op,8e) { ADC(RM(cpustate, cpustate->_HL));                                           } /* ADC  A,(HL)      */
OP(op,8f) { ADC(cpustate->_A);                                              } /* ADC  A,A         */

OP(op,90) { SUB(cpustate->_B);                                              } /* SUB  B           */
OP(op,91) { SUB(cpustate->_C);                                              } /* SUB  C           */
OP(op,92) { SUB(cpustate->_D);                                              } /* SUB  D           */
OP(op,93) { SUB(cpustate->_E);                                              } /* SUB  E           */
OP(op,94) { SUB(cpustate->_H);                                              } /* SUB  H           */
OP(op,95) { SUB(cpustate->_L);                                              } /* SUB  L           */
OP(op,96) { SUB(RM(cpustate, cpustate->_HL));                                           } /* SUB  (HL)        */
OP(op,97) { SUB(cpustate->_A);                                              } /* SUB  A           */

OP(op,98) { SBC(cpustate->_B);                                              } /* SBC  A,B         */
OP(op,99) { SBC(cpustate->_C);                                              } /* SBC  A,C         */
OP(op,9a) { SBC(cpustate->_D);                                              } /* SBC  A,D         */
OP(op,9b) { SBC(cpustate->_E);                                              } /* SBC  A,E         */
OP(op,9c) { SBC(cpustate->_H);                                              } /* SBC  A,H         */
OP(op,9d) { SBC(cpustate->_L);                                              } /* SBC  A,L         */
OP(op,9e) { SBC(RM(cpustate, cpustate->_HL));                                           } /* SBC  A,(HL)      */
OP(op,9f) { SBC(cpustate->_A);                                              } /* SBC  A,A         */

OP(op,a0) { AND(cpustate->_B);                                              } /* AND  B           */
OP(op,a1) { AND(cpustate->_C);                                              } /* AND  C           */
OP(op,a2) { AND(cpustate->_D);                                              } /* AND  D           */
OP(op,a3) { AND(cpustate->_E);                                              } /* AND  E           */
OP(op,a4) { AND(cpustate->_H);                                              } /* AND  H           */
OP(op,a5) { AND(cpustate->_L);                                              } /* AND  L           */
OP(op,a6) { AND(RM(cpustate, cpustate->_HL));                                           } /* AND  (HL)        */
OP(op,a7) { AND(cpustate->_A);                                              } /* AND  A           */

OP(op,a8) { XOR(cpustate->_B);                                              } /* XOR  B           */
OP(op,a9) { XOR(cpustate->_C);                                              } /* XOR  C           */
OP(op,aa) { XOR(cpustate->_D);                                              } /* XOR  D           */
OP(op,ab) { XOR(cpustate->_E);                                              } /* XOR  E           */
OP(op,ac) { XOR(cpustate->_H);                                              } /* XOR  H           */
OP(op,ad) { XOR(cpustate->_L);                                              } /* XOR  L           */
OP(op,ae) { XOR(RM(cpustate, cpustate->_HL));                                           } /* XOR  (HL)        */
OP(op,af) { XOR(cpustate->_A);                                              } /* XOR  A           */

OP(op,b0) { OR(cpustate->_B);                                               } /* OR   B           */
OP(op,b1) { OR(cpustate->_C);                                               } /* OR   C           */
OP(op,b2) { OR(cpustate->_D);                                               } /* OR   D           */
OP(op,b3) { OR(cpustate->_E);                                               } /* OR   E           */
OP(op,b4) { OR(cpustate->_H);                                               } /* OR   H           */
OP(op,b5) { OR(cpustate->_L);                                               } /* OR   L           */
OP(op,b6) { OR(RM(cpustate, cpustate->_HL));                                            } /* OR   (HL)        */
OP(op,b7) { OR(cpustate->_A);                                               } /* OR   A           */

OP(op,b8) { CP(cpustate->_B);                                               } /* CP   B           */
OP(op,b9) { CP(cpustate->_C);                                               } /* CP   C           */
OP(op,ba) { CP(cpustate->_D);                                               } /* CP   D           */
OP(op,bb) { CP(cpustate->_E);                                               } /* CP   E           */
OP(op,bc) { CP(cpustate->_H);                                               } /* CP   H           */
OP(op,bd) { CP(cpustate->_L);                                               } /* CP   L           */
OP(op,be) { CP(RM(cpustate, cpustate->_HL));                                            } /* CP   (HL)        */
OP(op,bf) { CP(cpustate->_A);                                               } /* CP   A           */

OP(op,c0) { RET_COND( !(cpustate->_F & ZF), 0xc0 );                         } /* RET  NZ          */
OP(op,c1) { POP(cpustate, BC);                                              } /* POP  BC          */
OP(op,c2) { JP_COND( !(cpustate->_F & ZF) );                                    } /* JP   NZ,a        */
OP(op,c3) { JP;                                                     } /* JP   a           */
OP(op,c4) { CALL_COND( !(cpustate->_F & ZF), 0xc4 );                            } /* CALL NZ,a        */
OP(op,c5) { PUSH(cpustate,  BC );                                           } /* PUSH BC          */
OP(op,c6) { ADD(ARG(cpustate));                                             } /* ADD  A,n         */
OP(op,c7) { RST(0x00);                                              } /* RST  0           */

OP(op,c8) { RET_COND( cpustate->_F & ZF, 0xc8 );                                } /* RET  Z           */
OP(op,c9) { POP(cpustate, PC);                                              } /* RET              */
OP(op,ca) { JP_COND( cpustate->_F & ZF );                                   } /* JP   Z,a         */
OP(op,cb) { cpustate->extra_cycles += exec_cb(cpustate,ROP(cpustate));                                   } /* **** CB xx       */
OP(op,cc) { CALL_COND( cpustate->_F & ZF, 0xcc );                           } /* CALL Z,a         */
OP(op,cd) { CALL();                                                 } /* CALL a           */
OP(op,ce) { ADC(ARG(cpustate));                                             } /* ADC  A,n         */
OP(op,cf) { RST(0x08);                                              } /* RST  1           */

OP(op,d0) { RET_COND( !(cpustate->_F & CF), 0xd0 );                         } /* RET  NC          */
OP(op,d1) { POP(cpustate, DE);                                              } /* POP  DE          */
OP(op,d2) { JP_COND( !(cpustate->_F & CF) );                                    } /* JP   NC,a        */
OP(op,d3) { CHECK_PRIV_IO(cpustate) { unsigned n = ARG(cpustate) | (cpustate->_A << 8); OUT( cpustate, n, cpustate->_A ); } } /* OUT  (n),A       */
OP(op,d4) { CALL_COND( !(cpustate->_F & CF), 0xd4 );                            } /* CALL NC,a        */
OP(op,d5) { PUSH(cpustate,  DE );                                           } /* PUSH DE          */
OP(op,d6) { SUB(ARG(cpustate));                                             } /* SUB  n           */
OP(op,d7) { RST(0x10);                                              } /* RST  2           */

OP(op,d8) { RET_COND( cpustate->_F & CF, 0xd8 );                                } /* RET  C           */
OP(op,d9) { EXX;                                                    } /* EXX              */
OP(op,da) { JP_COND( cpustate->_F & CF );                                   } /* JP   C,a         */
OP(op,db) { CHECK_PRIV_IO(cpustate) { unsigned n = ARG(cpustate) | (cpustate->_A << 8); cpustate->_A = IN( cpustate, n ); } } /* IN   A,(n)       */
OP(op,dc) { CALL_COND( cpustate->_F & CF, 0xdc );                           } /* CALL C,a         */
OP(op,dd) { cpustate->extra_cycles += exec_dd(cpustate,ROP(cpustate));                                   } /* **** DD xx       */
OP(op,de) { SBC(ARG(cpustate));                                             } /* SBC  A,n         */
OP(op,df) { RST(0x18);                                              } /* RST  3           */

OP(op,e0) { RET_COND( !(cpustate->_F & PF), 0xe0 );                         } /* RET  PO          */
OP(op,e1) { POP(cpustate, HL);                                              } /* POP  HL          */
OP(op,e2) { JP_COND( !(cpustate->_F & PF) );                                    } /* JP   PO,a        */
OP(op,e3) { EXSP(HL);                                               } /* EX   HL,(SP)     */
OP(op,e4) { CALL_COND( !(cpustate->_F & PF), 0xe4 );                            } /* CALL PO,a        */
OP(op,e5) { PUSH(cpustate,  HL );                                           } /* PUSH HL          */
OP(op,e6) { AND(ARG(cpustate));                                             } /* AND  n           */
OP(op,e7) { RST(0x20);                                              } /* RST  4           */

OP(op,e8) { RET_COND( cpustate->_F & PF, 0xe8 );                                } /* RET  PE          */
OP(op,e9) { cpustate->_PC = cpustate->_HL;                                              } /* JP   (HL)        */
OP(op,ea) { JP_COND( cpustate->_F & PF );                                   } /* JP   PE,a        */
OP(op,eb) { EX_DE_HL;                                               } /* EX   DE,HL       */
OP(op,ec) { CALL_COND( cpustate->_F & PF, 0xec );                           } /* CALL PE,a        */
OP(op,ed) { cpustate->extra_cycles += exec_ed(cpustate,ROP(cpustate));                                   } /* **** ED xx       */
OP(op,ee) { XOR(ARG(cpustate));                                             } /* XOR  n           */
OP(op,ef) { RST(0x28);                                              } /* RST  5           */

OP(op,f0) { RET_COND( !(cpustate->_F & SF), 0xf0 );                         } /* RET  P           */
OP(op,f1) { POP(cpustate, AF);                                              } /* POP  AF          */
OP(op,f2) { JP_COND( !(cpustate->_F & SF) );                                    } /* JP   P,a         */
OP(op,f3) { DI(0x7f);                                                           } /* DI               */
OP(op,f4) { CALL_COND( !(cpustate->_F & SF), 0xf4 );                            } /* CALL P,a         */
OP(op,f5) { PUSH(cpustate,  AF );                                           } /* PUSH AF          */
OP(op,f6) { OR(ARG(cpustate));                                              } /* OR   n           */
OP(op,f7) { RST(0x30);                                              } /* RST  6           */

OP(op,f8) { RET_COND( cpustate->_F & SF, 0xf8 );                                } /* RET  M           */
OP(op,f9) { SET_SP(cpustate, cpustate->_HL);                                              } /* LD   SP,HL       */
OP(op,fa) { JP_COND(cpustate->_F & SF);                                     } /* JP   M,a         */
OP(op,fb) { EI(0x7f);                                                     } /* EI               */
OP(op,fc) { CALL_COND( cpustate->_F & SF, 0xfc );                           } /* CALL M,a         */
OP(op,fd) { cpustate->extra_cycles += exec_fd(cpustate,ROP(cpustate));                                   } /* **** FD xx       */
OP(op,fe) { CP(ARG(cpustate));                                              } /* CP   n           */
OP(op,ff) { RST(0x38);                                              } /* RST  7           */


// get IRQ vector (or 1/3-byte instruction if in mode 0) from the irq ack bus transaction
UINT32 get_irq_vector(struct z280_state *cpustate, int irq)
{
	UINT32 irq_vector = 0;
	int irqline;

	/* Daisy chain mode? If so, call the requesting device */
	struct z80daisy_interface * irqdev;
	if (irq == Z280_INT_IRQ0 && cpustate->daisy != NULL) {  // TODO: probably works on all 3 irqlines on bus16
		irqdev = z80_daisy_chain_get_irq_device(cpustate->daisy);
		irq_vector = irqdev->z80daisy_irq_ack_cb(irqdev);
	}

	/* else call back the cpu interface to retrieve the vector */
	else
	{
		if (cpustate->device->m_bus16)
			irqline = irq>>2; /* INPUT_LINE_IRQ0,IRQ1,IRQ2 */
		else /* Z80 compat p.6-3 */
			irqline = INPUT_LINE_IRQ0;
		if (cpustate->irq_callback != NULL)
			irq_vector = (*cpustate->irq_callback)(cpustate->device, irqline);
	}
	if (cpustate->IM == 0)
		LOG("Z280 '%s' IACK irq=%d %06x\n", cpustate->device->m_tag, irqline, irq_vector);
	else
		LOG("Z280 '%s' IACK irq=%d %02x\n", cpustate->device->m_tag, irqline, irq_vector);
	return irq_vector;
}


int take_interrupt(struct z280_state *cpustate, int irq)
{
	int cycles = 0;
	UINT32 irq_vector;

	/* there isn't a valid previous program counter */
	cpustate->_PPC = -1;

	/* Check if processor was halted */
	if (cpustate->abort_type != Z280_ABORT_FATAL)
	{
		LEAVE_HALT(cpustate);
	}

	if(cpustate->IM != 3 && (irq == Z280_INT_NMI || irq == Z280_INT_IRQ0 || irq == Z280_INT_IRQ1 || irq == Z280_INT_IRQ2))
	{
		/* NMI mode 0,1,2 */
		if (irq == Z280_INT_NMI)
		{
			cpustate->IFF2 = cpustate->cr[Z280_MSR] & Z280_MSR_IREMASK;
			/* Clear MSR */
			MSR(cpustate) &= ~(Z280_MSR_US | Z280_MSR_SS | Z280_MSR_IREMASK);
			cpustate->abort_type = Z280_ABORT_FATAL;
			PUSH(cpustate,  PC );
			cpustate->abort_type = Z280_ABORT_ACCV;
			cpustate->_PCD = 0x0066;
			LOG("Z280 '%s' NMI $0066\n", cpustate->device->m_tag);
			//cpustate->IO_DSTAT &= ~Z280_DSTAT_DME;
			//cpustate->IFF2 = cpustate->cr[Z280_MSR]&Z280_MSR_IREMASK;
			/* CALL opcode timing */
			cycles += cpustate->cc[Z280_TABLE_op][0xcd];  // ???
		}
		else /* IRQ0,1,2 mode 0,1,2 */
		{
			switch ( cpustate->IM  )
			{
				case 0:
					/* Interrupt mode 0. */
					/* Clear MSR */
					MSR(cpustate) &= ~(Z280_MSR_US | Z280_MSR_SS | Z280_MSR_IREMASK);
					/* We check for CALL and JP instructions, */
					/* if neither of these were found we assume a 1 byte opcode */
					/* was placed on the databus                                */
					irq_vector = get_irq_vector(cpustate, irq);
					LOG("Z280 '%s' IM0 $%06x\n",cpustate->device->m_tag , irq_vector);
					cpustate->abort_type = Z280_ABORT_FATAL;
					switch (irq_vector & 0xff0000)
					{
						case 0xcd0000:  /* call */
							PUSH(cpustate,  PC );
							cpustate->_PCD = irq_vector & 0xffff;
								/* CALL $xxxx + 'interrupt latency' cycles */
							cycles += cpustate->cc[Z280_TABLE_op][0xcd] - cpustate->cc[Z280_TABLE_ex][0xff];
							break;
						case 0xc30000:  /* jump */
							cpustate->_PCD = irq_vector & 0xffff;
							/* JP $xxxx + 2 cycles */
							cycles += cpustate->cc[Z280_TABLE_op][0xc3] - cpustate->cc[Z280_TABLE_ex][0xff];
							break;
						default:        /* rst (or other opcodes?) */
							PUSH(cpustate,  PC );
							cpustate->_PCD = irq_vector & 0x0038;
							/* RST $xx + 2 cycles */
							cycles += cpustate->cc[Z280_TABLE_op][cpustate->_PCD] - cpustate->cc[Z280_TABLE_ex][cpustate->_PCD];
							break;
					}
					cpustate->abort_type = Z280_ABORT_ACCV;
					break;
				case 1:
					/* Interrupt mode 1. RST 38h */
					/* Clear MSR */
					MSR(cpustate) &= ~(Z280_MSR_US | Z280_MSR_SS | Z280_MSR_IREMASK);
					LOG("Z280 '%s' IM1 $0038\n",cpustate->device->m_tag );
					cpustate->abort_type = Z280_ABORT_FATAL;
					PUSH(cpustate,  PC );
					cpustate->abort_type = Z280_ABORT_ACCV;
					cpustate->_PCD = 0x0038;
					/* RST $38 + 'interrupt latency' cycles */
					cycles += cpustate->cc[Z280_TABLE_op][0xff] - cpustate->cc[Z280_TABLE_ex][0xff];
					break;
				case 2:
					/* Interrupt mode 2. Vector through [I:databyte] */
					/* Clear MSR */
					irq_vector = get_irq_vector(cpustate, irq);
					MSR(cpustate) &= ~(Z280_MSR_US | Z280_MSR_SS | Z280_MSR_IREMASK);
					irq_vector = (irq_vector & 0xff) + (cpustate->I << 8);
					cpustate->abort_type = Z280_ABORT_FATAL;
					PUSH(cpustate,  PC );
					cpustate->abort_type = Z280_ABORT_ACCV;
					RM16( cpustate, irq_vector, &cpustate->PC ); // system mode p.6-2
					LOG("Z280 '%s' IM2 [$%02x] = $%04x\n",cpustate->device->m_tag , irq_vector, cpustate->_PCD);
					/* CALL opcode timing */
					cycles += cpustate->cc[Z280_TABLE_op][0xcd];
					break;
			}
		}
	}
	else
	{
		/* Interrupt mode 3. Lookup through IVT and optional vectoring */
		// all internal peripherals always use mode 3; NMI,IRQ0-2 only when IM 3 is set
		union PAIR tmp;
		UINT16 vecoffs,isrmask=0,vectable;
		switch (irq)
		{
			case Z280_INT_NMI:
				vecoffs = 0x4;
				isrmask = 0x1000;
				vectable = 0x70;
				break;
			case Z280_INT_IRQ0:
				vecoffs = 0x8;
				isrmask = 0x2000;
				vectable = 0x70;
				break;
			case Z280_INT_IRQ1:
				vecoffs = 0xc;
				isrmask = 0x3000;
				vectable = 0x170;
				break;
			case Z280_INT_IRQ2:
				vecoffs = 0x10;
				isrmask = 0x4000;
				vectable = 0x270;
				break;
			case Z280_INT_CTR0:
				vecoffs = 0x14;
				break;
			case Z280_INT_CTR1:
				vecoffs = 0x18;
				break;
			case Z280_INT_CTR2:
				vecoffs = 0x20;
				break;
			case Z280_INT_DMA0:
				vecoffs = 0x24;
				break;
			case Z280_INT_DMA1:
				vecoffs = 0x28;
				break;
			case Z280_INT_DMA2:
				vecoffs = 0x2C;
				break;
			case Z280_INT_DMA3:
				vecoffs = 0x30;
				break;
			case Z280_INT_UARTRX:
				vecoffs = 0x34;
				break;
			case Z280_INT_UARTTX:
				vecoffs = 0x38;
				break;
		}

		if (irq == Z280_INT_NMI || irq == Z280_INT_IRQ0 || irq == Z280_INT_IRQ1 || irq == Z280_INT_IRQ2)
		{
			irq_vector = get_irq_vector(cpustate, irq);
			if (!cpustate->device->m_bus16)
			{
				irq_vector &= 0xff;
			}
		}
		else
		{
			// internal peripheral p.6-3
			irq_vector = vecoffs;
		}

		// fetch state while still in original mode
		tmp.w.l = MSR(cpustate);

		// switch to system mode
		MSR(cpustate) &= ~Z280_MSR_US;
		// save state
		cpustate->abort_type = Z280_ABORT_FATAL;
		cpustate->_SSP-=2; WM16(cpustate, cpustate->_SSPD, &cpustate->PC); // PC of current instr
		cpustate->_SSP-=2; WM16(cpustate, cpustate->_SSPD, &tmp); // MSR
		tmp.w.l = (UINT16)irq_vector;
		cpustate->_SSP-=2; WM16(cpustate, cpustate->_SSPD, &tmp);
		cpustate->abort_type = Z280_ABORT_ACCV;

		// load IV
		offs_t ivaddr = CALC_IVADDR(cpustate, vecoffs);
		MSR(cpustate) = RM16PHY(cpustate,ivaddr);
		if (!(cpustate->cr[Z280_ISR] & isrmask)) // not vectored
		{
			cpustate->_PCD = RM16PHY(cpustate,ivaddr+2);
			LOG("Z280 '%s' IM3 IVT:%06x [$%02x] = $%04x,msr:$%04x\n",cpustate->device->m_tag , 
		        CALC_IVADDR(cpustate, 0), vecoffs, cpustate->_PCD, MSR(cpustate));
		}
		else // vectored
		{
			// irq_vector must be even p.6-10
			irq_vector += vectable;
			cpustate->_PCD = RM16PHY(cpustate,ivaddr + irq_vector );
			LOG("Z280 '%s' IM3 IVT:%06x [$%02x] vectored $%03x = $%04x,msr:$%04x\n",cpustate->device->m_tag ,
			    CALC_IVADDR(cpustate, 0), vecoffs, irq_vector, cpustate->_PCD, MSR(cpustate));
		}
		
		/* CALL opcode timing */
		cycles += cpustate->cc[Z280_TABLE_op][0xcd];
	}
	CHECK_SSO(cpustate);
	return cycles;
}

void TRAP(struct z280_state *cpustate, int vector, int trapsave)
{
	union PAIR tmp, tmparg;
	// fetch state while still in original mode
	tmp.w.l = MSR(cpustate);
	if (trapsave&Z280_TRAPSAVE_ARG16) // SC also pushes word argument
	{
		tmparg.w.l = ARG16(cpustate);
	}

	// switch to system mode
	MSR(cpustate) &= ~Z280_MSR_US;
	// save state
	cpustate->abort_type = Z280_ABORT_FATAL;
	cpustate->_SSP-=2;
	if (trapsave&Z280_TRAPSAVE_PREPC)
	{
		WM16(cpustate, cpustate->_SSPD, &cpustate->PREPC); // PC of the causing instruction
		tmp.w.l &= ~Z280_MSR_SSP; // p.6-5
	}
	else
	{
		WM16(cpustate, cpustate->_SSPD, &cpustate->PC);  // PC of next instruction
	}
	cpustate->_SSP-=2; WM16(cpustate, cpustate->_SSPD, &tmp); // MSR
	if (trapsave&Z280_TRAPSAVE_ARG16)
	{
		cpustate->_SSP-=2; WM16(cpustate, cpustate->_SSPD, &tmparg);
	}
	if (trapsave&Z280_TRAPSAVE_EA)
	{
		tmparg.d = cpustate->ea;
		cpustate->_SSP-=2; WM16(cpustate, cpustate->_SSPD, &tmparg);
	}
	if (trapsave&Z280_TRAPSAVE_EPU)
	{
		tmparg.w.l = cpustate->_PC - 4;
		cpustate->_SSP-=2; WM16(cpustate, cpustate->_SSPD, &tmparg);
	}
	cpustate->abort_type = Z280_ABORT_ACCV;
	// load IV
	offs_t ivaddr = CALC_IVADDR(cpustate, vector);
	MSR(cpustate) = RM16PHY(cpustate,ivaddr);
	cpustate->_PCD = RM16PHY(cpustate,ivaddr+2);
	LOG("Z280 '%s' TRAP IVT:%06x [$%02x] = $%04x,msr:$%04x\n",cpustate->device->m_tag , CALC_IVADDR(cpustate, 0), vector, cpustate->_PCD, MSR(cpustate));
	CHECK_SSO(cpustate);
}

int take_trap(struct z280_state *cpustate, int trap)
{
	int cycles;
	switch (trap)
	{
		case Z280_TRAP_SS:
			TRAP(cpustate, 0x3C, 0);
			cycles = 26;
			break;
		case Z280_TRAP_BP:
			TRAP(cpustate, 0x40, Z280_TRAPSAVE_PREPC);
			cycles = 26;
			break;
		case Z280_TRAP_ACCV:
			MMUMCR(cpustate) = (MMUMCR(cpustate) &~ Z280_MMUMCR_PFIMASK) | cpustate->eapdr;
			TRAP(cpustate, 0x4C, Z280_TRAPSAVE_PREPC);
			cycles = 25;
			break;
		case Z280_TRAP_DIV:
			TRAP(cpustate, 0x44, Z280_TRAPSAVE_PREPC);
			cycles = 25;
			break;
		case Z280_TRAP_SSO:
			(cpustate)->cr[Z280_TCR] &= ~Z280_TCR_S;
			TRAP(cpustate, 0x48, 0);
			cycles = 26;
			break;
		case Z280_TRAP_PRIV:
			TRAP(cpustate, 0x54, Z280_TRAPSAVE_PREPC);
			cycles = 26;
			break;
		case Z280_TRAP_SC:
			TRAP(cpustate, 0x50, Z280_TRAPSAVE_ARG16);
			cycles = 30;
			break;
		case Z280_TRAP_EPUM:
			TRAP(cpustate, 0x58, Z280_TRAPSAVE_EPU|Z280_TRAPSAVE_EA);
			cycles = 38;
			break;
		case Z280_TRAP_MEPU:
			TRAP(cpustate, 0x5C, Z280_TRAPSAVE_EPU|Z280_TRAPSAVE_EA);
			cycles = 38;
			break;
		case Z280_TRAP_EPUF:
			TRAP(cpustate, 0x60, Z280_TRAPSAVE_EPU);
			cycles = 31;
			break;
		case Z280_TRAP_EPUI:
			TRAP(cpustate, 0x64, Z280_TRAPSAVE_EPU);
			cycles = 31;
			break;
	}
	return cycles;
}

int take_fatal(struct z280_state *cpustate)
{
	cpustate->_HL = cpustate->_PPC;
	cpustate->_DE = MSR(cpustate);
	cpustate->cr[Z280_MSR] &= ~Z280_MSR_IREMASK;
	cpustate->HALT = 1;
	return 15;
}
