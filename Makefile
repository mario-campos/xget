PROG = xdccget
LIBIRCCLIENT = libircclient-1.10
LIBS = `pkg-config --silence-errors --libs libbsd-overlay`
CFLAGS += -std=gnu99 -I$(LIBIRCCLIENT)/include `pkg-config --silence-errors --cflags libbsd-overlay`

SRCS = xdccget.c $(LIBIRCCLIENT)/src/libircclient.a

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS) $(LIBS)

$(LIBIRCCLIENT)/src/libircclient.a:
	cd $(LIBIRCCLIENT) && ./configure && $(MAKE)

clean:
	rm -f $(PROG)
	cd $(LIBIRCCLIENT) && $(MAKE) clean

distclean:
	cd $(LIBIRCCLIENT) && $(MAKE) distclean
