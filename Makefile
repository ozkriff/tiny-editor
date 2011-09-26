CFLAGS=-Wall -g -Wextra
e: e.c
	gcc $(CFLAGS) -lncurses e.c -o e
clean:
	rm e

