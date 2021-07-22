# Emulator Kit

This is a kit of emulators primarily focussed on the RC2014 environment and
some of the Retrobrew (formerly N8VEM) systems. The emulators are focussed
on software development and testing rather than cycle accurate simulation.
They are designed to be easy to hack on when working on a problem, and
simple to understand.

Device emulation varies in detail depending upon need. The device models
should be easy to extend as required and currently reflect the focus on
RC2014 and Fuzix.

## Processors Supported

- 1802 - initial model of 1802/4/5 processors
- 6303
- 6502 - NMOS part with no illegal models (Mike Chambers)
- 65C816 (uses a slightly modified lib65816)
- 6800
- 68000/10/EC020/020 (uses Musashi)
- 6803
- 6809 (vecX)
- 68HC11 - I/O model is very basic
- 8008
- 8085 - Extended from Mike Chambers 8080 emulation
- 8086 (from Hampa Hug's pcem)
- Z8 - initial model
- Z80 (libz80)
- Z180 - dervied from libz80

## RC2014 / BP80 Processor Cards Emulated
- Z80
- Z180 (only in linear memory mode at the moment)
- 1802
- 6303/6803
- 6502
- 65C816 (with banked RAM)
- 68008
- 6808
- 6809
- 68HC11
- 80C85
- 80C188
- Z8

## RC2014 / BP80 Integrated Systems Emulated
- SC108
- SC114
- SC126

## Systems with RC2014 bus connector
- Easy Z80
- Z80MB64
- Z80SBC64

## RC2014 Card Emulation (Official)
- 512K ROM 512K RAM
- Compact Flash
- Dual serial SIO/2
- IDE (82C55 PPIDE)
- Pageable ROM
- Real Time Clock
- Switchable ROM
- 64K RAM

## RC2014 Card Emulation (Other)
- 16x50
- CTC
- Floppy Disk
- Gluino/Z80 PIO with SD card
- PS/2 Keyboard
- TMS9918A (no sprites yet)
- W5100 (actual card not yet available)
- Z80 SIO
- ZXKey

# Retrobrew/N8VEM Systems Emulated
- SBC v2
- Mark IV
- N8

## Other Systems Emulated

- Amstrad NC100
- Amstrad NC150
- Amstrad NC200
- Flex (minimal Flex 6800 system)
- Grant Searle's 9 chip Z80
- Linc-80
- Micro80
- Retrobrew N8
- Rewtrobrew SBC v2
- Retrobrew Mark-IV
- SBC 2G
- Simple80
- Scelbi
- Small Z80
- Tiny68K
- TinyZ80
- Tom's SBC
- Z80Master
- Z80 Membership Card
- Z80 MBC2
- ZRCC

# Hardware And ROM Images

## RC2014

### Community

Google Groups: rc2014-z80. Note that the list owner's policy is that this
list should be for discussing Z80 based RC2014 systems only.

Google Groups: retro-comp. RC2014 and BP80 based systems and other stuff.
Created as a place to discuss things not suitable for rc2014-z80.

### Boards

- RFC2795 Ltd: https://www.tindie.com/stores/semachthemonkey/
- Small Computer Central: https://www.tindie.com/stores/tindiescx/
- Etched Pixels: https://hackaday.io/projects/hacker/425483
- TMS9918A card: https://www.tindie.com/stores/mfkamprath/

### ROM images:
- Z80 Base ROM images - https://github.com/RC2014Z80/RC2014/tree/master/ROMs/Factory
- Z80/180 Small Computer Monitor - https://smallcomputercentral.wordpress.com/small-computer-monitor/
- Z80/180 ROMWBW - https://github.com/wwarthen/RomWBW/releases
- Etched Pixels non Z80/180 cards - https://github.com/EtchedPixels/RC2014-ROM

### CP/M Disk Images (for non ROMWBW systems)

https://github.com/RC2014Z80/RC2014/tree/master/CPM

## Retrobrew (formerly N8VEM)

ROM images, designs and board stocks etc and all to be found at
https://retrobrewcomputers.org along with a forum. Most systems use the
ROMWBW (see RC2014).

This site also hosts many of the other designs like the Tiny68K.

## Amstrad NC series

The ROM images for these machines are not redistributable, you will need an
actual system to hand. nc100em has a small free ROM image if you just want
to run ZCN, and it may work for Fuzix too.

## Grant Searle SBC

Grant's website http://searle.wales/ contains full instructions on the
design and building of this system. Also available are the needed BIOS,
monitor and disk images. If you follow this design I strongly recommend you
build it entirely using the equivalent CMOS parts (74HCTxx) and CMOS Zilog
devices, as this makes the CF much more reliable and you can run far faster.

There are also designs for a lot of other systems that are not emulated.

## SBC2G

This lives at retrobrewcomputers.org. The ROM images are available as HEX
files. To generate the rom do

	makebin 10-HEXFILES/SMON35.HEX > sbc2g.rom

## SmallZ80

Available from http://stack180.com/. ROM images are available on the site.

## Tom's SBC

A PCB based design closely related to Grant Searle's. Some discussion on the
retro-comp group. This platform can use the Grant Searle ROM and CF images.
Fuzix runs directly from ROM.

https://easyeda.com/peabody1929/CPM_Z80_Board_REV_B_copy-76313012f79945d3b8b9d3047368abf7

## Z80 MBC2

https://hackaday.io/project/159973-z80-mbc2-a-4-ics-homebrew-z80-computer

including the files needed for the emulator. Note that the emulator emulates
the Atmega API so no Atmega firmware is used or needed.

## Z80 Membership Card

This kit is available from http://www.sunrise-ev.com/z80.htm. The emulation
is of the full build, and does not emulate the LED display or keypad.

All needed emulation material is available from the site as well.

