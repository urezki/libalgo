#ifndef BTREE_BLUE_H
#define BTREE_BLUE_H

#define BITS_PER_BYTE 8
#define	BITS_PER_LONG 64
#define L1_CACHE_BYTES 64
#define PAGE_SIZE 4096

#define MAX_TREE_HEIGHT 8
#define MIN_SLOTS_NUMBER 16
#define MIN_NODE_SIZE (MIN_SLOTS_NUMBER * 2 * sizeof(unsigned long))

#define GET_PREV 0
#define GET_NEXT 1

#define LONG_PER_U64 (64 / BITS_PER_LONG)
#define MAX_KEYLEN (2 * LONG_PER_U64)
#define VALUE_LEN (sizeof(unsigned long))

#define likely(x)   __builtin_expect((unsigned long) (x), 1)
#define unlikely(x) __builtin_expect((unsigned long) (x), 0)

#define BUG() *((char *) 0) = 0xff
#define BUG_ON(cond) do { if (unlikely(cond)) BUG(); } while (0)

typedef unsigned char u8;
typedef unsigned short u16;

struct btree_blue_slots_info {
	u16 slots_nr;
	u16 offset;
	u16 reserved_a;
	u16 reserved_b;
};

struct btree_blue_node_cb {
	struct btree_blue_slots_info slots_info;
	unsigned long slots_base[];
};

struct btree_blue_stub {
	unsigned long *prev;
	unsigned long *next;
};

struct btree_blue_head {
	unsigned long *node;

	u16 node_size;
	u16 leaf_size;
	u16 stub_base;
	u8 keylen;
	u8 slot_width;
	u8 height;
	u8 reserved[1];

	u16 slot_vols[MAX_TREE_HEIGHT + 1];
};

int btree_blue_init(struct btree_blue_head *head,
				 int node_size_in_byte, int key_len_in_byte,
				 int flags);

void btree_blue_destroy(struct btree_blue_head *head);

void *btree_blue_lookup(struct btree_blue_head *head, unsigned long *key);

int btree_blue_insert(struct btree_blue_head *head,
				   unsigned long *key, void *val);

int btree_blue_update(struct btree_blue_head *head, unsigned long *key,
		      void *val);

void *btree_blue_remove(struct btree_blue_head *head, unsigned long *key);

void *btree_blue_first(struct btree_blue_head *head, unsigned long *__key);
void *btree_blue_last(struct btree_blue_head *head, unsigned long *__key);

void *btree_blue_get_prev(struct btree_blue_head *head, unsigned long *__key);
void *btree_blue_get_next(struct btree_blue_head *head, unsigned long *__key);
#endif
