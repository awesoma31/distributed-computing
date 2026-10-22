CC = clang
CFLAGS = -std=c99 -Wall -pedantic

SRC = $(wildcard *.c)
HDR = $(wildcard *.h)

all: pa2

pa2: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SRC) -L. -lruntime -o $@

run: pa2
	LD_LIBRARY_PATH=. LD_PRELOAD=$(CURDIR)/libruntime.so ./pa2 -p 3 10 20 30

dist:
	rm -rf build && mkdir -p build/pa2
	cp $(SRC) $(HDR) build/pa2/
	COPYFILE_DISABLE=1 tar --no-xattrs --no-mac-metadata --format=ustar -czf pa2.tar.gz -C build pa2
	rm -rf build

clean:
	rm -f pa2 pa2.tar.gz *.log

.PHONY: all run dist clean
