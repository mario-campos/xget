PROG = xdccget
SRCS = xdccget.c $(LIBIRCCLIENT)/src/libircclient.a
LIBIRCCLIENT = libircclient-1.10

LIBS = `pkg-config --silence-errors --libs libbsd-overlay`
CPPFLAGS += -I$(LIBIRCCLIENT)/include
CFLAGS += -std=gnu99 `pkg-config --silence-errors --cflags libbsd-overlay`

$(PROG): $(SRCS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBS) -o $(PROG) $(SRCS)

$(LIBIRCCLIENT)/src/libircclient.a:
	cd $(LIBIRCCLIENT) && ./configure && $(MAKE)

clean:
	rm -f $(PROG)
	cd $(LIBIRCCLIENT) && $(MAKE) clean

distclean:
	cd $(LIBIRCCLIENT) && $(MAKE) distclean
