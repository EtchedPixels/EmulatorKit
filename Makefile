
CFLAGS = -Wall -pedantic -g3 -Werror

all:	rc2014 rc2014-1802 rc2014-6303 rc2014-6502 rc2014-65c816-mini \
	rc2014-6800 rc2014-68008 rc2014-68hc11 rc2014-80c188 rc2014-8085 \
	rc2014-z8 rbcv2 searle linc80 makedisk mbc2 smallz80 sbc2g z80mc \
	simple80 flexbox tiny68k

libz80/libz80.o:
	$(MAKE) --directory libz80

lib765/lib/lib765.a:
	$(MAKE) --directory lib765/lib

rc2014:	rc2014.o acia.o ide.o ppide.o rtc_bitbang.o sdcard.o w5100.o z80dma.o z80copro.o libz80/libz80.o lib765/lib/lib765.a
	cc -g3 rc2014.o acia.o ide.o ppide.o rtc_bitbang.o sdcard.o w5100.o z80dma.o z80copro.o libz80/libz80.o lib765/lib/lib765.a -o rc2014

rbcv2:	rbcv2.o ide.o w5100.o libz80/libz80.o
	cc -g3 rbcv2.o ide.o w5100.o libz80/libz80.o -o rbcv2

searle:	searle.o ide.o libz80/libz80.o
	cc -g3 searle.o ide.o libz80/libz80.o -o searle

linc80:	linc80.o ide.o sdcard.o libz80/libz80.o
	cc -g3 linc80.o ide.o sdcard.o libz80/libz80.o -o linc80

mbc2:	mbc2.o ide.o libz80/libz80.o
	cc -g3 mbc2.o libz80/libz80.o -o mbc2

rc2014-1802: rc2014-1802.o 1802.o ide.o acia.o w5100.o ppide.o rtc_bitbang.o
	cc -g3 rc2014-1802.o acia.o ide.o ppide.o rtc_bitbang.o w5100.o 1802.o -o rc2014-1802

rc2014-6303: rc2014-6303.o 6800.o ide.o w5100.o ppide.o rtc_bitbang.o
	cc -g3 rc2014-6303.o ide.o ppide.o rtc_bitbang.o w5100.o 6800.o -o rc2014-6303

rc2014-6502: rc2014-6502.o 6502.o 6502dis.o ide.o w5100.o
	cc -g3 rc2014-6502.o ide.o w5100.o 6502.o 6502dis.o -o rc2014-6502

rc2014-65c816-mini: rc2014-65c816-mini.o ide.o w5100.o lib65c816/src/lib65816.a
	cc -g3 rc2014-65c816-mini.o ide.o w5100.o lib65c816/src/lib65816.a -o rc2014-65c816-mini

lib65c816/src/lib65816.a:
	$(MAKE) --directory lib65c816 -j 1

lib65816/config.h: lib65c816/src/lib65816.a

rc2014-65c816-mini.o: rc2014-65c816-mini.c lib65816/config.h
	$(CC) $(CFLAGS) -Ilib65c816 -c rc2014-65c816-mini.c

rc2014-6800: rc2014-6800.o 6800.o ide.o acia.o
	cc -g3 rc2014-6800.o ide.o acia.o 6800.o -o rc2014-6800

rc2014-6809: rc2014-6809.o e6809.o ide.o ppide.o w5100.o rtc_bitbang.o
	cc -g3 rc2014-6809.o ide.o ppide.o w5100.o rtc_bitbang.o e6809.o -o rc2014-6809

rc2014-68hc11: rc2014-68hc11.o 68hc11.o ide.o w5100.o ppide.o rtc_bitbang.o sdcard.o
	cc -g3 rc2014-68hc11.o ide.o ppide.o rtc_bitbang.o sdcard.o w5100.o 68hc11.o -o rc2014-68hc11

rc2014-68008: rc2014-68008.o ide.o w5100.o m68k/lib68k.a
	cc -g3 rc2014-68008.o ide.o w5100.o ppide.o m68k/lib68k.a -o rc2014-68008

m68k/lib68k.a:
	$(MAKE) --directory m68k

rc2014-68008.o: rc2014-68008.c m68k/lib68k.a
	$(CC) $(CFLAGS) -Im68k -c rc2014-68008.c

rc2014-8085: rc2014-8085.o intel_8085_emulator.o ide.o acia.o w5100.o ppide.o rtc_bitbang.o
	cc -g3 rc2014-8085.o acia.o ide.o ppide.o rtc_bitbang.o w5100.o intel_8085_emulator.o -o rc2014-8085

rc2014-80c188: rc2014-80c188.o ide.o w5100.o ppide.o rtc_bitbang.o
	$(MAKE) --directory 80x86 && \
	cc -g3 rc2014-80c188.o ide.o ppide.o rtc_bitbang.o w5100.o 80x86/*.o -o rc2014-80c188

rc2014-z280: rc2014-z280.o ide.o libz280/libz80.o
	cc -g3 rc2014-z280.o ide.o libz280/libz80.o -o rc2014-z280

rc2014-z8: rc2014-z8.o z8.o ide.o acia.o w5100.o ppide.o rtc_bitbang.o
	cc -g3 rc2014-z8.o acia.o ide.o ppide.o rtc_bitbang.o w5100.o z8.o -o rc2014-z8

smallz80: smallz80.o ide.o libz80/libz80.o
	cc -g3 smallz80.o ide.o libz80/libz80.o -o smallz80

sbc2g:	sbc2g.o ide.o libz80/libz80.o
	cc -g3 sbc2g.o ide.o libz80/libz80.o -o sbc2g

tiny68k: tiny68k.o ide.o duart.o m68k/lib68k.a
	cc -g3 tiny68k.o ide.o duart.o m68k/lib68k.a -o tiny68k

tiny68k.o: tiny68k.c m68k/lib68k.a
	$(CC) $(CFLAGS) -Im68k -c tiny68k.c

z80mc:	z80mc.o sdcard.o libz80/libz80.o
	cc -g3 z80mc.o sdcard.o libz80/libz80.o -o z80mc

flexbox: flexbox.o 6800.o acia.o ide.o
	cc -g3 flexbox.o 6800.o acia.o ide.o -o flexbox

simple80: simple80.o ide.o libz80/libz80.o
	cc -g3 simple80.o ide.o libz80/libz80.o -o simple80

zsc: zsc.o ide.o libz80/libz80.o
	cc -g3 zsc.o ide.o libz80/libz80.o -o zsc

makedisk: makedisk.o ide.o
	cc -O2 -o makedisk makedisk.o ide.o

clean:
	$(MAKE) --directory libz80 clean && \
	$(MAKE) --directory lib765/lib clean && \
	$(MAKE) --directory 80x86 clean && \
	$(MAKE) --directory lib65c816 clean && \
	$(MAKE) --directory m68k clean && \
	rm -f *.o *~ rc2014 rbcv2

SRCS := $(subst ./,,$(shell find . -name '*.c'))
DEPDIR := .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c

%.o : %.c
%.o : %.c $(DEPDIR)/%.d | $(DEPDIR)
	$(COMPILE.c) $(OUTPUT_OPTION) $<

$(DEPDIR): ; @mkdir -p $@

DEPFILES := $(SRCS:%.c=$(DEPDIR)/%.d)
$(DEPFILES):

lib765/lib/lib765.a: lib765/lib/765drive.c lib765/lib/765dsk.c \
		     lib765/lib/765fdc.c lib765/lib/765i.h \
		     lib765/lib/765ldsk.c lib765/lib/error.c
libz80/libz80.o: libz80/z80.c libz80/z80.h
libz280/libz80.o: libz280/z80.c libz280/z80.h
cpu.c: lib65816/config.h

include $(wildcard $(DEPFILES))
