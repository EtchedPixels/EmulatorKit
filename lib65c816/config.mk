CC      := gcc
CCFLAGS := -c -O2 -fomit-frame-pointer $(CCOPTS) -DDEBUG -Werror
LD      := gcc
LDFLAGS :=

PREFIX  := /opt/v65c816/
LIBPATH := $(PREFIX)/lib
INCPATH := $(PREFIX)/include

DELETE  := rm -rf

