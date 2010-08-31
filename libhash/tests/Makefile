GCC = gcc
CFLAGS = -g -Wall -O0 -std=c99
INCLUDE = -I../include
LIB = -L=../ -lhash2 -Wl,-rpath=../

SRC = $(wildcard ./*.c)
OBJ = $(subst .c,.o, $(SRC))

all: $(OBJ)

.c.o:
	$(GCC) $(CFLAGS) $< $(INCLUDE) -o $@ $(LIB)

clean:
	rm -rf $(OBJ)
