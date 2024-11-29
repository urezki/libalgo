#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "b+tree.h"
#include "array.h"

static struct bpn *
bpn_calloc_init(u8 type)
{
	struct bpn *n = calloc(1, sizeof(*n));

	if (unlikely(!n))
		assert(0);

	n->info.type = type;

	if (type == BPN_TYPE_EXTER)
		list_init(&n->page.external.list);

	return n;
}

/* Position condition codes. */
typedef enum {
	POS_CC_EQ = 0,					/* key = mkey */
	POS_CC_LT = 1,					/* key < mkey */
	POS_CC_GT = 2,					/* key > mkey */
} pos_cc_t;

/*
 * Returns an index j, such that array[j-1] < key <= array[j].
 * Example, array = 3 4 6 7, if key=5 then j=2, if key=8, j=4.
 */
static __always_inline pos_cc_t
bpn_bin_search(struct bpn *n, ulong key, int *pos)
{
	pos_cc_t pos_cc;
	int l, r;

	check_bpn_geometry(n);

	l = -1;
	r = n->entries;

	/* invariant: a[lo] < key <= a[hi] */
	while (l + 1 < r) {
		int m = (l + r) >> 1;
		(get_bpn_key(n, m) < key) ? (l = m):(r = m);
	}

	if (r < n->entries) {
		if (get_bpn_key(n, r) == key)
			pos_cc = POS_CC_EQ;
		else
			pos_cc = POS_CC_LT;
	} else {
		pos_cc = POS_CC_GT;
	}

	*pos = r;
	return pos_cc;
}

static __always_inline int
bpn_insert_to_leaf(struct bpn *n, int pos, record *r)
{
	BUG_ON(pos >= MAX_ENTRIES);

	/* Check __alive__ entries for duplicates. */
	if (pos < n->entries)
		if (unlikely(get_bpn_key(n, pos) == r->key))
			return -1;

	array_insert(n->slot, pos, n->entries, (ulong) r);
	n->entries++;
	return 0;
}

static __always_inline
record *bpn_remove_from_leaf(struct bpn *n, int pos, ulong key)
{
	record *r;

	BUG_ON(pos >= MAX_ENTRIES);

	/* Wrong position or value? */
	if (unlikely(get_bpn_key(n, pos) != key))
		return NULL;

	r = get_bpn_val(n, pos);
	array_remove(n->slot, pos, n->entries);
	n->entries--;
	return r;
}

/*
 * NOTE: it does not introduce dis-balance to left nor right nodes!
 */
static bool
bpn_try_shift_left(struct bpn *l, struct bpn *r, struct bpn *p, int pos)
{
	/* Adjust position. */
	if (pos == p->entries)
		pos--;

	/*
	 * Bail out if:
	 * - position is wrong;
	 * - no way to shift left(it is full);
	 * - right becomes unbalanced.
	 */
	if (pos >= p->entries || is_bpn_full(l) || !is_bpn_gt_min(r))
		return false;

	if (is_bpn_internal(l)) {
		l->slot[l->entries] = p->slot[pos];
		p->slot[pos] = r->slot[0];

		l->SUB_LINKS[l->entries + 1] = r->SUB_LINKS[0];
		array_move(r->SUB_LINKS, 0, 1, bpn_nr_sub_entries(r));
	} else {
		l->slot[l->entries] = r->slot[0];
		p->slot[pos] = get_bpn_key(r, 1);
	}

	/* Remove the element from right node. */
	array_move(r->slot, 0, 1, r->entries);

	r->entries--;
	l->entries++;
	return true;
}

/*
 * @l: @r: @p: left, right, parent
 * @pos: a separator position in a parent node between left and right children
 *
 * NOTE: it does not introduce dis-balance to left nor right nodes!
 */
static bool
bpn_try_shift_right(struct bpn *l, struct bpn *r, struct bpn *p, int pos)
{
	/* Adjust position. */
	if (pos == p->entries)
		pos--;

	/*
	 * Bail out if:
	 * - position is wrong;
	 * - no way to shift right(it is full);
	 * - left becomes unbalanced.
	 */
	if (pos >= p->entries || is_bpn_full(r) || !is_bpn_gt_min(l))
		return false;

	if (is_bpn_internal(l)) {
		array_insert(r->slot, 0, r->entries, p->slot[pos]);
		array_insert(r->SUB_LINKS, 0, bpn_nr_sub_entries(r),
			(ulong) l->SUB_LINKS[l->entries]);
	} else {
		array_insert(r->slot, 0, r->entries, l->slot[l->entries - 1]);
	}

	/* Update parent position with a new separator index. */
	p->slot[pos] = get_bpn_key(l, l->entries - 1);

	l->entries--;
	r->entries++;
	return true;
}

static struct bpn *
bpn_merge_siblings(struct bpn *p, int pos)
{
	struct bpn *l = bpn_get_left(p, pos);
	struct bpn *r = bpn_get_right(p, pos);

	/* Adjust position. */
	if (pos == p->entries)
		pos--;

	if (is_bpn_internal(l)) {
		/* One extra for parent. */
		BUG_ON(l->entries + r->entries + 1 > MAX_ENTRIES);

		l->slot[l->entries] = p->slot[pos];
		array_copy(l->slot + l->entries + 1, r->slot, r->entries);
		array_copy(l->SUB_LINKS + l->entries + 1, r->SUB_LINKS, bpn_nr_sub_entries(r));
		l->entries += (r->entries + 1);
	} else {
		BUG_ON(l->entries + r->entries > MAX_ENTRIES);
		array_copy(l->slot + l->entries, r->slot, r->entries);
		list_del(&r->page.external.list);
		l->entries += r->entries;
	}

	/* Fix the parent. */
	array_move(p->slot, pos, pos + 1, p->entries);
	array_move(p->SUB_LINKS, pos + 1, pos + 2, bpn_nr_sub_entries(p));
	p->entries--;
	free(r);

	return l;
}

/*
 * "left" is a node which is split.
 */
static __always_inline void
bpn_split_internal(struct bpn *l, struct bpn *r)
{
	BUG_ON(!is_bpn_internal(l));

	/*
	 * Minus one entries is set because, during the split process
	 * of internal node, a separator key is __moved__ to the parent.
	 *
	 * See example:
	 *
	 *  3 5 7             (5)
	 * A B C D   ->    3       7
	 *                A B     C D
	 */
	r->entries = split(MAX_ENTRIES) - 1;
	l->entries = (MAX_ENTRIES - (r->entries + 1));

	/* Copy keys to the new node (right part). */
	array_copy(r->slot, l->slot + l->entries + 1, r->entries);

	/* Number of children in a node is node entries + 1. */
	array_copy(r->SUB_LINKS,
		l->SUB_LINKS + bpn_nr_sub_entries(l), bpn_nr_sub_entries(r));
}

static __always_inline void
bpn_split_external(struct bpn *l, struct bpn *r)
{
	BUG_ON(!is_bpn_external(l));

	/*
	 * During the split process of external node, a separator
	 * key is __copied__ to the parent. Example:
	 *
	 *  3 5 7              (5)
	 * A B C D   ->    3        5 7
	 *                A B      C   D
	 */
	r->entries = split(MAX_ENTRIES);
	l->entries = MAX_ENTRIES - r->entries;

	/* Copy keys to the new node (right part). */
	array_copy(r->slot, l->slot + l->entries, r->entries);

	/* Add a new entry to a double-linked list. */
	list_add(&r->page.external.list, &l->page.external.list);
}

static __always_inline void
bpn_split(struct bpn *n, struct bpn *parent, int pindex)
{
	struct bpn *right;
	ulong split_key;

	right = bpn_calloc_init(n->info.type);
	if (unlikely(!right))
		return;

	if (is_bpn_internal(n)) {
		bpn_split_internal(n, right);
		split_key = get_bpn_key(n, n->entries); /* will be moved. */
	} else {
		bpn_split_external(n, right);
		split_key = get_bpn_key(right, 0); /* is a __copy__. */
	}

	/* Move the new separator key to the parent node. */
	array_insert(parent->slot, pindex, parent->entries, split_key);

	/* new right kid(n) goes in pindex + 1 */
	array_insert(parent->SUB_LINKS, pindex + 1,
		bpn_nr_sub_entries(parent), (ulong) right);

	/* Set the parent for both kids */
	right->info.parent = n->info.parent = parent;
	parent->entries++;
}

/* Splits the root node */
static struct bpn *
bpn_split_root(struct bpn *root)
{
	struct bpn *new_root = bpn_calloc_init(BPN_TYPE_INTER);

	if (likely(new_root)) {
		/* Old root becomes left kid. */
		new_root->page.internal.sub_links[0] = root;
		bpn_split(root, new_root, 0);
	}

	return new_root;
}

static int
bpt_insert_non_full(struct bpt_root *root, record *r)
{
	struct bpn *n = root->node;
	struct bpn *p;
	pos_cc_t pos_cc;
	int pos;

	while (is_bpn_internal(n)) {
		pos_cc = bpn_bin_search(n, r->key, &pos);
		p = n;

		if (pos_cc == POS_CC_EQ)
			/* follow right direction. */
			n = n->SUB_LINKS[pos + 1];
		else
			/* follow left direction. */
			n = n->SUB_LINKS[pos];

		if (is_bpn_full(n)) {
			/*
			 * After split operation, the parent node(p) gets
			 * the separator key from the child node(n). The key
			 * is inserted into "pos" position of the parent(p).
			 */
			if (pos_cc == POS_CC_EQ)
				bpn_split(n, p, pos + 1);
			else
				bpn_split(n, p, pos);

			/*
			 * Please note, after split and updating the parent(p)
			 * with a new separator index, we might need to follow
			 * to the right direction. Check position and adjust a
			 * route.
			 */
			if (r->key >= p->slot[pos])
				n = p->SUB_LINKS[pos + 1];
		}
	}

	/* It is guaranteed "n" is not full. */
	(void) bpn_bin_search(n, r->key, &pos);
	return bpn_insert_to_leaf(n, pos, r);
}

int bpt_po_insert(struct bpt_root *root, record *r)
{
	struct bpn *n = root->node;

	if (is_bpn_full(n)) {
		n = bpn_split_root(n);
		if (unlikely(!n))
			return -1;

		/* Set a new root. */
		root->node = n;
	}

	return bpt_insert_non_full(root, r);
}

static inline struct bpn *
bpt_find_leaf(struct bpt_root *root, ulong val)
{
	struct bpn *n = root->node;
	pos_cc_t pos_cc;
	int pos;

	while (is_bpn_internal(n)) {
		pos_cc = bpn_bin_search(n, val, &pos);

		/* If true, "val" is located in a right sub-tree. */
		if (pos_cc == POS_CC_EQ)
			pos++;

		n = n->SUB_LINKS[pos];
	}

	return n;
}

struct record *bpt_lookup(struct bpt_root *root, ulong val, int *out_pos)
{
	struct bpn *n;
	pos_cc_t pos_cc;
	int pos;

	n = bpt_find_leaf(root, val);
	pos_cc = bpn_bin_search(n, val, &pos);

	if (out_pos)
		*out_pos = pos;

	/* return pos_cc == POS_CC_EQ ? (record *) n->slot[pos] : NULL; */
	return pos_cc == POS_CC_EQ ? get_bpn_val(n, pos) : NULL;
}

/* Preemptive overflow delete operation. */
struct record *bpt_po_delete(struct bpt_root *root, ulong val)
{
	struct bpn *n = root->node;
	struct bpn *parent = NULL;
	pos_cc_t pos_cc;
	int pos;

	while (1) {
		pos_cc = bpn_bin_search(n, val, &pos);

		/* Hit the bottom. */
		if (is_bpn_external(n))
			break;

		parent = n;

		/*
		 * If true, "r->val" is located in a right sub-tree.
		 */
		if (pos_cc == POS_CC_EQ)
			n = n->SUB_LINKS[pos + 1];
		else
			n = n->SUB_LINKS[pos];

		if (is_bpn_gt_min(n))
			continue;

		{
			struct bpn *l = bpn_get_left(parent, pos);
			struct bpn *r = bpn_get_right(parent, pos);
			bool balanced;

			if (l == n)
				balanced = bpn_try_shift_left(l, r, parent, pos);
			else
				balanced = bpn_try_shift_right(l, r, parent, pos);

			/* Need merging. */
			if (!balanced) {
				n = bpn_merge_siblings(parent, pos);

				/* Set a new parent. Can be a leaf node only. */
				if (!parent->entries && parent == root->node) {
					n->info.parent = NULL;
					root->node = n;
					free(parent);
				}
			}
		}
	}

	/* Success. */
	if (pos_cc == POS_CC_EQ)
		return bpn_remove_from_leaf(n, pos, val);

	return NULL;
}

int bpt_root_init(struct bpt_root *root)
{
	root->node = bpn_calloc_init(BPN_TYPE_EXTER);
	if (!root->node)
		return -1;

	list_init(&root->head);
	list_add(&root->node->page.external.list, &root->head);

	return 0;
}

void bpt_root_destroy(struct bpt_root *root)
{
	/* list_del(&root->node->page.external.list); */
	list_init(&root->head);
	free(root->node);
	root->node = NULL;
}
