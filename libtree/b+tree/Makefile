# Mafifile

CC = gcc
CFLAGS = -Wall -D_GNU_SOURCE -std=c99 -I./ -DDEBUG_BP_TREE
#DEBUG_CFLAGS = -O0 -g -fsanitize=bounds-strict -fsanitize=address -static-libasan

BINARY = test
SRC = $(wildcard *.c)
OBJ = $(subst .c,.o, $(SRC))

all: clean $(OBJ)
	$(CC) $(CFLAGS) $(DEBUG_CFLAGS) -o $(BINARY) $(OBJ)

%.o: %.c
	@echo [Compiling]: $<
	$(CC) $(CFLAGS) $(DEBUG_CFLAGS) -o $@ -c $<

clean:
	rm -rf $(OBJ) $(BINARY)

