#ifndef __B_PLUS_TREE__
#define __B_PLUS_TREE__

#include <string.h>
#include <stdbool.h>
#include "./list.h"

typedef unsigned long ulong;
typedef unsigned short u16;
typedef unsigned char u8;

#define likely(x)   __builtin_expect((ulong) (x), 1)
#define unlikely(x) __builtin_expect((ulong) (x), 0)

#define BUG() *((char *) 0) = 0xff
#define BUG_ON(cond) do { if (unlikely(cond)) BUG(); } while (0)

#define ARRAY_MOVE(a, i, j, n)		\
	memmove((a) + (i), (a) + (j), sizeof(*a) * ((n) - (j)))

#define ARRAY_COPY(dest, src, n)	\
	memcpy(dest, src, n)

#define ARRAY_INSERT(a, i, j, n)	\
	do { ARRAY_MOVE(a, (i) + 1, i, j); (a)[i] = (n); } while(0)

/*
 * The standard definition of order for a tree is the maximum
 * branching factor, i.e. the maximum number of children that
 * a node may have.
 *
 * Example, Tree-Order 8 (maximum branching factor):
 *
 * maximum children: m = 8
 * minimum children: m / 2 = 4
 * maximum keys:     m - 1 = 7
 * minimum keys:     (m - 1) / 2 = 3
 */
enum tree_properties {
	TREE_ORDER = 4,
	MAX_ENTRIES = (TREE_ORDER - 1),
	MIN_ENTRIES_EXTER = MAX_ENTRIES >> 1,

	MAX_CHILDREN = (TREE_ORDER),
	MIN_CHILDREN = (MAX_CHILDREN >> 1),

	/*
	 * An internal node with N children has (N - 1)
	 * search field values, i.e. the minimum number
	 * of entries is MIN_CHILDREN - 1.
	 */
	MIN_ENTRIES_INTER = MIN_CHILDREN - 1,
};

enum {
	BP_TYPE_INTER = 1,
	BP_TYPE_EXTER = 2,
};

#define NODE_SUB_LINK(n, idx) n->page.internal.sub_links[idx]

/* A common node structure. */
struct node {
	struct {
		struct node *parent;
		u8 parent_key_idx;
		u8 type;
	} info;

	/* indexes or records. */
	ulong slot[MAX_ENTRIES];
	u8 entries;

	/*
	 * This union is used for differentiating between leaf
	 * and internal nodes. A page keeps either references
	 * to sub-nodes or leaf-nodes where data is stored.
	 */
	union {
		struct {				/* internal/index nodes. */
			ulong sub_max_size[MAX_CHILDREN];
			struct node *sub_links[MAX_CHILDREN];
		} internal;

		struct {				/* leaf nodes. */
			struct list_head list;
		} external;
	} page;

#ifdef DEBUG_BP_TREE
	unsigned long num;			/* for debug */
#endif
};

struct bp_root {
	struct node *node;
	struct list_head head;
};

/*
 * Halve an index with adjustment for odd numbers.
 */
static inline int split(int x)
{
	return (x >> 1) + (x & 1);
}

static inline bool
is_node_internal(struct node *n)
{
	return n->info.type == BP_TYPE_INTER;
}

static inline bool
is_node_external(struct node *n)
{
	return n->info.type == BP_TYPE_EXTER;
}

static inline bool
is_node_full(struct node *n)
{
	return (n->entries == MAX_ENTRIES);
}

static inline int bp_tree_high(struct node *n)
{
	int depth = 1;

	while (is_node_internal(n)) {
		n = NODE_SUB_LINK(n, 0);
		depth++;
	}

	return depth;
}

static inline void
check_node_geometry(struct node *n)
{
	if (!n->info.parent)
		return;

	if (is_node_internal(n))
		BUG_ON(n->entries < MIN_ENTRIES_INTER);
	else
		BUG_ON(n->entries < MIN_ENTRIES_EXTER);
}

#endif
