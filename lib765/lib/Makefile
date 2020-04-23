OBJS = 765drive.o 765dsk.o 765fdc.o 765ldsk.o error.o

CFLAGS = -O2 -I../include

lib765.a: $(OBJS)
	ar rc lib765.a $^

clean:
	rm $(OBJS) *~ lib765.a

765drive.o: 765drive.c 765i.h ../include/config.h ../include/765.h
765dsk.o: 765dsk.c 765i.h ../include/config.h ../include/765.h
765fdc.o: 765fdc.c 765i.h ../include/config.h ../include/765.h
765ldsk.o: 765ldsk.c ../include/config.h
error.o: error.c ../include/765.h
