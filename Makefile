# To change build options use either command line or put the variables
# to Config.mk file (optional).
#
# Define V=1 for more verbose build.
#
# Define WERROR if most warnings should be treated as errors.
#
# Define NO_DEPS to disable automatic dependency calculation.
# Dependency calculation is enabled by default if CC supports
# the -MMD -MP -MF options.

CC = gcc
LD = $(CC)
CFLAGS = -g -O2 -Wall
LDFLAGS =
INSTALL = install
DESTDIR =
prefix = /usr/local
bindir = $(prefix)/bin
datadir = $(prefix)/share

# 0: Disable debugging.
# 1: Enable BUG_ON() and light-weight sanity checks.
# 2: Enable logging to /tmp/editor-$UID.log.
# 3: Enable expensive sanity checks.
DEBUG = 1

# enabled if CC supports them
WARNINGS = \
	-Wdeclaration-after-statement \
	-Wformat-security \
	-Wmissing-prototypes \
	-Wold-style-definition \
	-Wredundant-decls \
	-Wwrite-strings \
	-Wundef \
	-Wshadow

# End of configuration

all: editor

OBJECTS	:= 			\
	alias.o			\
	bind.o			\
	block.o			\
	buffer-iter.o		\
	buffer.o		\
	change.o		\
	cmdline.o		\
	color.o			\
	commands.o		\
	completion.o		\
	config.o		\
	ctags.o			\
	ctype.o			\
	edit.o			\
	editor.o		\
	file-history.o		\
	file-option.o		\
	filetype.o		\
	gbuf.o			\
	history.o		\
	hl.o			\
	indent.o		\
	iter.o			\
	lock.o			\
	move.o			\
	obuf.o			\
	options.o		\
	parse-args.o		\
	parse-command.o		\
	ptr-array.o		\
	regexp.o		\
	run.o			\
	screen.o		\
	search.o		\
	spawn.o			\
	format-status.o		\
	state.o			\
	tag.o			\
	term.o			\
	termcap.o		\
	terminfo.o		\
	uchar.o			\
	util.o			\
	wbuf.o			\
	window.o		\
	xmalloc.o		\
	# end

binding	:=	 		\
	binding/default		\
	# end

color	:=			\
	color/light		\
	# end

compiler :=			\
	compiler/gcc		\
	compiler/go		\
	# end

config	:=			\
	filetype		\
	rc			\
	# end

syntax	:=			\
	syntax/c		\
	syntax/config		\
	syntax/css		\
	syntax/diff		\
	syntax/editor		\
	syntax/gitcommit	\
	syntax/gitrebase	\
	syntax/go		\
	syntax/html		\
	syntax/make		\
	syntax/python		\
	syntax/sh		\
	syntax/sql		\
	# end

binding	:= $(addprefix share/,$(binding))
color	:= $(addprefix share/,$(color))
compiler:= $(addprefix share/,$(compiler))
config	:= $(addprefix share/,$(config))
syntax	:= $(addprefix share/,$(syntax))

-include Config.mk
include Makefile.lib

# clang does not like container_of()
ifneq ($(CC),clang)
WARNINGS += -Wcast-align
endif

BASIC_CFLAGS += $(call cc-option,$(WARNINGS))

ifdef WERROR
BASIC_CFLAGS += $(call cc-option,-Werror -Wno-error=shadow -Wno-error=unused-variable)
endif

BASIC_CFLAGS += -DDATADIR=\"$(datadir)\"
BASIC_CFLAGS += -DDEBUG=$(DEBUG)

editor: $(OBJECTS)
	$(call cmd,ld,)

install: all
	$(INSTALL) -d -m755 $(DESTDIR)$(bindir)
	$(INSTALL) -d -m755 $(DESTDIR)$(datadir)/editor/binding
	$(INSTALL) -d -m755 $(DESTDIR)$(datadir)/editor/color
	$(INSTALL) -d -m755 $(DESTDIR)$(datadir)/editor/compiler
	$(INSTALL) -d -m755 $(DESTDIR)$(datadir)/editor/syntax
	$(INSTALL) -m755 editor      $(DESTDIR)$(bindir)
	$(INSTALL) -m644 $(config)   $(DESTDIR)$(datadir)/editor
	$(INSTALL) -m644 $(binding)  $(DESTDIR)$(datadir)/editor/binding
	$(INSTALL) -m644 $(color)    $(DESTDIR)$(datadir)/editor/color
	$(INSTALL) -m644 $(compiler) $(DESTDIR)$(datadir)/editor/compiler
	$(INSTALL) -m644 $(syntax)   $(DESTDIR)$(datadir)/editor/syntax

clean:
	rm -f *.o editor

distclean: clean
	rm -f tags

tags:
	ctags *.[ch]

REV     = $(shell git rev-parse --short HEAD)
RELEASE	= editor-$(REV)
TARBALL	= $(RELEASE).tar.gz

dist:
	git archive --format=tar --prefix=$(RELEASE)/ $(REV) | gzip -c -9 > $(TARBALL)

.PHONY: all clean distclean install tags dist
