##### Win32 variables #####

WIN32_EXE=dpmaster.exe
WIN32_CFLAGS=-D_WIN32_WINNT=0x0501
WIN32_LDFLAGS=-lws2_32
WIN32_RM=del

##### Unix variables #####

UNIX_EXE=dpmaster
UNIX_LDFLAGS=
UNIX_RM=rm -f

##### Common variables #####

CC=gcc
CFLAGS_COMMON=-Wall -Wdeclaration-after-statement
CFLAGS_DEBUG=$(CFLAGS_COMMON) -g
CFLAGS_RELEASE=$(CFLAGS_COMMON) -O2 -DNDEBUG
OBJECTS=common.o dpmaster.o messages.o servers.o system.o

##### Commands #####

.PHONY: all release debug mingw mingw-debug clean win32clean

all: release

release:
	$(MAKE) EXE=$(UNIX_EXE) LDFLAGS="$(UNIX_LDFLAGS)" CFLAGS="$(CFLAGS_RELEASE)" $(UNIX_EXE) 
	strip $(UNIX_EXE)

debug:
	$(MAKE) EXE=$(UNIX_EXE) LDFLAGS="$(UNIX_LDFLAGS)" CFLAGS="$(CFLAGS_DEBUG)" $(UNIX_EXE) 

mingw:
	$(MAKE) EXE=$(WIN32_EXE) LDFLAGS="$(WIN32_LDFLAGS)" CFLAGS="$(WIN32_CFLAGS) $(CFLAGS_RELEASE)" $(WIN32_EXE)
	strip $(WIN32_EXE)

mingw-debug:
	$(MAKE) EXE=$(WIN32_EXE) LDFLAGS="$(WIN32_LDFLAGS)" CFLAGS="$(WIN32_CFLAGS) $(CFLAGS_DEBUG)" $(WIN32_EXE)

.c.o:
	$(CC) $(CFLAGS) -c $*.c

$(EXE): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

clean:
	-$(UNIX_RM) $(WIN32_EXE)
	-$(UNIX_RM) $(UNIX_EXE)
	-$(UNIX_RM) *.o *~

mingw-clean:
	-$(WIN32_RM) $(WIN32_EXE)
	-$(WIN32_RM) *.o
