SOURCES = z180.c
FLAGS = -Wall -ansi -g -c

all: libz180.o

libz180.o: z180.c z180.h
	cd codegen && make opcodes
	$(CC) $(FLAGS) -o libz180.o $(SOURCES)

.PHONY: clean
clean:
	rm -f *.o core *~
	cd codegen && make clean

.PHONY: realclean
realclean: clean
	rm -rf doc

doc:	*.h *.c
	doxygen

