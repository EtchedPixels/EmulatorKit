/**********************************************************
* opcodes with DD/FD CB prefix
* rotate, shift and bit operations with (IX+o)
**********************************************************/
OP(xycb,00) { xycb_06(cpustate);                                            } /* RLC  (XY+o)      */
OP(xycb,01) { xycb_06(cpustate);                                            } /* RLC  (XY+o)      */
OP(xycb,02) { xycb_06(cpustate);                                            } /* RLC  (XY+o)      */
OP(xycb,03) { xycb_06(cpustate);                                            } /* RLC  (XY+o)      */
OP(xycb,04) { xycb_06(cpustate);                                            } /* RLC  (XY+o)      */
OP(xycb,05) { xycb_06(cpustate);                                            } /* RLC  (XY+o)      */
OP(xycb,06) { WM(cpustate,  cpustate->ea, RLC( cpustate, RM(cpustate, cpustate->ea) ) );                            } /* RLC  (XY+o)      */
OP(xycb,07) { xycb_06(cpustate);                                            } /* RLC  (XY+o)      */

OP(xycb,08) { xycb_0e(cpustate);                                            } /* RRC  (XY+o)      */
OP(xycb,09) { xycb_0e(cpustate);                                            } /* RRC  (XY+o)      */
OP(xycb,0a) { xycb_0e(cpustate);                                            } /* RRC  (XY+o)      */
OP(xycb,0b) { xycb_0e(cpustate);                                            } /* RRC  (XY+o)      */
OP(xycb,0c) { xycb_0e(cpustate);                                            } /* RRC  (XY+o)      */
OP(xycb,0d) { xycb_0e(cpustate);                                            } /* RRC  (XY+o)      */
OP(xycb,0e) { WM(cpustate,  cpustate->ea,RRC( cpustate, RM(cpustate, cpustate->ea) ) );                             } /* RRC  (XY+o)      */
OP(xycb,0f) { xycb_0e(cpustate);                                            } /* RRC  (XY+o)      */

OP(xycb,10) { xycb_16(cpustate);                                            } /* RL   (XY+o)      */
OP(xycb,11) { xycb_16(cpustate);                                            } /* RL   (XY+o)      */
OP(xycb,12) { xycb_16(cpustate);                                            } /* RL   (XY+o)      */
OP(xycb,13) { xycb_16(cpustate);                                            } /* RL   (XY+o)      */
OP(xycb,14) { xycb_16(cpustate);                                            } /* RL   (XY+o)      */
OP(xycb,15) { xycb_16(cpustate);                                            } /* RL   (XY+o)      */
OP(xycb,16) { WM(cpustate,  cpustate->ea,RL( cpustate, RM(cpustate, cpustate->ea) ) );                              } /* RL   (XY+o)      */
OP(xycb,17) { xycb_16(cpustate);                                            } /* RL   (XY+o)      */

OP(xycb,18) { xycb_1e(cpustate);                                            } /* RR   (XY+o)      */
OP(xycb,19) { xycb_1e(cpustate);                                            } /* RR   (XY+o)      */
OP(xycb,1a) { xycb_1e(cpustate);                                            } /* RR   (XY+o)      */
OP(xycb,1b) { xycb_1e(cpustate);                                            } /* RR   (XY+o)      */
OP(xycb,1c) { xycb_1e(cpustate);                                            } /* RR   (XY+o)      */
OP(xycb,1d) { xycb_1e(cpustate);                                            } /* RR   (XY+o)      */
OP(xycb,1e) { WM(cpustate,  cpustate->ea,RR( cpustate, RM(cpustate, cpustate->ea) ) );                              } /* RR   (XY+o)      */
OP(xycb,1f) { xycb_1e(cpustate);                                            } /* RR   (XY+o)      */

OP(xycb,20) { xycb_26(cpustate);                                            } /* SLA  (XY+o)      */
OP(xycb,21) { xycb_26(cpustate);                                            } /* SLA  (XY+o)      */
OP(xycb,22) { xycb_26(cpustate);                                            } /* SLA  (XY+o)      */
OP(xycb,23) { xycb_26(cpustate);                                            } /* SLA  (XY+o)      */
OP(xycb,24) { xycb_26(cpustate);                                            } /* SLA  (XY+o)      */
OP(xycb,25) { xycb_26(cpustate);                                            } /* SLA  (XY+o)      */
OP(xycb,26) { WM(cpustate,  cpustate->ea,SLA( cpustate, RM(cpustate, cpustate->ea) ) );                             } /* SLA  (XY+o)      */
OP(xycb,27) { xycb_26(cpustate);                                            } /* SLA  (XY+o)      */

OP(xycb,28) { xycb_2e(cpustate);                                            } /* SRA  (XY+o)      */
OP(xycb,29) { xycb_2e(cpustate);                                            } /* SRA  (XY+o)      */
OP(xycb,2a) { xycb_2e(cpustate);                                            } /* SRA  (XY+o)      */
OP(xycb,2b) { xycb_2e(cpustate);                                            } /* SRA  (XY+o)      */
OP(xycb,2c) { xycb_2e(cpustate);                                            } /* SRA  (XY+o)      */
OP(xycb,2d) { xycb_2e(cpustate);                                            } /* SRA  (XY+o)      */
OP(xycb,2e) { WM(cpustate,  cpustate->ea,SRA( cpustate, RM(cpustate, cpustate->ea) ) );                             } /* SRA  (XY+o)      */
OP(xycb,2f) { xycb_2e(cpustate);                                            } /* SRA  (XY+o)      */

OP(xycb,30) { xycb_36(cpustate);                                            } /* TSET  (XY+o)      */
OP(xycb,31) { xycb_36(cpustate);                                            } /* TSET  (XY+o)      */
OP(xycb,32) { xycb_36(cpustate);                                            } /* TSET  (XY+o)      */
OP(xycb,33) { xycb_36(cpustate);                                            } /* TSET  (XY+o)      */
OP(xycb,34) { xycb_36(cpustate);                                            } /* TSET  (XY+o)      */
OP(xycb,35) { xycb_36(cpustate);                                            } /* TSET  (XY+o)      */
OP(xycb,36) { WM(cpustate,  cpustate->ea,TSET( cpustate, RM(cpustate, cpustate->ea) ) );                            } /* TSET  (XY+o)      */
OP(xycb,37) { xycb_36(cpustate);                                            } /* TSET  (XY+o)      */

OP(xycb,38) { xycb_3e(cpustate);                                            } /* SRL  (XY+o)      */
OP(xycb,39) { xycb_3e(cpustate);                                            } /* SRL  (XY+o)      */
OP(xycb,3a) { xycb_3e(cpustate);                                            } /* SRL  (XY+o)      */
OP(xycb,3b) { xycb_3e(cpustate);                                            } /* SRL  (XY+o)      */
OP(xycb,3c) { xycb_3e(cpustate);                                            } /* SRL  (XY+o)      */
OP(xycb,3d) { xycb_3e(cpustate);                                            } /* SRL  (XY+o)      */
OP(xycb,3e) { WM(cpustate,  cpustate->ea,SRL( cpustate, RM(cpustate, cpustate->ea) ) );                             } /* SRL  (XY+o)      */
OP(xycb,3f) { xycb_3e(cpustate);                                            } /* SRL  (XY+o)      */

OP(xycb,40) { xycb_46(cpustate);                                            } /* BIT  0,(XY+o)    */
OP(xycb,41) { xycb_46(cpustate);                                            } /* BIT  0,(XY+o)    */
OP(xycb,42) { xycb_46(cpustate);                                            } /* BIT  0,(XY+o)    */
OP(xycb,43) { xycb_46(cpustate);                                            } /* BIT  0,(XY+o)    */
OP(xycb,44) { xycb_46(cpustate);                                            } /* BIT  0,(XY+o)    */
OP(xycb,45) { xycb_46(cpustate);                                            } /* BIT  0,(XY+o)    */
OP(xycb,46) { BIT_XY(0,RM(cpustate, cpustate->ea));                                     } /* BIT  0,(XY+o)    */
OP(xycb,47) { xycb_46(cpustate);                                            } /* BIT  0,(XY+o)    */

OP(xycb,48) { xycb_4e(cpustate);                                            } /* BIT  1,(XY+o)    */
OP(xycb,49) { xycb_4e(cpustate);                                            } /* BIT  1,(XY+o)    */
OP(xycb,4a) { xycb_4e(cpustate);                                            } /* BIT  1,(XY+o)    */
OP(xycb,4b) { xycb_4e(cpustate);                                            } /* BIT  1,(XY+o)    */
OP(xycb,4c) { xycb_4e(cpustate);                                            } /* BIT  1,(XY+o)    */
OP(xycb,4d) { xycb_4e(cpustate);                                            } /* BIT  1,(XY+o)    */
OP(xycb,4e) { BIT_XY(1,RM(cpustate, cpustate->ea));                                     } /* BIT  1,(XY+o)    */
OP(xycb,4f) { xycb_4e(cpustate);                                            } /* BIT  1,(XY+o)    */

OP(xycb,50) { xycb_56(cpustate);                                            } /* BIT  2,(XY+o)    */
OP(xycb,51) { xycb_56(cpustate);                                            } /* BIT  2,(XY+o)    */
OP(xycb,52) { xycb_56(cpustate);                                            } /* BIT  2,(XY+o)    */
OP(xycb,53) { xycb_56(cpustate);                                            } /* BIT  2,(XY+o)    */
OP(xycb,54) { xycb_56(cpustate);                                            } /* BIT  2,(XY+o)    */
OP(xycb,55) { xycb_56(cpustate);                                            } /* BIT  2,(XY+o)    */
OP(xycb,56) { BIT_XY(2,RM(cpustate, cpustate->ea));                                     } /* BIT  2,(XY+o)    */
OP(xycb,57) { xycb_56(cpustate);                                            } /* BIT  2,(XY+o)    */

OP(xycb,58) { xycb_5e(cpustate);                                            } /* BIT  3,(XY+o)    */
OP(xycb,59) { xycb_5e(cpustate);                                            } /* BIT  3,(XY+o)    */
OP(xycb,5a) { xycb_5e(cpustate);                                            } /* BIT  3,(XY+o)    */
OP(xycb,5b) { xycb_5e(cpustate);                                            } /* BIT  3,(XY+o)    */
OP(xycb,5c) { xycb_5e(cpustate);                                            } /* BIT  3,(XY+o)    */
OP(xycb,5d) { xycb_5e(cpustate);                                            } /* BIT  3,(XY+o)    */
OP(xycb,5e) { BIT_XY(3,RM(cpustate, cpustate->ea));                                     } /* BIT  3,(XY+o)    */
OP(xycb,5f) { xycb_5e(cpustate);                                            } /* BIT  3,(XY+o)    */

OP(xycb,60) { xycb_66(cpustate);                                            } /* BIT  4,(XY+o)  */
OP(xycb,61) { xycb_66(cpustate);                                            } /* BIT  4,(XY+o)  */
OP(xycb,62) { xycb_66(cpustate);                                            } /* BIT  4,(XY+o)  */
OP(xycb,63) { xycb_66(cpustate);                                            } /* BIT  4,(XY+o)  */
OP(xycb,64) { xycb_66(cpustate);                                            } /* BIT  4,(XY+o)  */
OP(xycb,65) { xycb_66(cpustate);                                            } /* BIT  4,(XY+o)  */
OP(xycb,66) { BIT_XY(4,RM(cpustate, cpustate->ea));                                     } /* BIT  4,(XY+o)    */
OP(xycb,67) { xycb_66(cpustate);                                            } /* BIT  4,(XY+o)  */

OP(xycb,68) { xycb_6e(cpustate);                                            } /* BIT  5,(XY+o)  */
OP(xycb,69) { xycb_6e(cpustate);                                                      } /* BIT  5,(XY+o)  */
OP(xycb,6a) { xycb_6e(cpustate);                                            } /* BIT  5,(XY+o)  */
OP(xycb,6b) { xycb_6e(cpustate);                                            } /* BIT  5,(XY+o)  */
OP(xycb,6c) { xycb_6e(cpustate);                                            } /* BIT  5,(XY+o)  */
OP(xycb,6d) { xycb_6e(cpustate);                                            } /* BIT  5,(XY+o)  */
OP(xycb,6e) { BIT_XY(5,RM(cpustate, cpustate->ea));                                     } /* BIT  5,(XY+o)    */
OP(xycb,6f) { xycb_6e(cpustate);                                            } /* BIT  5,(XY+o)  */

OP(xycb,70) { xycb_76(cpustate);                                            } /* BIT  6,(XY+o)  */
OP(xycb,71) { xycb_76(cpustate);                                                      } /* BIT  6,(XY+o)  */
OP(xycb,72) { xycb_76(cpustate);                                            } /* BIT  6,(XY+o)  */
OP(xycb,73) { xycb_76(cpustate);                                            } /* BIT  6,(XY+o)  */
OP(xycb,74) { xycb_76(cpustate);                                            } /* BIT  6,(XY+o)  */
OP(xycb,75) { xycb_76(cpustate);                                            } /* BIT  6,(XY+o)  */
OP(xycb,76) { BIT_XY(6,RM(cpustate, cpustate->ea));                                     } /* BIT  6,(XY+o)    */
OP(xycb,77) { xycb_76(cpustate);                                            } /* BIT  6,(XY+o)  */

OP(xycb,78) { xycb_7e(cpustate);                                            } /* BIT  7,(XY+o)  */
OP(xycb,79) { xycb_7e(cpustate);                                                      } /* BIT  7,(XY+o)  */
OP(xycb,7a) { xycb_7e(cpustate);                                            } /* BIT  7,(XY+o)  */
OP(xycb,7b) { xycb_7e(cpustate);                                            } /* BIT  7,(XY+o)  */
OP(xycb,7c) { xycb_7e(cpustate);                                            } /* BIT  7,(XY+o)  */
OP(xycb,7d) { xycb_7e(cpustate);                                            } /* BIT  7,(XY+o)  */
OP(xycb,7e) { BIT_XY(7,RM(cpustate, cpustate->ea));                                     } /* BIT  7,(XY+o)    */
OP(xycb,7f) { xycb_7e(cpustate);                                            } /* BIT  7,(XY+o)  */

OP(xycb,80) { xycb_86(cpustate);                                            } /* RES  0,(XY+o)  */
OP(xycb,81) { xycb_86(cpustate);                                            } /* RES  0,(XY+o)  */
OP(xycb,82) { xycb_86(cpustate);                                            } /* RES  0,(XY+o)  */
OP(xycb,83) { xycb_86(cpustate);                                            } /* RES  0,(XY+o)  */
OP(xycb,84) { xycb_86(cpustate);                                            } /* RES  0,(XY+o)  */
OP(xycb,85) { xycb_86(cpustate);                                            } /* RES  0,(XY+o)  */
OP(xycb,86) { WM(cpustate,  cpustate->ea, RES(0,RM(cpustate, cpustate->ea)) );                              } /* RES  0,(XY+o)    */
OP(xycb,87) { xycb_86(cpustate);                                            } /* RES  0,(XY+o)  */

OP(xycb,88) { xycb_8e(cpustate);                                            } /* RES  1,(XY+o)  */
OP(xycb,89) { xycb_8e(cpustate);                                            } /* RES  1,(XY+o)  */
OP(xycb,8a) { xycb_8e(cpustate);                                            } /* RES  1,(XY+o)  */
OP(xycb,8b) { xycb_8e(cpustate);                                            } /* RES  1,(XY+o)  */
OP(xycb,8c) { xycb_8e(cpustate);                                            } /* RES  1,(XY+o)  */
OP(xycb,8d) { xycb_8e(cpustate);                                            } /* RES  1,(XY+o)  */
OP(xycb,8e) { WM(cpustate,  cpustate->ea, RES(1,RM(cpustate, cpustate->ea)) );                              } /* RES  1,(XY+o)    */
OP(xycb,8f) { xycb_8e(cpustate);                                            } /* RES  1,(XY+o)  */

OP(xycb,90) { xycb_96(cpustate);                                            } /* RES  2,(XY+o)  */
OP(xycb,91) { xycb_96(cpustate);                                            } /* RES  2,(XY+o)  */
OP(xycb,92) { xycb_96(cpustate);                                            } /* RES  2,(XY+o)  */
OP(xycb,93) { xycb_96(cpustate);                                            } /* RES  2,(XY+o)  */
OP(xycb,94) { xycb_96(cpustate);                                            } /* RES  2,(XY+o)  */
OP(xycb,95) { xycb_96(cpustate);                                            } /* RES  2,(XY+o)  */
OP(xycb,96) { WM(cpustate,  cpustate->ea, RES(2,RM(cpustate, cpustate->ea)) );                              } /* RES  2,(XY+o)    */
OP(xycb,97) { xycb_96(cpustate);                                            } /* RES  2,(XY+o)  */

OP(xycb,98) { xycb_9e(cpustate);                                            } /* RES  3,(XY+o)  */
OP(xycb,99) { xycb_9e(cpustate);                                            } /* RES  3,(XY+o)  */
OP(xycb,9a) { xycb_9e(cpustate);                                            } /* RES  3,(XY+o)  */
OP(xycb,9b) { xycb_9e(cpustate);                                            } /* RES  3,(XY+o)  */
OP(xycb,9c) { xycb_9e(cpustate);                                            } /* RES  3,(XY+o)  */
OP(xycb,9d) { xycb_9e(cpustate);                                            } /* RES  3,(XY+o)  */
OP(xycb,9e) { WM(cpustate,  cpustate->ea, RES(3,RM(cpustate, cpustate->ea)) );                              } /* RES  3,(XY+o)    */
OP(xycb,9f) { xycb_9e(cpustate);                                            } /* RES  3,(XY+o)  */

OP(xycb,a0) { xycb_a6(cpustate);                                            } /* RES  4,(XY+o)  */
OP(xycb,a1) { xycb_a6(cpustate);                                            } /* RES  4,(XY+o)  */
OP(xycb,a2) { xycb_a6(cpustate);                                            } /* RES  4,(XY+o)  */
OP(xycb,a3) { xycb_a6(cpustate);                                            } /* RES  4,(XY+o)  */
OP(xycb,a4) { xycb_a6(cpustate);                                            } /* RES  4,(XY+o)  */
OP(xycb,a5) { xycb_a6(cpustate);                                            } /* RES  4,(XY+o)  */
OP(xycb,a6) { WM(cpustate,  cpustate->ea, RES(4,RM(cpustate, cpustate->ea)) );                              } /* RES  4,(XY+o)    */
OP(xycb,a7) { xycb_a6(cpustate);                                            } /* RES  4,(XY+o)  */

OP(xycb,a8) { xycb_ae(cpustate);                                            } /* RES  5,(XY+o)  */
OP(xycb,a9) { xycb_ae(cpustate);                                            } /* RES  5,(XY+o)  */
OP(xycb,aa) { xycb_ae(cpustate);                                            } /* RES  5,(XY+o)  */
OP(xycb,ab) { xycb_ae(cpustate);                                            } /* RES  5,(XY+o)  */
OP(xycb,ac) { xycb_ae(cpustate);                                            } /* RES  5,(XY+o)  */
OP(xycb,ad) { xycb_ae(cpustate);                                            } /* RES  5,(XY+o)  */
OP(xycb,ae) { WM(cpustate,  cpustate->ea, RES(5,RM(cpustate, cpustate->ea)) );                              } /* RES  5,(XY+o)    */
OP(xycb,af) { xycb_ae(cpustate);                                            } /* RES  5,(XY+o)  */

OP(xycb,b0) { xycb_b6(cpustate);                                            } /* RES  6,(XY+o)  */
OP(xycb,b1) { xycb_b6(cpustate);                                            } /* RES  6,(XY+o)  */
OP(xycb,b2) { xycb_b6(cpustate);                                            } /* RES  6,(XY+o)  */
OP(xycb,b3) { xycb_b6(cpustate);                                            } /* RES  6,(XY+o)  */
OP(xycb,b4) { xycb_b6(cpustate);                                            } /* RES  6,(XY+o)  */
OP(xycb,b5) { xycb_b6(cpustate);                                            } /* RES  6,(XY+o)  */
OP(xycb,b6) { WM(cpustate,  cpustate->ea, RES(6,RM(cpustate, cpustate->ea)) );                              } /* RES  6,(XY+o)    */
OP(xycb,b7) { xycb_b6(cpustate);                                            } /* RES  6,(XY+o)  */

OP(xycb,b8) { xycb_be(cpustate);                                            } /* RES  7,(XY+o)  */
OP(xycb,b9) { xycb_be(cpustate);                                            } /* RES  7,(XY+o)  */
OP(xycb,ba) { xycb_be(cpustate);                                            } /* RES  7,(XY+o)  */
OP(xycb,bb) { xycb_be(cpustate);                                            } /* RES  7,(XY+o)  */
OP(xycb,bc) { xycb_be(cpustate);                                            } /* RES  7,(XY+o)  */
OP(xycb,bd) { xycb_be(cpustate);                                            } /* RES  7,(XY+o)  */
OP(xycb,be) { WM(cpustate,  cpustate->ea, RES(7,RM(cpustate, cpustate->ea)) );                              } /* RES  7,(XY+o)    */
OP(xycb,bf) { xycb_be(cpustate);                                            } /* RES  7,(XY+o)  */

OP(xycb,c0) { xycb_c6(cpustate);                                            } /* SET  0,(XY+o)  */
OP(xycb,c1) { xycb_c6(cpustate);                                            } /* SET  0,(XY+o)  */
OP(xycb,c2) { xycb_c6(cpustate);                                            } /* SET  0,(XY+o)  */
OP(xycb,c3) { xycb_c6(cpustate);                                            } /* SET  0,(XY+o)  */
OP(xycb,c4) { xycb_c6(cpustate);                                            } /* SET  0,(XY+o)  */
OP(xycb,c5) { xycb_c6(cpustate);                                            } /* SET  0,(XY+o)  */
OP(xycb,c6) { WM(cpustate,  cpustate->ea, SET(0,RM(cpustate, cpustate->ea)) );                              } /* SET  0,(XY+o)    */
OP(xycb,c7) { xycb_c6(cpustate);                                            } /* SET  0,(XY+o)  */

OP(xycb,c8) { xycb_ce(cpustate);                                            } /* SET  1,(XY+o)  */
OP(xycb,c9) { xycb_ce(cpustate);                                            } /* SET  1,(XY+o)  */
OP(xycb,ca) { xycb_ce(cpustate);                                            } /* SET  1,(XY+o)  */
OP(xycb,cb) { xycb_ce(cpustate);                                            } /* SET  1,(XY+o)  */
OP(xycb,cc) { xycb_ce(cpustate);                                            } /* SET  1,(XY+o)  */
OP(xycb,cd) { xycb_ce(cpustate);                                            } /* SET  1,(XY+o)  */
OP(xycb,ce) { WM(cpustate,  cpustate->ea, SET(1,RM(cpustate, cpustate->ea)) );                              } /* SET  1,(XY+o)    */
OP(xycb,cf) { xycb_ce(cpustate);                                            } /* SET  1,(XY+o)  */

OP(xycb,d0) { xycb_d6(cpustate);                                            } /* SET  2,(XY+o)  */
OP(xycb,d1) { xycb_d6(cpustate);                                            } /* SET  2,(XY+o)  */
OP(xycb,d2) { xycb_d6(cpustate);                                            } /* SET  2,(XY+o)  */
OP(xycb,d3) { xycb_d6(cpustate);                                            } /* SET  2,(XY+o)  */
OP(xycb,d4) { xycb_d6(cpustate);                                            } /* SET  2,(XY+o)  */
OP(xycb,d5) { xycb_d6(cpustate);                                            } /* SET  2,(XY+o)  */
OP(xycb,d6) { WM(cpustate,  cpustate->ea, SET(2,RM(cpustate, cpustate->ea)) );                              } /* SET  2,(XY+o)    */
OP(xycb,d7) { xycb_d6(cpustate);                                            } /* SET  2,(XY+o)  */

OP(xycb,d8) { xycb_de(cpustate);                                            } /* SET  3,(XY+o)  */
OP(xycb,d9) { xycb_de(cpustate);                                            } /* SET  3,(XY+o)  */
OP(xycb,da) { xycb_de(cpustate);                                            } /* SET  3,(XY+o)  */
OP(xycb,db) { xycb_de(cpustate);                                            } /* SET  3,(XY+o)  */
OP(xycb,dc) { xycb_de(cpustate);                                            } /* SET  3,(XY+o)  */
OP(xycb,dd) { xycb_de(cpustate);                                            } /* SET  3,(XY+o)  */
OP(xycb,de) { WM(cpustate,  cpustate->ea, SET(3,RM(cpustate, cpustate->ea)) );                              } /* SET  3,(XY+o)    */
OP(xycb,df) { xycb_de(cpustate);                                            } /* SET  3,(XY+o)  */

OP(xycb,e0) { xycb_e6(cpustate);                                            } /* SET  4,(XY+o)  */
OP(xycb,e1) { xycb_e6(cpustate);                                            } /* SET  4,(XY+o)  */
OP(xycb,e2) { xycb_e6(cpustate);                                            } /* SET  4,(XY+o)  */
OP(xycb,e3) { xycb_e6(cpustate);                                            } /* SET  4,(XY+o)  */
OP(xycb,e4) { xycb_e6(cpustate);                                            } /* SET  4,(XY+o)  */
OP(xycb,e5) { xycb_e6(cpustate);                                            } /* SET  4,(XY+o)  */
OP(xycb,e6) { WM(cpustate,  cpustate->ea, SET(4,RM(cpustate, cpustate->ea)) );                              } /* SET  4,(XY+o)    */
OP(xycb,e7) { xycb_e6(cpustate);                                            } /* SET  4,(XY+o)  */

OP(xycb,e8) { xycb_ee(cpustate);                                            } /* SET  5,(XY+o)  */
OP(xycb,e9) { xycb_ee(cpustate);                                            } /* SET  5,(XY+o)  */
OP(xycb,ea) { xycb_ee(cpustate);                                            } /* SET  5,(XY+o)  */
OP(xycb,eb) { xycb_ee(cpustate);                                            } /* SET  5,(XY+o)  */
OP(xycb,ec) { xycb_ee(cpustate);                                            } /* SET  5,(XY+o)  */
OP(xycb,ed) { xycb_ee(cpustate);                                            } /* SET  5,(XY+o)  */
OP(xycb,ee) { WM(cpustate,  cpustate->ea, SET(5,RM(cpustate, cpustate->ea)) );                              } /* SET  5,(XY+o)    */
OP(xycb,ef) { xycb_ee(cpustate);                                            } /* SET  5,(XY+o)  */

OP(xycb,f0) { xycb_f6(cpustate);                                            } /* SET  6,(XY+o)  */
OP(xycb,f1) { xycb_f6(cpustate);                                            } /* SET  6,(XY+o)  */
OP(xycb,f2) { xycb_f6(cpustate);                                            } /* SET  6,(XY+o)  */
OP(xycb,f3) { xycb_f6(cpustate);                                            } /* SET  6,(XY+o)  */
OP(xycb,f4) { xycb_f6(cpustate);                                            } /* SET  6,(XY+o)  */
OP(xycb,f5) { xycb_f6(cpustate);                                            } /* SET  6,(XY+o)  */
OP(xycb,f6) { WM(cpustate,  cpustate->ea, SET(6,RM(cpustate, cpustate->ea)) );                              } /* SET  6,(XY+o)    */
OP(xycb,f7) { xycb_f6(cpustate);                                            } /* SET  6,(XY+o)  */

OP(xycb,f8) { xycb_fe(cpustate);                                            } /* SET  7,(XY+o)  */
OP(xycb,f9) { xycb_fe(cpustate);                                            } /* SET  7,(XY+o)  */
OP(xycb,fa) { xycb_fe(cpustate);                                            } /* SET  7,(XY+o)  */
OP(xycb,fb) { xycb_fe(cpustate);                                            } /* SET  7,(XY+o)  */
OP(xycb,fc) { xycb_fe(cpustate);                                            } /* SET  7,(XY+o)  */
OP(xycb,fd) { xycb_fe(cpustate);                                            } /* SET  7,(XY+o)  */
OP(xycb,fe) { WM(cpustate,  cpustate->ea, SET(7,RM(cpustate, cpustate->ea)) );                              } /* SET  7,(XY+o)    */
OP(xycb,ff) { xycb_fe(cpustate);                                            } /* SET  7,(XY+o)  */
