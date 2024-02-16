CFLAGS+=-Wall -Werror -pedantic -std=gnu89 -fno-omit-frame-pointer -flto -O2
LDLIBS += -lm -lpthread
OBJS = c1 c2 c3 c4 c5 c6 c7 c8 c9
all: $(OBJS) c8.txt

c8.txt: c8
	objdump -d c8 > c8.txt

debug: CFLAGS += -O0 -g -fsanitize=address
debug: all

clean:
	rm -f $(OBJS) c8.txt
