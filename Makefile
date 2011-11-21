CFLAGS=-Wall -g -Wextra -std=c89 --pedantic
e: e.c
	gcc $(CFLAGS) -lncurses e.c -o e
clean:
	rm e

