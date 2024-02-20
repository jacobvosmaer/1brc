CFLAGS=-Wall -Werror -pedantic -std=gnu89
LDLIBS=-lm

all: gendata
	$(MAKE) -C c
