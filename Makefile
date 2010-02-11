VERSION	= 000

all: editor

include config.mk
include scripts/lib.mk

CFLAGS	+= -DDATADIR=\"$(datadir)\"

objs 	:= 			\
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
	filetype.o		\
	gbuf.o			\
	highlight.o		\
	history.o		\
	iter.o			\
	lock.o			\
	obuf.o			\
	options.o		\
	parse-command.o		\
	ptr-array.o		\
	search.o		\
	spawn.o			\
	syntax.o		\
	term.o			\
	termcap.o		\
	terminfo.o		\
	uchar.o			\
	util.o			\
	view.o			\
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
	syntax/c		\
	syntax/config		\
	syntax/editor		\
	syntax/gitcommit	\
	syntax/go		\
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

editor: $(objs)
	$(call cmd,ld,)

clean		+= *.o editor
distclean	+= config.mk

install: all
	$(INSTALL) -m755 $(bindir) editor
	$(INSTALL) -m644 $(datadir)/editor $(config)
	$(INSTALL) -m644 $(datadir)/editor/binding $(binding)
	$(INSTALL) -m644 $(datadir)/editor/color $(color)
	$(INSTALL) -m644 $(datadir)/editor/compiler $(compiler)
	$(INSTALL) -m644 $(datadir)/editor/syntax $(syntax)

tags:
	exuberant-ctags *.[ch]

REV     = $(shell git rev-parse --short HEAD)
RELEASE	= editor-$(REV)
TARBALL	= $(RELEASE).tar.gz

dist:
	git archive --format=tar --prefix=$(RELEASE)/ $(REV) | gzip -c -9 > $(TARBALL)

.PHONY: all install dist
