# Mafifile

CC = gcc
DEFAULT_CFLAGS = -Wall -Warray-bounds -D_GNU_SOURCE \
	-std=c99 -I./ -DDEBUG_BP_TREE -lpthread

CFLAGS = -O0 -g ${DEFAULT_CFLAGS}
# CFLAGS = -O3 ${DEFAULT_CFLAGS}
# DEBUG_CFLAGS = -g -fsanitize=bounds-strict -fsanitize=address -static-libasan ${DEFAULT_CFLAGS} -DDEBUG

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

