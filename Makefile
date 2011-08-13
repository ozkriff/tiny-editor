CFLAGS=-Wall -g -Wextra
e: e.c
	tcc $(CFLAGS) -lncursesw e.c -o e
clean:
	rm e

