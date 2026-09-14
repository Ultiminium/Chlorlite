CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Iinclude -Isrc
all: libvoxel.a selftest
src/voxel.o: src/voxel.c src/mc_tritable.inc
	$(CC) $(CFLAGS) -c src/voxel.c -o src/voxel.o
libvoxel.a: src/voxel.o
	ar rcs $@ src/voxel.o
selftest: libvoxel.a examples/selftest.c
	$(CC) $(CFLAGS) examples/selftest.c libvoxel.a -lm -o selftest
test: selftest
	./selftest
clean:
	rm -f src/*.o libvoxel.a selftest
.PHONY: all test clean
