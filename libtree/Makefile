CC = gcc
CFLAGS = -g -Wall -W -O0 -fPIC -std=c99
INCLUDE = -I./bstree -I./avltree -I./splaytree -I ../include

BST_SRC = $(wildcard ./bstree/*.c)
BST_OBJ = $(subst .c,.o, $(BST_SRC))

AVL_SRC = $(wildcard ./avltree/*.c)
AVL_OBJ = $(subst .c,.o, $(AVL_SRC))

SPLAY_SRC = $(wildcard ./splaytree/*.c)
SPLAY_OBJ = $(subst .c,.o, $(SPLAY_SRC))

BST_SHARED_LIB = libbst.so
BST_STATIC_LIB = libbst_fpic.a

AVL_SHARED_LIB = libavl.so
AVL_STATIC_LIB = libavl_fpic.a

SPLAY_SHARED_LIB = libsplay.so
SPLAY_STATIC_LIB = libsplay_fpic.a

all: $(BST_SHARED_LIB) $(BST_STATIC_LIB) \
	$(AVL_SHARED_LIB) $(AVL_STATIC_LIB) \
	$(SPLAY_SHARED_LIB) $(SPLAY_STATIC_LIB)

$(BST_SHARED_LIB): $(BST_OBJ)
	$(CC) -shared -o $(BST_SHARED_LIB) $(BST_OBJ)

$(BST_STATIC_LIB): $(BST_OBJ)
	@ar r $(BST_STATIC_LIB) $?

$(AVL_SHARED_LIB): $(AVL_OBJ)
	$(CC) -shared -o $(AVL_SHARED_LIB) $(AVL_OBJ)

$(AVL_STATIC_LIB): $(AVL_OBJ)
	@ar r $(AVL_STATIC_LIB) $?

$(SPLAY_SHARED_LIB): $(SPLAY_OBJ)
	$(CC) -shared -o $(SPLAY_SHARED_LIB) $(SPLAY_OBJ)

$(SPLAY_STATIC_LIB): $(SPLAY_OBJ)
	@ar r $(SPLAY_STATIC_LIB) $?

.c.o:
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ -c $<

clean:
	@find . -name "*.so" | xargs rm -rf
	@find . -name "*.a" | xargs rm -rf
	@find . -name "*.o" | xargs rm -rf
	@echo Done!
