CCFLAGS		+= -I$D/..
INCDIR		:= $D
INSTALL		:= $D/install
CLEAN		:= $D/clean

INCLUDES	:= $(CONFIG_H_TARGET) \
			   $D/cpu.h \
			   $D/cpuevent.h

.PHONY: $(INSTALL)
$(INSTALL): $(INCLUDES)
	mkdir -p $(INCPATH)
	cp -r $(INCDIR) $(INCPATH)

ALL_INSTALLS += $(INSTALL)

.PHONY: $(CLEAN)

$(CLEAN):
	$(DELETE) $(CONFIG_H_TARGET)

ALL_CLEAN	+= $(CLEAN)

