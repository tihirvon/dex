VERSION	= 000

all: editor

include config.mk
include scripts/lib.mk

INSTALL_LOG	:=

CFLAGS	+= -g -I.

objs	:= buffer.o cmdline.o commands.o config.o edit.o editor.o gbuf.o iter.o obuf.o \
	parse-command.o search.o term.o termcap.o terminfo.o uchar.o util.o \
	view.o window.o xmalloc.o history.o

editor: $(objs)
	$(call cmd,ld,)

clean		+= *.o editor
distclean	+= config.mk

install: all
	$(INSTALL) -m755 $(bindir) editor

tags:
	exuberant-ctags *.[ch]

REV     = $(shell git-rev-parse HEAD)
RELEASE	= editor-$(REV)
TARBALL	= $(RELEASE).tar.bz2

dist:
	git-tar-tree $(REV) $(RELEASE) | bzip2 -9 > $(TARBALL)

.PHONY: all install dist
