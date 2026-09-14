CC=gcc

CFLAGS=$(shell pkg-config --cflags cairo)
LDLIBS=$(shell pkg-config --libs cairo) -lm

TARGET=poc

all: $(TARGET)

$(TARGET): test.c
	$(CC) -pthread $(CFLAGS) -g3 -O2 test.c $(LDLIBS) -o $(TARGET)

lsp_stuff: distclean
	bear -- $(MAKE) all

clean:
	rm -f $(TARGET)


distclean: clean
	rm -f compile_commands.json

.PHONY: all lsp_stuff clean distclean
