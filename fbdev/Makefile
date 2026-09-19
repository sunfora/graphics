SHELL:=/usr/bin/env bash

CC=gcc


CFLAGS=$(shell pkg-config --cflags cairo)
LDLIBS=$(shell pkg-config --libs cairo) -lm

all: framebuffer

framebuffer:
	$(CC) -pthread $(CFLAGS) -g3 -O2 -march=x86-64-v3 $(LDLIBS) .root/fbdev/framebuffer.c -o .root/fbdev/bin/framebuffer

lsp_stuff: distclean
	bear --output .root/fbdev/external_tools/compile_commands.json -- $(MAKE) all
	generate_clangd

watch:
	@echo $(script) | entr -r tcc '-run $(CFLAGS) $(LDLIBS)' $(script)

run: $(TARGET)
	sudo .root/fbdev/bin/framebuffer

clean:
	rm -f .root/fbdev/bin/framebuffer


distclean: clean
	rm -f .root/fbdev/external_tools/compile_commands.json
	rm -f .root/fbdev/.clangd

.PHONY: all lsp_stuff clean distclean
