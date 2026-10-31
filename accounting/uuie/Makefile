CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror \
          -fsanitize=address,undefined -fno-sanitize-recover=all

all: test_uuie

test_uuie: uuie.o uuie_lang.o test_uuie.o
	$(CC) $(CFLAGS) -o $@ uuie.o uuie_lang.o test_uuie.o

%.o: %.c uuie.h uuie_lang.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: test_uuie
	./test_uuie

clean:
	rm -f *.o test_uuie

.PHONY: all test clean
