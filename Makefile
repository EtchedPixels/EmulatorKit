
CFLAGS = -Wall -pedantic

all:	rc2014 rbcv2 searle linc80 makedisk

rc2014:	rc2014.o ide.o w5100.o
	(cd libz80; make)
	cc -g3 rc2014.o ide.o w5100.o libz80/libz80.o -o rc2014

rbcv2:	rbcv2.o ide.o w5100.o
	(cd libz80; make)
	cc -g3 rbcv2.o ide.o w5100.o libz80/libz80.o -o rbcv2

searle:	searle.o ide.o
	(cd libz80; make)
	cc -g3 searle.o ide.o libz80/libz80.o -o searle

linc80:	linc80.o ide.o
	(cd libz80; make)
	cc -g3 linc80.o ide.o libz80/libz80.o -o linc80


makedisk: makedisk.o ide.o
	cc -O2 -o makedisk makedisk.o ide.o

clean:
	(cd libz80; make clean)
	rm -f *.o *~ rc2014 rbcv2
