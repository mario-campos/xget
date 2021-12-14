PROG = xdccget
LIBIRCCLIENT = libircclient-1.6
LIBS = `pkg-config --silence-errors --libs libbsd`
CFLAGS += -std=gnu99 -D_FILE_OFFSET_BITS=64 -I$(LIBIRCCLIENT)/include

SRCS = xdccget.c \
       helper.c \
       argument_parser.c \
       $(LIBIRCCLIENT)/src/libircclient.c

all: $(SRCS)
	cd $(LIBIRCCLIENT) && ./configure
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS) $(LIBS)

clean:
	rm -f $(PROG)

distclean:
	cd $(LIBIRCCLIENT) && $(MAKE) distclean