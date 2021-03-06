#ifndef __BTREE_H__
#define __BTREE_H__

#define likely(x)   __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#define BUG() *((char *) 0) = 0xff
#define BUG_ON(cond) do { if (unlikely(cond)) BUG(); } while (0)

/*
 * The bound can be expressed in terms of a fixed integer
 * that must be t >= 2 called the minimum degree of the B-tree
 */
#define MIN_DEGREE (12)
#define MIN_UTIL_SLOTS (MIN_DEGREE - 1)
#define MAX_UTIL_SLOTS (2 * MIN_DEGREE - 1)

typedef struct vaslot {
	unsigned long va_start;
} vaslot_t;

typedef struct bt_node {
	unsigned long nr_entries;
	vaslot_t slots[MAX_UTIL_SLOTS];
	struct bt_node *links[MAX_UTIL_SLOTS + 1];
	struct bt_node *parent;
#ifdef DEBUG_BTREE
	unsigned long num;			/* for debug */
#endif
} bt_node;

#endif	/* __BTREE_H__ */
