XGET_OBJS = xget.o libircclient/src/libircclient.o log.c/src/log.o
TEST_OBJS = test/xget-test.o

all: xget
xget: $(XGET_OBJS)
xget-test: $(TEST_OBJS)

.PHONY: check
check: xget-test
	./xget-test

.PHONY: clean
clean:
	rm -f xget xget-test $(XGET_OBJS) $(TEST_OBJS)