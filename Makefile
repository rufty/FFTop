APPNAME=fftop
VERSION=0.5

# Where to install.
PREFIX=/usr/local

# What OS?
UNAME:=$(shell uname)

# Special compiler?
ifeq ($(UNAME), Linux)
CC=gcc
endif
ifeq ($(UNAME), Darwin)
CC=gcc
endif
ifeq ($(UNAME), FreeBSD)
CC=clang
endif

#MSYS_NT-6.1
#MINGW32_NT-6.1
#MINGW64_NT-6.1

#MSYS_NT-6.3

#MSYS_NT-10.0
#MINGW32_NT-10.0
#MINGW64_NT-10.0


# Basic options.
#FIXME - remove debugging options
CFLAGS=-g -DDEBUG -std=c99 -pthread -W -Wall -DAPPNAME=$(APPNAME) -DVERSION=$(VERSION)
LFLAGS=-g
LIBS=-lpthread -lncurses -lfftw3 -lportaudio

#-static -I/mingw32/include/ncurses -lwinmm -lsetupapi -lole32

# The other bits.
DOCS = README LICENSE ChangeLog
# And all the code.
CODE = Makefile $(wildcard *.h) $(wildcard *.c)
# Other bits.
MISC = ${APPNAME}.1

# All the C source files.
SOURCES = $(wildcard *.c)
# All compiled C source files.
OBJECTS = $(SOURCES:.c=.o)

# Compile one file
.c.o:
	$(CC) $(CFLAGS) -MMD -c $<

# Default target.
all: $(APPNAME)

# Pull in header info.
-include *.d

# Link the app.
$(APPNAME): $(OBJECTS)
	$(CC) $(LFLAGS) -o $(APPNAME) $(OBJECTS) $(LIBS)

# Manpage.
$(APPNAME).1.gz: $(APPNAME).1
	cat $(APPNAME).1 | gzip -9 > $(APPNAME).1.gz

# Install the app.
install: $(APPNAME) $(APPNAME).1.gz
	install -D -m755 $(APPNAME) $(PREFIX)/bin/$(APPNAME)
	install -D -m644 $(APPNAME).1.gz $(PREFIX)/man/man1/$(APPNAME).1.gz
uninstall:
	rm -f $(PREFIX)/bin/$(APPNAME)
	rm -f $(PREFIX)/man/man1/$(APPNAME).1.gz

# Zap the cruft.
clean:
	rm -f *~
	rm -f *.d
	rm -f *.o
	rm -f $(APPNAME)
	rm -f $(APPNAME).1.gz
	rm -rf $(APPNAME).dSYM
distclean: clean
	rm -f tags
	rm -f ${APPNAME}.log
	rm -f ${APPNAME}-${VERSION}.zip
	rm -f ${APPNAME}-${VERSION}.tar.gz

# Generate an archive.
zip:
	zip -9 ${APPNAME}-${VERSION}.zip ${CODE} ${DOCS} ${MISC}&>/dev/null
tar:
	tar --xform "s#^#${APPNAME}-${VERSION}/#" -zcvf ${APPNAME}-${VERSION}.tar.gz ${CODE} ${DOCS} ${MISC}&>/dev/null


# vim:ts=2:sw=2:tw=114:fo=tcnq2b:foldmethod=indent
