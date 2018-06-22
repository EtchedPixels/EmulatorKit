all: mktables

mktables: mktables.c
	gcc -g -o mktables mktables.c
	
force: clean opcodes
	
opcodes: mktables opcodes.lst mktables.spec
	./mktables
	cat opcodes_impl.c | grep "static void" | sed "s/)/);/g" >opcodes_decl.h	
	
clean:
	rm -f opcodes_impl.c opcodes_decl.h opcodes_table.h mktables
