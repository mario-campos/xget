PROG = xdccget
LIBS = -lpthread `pkg-config --libs openssl` `pkg-config --silence-errors --libs libbsd`
CFLAGS += -std=gnu99 -D_FILE_OFFSET_BITS=64 -DENABLE_SSL -DENABLE_IPV6 -DHAVE_POLL -Ilibircclient-include `pkg-config --cflags openssl`

SRCS = xdccget.c \
       helper.c \
       argument_parser.c \
       libircclient-src/libircclient.c

all: $(SRCS)
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS) $(LIBS)

clean:
	rm -f $(PROG)
