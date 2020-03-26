# Non-Recursive Make Framework
# Requires GNU Make version 3.80 or higher.

# Include this from your top-level Makefile.

# $D is the current directory.

# $(call subdir,foo) includes foo/Makefile, managing $D appropriately.

# YOU CANNOT USE $D IN RULE COMMANDS!  That text is not evaluated until
# the commands are run, and then you get the "empty" value of $D and not
# the correct value for that directory.  So use $D for the targets and
# prerequisites and use the automatic variables ($@ $< $^ etc.) for the
# commands.

# Likewise, be sure to use := (simply-expanded) variables when $D is part
# of the value, or (again) you'll get the "empty" value of $D.

ifndef D
D := .
endif

define include-subdir
SP := $(SP).x
D$(SP) := $D
D := $D/$1
include $D/Makefile
D := $(D$(SP))
SP := $(basename $(SP))
endef

subdir = $(eval $(value include-subdir))

