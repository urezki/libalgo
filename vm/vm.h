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
#define ALIGN(x, a)	(((x) + (a) - 1) & ~((a) - 1))

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
	BPT_ORDER = 24,
	MAX_ENTRIES = (BPT_ORDER - 1),
	MAX_CHILDREN = (BPT_ORDER),
	MIN_CHILDREN = (BPT_ORDER >> 1),
	MIN_ENTRIES_EXTER = MAX_ENTRIES >> 1,
	MIN_ENTRIES_INTER = MIN_CHILDREN - 1,
};

enum {
	BPN_TYPE_INTER = 1,
	BPN_TYPE_EXTER = 2,
};

/* Aliases. */
#define SUB_LINKS page.internal.subl
#define SUB_AVAIL page.internal.suba

/* A common node structure. */
struct bpn {
	struct {
		void *parent;
		u8 ppos;
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
			ulong suba[MAX_CHILDREN];
			void *subl[MAX_CHILDREN];
		} internal;

		struct {				/* leaf nodes. */
			struct list_head list;
		} external;
	} page;

#ifdef DEBUG_BP_TREE
	unsigned long num;			/* for debug */
#endif
};

struct bpt_root {
	struct bpn *node;
	struct list_head head;
};

/* Payload data. */
typedef struct vmap_area {
	unsigned long va_start;
	unsigned long va_end;
} vmap_area;

static __always_inline ulong
va_size(struct vmap_area *va)
{
	return va->va_end - va->va_start;
}

static inline bool
is_within_this_va(struct vmap_area *va, ulong size,
	ulong align, ulong vstart)
{
	ulong nva_start_addr;

	if (va->va_start > vstart)
		nva_start_addr = ALIGN(va->va_start, align);
	else
		nva_start_addr = ALIGN(vstart, align);

	/* Can be overflowed due to big size or alignment. */
	if (nva_start_addr + size < nva_start_addr ||
			nva_start_addr < vstart)
		return false;

	return (nva_start_addr + size <= va->va_end);
}

/*
 * Halve an index with adjustment for odd numbers.
 */
static __always_inline int split(int x)
{
	return (x >> 1) + (x & 1);
}

static __always_inline bool
is_bpn_internal(struct bpn *n)
{
	return n->info.type == BPN_TYPE_INTER;
}

static __always_inline bool
is_bpn_external(struct bpn *n)
{
	return n->info.type == BPN_TYPE_EXTER;
}

static __always_inline bool
is_bpn_full(struct bpn *n)
{
	return (n->entries == MAX_ENTRIES);
}

static __always_inline int
bpn_min_entries(struct bpn *n)
{
	return is_bpn_internal(n) ?
		MIN_ENTRIES_INTER:MIN_ENTRIES_EXTER;
}

static __always_inline int
nr_sub_entries(struct bpn *n)
{
	return is_bpn_internal(n) ? n->entries + 1:0;
}

/*
 * Leafs do not store an extra array for keys, instead a
 * separator value is kept within a payload data(record).
 *
 * Therefore there is a helper, in order to extract a key
 * from a node, i.e. for a leaf a record contains the key.
 */
static __always_inline ulong
bpn_get_key(struct bpn *n, int pos)
{
	if (is_bpn_external(n))
		return ((vmap_area *) n->slot[pos])->va_start;

	/* It is internal. */
	return n->slot[pos];
}

static __always_inline void *
bpn_get_val(struct bpn *n, int pos)
{
	if (is_bpn_external(n))
		return (vmap_area *) n->slot[pos];

	return NULL;
}

/*
 * Removing or adding does not unbalance a node.
 */
static inline bool
is_bpn_safe(struct bpn *n)
{
	int min_entries = bpn_min_entries(n);
	return (n->entries > min_entries && n->entries != MAX_ENTRIES);
}

static inline bool
is_bpn_gt_min(struct bpn *n)
{
	int min_entries = bpn_min_entries(n);
	return (n->entries > min_entries);
}

static inline struct bpn *
bpn_get_left(struct bpn *parent, int pos)
{
	return (pos < parent->entries) ?
		parent->SUB_LINKS[pos]:parent->SUB_LINKS[pos - 1];
}

static inline struct bpn *
bpn_get_right(struct bpn *parent, int pos)
{
	return (pos < parent->entries) ?
		parent->SUB_LINKS[pos + 1]:parent->SUB_LINKS[pos];
}

static inline int bpt_high(struct bpn *n)
{
	int h = 0;

	while (is_bpn_internal(n)) {
		n = n->SUB_LINKS[0];
		h++;
	}

	return h;
}

static inline void
check_bpn_geometry(struct bpn *n)
{
	if (!n->info.parent)
		return;

	if (is_bpn_internal(n))
		BUG_ON(n->entries < MIN_ENTRIES_INTER);
	else
		BUG_ON(n->entries < MIN_ENTRIES_EXTER);
}

extern int bpt_root_init(struct bpt_root *);
extern void bpt_root_destroy(struct bpt_root *);
extern int bpt_po_insert(struct bpt_root *, vmap_area *);
extern void *bpt_po_delete(struct bpt_root *, ulong);
extern void *bpt_lookup(struct bpt_root *, ulong, int *);
extern struct vmap_area *bpt_lookup_smallest(struct bpt_root *, ulong, ulong);

#endif
