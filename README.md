## RC2014 Emulator

This is an emulator (prototype) for the RC2014 Z80 system. Currently it
emulates the Z80 (via libz80) and fully emulates the 6850 ACIA including
partial decodes and interrupts.

The goal is to emulate
- Z80 CPU (mostly done, time sync wants work)
- ROM and switchable ROM cards (done)
- 6850 ACIA (done)
- Z80 SIO/2 (async polled done, needs interrupt support)
- 512K RAM/512K ROM card (done, untested)
- CF adapter (done)
- Real time clock (in progress)
- CTC at 0x90 (untested)

At this point in time the ACIA emulation is complete and sufficient to run
the standard ROM environment with BASIC. This represents the basic
configuration of Z80 CPU card, 32K RAM card, 8K or switchable ROM card and
ACIA serial I/O card. This also corresponds to the RC2014 Mini.

With the right options and ROM image you can also boot CP/M on it via the
emulated compact flash adapter. This corresponds to the RC2014 with the
switchable ROM, 64K RAM card, ACIA card and Compact Flash card. SIO is not
yet fully functional.

# To get actual hardware see

https://rc2014.co.uk/

# For ROM images see

https://github.com/RC2014Z80/RC2014/tree/master/ROMs/Factory

# Usage

Options:
- -a		enable 6850 ACIA
- -b		512K ROM/512K RAM board
- -c		CTC card present (not yet tested)
- -e n		Execute ROM bank n (0-7)
- -i path	Enable IDE and use this file
- -m board	Board type (z80 for default rc2014 or sc108)
- -p		Pageable ROM (needed for CP/M)
- -r path	Load the ROM image from this path
- -s		Enable the SIO/2 (no IRQ support yet)

To build a disk image

./makedisk 1 my.cf

dd if=cf-image of=my.cf bs=512 seek=2 conv=notrunc

In other words the IDE disk format has a 1K header that holds
meta-data and the virtual identify block.

Compact flash images can be found at

https://github.com/RC2014Z80/RC2014/tree/master/CPM

Remember to unzip the image before putting it on the virtual cf card.

## RBC (ex N8VEM) Mark 2 Emulator

This is a fairly basic emulator for the RBC v2 system board.

https://retrobrewcomputers.org/doku.php?id=boards:sbc:sbc_v2:start

Currently the following are emulated: 512K ROM, 512K RAM, memory mapping,
16x50 UART (minimally), 8255 PIO (PPIDE), 4UART (minimally), PropIOv2 (SD and
console), timer on serial hack but not yet ECB interrupt via serial hack.

The jumpers are not emulated. K7 is always assumed to be 1-2, K10 1-2 and
K11 1-2 giving a 32K/32K split.

# To get actual hardware see

https://retrobrewcomputers.org

# For ROM images see

https://retrobrewcomputers.org/doku.php?id=software:firmwareos:romwbw:start

# Usage

Options:
- -r path	path to ROM image
- -i path	path to IDE image (specify twice for 2 disks)
- -s path	path for PropIOv2 SD card
- -p		enable PropIOv2
- -t		enable timer hack
- -d n		set debug trace bits

The sd card image is just a raw file of the blocks at this point.
