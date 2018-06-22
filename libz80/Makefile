SOURCES = z80.c
FLAGS = -Wall -ansi -g -c 

force: clean all

all: libz80.o

libz80.o: z80.h $(OBJS)
	cd codegen && make opcodes
	gcc $(FLAGS) -o libz80.o $(SOURCES)

clean:
	rm -f *.o core
	cd codegen && make clean

realclean: clean
	rm -rf doc

doc:	*.h *.c
	doxygen

