SHELL = /bin/sh


subdirs = @subdirs@
top_srcdir = .
srcdir = .
prefix = /home/haru/tmp/abs/cxfe/pkg/usr/local
exec_prefix = ${prefix}
bindir = $(exec_prefix)/bin
infodir = $(prefix)/info
libdir = $(prefix)/lib/gnudl
mandir = $(prefix)/man/man1

CC = gcc -Wall
CPPFLAGS = 
CFLAGS = $(CPPFLAGS) -march=i686 -mtune=generic -O2 -pipe `xine-config --cflags`
LIBS = -lxine -lncurses -lm -lXext -lX11 
LDFLAGS = `xine-config --libs` -L/usr/X11R6/lib -lXext -lxine -lncurses -lm -lXext -lX11  -lX11
INSTALL = @INSTALL@

#CC=gcc -Wall -g -O2
#CFLAGS= `xine-config --cflags`
#LDFLAGS = `xine-config --libs` -L/usr/X11R6/lib -lXext -lxine -lncurses -lm -lXext -lX11  -lX11

all: cxfe 

getch2: termio/Makefile

cxfe: cxfe.o post.o termio/getch2.o

clean:
	rm cxfe.o cxfe termio/*.o post.o

install:
	cp cxfe $(bindir)/cxfe
	chmod 755 $(bindir)/cxfe
	if test ! -d $(prefix)/share/cxfe ; then mkdir -p $(prefix)/share/cxfe ; fi
	cp lircrc-example $(prefix)/share/cxfe/lircrc

uninstall:
	rm -f $(bindir)/cxfe
	rm -Rf $(prefix)/share/cxfe
