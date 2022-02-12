include Makefile.configure

OBJECTS = xdccget.o compats.o libircclient/src/libircclient.o

xdccget: $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS) $(LDADD)

$(OBJECTS): config.h

clean:
	rm -f xdccget $(OBJECTS)
