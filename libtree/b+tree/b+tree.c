#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "b+tree.h"
#include "array.h"

static struct node *
bp_calloc_node_init(u8 type)
{
	struct node *n = calloc(1, sizeof(*n));

	if (unlikely(!n))
		assert(0);

	n->info.type = type;

	if (type == BP_TYPE_EXTER)
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
bp_binary_search(struct node *n, ulong key, int *pos)
{
	pos_cc_t pos_cc;
	int l, r;

	check_node_geometry(n);

	l = -1;
	r = n->entries;

	/* invariant: a[lo] < key <= a[hi] */
	while (l + 1 < r) {
		int m = (l + r) >> 1;
		(n->slot[m] < key) ? (l = m):(r = m);
	}

	if (r < n->entries) {
		if (n->slot[r] == key)
			pos_cc = POS_CC_EQ;
		else
			pos_cc = POS_CC_LT;
	} else {
		pos_cc = POS_CC_GT;
	}

	if (pos)
		*pos = r;

	return pos_cc;
}

static __always_inline int
bp_insert_to_node(struct node *n, int pos, ulong val)
{
	BUG_ON(pos >= MAX_ENTRIES);

	/* First position can be garbage(if empty node). */
	if (n->entries)
		/* No duplicate keys. */
		if (unlikely(n->slot[pos] == val))
			return -1;

	array_insert(n->slot, pos, n->entries, val);
	n->entries++;
	return 0;
}

static __always_inline int
bp_remove_from_node(struct node *n, int pos, ulong val)
{
	BUG_ON(pos >= MAX_ENTRIES);

	/* Wrong position or value? */
	if (unlikely(n->slot[pos] != val))
		return -1;

	array_remove(n->slot, pos, n->entries);
	n->entries--;
	return 0;
}

/*
 * NOTE: it does not introduce dis-balance to left nor right nodes!
 */
static bool
bp_try_shift_left(struct node *l, struct node *r, struct node *p, int pos)
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
	if (pos >= p->entries || is_node_full(l) || !is_node_gt_min(r))
		return false;

	if (is_node_internal(l)) {
		l->slot[l->entries] = p->slot[pos];
		p->slot[pos] = r->slot[0];

		l->SUB_LINKS[l->entries + 1] = r->SUB_LINKS[0];
		array_move(r->SUB_LINKS, 0, 1, sub_entries(r));
	} else {
		/*
		 * TODO: leafs store only payload data. It does not keep
		 * indexes. An index can be extracted from the payload
		 * data in order to update parent.
		 */
		l->slot[l->entries] = r->slot[0];
		p->slot[pos] = r->slot[1];
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
bp_try_shift_right(struct node *l, struct node *r, struct node *p, int pos)
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
	if (pos >= p->entries || is_node_full(r) || !is_node_gt_min(l))
		return false;

	if (is_node_internal(l)) {
		array_insert(r->slot, 0, r->entries, p->slot[pos]);
		array_insert(r->SUB_LINKS, 0, r->entries + 1,
			(ulong) l->SUB_LINKS[l->entries]);
	} else {
		/*
		 * TODO: leafs store only payload data. It does not keep
		 * indexes. An index can be extracted from the payload
		 * data in order to update parent.
		 */
		array_insert(r->slot, 0, r->entries, l->slot[l->entries - 1]);
	}

	/* Update parent position with a new separator index. */
	p->slot[pos] = l->slot[l->entries - 1];

	l->entries--;
	r->entries++;
	return true;
}

static struct node *
bp_merge_siblings(struct node *p, int pos)
{
	struct node *l = bp_get_left_child(p, pos);
	struct node *r = bp_get_right_child(p, pos);

	/* Adjust position. */
	if (pos == p->entries)
		pos--;

	if (is_node_internal(l)) {
		/* One extra for parent. */
		BUG_ON(l->entries + r->entries + 1 > MAX_ENTRIES);

		l->slot[l->entries] = p->slot[pos];
		array_copy(l->slot + l->entries + 1, r->slot, r->entries);
		array_copy(l->SUB_LINKS + l->entries + 1, r->SUB_LINKS, sub_entries(r));
		l->entries += (r->entries + 1);
	} else {
		BUG_ON(l->entries + r->entries > MAX_ENTRIES);
		array_copy(l->slot + l->entries, r->slot, r->entries);
		list_del(&r->page.external.list);
		l->entries += r->entries;
	}

	/* Fix the parent. */
	array_move(p->slot, pos, pos + 1, p->entries);
	array_move(p->SUB_LINKS, pos + 1, pos + 2, sub_entries(p));
	p->entries--;
	free(r);

	return l;
}

/*
 * "left" is a node which is split.
 */
static __always_inline void
bp_split_internal_node(struct node *l, struct node *r)
{
	BUG_ON(!is_node_internal(l));

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
		l->SUB_LINKS + sub_entries(l), sub_entries(r));
}

static __always_inline void
bp_split_external_node(struct node *l, struct node *r)
{
	BUG_ON(!is_node_external(l));

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
bp_split_node(struct node *n, struct node *parent, int pindex)
{
	struct node *right;
	ulong split_key;

	right = bp_calloc_node_init(n->info.type);
	if (unlikely(!right))
		return;

	if (is_node_internal(n)) {
		bp_split_internal_node(n, right);
		split_key = n->slot[n->entries]; /* will be moved. */
	} else {
		bp_split_external_node(n, right);
		split_key = right->slot[0];	/* is a __copy__. */
	}

	/* Move the new separator key to the parent node. */
	array_insert(parent->slot, pindex, parent->entries, split_key);

	/* new right kid(n) goes in pindex + 1 */
	array_insert(parent->page.internal.sub_links,
		pindex + 1, parent->entries + 1, (ulong) right);

	/* Set the parent for both kids */
	right->info.parent = n->info.parent = parent;
	parent->entries++;
}

/* Splits the root node */
static struct node *
bp_split_root_node(struct node *root)
{
	struct node *new_root = bp_calloc_node_init(BP_TYPE_INTER);

	if (likely(new_root)) {
		/* Old root becomes left kid. */
		new_root->page.internal.sub_links[0] = root;
		bp_split_node(root, new_root, 0);
	}

	return new_root;
}

static int
bp_insert_non_full(struct bp_root *root, ulong val)
{
	struct node *n = root->node;
	int pos;

	while (is_node_internal(n)) {
		pos_cc_t pos_cc = bp_binary_search(n, val, &pos);

		/* If same key, bail out. */
		if (pos_cc == POS_CC_EQ)
			return -1;

		if (is_node_full(n->SUB_LINKS[pos])) {
			/*
			 * After split operation, the parent node (n) gets
			 * the separator key from the child node. The key
			 * is inserted into "pos" position of the parent.
			 */
			bp_split_node(n->SUB_LINKS[pos], n, pos);

			if (val > n->slot[pos])
				pos++;
		}

		n = n->SUB_LINKS[pos];
	}

	/* It is guaranteed "n" is not full. */
	(void) bp_binary_search(n, val, &pos);
	return bp_insert_to_node(n, pos, val);
}

int bp_po_insert(struct bp_root *root, ulong val)
{
	struct node *n = root->node;

	if (is_node_full(n)) {
		n = bp_split_root_node(n);
		if (unlikely(!n))
			return -1;

		/* Set a new root. */
		root->node = n;
	}

	return bp_insert_non_full(root, val);
}

struct node *bp_lookup(struct bp_root *root, ulong val, int *pos)
{
	struct node *n = root->node;
	pos_cc_t pos_cc;
	int index;

	while (is_node_internal(n)) {
		pos_cc = bp_binary_search(n, val, &index);

		/* If true, "val" is located in a right sub-tree. */
		if (pos_cc == POS_CC_EQ)
			index++;

		n = n->SUB_LINKS[index];
	}

	pos_cc = bp_binary_search(n, val, pos);
	return pos_cc == POS_CC_EQ ? n : NULL;
}

/* Preemptive overflow delete operation. */
bool bp_po_delete(struct bp_root *root, ulong val)
{
	struct node *n = root->node;
	struct node *parent = NULL;
	pos_cc_t pos_cc;
	int pos;

	while (1) {
		pos_cc = bp_binary_search(n, val, &pos);

		/* Hit the bottom. */
		if (is_node_external(n))
			break;

		parent = n;

		/*
		 * If true, "val" is located in a right sub-tree.
		 */
		if (pos_cc == POS_CC_EQ)
			n = n->SUB_LINKS[pos + 1];
		else
			n = n->SUB_LINKS[pos];

		if (is_node_gt_min(n))
			continue;

		{
			struct node *l = bp_get_left_child(parent, pos);
			struct node *r = bp_get_right_child(parent, pos);
			bool balanced;

			if (l == n)
				balanced = bp_try_shift_left(l, r, parent, pos);
			else
				balanced = bp_try_shift_right(l, r, parent, pos);

			/* Need merging. */
			if (!balanced) {
				n = bp_merge_siblings(parent, pos);

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
	if (pos_cc == POS_CC_EQ) {
		bp_remove_from_node(n, pos, val);
		return true;
	}

	return false;
}

int bp_root_init(struct bp_root *root)
{
	root->node = bp_calloc_node_init(BP_TYPE_EXTER);
	if (!root->node)
		return -1;

	list_init(&root->head);
	list_add(&root->node->page.external.list, &root->head);

	return 0;
}

void bp_root_destroy(struct bp_root *root)
{
	/* list_del(&root->node->page.external.list); */
	list_init(&root->head);
	free(root->node);
	root->node = NULL;
}
