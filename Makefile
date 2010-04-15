# To change build options use either command line or put the variables
# to Config.mk file.
#
# Define V=1 for more verbose build.
#
# Define NO_WERROR if most warnings should not be treated as errors.
#
# Define NO_DEPS to disable automatic dependency calculation.
# Dependency calculation is enabled by default if CC supports
# the -MMD -MP -MF options.
#
# Compiler:
#   CC, LD, CFLAGS, LDFLAGS
#
# Installation:
#   prefix, bindir, datadir, DESTDIR, INSTALL

all: editor

CC = gcc
LD = $(CC)
CFLAGS = -g -O2 -Wall
LDFLAGS =
INSTALL = install
prefix = /usr/local
bindir = $(prefix)/bin
datadir = $(prefix)/share

DEBUG = 1
DEBUG_SYNTAX = 0

# these should be fatal errors in all C compilers ever made
WARNINGS = \
	-Wcast-align \
	-Wdeclaration-after-statement \
	-Wformat-security \
	-Wmissing-prototypes \
	-Wold-style-definition \
	-Wredundant-decls \
	-Wwrite-strings

# additional warnings
WARNINGS += \
	-Wundef \
	-Wshadow

OBJECTS	:= 			\
	alias.o			\
	bind.o			\
	block.o			\
	buffer-highlight.o	\
	buffer-iter.o		\
	buffer.o		\
	change.o		\
	cmdline.o		\
	color.o			\
	commands.o		\
	completion.o		\
	config.o		\
	ctags.o			\
	edit.o			\
	editor.o		\
	file-history.o		\
	file-option.o		\
	filetype.o		\
	gbuf.o			\
	highlight.o		\
	history.o		\
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
	syntax.o		\
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
	# end

config	:=			\
	filetype		\
	rc			\
	# end

syntax	:=			\
	syntax/bash		\
	syntax/c		\
	syntax/config		\
	syntax/cpp		\
	syntax/css		\
	syntax/diff		\
	syntax/editor		\
	syntax/gitcommit	\
	syntax/go		\
	syntax/html		\
	syntax/make		\
	syntax/php		\
	syntax/python		\
	syntax/sh		\
	syntax/smarty		\
	syntax/sql		\
	# end

binding	:= $(addprefix share/,$(binding))
color	:= $(addprefix share/,$(color))
compiler:= $(addprefix share/,$(compiler))
config	:= $(addprefix share/,$(config))
syntax	:= $(addprefix share/,$(syntax))

-include Config.mk
include Makefile.lib

CFLAGS += $(call cc-option,$(WARNINGS))

ifndef NO_WERROR
CFLAGS += $(call cc-option,-Werror -Wno-error=shadow -Wno-error=unused-variable)
endif

CFLAGS += -DDATADIR=\"$(datadir)\"
CFLAGS += -DDEBUG=$(DEBUG)
CFLAGS += -DDEBUG_SYNTAX=$(DEBUG_SYNTAX)

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
	exuberant-ctags *.[ch]

REV     = $(shell git rev-parse --short HEAD)
RELEASE	= editor-$(REV)
TARBALL	= $(RELEASE).tar.gz

dist:
	git archive --format=tar --prefix=$(RELEASE)/ $(REV) | gzip -c -9 > $(TARBALL)

.PHONY: all clean distclean install dist
