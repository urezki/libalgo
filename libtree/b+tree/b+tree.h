#ifndef __B_PLUS_TREE__
#define __B_PLUS_TREE__

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "list.h"

typedef unsigned char u8;

#define __must_check __attribute__((warn_unused_result))
#define likely(x)   __builtin_expect((ulong) (x), 1)
#define unlikely(x) __builtin_expect((ulong) (x), 0)

#define BUG() *((char *) 0) = 0xff
#define BUG_ON(cond) do { if (unlikely(cond)) BUG(); } while (0)

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
	MAX_CHILDREN = (TREE_ORDER),
	MIN_CHILDREN = (TREE_ORDER >> 1),
	MIN_ENTRIES_EXTER = MAX_ENTRIES >> 1,
	MIN_ENTRIES_INTER = MIN_CHILDREN - 1,
};

enum {
	BP_TYPE_INTER = 1,
	BP_TYPE_EXTER = 2,
};

/* Aliases. */
#define SUB_LINKS page.internal.sub_links

/* A common node structure. */
struct node {
	struct {
		struct node *parent;
		u8 parent_key_idx;
		u8 type;
	} info;

	/* indexes or records. */
	ulong entries;
	ulong slot[MAX_ENTRIES];

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

static inline int
node_min_entries(struct node *n)
{
	return is_node_internal(n) ?
		MIN_ENTRIES_INTER:MIN_ENTRIES_EXTER;
}

static inline int
sub_entries(struct node *n)
{
	return n->entries + 1;
}

/*
 * Removing or adding does not unbalance a node.
 */
static inline bool
is_node_safe(struct node *n)
{
	int min_entries = node_min_entries(n);
	return (n->entries > min_entries && n->entries != MAX_ENTRIES);
}

static inline bool
is_node_gt_min(struct node *n)
{
	int min_entries = node_min_entries(n);
	return (n->entries > min_entries);
}

static inline struct node *
bp_prev_node_or_null(struct node *n, struct list_head *head)
{
	struct list_head *prev;

	prev = list_prev_or_null(&n->page.external.list, head);
	if (prev)
		return list_entry(prev, struct node, page.external.list);

	return NULL;
}

static inline struct node *
bp_next_node_or_null(struct node *n, struct list_head *head)
{
	struct list_head *next;

	next = list_next_or_null(&n->page.external.list, head);
	if (next)
		return list_entry(next, struct node, page.external.list);

	return NULL;
}

static inline struct node *
bp_get_left_child(struct node *parent, int pos)
{
	return (pos < parent->entries) ?
		parent->SUB_LINKS[pos]:parent->SUB_LINKS[pos - 1];
}

static inline struct node *
bp_get_right_child(struct node *parent, int pos)
{
	return (pos < parent->entries) ?
		parent->SUB_LINKS[pos + 1]:parent->SUB_LINKS[pos];
}

static inline int bp_high(struct node *n)
{
	int h = 0;

	while (is_node_internal(n)) {
		n = n->SUB_LINKS[0];
		h++;
	}

	return h;
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

extern int bp_root_init(struct bp_root *);
extern void bp_root_destroy(struct bp_root *);
extern int bp_po_insert(struct bp_root *, ulong);
extern int bp_po_delete(struct bp_root *, ulong);
extern struct node *bp_lookup(struct bp_root *, ulong, int *);

#endif
