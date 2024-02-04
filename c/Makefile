CFLAGS+=-Wall -Werror -pedantic -std=gnu89 -fno-omit-frame-pointer -flto -O2
LDLIBS += -lm
OBJS = c1 c2 c3 c4 c5 c6
all: $(OBJS)

debug: CFLAGS += -O0 -g -fsanitize=address
debug: all

clean:
	rm -f $(OBJS)
