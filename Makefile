
CFLAGS= -MD -Wall -Werror -O2

LDFLAGS=

all: dpmaster

.c.o:
	gcc $(CFLAGS) -c $*.c

dpmaster: dpmaster.o
	gcc -o $@ $^ $(LDFLAGS)

clean:
	-rm -f dpmaster *.o *.d

.PHONY: clean

-include *.d

