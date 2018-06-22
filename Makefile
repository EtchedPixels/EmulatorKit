
all:	rc2014

rc2014:	rc2014.c
	(cd libz80; make)
	cc -g3 rc2014.c libz80/libz80.o -o rc2014

clean:
	(cd libz80; make clean)
	rm -f *.o *~ rc2014
