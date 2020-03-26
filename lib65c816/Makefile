#
# This file should not be changed.	If you need to make alterations to the configuration, you really ought to edit config.mk instead.
#

all: configure libs

include config.mk
include nrmf.mk

ALL_CLEAN           :=
ALL_CONFIG          :=
ALL_LIBS            :=
ALL_INSTALLS        :=

CONFIG_H_TARGET     := lib65816/config.h
$(call subdir,config)

$(call subdir,lib65816)
$(call subdir,src)

.c.o:
	$(CC) $(CCFLAGS) -g3 -o $@ $<

configure: $(ALL_CONFIG)

libs: $(ALL_LIBS)

clean: $(ALL_CLEAN)

install: $(ALL_INSTALLS)

