## z280emu
A portable full system emulator of Z280 based boards

## Motivation
There was no working Z280 emulator that I was aware of so I decided to write one.  

## Build status
**build passing**
There is no CI yet.  
Supported targets are MinGW and Linux. gcc-win64-mingw64 and gcc-linux-amd64 are actively developed and tested. gcc-win32-mingw32, gcc-linux-i686 and gcc-linux-armhf should work without any issues, but are not tested.  
Currently used compiler versions:  
x86_64-w64-mingw32 gcc version 10.3.0 (Rev2, Built by MSYS2 project)	- used for primary development  
x86_64-linux-gnu gcc version 6.3.0 20170516 (Debian 6.3.0-18+deb9u1)  


## Code style
C89  

## Screenshots

## Tech/framework used
gcc  
GNU make  

## Features
### General  
**Z280 core - beta version**  
-Z280 core and instruction set  
-IM 3 and traps  
-MMU  
-4 channel DMA  
-3 counter/timers (timer mode only)  
-Z280 UART  
**standalone PIO IDE**  
-16-bit CompactFlash (big or little endian, see How to use)  
**DS1202/1302 RTC with NVRAM file storage**  
**Z280 dissasembler and simple yet powerful tracer**  
Serial ports are implemented as **byte-oriented streams over a raw TCP socket** (works with Putty, nc, ckermit etc.)  
**QuadSer OX16C954 serial board** (so far except fifo trigger levels, flow control, 9bit mode, and some other advanced features)  
**Modular structure** allowing adding new boards and peripherals with reasonable effort  

### Boards/ROM/OS support  
**Z280RC (stable):**  
-original plasmo's ZZMon (v0.99 6/9/18) - OK  
-SC Monitor 1.0 - OK  
-plasmo's CP/M 2.2 image - OK  
-plasmo's CP/M 3 image - OK  
-Hector Peraza's ZZMon2 - OK  
-Hector Peraza's RSX280 image - OK  
-UZI280 - OK

### TODO (unimplemented)  
UART bootstrap (currently only RAM/memory-mapped boot is supported)  
better page/EA display in tracer  
cleanup Z-BUS/Z80 bus modes (Z-BUS instr fetches should be WORD, ugh)  
cleanup 8-bit/16-bit internal IO (DMA etc.)  
cache memory (so far unimplemented on purpose, likely not needed and slows emulation down)  
fixed memory  
EPU (only trap is implemented)  
counter inputs (needed?)  
DMA linking, DMA UART, longword transactions  
Emulation of Tilmann's CPU280 board  
Emulator console ala SimH  
Interactive debugger (display/edit/disasm/asm/single/breakpoint)  
Clock-accurate emulation (currently all timings are Z180-like)  

## Installation
```
git clone
make
```

You get the z280rc and makedisk binaries.  

## API Reference

## Tests

## How to use?
**Z280RC**

cfmonldr and ZZMon are needed.  

1.  
original plasmo's version (CP/M 2.2 and 3):
```
# convert to bin
wget -O zzmon_099.zip "https://www.retrobrewcomputers.org/lib/exe/fetch.php?media=builderpages:plasmo:z280rc:zzmon_099.zip"
unzip zzmon_099.zip	ZZMon.hex
head -n-2 ZZMon.hex >tmp.hex
objcopy -I ihex -O binary tmp.hex zzmon.bin

# split it
cp zzmon.bin cfmonldr.bin
truncate -s256 cfmonldr.bin
tail -c+513 zzmon.bin >ZZ.bin
truncate -s2560 ZZ.bin

# create disk image
makedisk 1 cf00.dsk
truncate -s125k cf00.dsk   # sector 0xF8
cat ZZ.bin >>cf00.dsk
truncate -s32769K cf00.dsk
```

2.  
If you want to boot RSX280 or UZI280, get the modified version (ZZMon2) by Hector Peraza:
```
wget https://github.com/hperaza/ZZmon2/raw/master/bin/zzmon.bin
# split it and create disk image (as above)

# append RSX280 partition to disk image
wget https://github.com/hperaza/RSX280/raw/master/DiskImages/cf-partition-z280rc-32M-210522.img.gz # or newer
zcat cf-partition-z280rc-*.img.gz >>cf00.dsk
```

3.  
Note that the original Z280RC board has IDE wired as little endian whereas Z-BUS is big endian.
This requires that either the CF image is byte-swapped, or the words are swapped in software e.g. after 
executing an INIRW/OTIRW instruction.  

To get the behavior of the original Z280RC board, define IDELE (IDE little endian) in z280rc.c and recompile.  

To run IDE in big endian mode, keep it undefined. This allows accessing disk images directly without swapping
and is equivalent to twisting the IDE cable, i.e. the l.o. IDE byte to AD8-15 and h.o. IDE byte to AD0-7.
This is the default for the emulator.  

---
Run without trace:  
```
z280rc
```


The emulator opens with:
```
z280emu v1.0 Z280RC
Serial port 0 listening on 10280
```
waits until the socket is connected, and then starts execution.


---
Run with trace:  
```
z280rc -d >mytrace.log
```
Then grep for it.


Run with trace from 10000000-th instruction:  
```
z280rc -d=10000000 >mytrace.log
```
The above can be used to bisect into the routine you're debugging.  

---
Enabling the QuadSer card:  
```
z280rc -quadser	  # enable port0 only

z280emu v1.0 Z280RC
Serial port 0 listening on 10280 # UART
Serial port 1 listening on 10281 # QuadSer port 1

z280rc -quadser=1 # same as previous
z280rc -quadser=2 # enable port1,2
z280rc -quadser=4 # enable all 4 ports
```
Note that all sockets need to be connected for the emulation to start.

---
Exiting the emulator  
CTRL+C/SIGINT is completely disabled to allow ^C passthrough to the emulated system, esp. in case socket console isn't used.  
The emulator can be exited by CTRL+Break (or closing the window) on MinGW, and CTRL+\ (SIGQUIT) on POSIX systems.  

---
Socket console  
If using Putty, set Connection type to "Raw".  
For nc, use  
```
stty raw -echo; nc -C 127.0.0.1 10280
```

## Contribute
If you'd like to commit, please contact me (Michal Tomek) at <mtdev79b@gmail.com>

## Credits
Hector Peraza, for the RSX280 source and prebuilt images, without which writing and testing the emulator would be extremely
hard if not impossible. Hector also provided very helpful advice about the Z280 and internal peripherals.  
Developers of open source code included in this project, most notably:  
-Portable Z180 emulator and Portable Z8x180 disassembler Copyright by Juergen Buchmueller  
-Z80-SCC Serial Communications Controller emulation Copyright by Joakim Larsson Edstrom  
-IDE Emulation Layer Copyright by Alan Cox, 2015  
-DS1202/1302 RTC emulation Copyright by Marco van den Heuvel  
-8250 UART interface and emulation Copyright by smf, Carl  

If you aren't listed and feel you should be, please write to me.

## Other

## License
**GPL**  

z280emu is Copyright Michal Tomek 2018-2021, and others  

```
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
```

See the file COPYING for details.


This program includes software code developed by several people, subject
to the GNU General Public License. Some files are also under the 
BSD 3-clause license. All copyrights are acknowledged to their respective
holders. See individual files for details. 

```
                            NO WARRANTY

BECAUSE THE PROGRAM IS LICENSED FREE OF CHARGE, THERE IS NO WARRANTY
FOR THE PROGRAM, TO THE EXTENT PERMITTED BY APPLICABLE LAW.  EXCEPT WHEN
OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES
PROVIDE THE PROGRAM "AS IS" WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED
OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE ENTIRE RISK AS
TO THE QUALITY AND PERFORMANCE OF THE PROGRAM IS WITH YOU.  SHOULD THE
PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF ALL NECESSARY SERVICING,
REPAIR OR CORRECTION.

IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING
WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MAY MODIFY AND/OR
REDISTRIBUTE THE PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR DAMAGES,
INCLUDING ANY GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING
OUT OF THE USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED
TO LOSS OF DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY
YOU OR THIRD PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY OTHER
PROGRAMS), EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE
POSSIBILITY OF SUCH DAMAGES.
```
