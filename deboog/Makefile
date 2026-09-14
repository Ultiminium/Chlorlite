# Deboog — standalone build. Depends only on a C compiler + libm.
# `make`         → static lib (libdeboog.a) + the self-test
# `make test`    → build and run the self-test
# `make shared`  → shared lib (libdeboog.so)
CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wno-misleading-indentation -Iinclude
AR      ?= ar

SRC     = src/deboog.c
OBJ     = $(SRC:.c=.o)

all: libdeboog.a selftest

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

libdeboog.a: $(OBJ)
	$(AR) rcs $@ $(OBJ)

libdeboog.so: $(SRC)
	$(CC) $(CFLAGS) -fPIC -shared $(SRC) -lm -o $@

shared: libdeboog.so

selftest: libdeboog.a examples/selftest.c
	$(CC) $(CFLAGS) examples/selftest.c libdeboog.a -lm -o selftest

test: selftest
	./selftest

clean:
	rm -f $(OBJ) libdeboog.a libdeboog.so selftest

.PHONY: all shared test clean
