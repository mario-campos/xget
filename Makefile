PROG = xdccget
SRCS = xdccget.c libircclient/src/libircclient.a

LIBS = `pkg-config --silence-errors --libs libbsd-overlay`
CPPFLAGS += -D_FILE_OFFSET_BITS=64 -I libircclient/include
CFLAGS += -std=gnu99 `pkg-config --silence-errors --cflags libbsd-overlay`

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $(PROG) $(SRCS) $(LIBS)

libircclient/src/libircclient.a:
	cd libircclient && ./configure && $(MAKE)

clean:
	rm -f $(PROG)
	cd libircclient && $(MAKE) clean

distclean:
	cd libircclient && $(MAKE) distclean
