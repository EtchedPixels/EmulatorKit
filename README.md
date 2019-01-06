## RC2014 Emulator

This is an emulator for the RC2014 Z80 system.

The emulation includes
- Z80 CPU (time sync wants work)
- Standard RC2014 CPU card
- SC108 CPU card with 128K banked RAM and 32K ROM
- SC114 CPU card with 128K banked RAM and 32K ROM
- ROM and switchable ROM cards
- 6850 ACIA
- Z80 SIO/2
- 512K RAM/512K ROM card
- CF adapter
- DS1302 real time clock (time setting not supported)
- CTC at 0x88 (untested)

At this point in time the serial emulation is complete and sufficient to run
the standard ROM environment with BASIC. This represents the basic
configuration of Z80 CPU card, 32K RAM card, 8K or switchable ROM card and
ACIA serial I/O card. This also corresponds to the RC2014 Mini.

With the right options and ROM image you can also boot CP/M on it via the
emulated compact flash adapter. This corresponds to the RC2014 with the
switchable ROM, 64K RAM card, ACIA card and Compact Flash card.

In 512K mode the emulator can run the ROMWBW 512K image for the RC2014
system providing an SIO is selected. With ACIA ROMWBW fails for reasons not
yet understood.

To exit use ctrl-backslash

# To get actual hardware see

https://rc2014.co.uk/
https://smallcomputercentral.wordpress.com/

# For ROM images see

https://github.com/RC2014Z80/RC2014/tree/master/ROMs/Factory
https://retrobrewcomputers.org/doku.php?id=software:firmwareos:romwbw:start
https://smallcomputercentral.wordpress.com/projects/small-computer-monitor/

# Usage

Options:
- -a		enable 6850 ACIA
- -b		512K ROM/512K RAM board
- -c		CTC card present (not yet tested)
- -d n		Turn on debug flags
- -e n		Execute ROM bank n (0-7) (not used with -b)
- -f		Fast mode (run flat out)
- -i path	Enable IDE and use this file
- -m board	Board type (z80 for default rc2014, sc108 or sc114)
- -p		Pageable ROM (needed for CP/M)
- -r path	Load the ROM image from this path
- -s		Enable the SIO/2
- -R		Enable the DS1302 RTC
- -w		WizNET 5100 at 0x28-0x2B (works but buggy)

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

The emulator also supports the RAMF card (not yet tested), a WizNET 5100 at
0x28-0x2B in indirect mode (buggy but works for socket 0 at this point),
and a debug port. Writing to 0xFD sets the debug value so you can trace
pieces of code.

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
- -f		fast (run flat out)
- -R		RAMFS ECB module (not yet tested
- -w		WizNET 5100 at 0x28-0x2B (works but buggy)

The sd card image is just a raw file of the blocks at this point.

## Grant Searle SBC Emulator

This is an emulator for the Grant Searle 9 chip CP/M computer. This is a
Z80 machine with 128K of RAM (64K accessible), 16K of boot ROM, dual serial
and a simple 8bit IDE CF interface.

# To build yourself an actual system see

http://searle.hostei.com/grant/cpm/

# For ROM images

Get the zip file from the web page above.

makebin -s 16384 ROM.HEX >searle.rom

You can build the CP/M bootable image and put it onto an IDE image using

cat hexFiles/CPM22.HEX | makebin -s 65535 >1.rom
cat hexFiles/CBIOS64.HEX | makebin -s 65535 >2.rom
dd if=2.rom of=1.rom bs=58880 skip=1 seek=1 conv=notrunc
dd if=1.rom of=3.rom bs=1 skip=53248
dd if=/tmp/2.rom of=searle.cf bs=512 seek=2 conv=notrunc

cpmtools can then be used to manipulate it further.

# Usage

Options:
- -r path	path to ROM image (default searle.rom)
- -i path	path to IDE image (default searle.cf)
- -d n		set debug trace bits
- -f		fast (run flat out)
- -t		external 10Hz timer on DCD (not yet accurate to 10Hz)
- -b		A16 of RAM is controlled by UART RTS line

## LiNC 80 SBC

This is an emulator for the LiNC80 system with optional expanded memory. It
emulates the 64K ROM, 64K base RAM, optional banked RAM, memory mapping
control, Z80 SIO UART, Z80 CTC and onboard IDE. The PIO is only minimally
emulated.

Additionally a board mod to allow the top 32K to be banked is also supported
in emulation.

It has been tested with SCMon, CP/M and Fuzix.

# To get actual hardware see

http://linc.no

# For ROM images see

http://linc.no/products/linc80-sbc1/software-for-the-linc80/

# Usage

Options:
- -r path	path to ROM (default linc80.rom)
- -i path	path to IDE image (default linc80.ide)
- -b n		number of banks (must be a power of two, default 1 - unbanked)
- -d n		set debug trace bits
- -f		fast (run flat out)
- -x		emulate the 32K banking mod
