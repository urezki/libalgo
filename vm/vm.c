#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "vm.h"				/* Main header */
#include "vm_ops.h"
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

static __always_inline int
bpn_insert_to_leaf(struct bpn *n, int pos, vmap_area *va)
{
	struct vmap_area *sibling;

	BUG_ON(pos >= MAX_ENTRIES);

	/* Sanity check for the right. */
	if (pos < n->entries) {
		sibling = bpn_get_val(n, pos);

		if (unlikely(va->va_end > sibling->va_start))
			return -1;
	}

	/* Sanity check for the left. */
	if (pos > 0) {
		sibling = bpn_get_val(n, pos - 1);

		if (unlikely(va->va_start < sibling->va_end))
			return -1;
	}

	slot_insert(n, pos, (ulong) va);
	n->entries++;
	return 0;
}

static __always_inline
vmap_area *bpn_remove_from_leaf(struct bpn *n, int pos, ulong key)
{
	vmap_area *va;

	BUG_ON(pos >= MAX_ENTRIES);

	/* Wrong position or value? */
	if (unlikely(bpn_get_key(n, pos) != key))
		return NULL;

	va = bpn_get_val(n, pos);
	slot_remove(n, pos);
	n->entries--;
	return va;
}

/*
 * NOTE: it does not introduce dis-balance to left nor right nodes!
 */
bool
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

		/* Update links. */
		l->SUB_LINKS[l->entries + 1] = r->SUB_LINKS[0];
		subl_move(r, 0, 1);

		/* Update sub-avail. */
		l->SUB_AVAIL[l->entries + 1] = r->SUB_AVAIL[0];
		suba_move(r, 0, 1);

		/* Update sub-parent. */
		((struct bpn *) l->SUB_LINKS[l->entries + 1])->info.parent = l;
	} else {
		l->slot[l->entries] = r->slot[0];
		p->slot[pos] = bpn_get_key(r, 1);
	}

	/* Remove the element from right node. */
	slot_move(r, 0, 1);

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
bool
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
		slot_insert(r, 0, p->slot[pos]);
		subl_insert(r, 0, l->SUB_LINKS[l->entries]);
		suba_insert(r, 0, l->SUB_AVAIL[l->entries]);

		((struct bpn *) r->SUB_LINKS[0])->info.parent = r;
	} else {
		slot_insert(r, 0, l->slot[l->entries - 1]);
	}

	/* Update parent position with a new separator index. */
	p->slot[pos] = bpn_get_key(l, l->entries - 1);

	l->entries--;
	r->entries++;
	return true;
}

static struct bpn *
bpn_merge_siblings(struct bpn *p, int pos)
{
	struct bpn *l = bpn_get_left(p, pos);
	struct bpn *r = bpn_get_right(p, pos);
	int i;

	/* Adjust position. */
	if (pos == p->entries)
		pos--;

	if (is_bpn_internal(l)) {
		/* One extra for parent. */
		BUG_ON(l->entries + r->entries + 1 > MAX_ENTRIES);

		l->slot[l->entries] = p->slot[pos];
		slot_copy(l, l->entries + 1, r, 0, r->entries);
		subl_copy(l, nr_sub_entries(l), r, 0, nr_sub_entries(r));
		suba_copy(l, nr_sub_entries(l), r, 0, nr_sub_entries(r));

		/* TODO: Move into separate function. */
		for (i = 0; i < r->entries + 1; i++)
			((struct bpn *) r->SUB_LINKS[i])->info.parent = l;

		l->entries += (r->entries + 1);
	} else {
		BUG_ON(l->entries + r->entries > MAX_ENTRIES);
		slot_copy(l, l->entries, r, 0, r->entries);
		list_del(&r->page.external.list);
		l->entries += r->entries;
	}

	/* Shrink it. -1 element in the parent. */
	slot_move(p, pos, pos + 1);
	subl_move(p, pos + 1, pos + 2);
	suba_move(p, pos + 1, pos + 2);
	/* suba_move(p, pos, pos + 1); */
	/* printf("-> update SUB_AVAIL: copy to %d from %d pos\n", pos + 1, pos + 2); */

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
	int i;

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
	slot_copy(r, 0, l, l->entries + 1, r->entries);
	subl_copy(r, 0, l, nr_sub_entries(l), nr_sub_entries(r));
	suba_copy(r, 0, l, nr_sub_entries(l), nr_sub_entries(r));

	/*
	 * "r" is a new node. Its SUB-nodes still point to a node "l"
	 * that has been split here. Therefore we need to update SUB
	 * nodes parent pointers for which the "r" is a new parent.
	 *
	 * Only valid for internal nodes.
	 */
	for (i = 0; i < r->entries + 1; i++)
		((struct bpn *) r->SUB_LINKS[i])->info.parent = r;
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
	slot_copy(r, 0, l, l->entries, r->entries);
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
		split_key = bpn_get_key(n, n->entries); /* will be moved. */
	} else {
		bpn_split_external(n, right);
		split_key = bpn_get_key(right, 0); /* is a __copy__. */
	}

	/* Move the new separator key to the parent node. */
	slot_insert(parent, pindex, split_key);

	/* new right kid(n) goes in pindex + 1 */
	subl_insert(parent, pindex + 1, right);

	/* Update left avail. */
	array_insert(parent->SUB_AVAIL, pindex,
		parent->entries + 1, bpn_max_avail(parent->SUB_LINKS[pindex]));

	/* Update right avail. */
	parent->SUB_AVAIL[pindex + 1] =
		bpn_max_avail(parent->SUB_LINKS[pindex + 1]);

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
		new_root->SUB_LINKS[0] = root;
		bpn_split(root, new_root, 0);
	}

	return new_root;
}

static int
bpt_insert_non_full(struct bpt_root *root, struct vmap_area *va)
{
	struct bpn *n = root->node;
	struct bpn *p;
	pos_cc_t pos_cc;
	bool merged;
	int pos, rv;

	while (is_bpn_internal(n)) {
		pos_cc = bpn_bin_search(n, va->va_start, &pos);
		p = n;

		if (pos_cc == POS_CC_EQ) {
			/* Follow right. */
			n->info.ppos = pos + 1;
			n = n->SUB_LINKS[pos + 1];
		} else {
			/* Follow left. */
			n->info.ppos = pos;
			n = n->SUB_LINKS[pos];
		}

		if (is_bpn_full(n)) {
			/*
			 * After split operation, the parent node(p) gets
			 * the separator key from the child node(n). The key
			 * is inserted into "pos" position of the parent(p).
			 */
			bpn_split(n, p, pos_cc == POS_CC_EQ ? pos + 1 : pos);

			/*
			 * Please note, after split and updating the parent(p)
			 * with a new separator index, we might need to follow
			 * to the right direction. Check position and adjust a
			 * route.
			 */
			if (va->va_start >= p->slot[pos]) {
				n = p->SUB_LINKS[pos + 1];
				p->info.ppos = pos + 1;
			}
		}
	}

	/* It is guaranteed "n" is not full. */
	(void) bpn_bin_search(n, va->va_start, &pos);
	merged = try_merge_va(root, n, va, pos);

	if (!merged) {
		rv = bpn_insert_to_leaf(n, pos, va);
		if (!rv)
			fixup_metadata(n);
	}

	return (merged || !rv) ? 0 : -1;
}

int bpt_po_insert(struct bpt_root *root, vmap_area *va)
{
	struct bpn *n = root->node;

	if (is_bpn_full(n)) {
		n = bpn_split_root(n);
		if (unlikely(!n))
			return -1;

		/* Set a new root. */
		root->node = n;
	}

	return bpt_insert_non_full(root, va);
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

void *bpt_lookup(struct bpt_root *root, ulong val, int *out_pos)
{
	struct bpn *n;
	pos_cc_t pos_cc;
	int pos;

	n = bpt_find_leaf(root, val);
	pos_cc = bpn_bin_search(n, val, &pos);

	if (out_pos)
		*out_pos = pos;

	return pos_cc == POS_CC_EQ ? bpn_get_val(n, pos) : NULL;
}

static inline struct vmap_area *
leaf_get_va_cond(struct bpn *n, ulong size, ulong align, ulong vstart)
{
	struct vmap_area *va;
	int i;

	if (likely(is_bpn_external(n))) {
		for (i = 0; i < n->entries; i++) {
			va = bpn_get_val(n, i);

			if (is_within_this_va(va, size, align, vstart))
				return va;
		}
	}

	return NULL;
}

/* Preemptive overflow delete operation. */
struct vmap_area *
bpt_po_delete(struct bpt_root *root, ulong val)
{
	struct bpn *n = root->node;
	struct vmap_area *va = NULL;
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
		if (pos_cc == POS_CC_EQ) {
			n->info.ppos = pos + 1;
			n = n->SUB_LINKS[pos + 1];
		} else {
			n->info.ppos = pos;
			n = n->SUB_LINKS[pos];
		}

		if (is_bpn_gt_min(n))
			continue;

		{
			struct bpn *l = bpn_get_left(parent, pos);
			struct bpn *r = bpn_get_right(parent, pos);
			int lpos = (pos < parent->entries) ? pos : pos - 1;
			int rpos = (pos < parent->entries) ? pos + 1 : pos;
			bool balanced;

			if (l == n)
				balanced = bpn_try_shift_left(l, r, parent, pos);
			else
				balanced = bpn_try_shift_right(l, r, parent, pos);

			if (balanced) {
				parent->SUB_AVAIL[lpos] = bpn_max_avail(l);
				parent->SUB_AVAIL[rpos] = bpn_max_avail(r);
			} else {
				/* Need merging. */
				n = bpn_merge_siblings(parent, pos);
				parent->SUB_AVAIL[lpos] = bpn_max_avail(n);
				parent->info.ppos = lpos;

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
	va = (pos_cc == POS_CC_EQ) ?
		bpn_remove_from_leaf(n, pos, val) : NULL;

	if (va)
		fixup_metadata(n);

	return va;
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
