# How to debug SPI code with the Z180 CSIO/Bus Pirate interface

## Requirements

Bus Pirate with a ROM that supports SPI and SPI binary mode (I use the
community 7.0 release).

Permission to access the serial port in question. On some Linux systems you
may need to add yourself to the group dialout or use udev to change the
permissions automatically on the Bus Pirate port. Please see your
distribution documentation.

## Set up

Connect the Bus Pirate to your USB and make sure it is being detected and
the serial driver loaded. Check you can talk to it normally.

Wire the SPI target device to the Bus Pirate power and SPI lines. The ALT
line is driven by the emulator to provide the RC2014 RESET line as
several SPI setups also need this line as well as the SPI port lines.

The SPI bus speed is set fairly low - it is possible to run the Bus Pirate at
similar to CSIO speeds but with long dangly jumper wires may not be a good
idea.

*CAUTION*: The bus pirate runs the device at 3v3, the SC126 runs it at 5v.
For most hardware this is not a problem, but do not plug none 5v capable
devices directly onto an SC126. If the device has differing 3v3 and 5v
settings then the emulation may not be quite so exact.

## Emulation

rc2014-z180 emulates the Bus Pirate as being the device on the second
chip-select line. The alt line is driven low briefly on emulator start up
as per the bus reset line. The Bus Pirate is powered up when the emulator
begins and powered down on exit. It may not always power back down on a
crash.

All data to and from the CSIO when CS2 is low is sent out to the bus pirate
and the bus pirate result returned. The CSIO sends the bits in reverse order
to standard SPI and this is correctly emulated in each direction.

## Options

The -P option is used to give a path to and enable the bus pirate interface
on the supported emulators (currently only rc2014-z180).

You can also use the debug options for tracing. On rc2014-z180 the useful
ones for this are

	4096	- trace SPI
	16	- trace instruction stream
	32	- trace Z180 I/O

and these and others can be combined (add them together) to select what
is trade to stderr. You can thus do


rc2014-z180 -P /dev/ttyUSB1 -r Z180_nat.rom -i cfimage.ide -f -d 4096 2>log

and run the system at maximum emulatable speed with logging going to the
file "log" and the bus pirate on ttyUSB1.

It's generally easiest to have any OS and storage on IDE in the emulation
when needed as you can then avoid the log getting full of SD card tracing.

In the emulator I/O port 0xFD sets the low byte of the debug trace and
I/O port 0xFE sets the high byte. You can thus also turn tracing on and off
in the code being debugged.

