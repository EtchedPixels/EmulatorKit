## RC2014 Emulator

This is an emulator (prototype) for the RC2014 Z80 system. Currently it
emulates the Z80 (via libz80) and fully emulates the 6850 ACIA including
partial decodes and interrupts.

The goal is to emulate
- Z80 CPU (mostly done, time sync wants work)
- ROM and switchable ROM cards (done)
- 6850 ACIA (done)
- Z80 SIO/2 (in progress)
- 512K RAM/512K ROM card (in progress)
- CF adapter (not started)
- Real time clock (in progress)

At this point in time the ACIA emulation is complete and sufficient to run
the standard ROM environment with BASIC. This represents the basic
configuration of Z80 CPU card, 32K RAM card, 8K or switchable ROM card and
ACIA serial I/O card. This also corresponds to the RC2014 Mini.

# To get actual hardware see

https://rc2014.co.uk/

# For ROM images see

https://github.com/RC2014Z80/RC2014/tree/master/ROMs/Factory
