VERSION	= 000

all: editor

include config.mk
include scripts/lib.mk

INSTALL_LOG	:=

CFLAGS	+= -g -DDATADIR=\"$(datadir)\"

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
	obuf.o			\
	options.o		\
	parse-command.o		\
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

editor: $(objs)
	$(call cmd,ld,)

clean		+= *.o editor
distclean	+= config.mk

install: all
	$(INSTALL) -m755 $(bindir) editor

tags:
	exuberant-ctags *.[ch]

REV     = $(shell git rev-parse --short HEAD)
RELEASE	= editor-$(REV)
TARBALL	= $(RELEASE).tar.bz2

dist:
	git archive --format=tar --prefix=$(RELEASE)/ $(REV) | gzip -c -9 > $(TARBALL)

.PHONY: all install dist
