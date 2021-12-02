PROG = xdccget
LIBS = -lpthread `pkg-config --libs openssl`
CFLAGS += -std=gnu99 -D_FILE_OFFSET_BITS=64 -DENABLE_SSL -DENABLE_IPV6 -DHAVE_POLL -Wall -Wfatal-errors -Os -fstack-protector -Ilibircclient-include `pkg-config --cflags openssl`

SRCS = xdccget.c \
       helper.c \
       argument_parser.c \
       libircclient-src/libircclient.c \
       sds.c

all: $(SRCS)
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS) $(LIBS)

clean:
	rm -f $(PROG)
