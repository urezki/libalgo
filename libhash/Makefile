CC = gcc
CFLAGS = -g -Wall -W -O0 -fPIC -std=c99
INCLUDE = -I./include

SRC = $(wildcard ./src/*.c)
LIB_OBJ = $(subst .c,.o, $(SRC))

SHARED_LIB = libhash2.so
STATIC_LIB = libhash2_fpic.a

all: $(SHARED_LIB) $(STATIC_LIB)

$(SHARED_LIB): $(LIB_OBJ)
	$(CC) -shared -o $(SHARED_LIB) $(LIB_OBJ)

$(STATIC_LIB): $(LIB_OBJ)
	@ar r $(STATIC_LIB) $?

.c.o:
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ -c $<

clean:
	rm -rf $(LIB_OBJ) $(SHARED_LIB) $(STATIC_LIB)
