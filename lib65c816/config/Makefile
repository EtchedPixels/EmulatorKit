SIZEOF		:= $D/sizeof
SIZEOF_C	:= $(SIZEOF:%=%.c)
SIZEOF_O	:= $(SIZEOF:%=%.o)

CONFIG_H	:= $D/config.h
CLEAN		:= $D/clean

$(SIZEOF_O): $(SIZEOF_C)
	$(CC) $(CCFLAGS) -o $@ $<

$(SIZEOF): $(SIZEOF_O)
	$(LD) $(LDFLAGS) -o $@ $<

.PHONY: $(CONFIG_H)

# CONFIG_H_TARGET defined in master Makefile.
$(CONFIG_H): $(SIZEOF)
	$(SIZEOF) >$(CONFIG_H_TARGET)

ALL_CONFIG	+= $(CONFIG_H)

.PHONY: $CLEAN

$CLEAN:
	$(DELETE) $(SIZEOF_O)
	$(DELETE) $(SIZEOF)

ALL_CLEAN	+= $CLEAN

