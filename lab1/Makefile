CC = clang
CFLAGS = -std=c99 -Wall -pedantic

SRC = $(wildcard *.c)
HDR = $(wildcard *.h)

all: pa1

pa1: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SRC) -o $@

dist:
	rm -rf build && mkdir -p build/pa1
	cp $(SRC) $(HDR) build/pa1/
	tar -czf pa1.tar.gz -C build pa1
	rm -rf build

clean:
	rm -f pa1 pa1.tar.gz *.log

.PHONY: all dist clean
