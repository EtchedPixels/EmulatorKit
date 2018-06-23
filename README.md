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
- CF adapter (done)
- Real time clock (in progress)

At this point in time the ACIA emulation is complete and sufficient to run
the standard ROM environment with BASIC. This represents the basic
configuration of Z80 CPU card, 32K RAM card, 8K or switchable ROM card and
ACIA serial I/O card. This also corresponds to the RC2014 Mini.

# To get actual hardware see

https://rc2014.co.uk/

# For ROM images see

https://github.com/RC2014Z80/RC2014/tree/master/ROMs/Factory

# Usage

Options:
- -a		enable 6850 ACIA
- -b		512K ROM/512K RAM board (not yet enabled)
- -e n		Execute ROM back n (0-7)
- -i path	Enable IDE and use this file
- -r path	Load the ROM image from this path
- -s		Enable the SIO/2 (broken)

To build a disk image

./makedisk 3 my.cf
dd if=filesystem of=my.cf bs=512 skip=2 conv=notrunc

In other words the IDE disk format has a 1K header that holds meta-data and
the virtual identify block.
