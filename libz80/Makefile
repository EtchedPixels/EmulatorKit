SOURCES = z80.c
FLAGS = -Wall -ansi -g -c

all: libz80.o

libz80.o: z80.c z80.h
	cd codegen && make opcodes
	$(CC) $(FLAGS) -o libz80.o $(SOURCES)

.PHONY: clean
clean:
	rm -f *.o core
	cd codegen && make clean

.PHONY: realclean
realclean: clean
	rm -rf doc

doc:	*.h *.c
	doxygen

