LUADIR=/usr/include/lua5.1
LUADIR=/usr/include
LPEGDIR=../lpeg-0.12
CFLAGS=-Wall -Wextra -O2 -I$(LUADIR) -I$(LPEGDIR)
CC=gcc

# For Linux
linux:
	make lpeg/serialize.so "DLLFLAGS = -shared -fPIC"

# For Mac OS
macosx:
	make lpeg/serialize.so "DLLFLAGS = -bundle -undefined dynamic_lookup"

lpeg:
	mkdir lpeg

lpeg/serialize.so: lpserialize.c lpeg Makefile
	$(CC) $(CFLAGS) $(DLLFLAGS) -o $@ $<

clean:
	rm serialize.so
