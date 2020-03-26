UNITS := $D/cpu		 \
		 $D/cpuevent	 \
		 $D/debugger	 \
		 $D/dispatch	 \
		 $D/opcodes1	 \
		 $D/opcodes2	 \
		 $D/opcodes3	 \
		 $D/opcodes4	 \
		 $D/opcodes5	 \
		 $D/table

SRCS	:= $(UNITS:%=%.c)
OBJS	:= $(UNITS:%=%.o)
LIBNAME := $D/lib65816.a

INSTALL := $D/install
CLEAN   := $D/clean

$(LIBNAME): $(OBJS)
	ar r $(LIBNAME) $(OBJS)

ALL_LIBS	+= $(LIBNAME)

.PHONY: $(CLEAN)

$(CLEAN):
	$(DELETE) $(OBJS)
	$(DELETE) $(LIBNAME)

ALL_CLEAN	+= $(CLEAN)

.PHONY: $(INSTALL)

$(INSTALL): $(LIBNAME)
	mkdir -p $(LIBPATH)
	cp $(LIBNAME) $(LIBPATH)

ALL_INSTALLS += $(INSTALL)
