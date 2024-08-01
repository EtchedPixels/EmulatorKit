CCFLAGS := -c -O2 -fomit-frame-pointer $(CCOPTS) -DDEBUG -Werror
LD      := $(CC)
LDFLAGS :=

PREFIX  := /opt/v65c816/
LIBPATH := $(PREFIX)/lib
INCPATH := $(PREFIX)/include

DELETE  := rm -rf

