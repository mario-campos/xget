PROG = xdccget
LIBIRCCLIENT = libircclient-1.10
LIBS = `pkg-config --silence-errors --libs libbsd-overlay`
CFLAGS += -std=gnu99 -D_FILE_OFFSET_BITS=64 -I$(LIBIRCCLIENT)/include `pkg-config --silence-errors --cflags libbsd-overlay`

SRCS = xdccget.c $(LIBIRCCLIENT)/src/libircclient.c

all: $(SRCS)
	cd $(LIBIRCCLIENT) && ./configure
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS) $(LIBS)

clean:
	rm -f $(PROG)

distclean:
	cd $(LIBIRCCLIENT) && $(MAKE) distclean
