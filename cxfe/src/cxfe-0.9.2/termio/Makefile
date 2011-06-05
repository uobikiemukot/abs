SHELL = /bin/sh


subdirs = @subdirs@
top_srcdir = ..
srcdir = .
prefix = /home/haru/tmp/abs/cxfe/pkg/usr/local
exec_prefix = ${prefix}
bindir = $(exec_prefix)/bin
infodir = $(prefix)/info
libdir = $(prefix)/lib/gnudl
mandir = $(prefix)/man/man1

CC = gcc
CPPFLAGS = 
CFLAGS = $(CPPFLAGS) -march=i686 -mtune=generic -O2 -pipe
LDFLAGS = -Wl,--hash-style=gnu -Wl,--as-needed
LIBS = -lxine -lncurses -lm -lXext -lX11 
INSTALL = @INSTALL@
#CC = gcc

#CFLAGS = -Wall

.SUFFIXES: .c .o

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

all: getch2.o

clean: 
	@if test -f getch2.o ; then rm getch2.o ; fi
