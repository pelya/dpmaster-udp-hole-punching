##### Win32 variables #####

ifdef windir

EXE=dpmaster.exe
LDFLAGS=-lwsock32
CMD_RM=del

##### Unix variables #####

else

EXE=dpmaster
LDFLAGS=
CMD_RM=rm -f

endif

##### Common variables and commands #####

CFLAGS= -MD -Wall -Werror -O2

all: $(EXE)

.c.o:
	gcc $(CFLAGS) -c $*.c

$(EXE): dpmaster.o
	gcc -o $@ $^ $(LDFLAGS)

clean:
	-$(CMD_RM) $(EXE) *.o *.d

.PHONY: clean

-include *.d
