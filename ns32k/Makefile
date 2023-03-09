all:	32016.o disassemble.o

32016.o: 32016.c 32016.h defs.h Trap.h Decode.h
	cc -g3 -c 32016.c

disassemble.o: disassemble.c disassemble.h defs.h Trap.h Decode.h
	cc -g3 -c disassemble.c


clean:
	rm -f *.o *~
