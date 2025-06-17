# SCELBI 8008

## System
This is a model of the SCELBI, a pre-Altair i8008 based system. It models
the basic machine and some peripherals. The original machine just came with
RAM and some 8bit I/O ports.

The real machine has a bunch of switches to toggle in stuff and get going.
Rather than simulating the front panel you are dropped into a simple command
line monitor.

## Addressing

Most things in the i8008 world are written in octal format and this emulator
reflects that usage. For one instructions are much easier to toggle in when
using octal.

The platform itself has a 14bit addressing scheme using byte wide registers.
Thus a second scheme you will met is the use of octal-octal notation
indicating a pair of bytes in octal format. Thus "017-000" is in fact 7400.
This particular insanity is not currently supported by the emulator.

## ROMs

There aren't any. The emulator does support loading 2K of ROM at the top of
memory, although a 256 byte ROM was more common but the base system didn't
have any ROM. You toggled in a bootstrap and went from there.

## Devices

The "-s" option enables the Scopewriter - a popular electronics project from
August 1974 that gives you a single 32 character ASCII display on an
oscilloscope. In 1974 that was a serious step up from lights and switches
and a lot cheaper than an ASR-33 teletype.

The "-v" option enables the Digital Video Group interface. Giving you 8
lines of 32 characters it was pretty serious stuff.

The default emulation also knows about the usual way of connecting an ASCII
keyboard providing -s or -v is specified. It also understands the standard 20mA
current loop teletype arrangement.

The teletype interface uses a few tricks. All scelbi (in fact pretty much
all bitbang i8008 serial it seems) used the same exact code with delay
loops. Instead of emulating the timing the code uses the instruction pattern
to decode and encode the serial. This conveniently means things like the
baud rate just don't matter.

The non-SDL version only understands the teletype.

## Devices Not Emulated

The tape driver. This is not a simple bit banging interface but worked in
nibbles and was slightly smart.

The oscilloscope based display.

Attaching a paper tape reader on some other port for bit banging.

The stack hack, a mod that fixed the lack of a data stack on the 8008 by
attaching one to an IN and OUT port. This may well get added.

## Usage

Assuming you don't want to toggle programs in then you can load them using
the -l and -b option. Use -l to specify the binary path and -b the octal
load address. Remember to convert any weird page-offset notation into octal
first.

So for example to load and run MCMON with a scopewriter

scelbi_sdl2 -s -l /tmp/cmon_w_keyboard_scope.bin -b 7400

At the prompt

x 7400

Or to run startrek

scelbi -l trek.bin
x40

It runs at 110 baud so isn't the fastest trek in the universe.

## Other options

The -m option allows you to specify the memory size - up to 16Kbytes. The -r
option will load a ROM into the top 2K space. Using -f will run at emulator
speed not the true 500Khz joy of the original.

## Software

The best location is probably https://www.willegal.net/ which
also has a collection of material and example software. Note that a lot of
it gives load and execution addresses in page-offset octal format so
remember to convert!
