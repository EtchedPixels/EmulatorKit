# Settings for standard RC2014 Platforms

## RC2014 Classic or Classic II

rc2014 -a -r classic.rom -e bank

where bank is 0-7 matching the jumpers on the card


## RC2014 with CP/M

rc2014 -a -r cpm.rom -e bank -i cfdisk.ide

Where bank is the jumpers on the card and and cfdisk.ide is a CP/M disk
image in IDE emulator format.


## RC2014 with 512K/512K ROM and SIO

rc2014 -s -r RCZ80_std.rom -b

This will let you play with ROMWBW. The -s option selects the SIO card,
-a the ACIA.

To add CF support

rc2014 -s -r RCZ80_std.rom -b -i cfdisk.ide

A fully loaded system with graphics, PS/2 bit bang card, CTC, RTC and AMD FPU

rc2014_sdl2 -s -r RCZ80_std.rom -b -i cfdisk.ide -P -T -R -9 -c

You can also swap -P with -z to get the ZX Keyboard instead, and you can add
-S sdcard.img to add emulation of the Gluino and an Arduino SD card shield

You can add floppy disk support (Etched Pixels card style) with the option
-F and specify the .DSK images to use with -A diska.dsk and -B diskb.dsk.

# Settings for Small Computer Central platforms

## SC108

rc2014 -a -r sc108.rom -i cfdisk.ide

The usual options for adding CF cards, CTC etc apply as above.

# SC111

Native mode

rc2014-z180 -r RCZ180_nat.rom -i idedisk.cf

For banked 512/512K mode use the options given for the RC2014 with 512K/512K
ROM but the command rc2014-z180 and the RCZ180_ext.rom image.

## SC114

rc2014 -a -r sc114.rom -i cfdisk.ide

## SC126

rc2014-z180 -r RCZ180_nat.rom -S sdcard.img -R

## SC130

rc2014-z180 -r RCZ180_nat.rom -S sdcard.img

## SC131

rc2014-z180 -r RCZ180_nat.rom -S sdcard.img

# Settings for Easy Z80

## EasyZ80

rc2014 -m easyz80 -r EZZ80_std.rom -i cfdisk.ide

