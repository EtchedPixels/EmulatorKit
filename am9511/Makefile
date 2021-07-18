OBJS = am9511.o floatcnv.o ova.o

CFLAGS = -I. -g

libam9511.a: $(OBJS)
	ar rc libam9511.a $^

clean:
	rm -f $(OBJS) *~ libam9511.a

am9511.c: am9511.h floatcnv.h ova.h types.h
floatcnv.c: floatcnv.h types.h
ova.c: ova.h types.h
