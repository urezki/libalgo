#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "vm.h"
#include "vm_ops.h"

ulong bpn_max_avail(struct bpn *n)
{
	ulong avail = 0;
	int i;

	if (is_bpn_internal(n)) {
		for (i = 0; i < n->entries + 1; i++) {
			if (n->SUB_AVAIL[i] > avail)
				avail = n->SUB_AVAIL[i];
		}
	} else {
		/* Open up all VAs in a node(cache misses). */
		for (i = 0; i < n->entries; i++) {
			struct vmap_area *va = bpn_get_val(n, i);

			if (va_size(va) > avail)
				avail = va_size(va);
		}
	}

	return avail;
}

void fixup_metadata(struct bpn *node)
{
	struct bpn *parent;
	ulong max_avail;
	u8 child_index;

	while (node->info.parent) {
		parent = node->info.parent;
		child_index = parent->info.ppos;

		max_avail = bpn_max_avail(node);
		if (parent->SUB_AVAIL[child_index] == max_avail)
			break;

		parent->SUB_AVAIL[child_index] = max_avail;
		node = parent;
	}
}

void fixup_subavail(struct bpn *n, ulong va_start)
{
	struct bpn *parent;
	ulong max_avail;
	pos_cc_t pos_cc;
	int pos;

	while (n->info.parent) {
		parent = n->info.parent;
		pos_cc = bpn_bin_search(parent, va_start, &pos);

		if (pos_cc == POS_CC_EQ)
			pos++;

		max_avail = bpn_max_avail(n);
		if (parent->SUB_AVAIL[pos] == max_avail)
			break;

		parent->SUB_AVAIL[pos] = max_avail;
		n = parent;
	}
}

#define BIT(pos) (1 << (pos))
#define SET_BIT(x, pos) ((x) |= (BIT(pos)))
#define TEST_BIT(x, pos) (((x) & BIT(pos)) >> pos)

/* BIT states. */
typedef enum {
	MERGE_WITH_LEFT =  0,
	MERGE_WITH_RIGHT = 1,
	MERGE_WITH_LEFT_LEAF = 2,
	MERGE_WITH_RIGHT_LEAF = 3,
} merge_state;

static __always_inline void
can_merge_to_left(struct bpn *n, struct vmap_area *va,
		int pos, merge_state *ms)
{
	int sibling_pos = pos - 1;
	struct vmap_area *sibling;

	if (sibling_pos >= 0 && sibling_pos < n->entries) {
		sibling = bpn_get_val(n, sibling_pos);

		if (sibling->va_end == va->va_start)
			SET_BIT(*ms, MERGE_WITH_LEFT);
	}
}

static __always_inline void
can_merge_to_right(struct bpn *n, struct vmap_area *va,
		int pos, merge_state *ms)
{
	int sibling_pos = pos;
	struct vmap_area *sibling;

	if (sibling_pos >= 0 && sibling_pos < n->entries) {
		sibling = bpn_get_val(n, sibling_pos);

		if (sibling->va_start == va->va_end)
			SET_BIT(*ms, MERGE_WITH_RIGHT);
	}
}

static __always_inline void
can_merge_to_left_leaf(struct bpt_root *root, struct bpn *n,
		struct vmap_area *va, int pos, merge_state *ms)
{
	struct vmap_area *leaf_va;

	if (!pos) {
		leaf_va = leaf_prev_last_entry(root, n);

		if (leaf_va) {
			if (leaf_va->va_end == va->va_start)
				SET_BIT(*ms, MERGE_WITH_LEFT_LEAF);
		}
	}
}

static __always_inline void
can_merge_to_right_leaf(struct bpt_root *root, struct bpn *n,
		struct vmap_area *va, int pos, merge_state *ms)
{
	struct vmap_area *leaf_va;

	if (pos == n->entries) {
		leaf_va = leaf_next_first_entry(root, n);

		if (leaf_va) {
			if (va->va_end == leaf_va->va_start)
				SET_BIT(*ms, MERGE_WITH_RIGHT_LEAF);
		}
	}
}

static __always_inline merge_state
get_va_merge_state(struct bpt_root *root, struct bpn *n,
		struct vmap_area *va, int pos)
{
	merge_state ms = 0;

	can_merge_to_left(n, va, pos, &ms);
	can_merge_to_right(n, va, pos, &ms);

	if (!TEST_BIT(ms, MERGE_WITH_LEFT) ||
			!TEST_BIT(ms, MERGE_WITH_RIGHT)) {
		can_merge_to_left_leaf(root, n, va, pos, &ms);
		can_merge_to_right_leaf(root, n, va, pos, &ms);
	}

	return ms;
}

static __always_inline bool
do_merge_va(struct bpt_root *root, struct bpn *n,
		struct vmap_area *va, int pos, merge_state ms,
		struct vmap_area **out)
{
	struct vmap_area *left;
	struct vmap_area *right;
	struct bpn *rl, *ll, *p;

	*out = NULL;

	/*
	 *    L          R
	 * |-----| VA |-----|
	 *    N1 |----|  N2
	 */
	if (TEST_BIT(ms, MERGE_WITH_LEFT) && TEST_BIT(ms, MERGE_WITH_RIGHT)) {
		left = bpn_get_val(n, pos - 1);
		right = bpn_get_val(n, pos);

		left->va_end = right->va_end;
		fixup_metadata(n);
		*out = right;
	} else {
		if (TEST_BIT(ms, MERGE_WITH_LEFT)) {
			left = bpn_get_val(n, pos - 1);
			BUG_ON(left->va_end != va->va_start);

			if (TEST_BIT(ms, MERGE_WITH_RIGHT_LEAF)) {
				va->va_start = left->va_start;
				*out = left;
				return true;
			} else {
				left->va_end = va->va_end;
				fixup_metadata(n);
			}
		} else if (TEST_BIT(ms, MERGE_WITH_RIGHT)) {
			right = bpn_get_val(n, pos);
			BUG_ON(va->va_end != right->va_start);

			if (TEST_BIT(ms, MERGE_WITH_LEFT_LEAF)) {
				left = leaf_prev_last_entry(root, n);
				BUG_ON(left->va_end != va->va_start);

				va->va_end = right->va_end;
				*out = right;
				return true;
			} else {
				right->va_start = va->va_start;
				fixup_metadata(n);
			}
		} else {
			/*
			 * Merge VA {40960-73728} with a left leaf:
			 *
			 *            40960                       40960
			 *     VA_L          VA_R     ->    VA_L          VA_R
			 * {4096-40960} {98304-102400}  {4096-73728} {98304-102400}
			 *
			 * We need to update a parent key 40960 to 98304. Otherwise
			 * allocator will violate a split key by moving va_start over
			 * the 40960 split value.
			 */
			if (TEST_BIT(ms, MERGE_WITH_LEFT_LEAF)) {
				ll = leaf_prev_or_null(root, n);
				left = bpn_get_val(ll, ll->entries - 1);
				right = bpn_get_val(n, 0);

				for (p = n->info.parent; p; p = p->info.parent) {
					pos = p->info.ppos;

					if (p->info.ppos > 0)
						pos = p->info.ppos - 1;

					/* Find a common parent and split key. */
					if (p->slot[pos] > right->va_start)
						continue;

					/* Merge and break. */
					left->va_end = va->va_end;
					p->slot[pos] = right->va_start;
					fixup_subavail(ll, left->va_start);
					break;
				}
			} else if (TEST_BIT(ms, MERGE_WITH_RIGHT_LEAF)) {
				rl = leaf_next_or_null(root, n);
				right = bpn_get_val(rl, 0);

				for (p = n->info.parent; p; p = p->info.parent) {
					pos = p->info.ppos;

					if (p->info.ppos == p->entries)
						pos = p->info.ppos - 1;

					/* Find a common parent. */
					if (p->slot[pos] < right->va_start)
						continue;

					/* Merge and break. */
					right->va_start = va->va_start;
					p->slot[pos] = right->va_start;
					fixup_subavail(rl, right->va_start);
					break;
				}
			}
		}
	}

	free(va);
	return false;
}

bool try_merge_va(struct bpt_root *root, struct bpn *n,
		struct vmap_area *va, int pos)
{
	struct vmap_area *out;
	pos_cc_t pos_cc;
	merge_state ms;
	bool repeat;
	int rv;

	/* Validate VA and position. */
	rv = validate_insert_req(n, pos, va);
	if (unlikely(rv))
		return false;

	ms = get_va_merge_state(root, n, va, pos);
	if (ms) {
		repeat = do_merge_va(root, n, va, pos, ms, &out);

		if (out) {
			out = bpt_po_delete(root, out->va_start);
			BUG_ON(!out);
			free(out);
		}

		/* We need to find a node again after bpt_po_delete(). */
		if (repeat) {
			n = root->node;

			while (is_bpn_internal(n)) {
				pos_cc = bpn_bin_search(n, va->va_start, &pos);

				if (pos_cc == POS_CC_EQ) {
					/* Follow right. */
					n->info.ppos = pos + 1;
					n = n->SUB_LINKS[pos + 1];
				} else {
					/* Follow left. */
					n->info.ppos = pos;
					n = n->SUB_LINKS[pos];
				}
			}

			(void) bpn_bin_search(n, va->va_start, &pos);
			ms = get_va_merge_state(root, n, va, pos);
			repeat = do_merge_va(root, n, va, pos, ms, &out);

			BUG_ON(repeat);
			BUG_ON(out);
		}

		return true;
	}

	return false;
}

/*
 * If success, it return a leaf that satisfies va->va_start >= vstart
 * condition, also it guarantees that there is a VA that is equal or
 * greater of given "size". Otherwise the most right(last) leaf is
 * returned.
 */
struct bpn *
bpt_lookup_lowest_leaf(struct bpt_root *root,
		ulong length, ulong vstart)
{
	struct bpn *n = root->node;
	int i;

	/* Find a leaf. */
	while (is_bpn_internal(n)) {
		for (i = 0; i < n->entries; i++) {
			if (vstart < n->slot[i] && n->SUB_AVAIL[i] >= length)
				break;
		}

		n->info.ppos = i;
#if 0
		ulong max_avail = bpn_max_avail(n->SUB_LINKS[i]);
		if (max_avail != n->SUB_AVAIL[i]) {
			printf("!!!!! TREE IS CORRUPTED !!!!! %lu != %lu\n",
				max_avail, n->SUB_AVAIL[i]);
			/* dump_tree(r); */
			/* exit(-1); */
		}
#endif
		n = n->SUB_LINKS[i];
	}

	return n;
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

static struct vmap_area *
lin_lookup_smallest_va(struct bpt_root *root, ulong size,
		ulong align, ulong vstart, struct bpn **out)
{
	struct vmap_area *va;
	struct list_head *pos;
	struct bpn *n;
	ulong length;
	int i;

	length = (align > PAGE_SIZE) ? size + align - 1:size;

	/* Now start verification. */
	list_for_each(pos, &root->head) {
		n = list_entry(pos, struct bpn, page.external.list);

		for (i = 0; i < n->entries; i++) {
			va = bpn_get_val(n, i);
			if (va_size(va) < length)
				continue;

			/* Lowest possible VA. */
			if (is_within_this_va(va, size, align, vstart)) {
				if (out)
					*out = n;
				return va;
			}
		}
	}

	return NULL;
}

static __always_inline bool
first_next_sub_avail(struct bpn *n, ulong length, ulong *vstart)
{
	int i;

	while ((n = n->info.parent)) {
		for (i = n->info.ppos + 1; i < n->entries + 1; i++) {
			if (n->SUB_AVAIL[i] >= length) {
				/* Update "vstart" to a new sub-tree start address. */
				*vstart = n->slot[i - 1];
				return true;
			}
		}
	}

	/* No any space avail. */
	return false;
}

struct vmap_area *
lookup_smallest_va(struct bpt_root *root, ulong size,
	ulong align, ulong vstart, struct bpn **out)
{
	struct bpn *n = root->node;
	struct vmap_area *va;
	bool is_sub_avail;
	ulong length;
	int i;

	length = (align > PAGE_SIZE) ? size + align - 1:size;

	/* We can repeat only once! */
	for (i = 0; i < 2; i++) {
		/* Find a leaf. */
		n = bpt_lookup_lowest_leaf(root, length, vstart);

		/* Check, if there is an appropriate VA. */
		va = leaf_get_va_cond(n, size, align, vstart);
		if (va) {
			*out = n;
			return va;
		}

		/*
		 * No. Reasons:
		 * - "vstart" restriction;
		 * - no VA available for a given length.
		 */
		is_sub_avail = first_next_sub_avail(n, length, &vstart);
		if (unlikely(!is_sub_avail))
			break;
	}

	return NULL;
}

enum fit_type {
	NOTHING_FIT = 0,
	FL_FIT_TYPE = 1,	/* full fit */
	LE_FIT_TYPE = 2,	/* left edge fit */
	RE_FIT_TYPE = 3,	/* right edge fit */
	NE_FIT_TYPE = 4		/* no edge fit */
};

static __always_inline enum fit_type
classify_va_fit_type(struct vmap_area *va,
	ulong nva_start_addr, ulong size)
{
	enum fit_type type;

	/* Check if it is within VA. */
	if (nva_start_addr < va->va_start ||
			nva_start_addr + size > va->va_end)
		return NOTHING_FIT;

	/* Now classify. */
	if (va->va_start == nva_start_addr) {
		if (va->va_end == nva_start_addr + size)
			type = FL_FIT_TYPE;
		else
			type = LE_FIT_TYPE;
	} else if (va->va_end == nva_start_addr + size) {
		type = RE_FIT_TYPE;
	} else {
		type = NE_FIT_TYPE;
	}

	return type;
}

static __always_inline int
va_clip(struct bpt_root *root, struct vmap_area *va,
		ulong nva_start_addr, ulong size, struct bpn *node)
{
	enum fit_type type = classify_va_fit_type(va, nva_start_addr, size);
	struct vmap_area *lva = NULL;

	if (type == FL_FIT_TYPE) {
		/*
		 * No need to split VA, it fully fits.
		 *
		 * |               |
		 * V      NVA      V
		 * |---------------|
		 */
		va = bpt_po_delete(root, va->va_start);
		BUG_ON(va == NULL);
		free(va);
	} else if (type == LE_FIT_TYPE) {
		/*
		 * Split left edge of fit VA.
		 *
		 * |       |
		 * V  NVA  V   R
		 * |-------|-------|
		 */
		va->va_start += size;
	} else if (type == RE_FIT_TYPE) {
		/*
		 * Split right edge of fit VA.
		 *
		 *         |       |
		 *     L   V  NVA  V
		 * |-------|-------|
		 */
		va->va_end = nva_start_addr;
	} else if (type == NE_FIT_TYPE) {
		/*
		 * Split no edge of fit VA.
		 *
		 *     |       |
		 *   L V  NVA  V R
		 * |---|-------|---|
		 */
		lva = malloc(sizeof(*lva));

		/*
		 * Build the remainder.
		 */
		lva->va_start = va->va_start;
		lva->va_end = nva_start_addr;

		/*
		 * Shrink this VA to remaining size.
		 */
		va->va_start = nva_start_addr + size;
	} else {
		return -1;
	}

	if (type != FL_FIT_TYPE) {
		fixup_metadata(node);

		if (lva)	/* type == NE_FIT_TYPE */
			bpt_po_insert(root, lva);
	}

	return 0;
}

static ulong
va_alloc(struct bpt_root *root, ulong size,
		ulong align, ulong vstart, ulong vend)
{
	struct vmap_area *va, *tmp;
	ulong nva_start_addr;
	struct bpn *node;
	int ret;

	/* va = lin_lookup_smallest_va(root, size, align, vstart, &node); */
	va = lookup_smallest_va(root, size, align, vstart, &node);
	if (!va)
		return vend;
#if DEBUG
	if (align <= PAGE_SIZE) {
		tmp = lin_lookup_smallest_va(root, size, align, vstart, NULL);
		if (va != tmp) {
			printf("-> Not the same: %lu-%lu, %lu-%lu, size: %lu, "
				"align: %lu, vstart: %lu\n",
				va->va_start, va->va_end,
				tmp->va_start, tmp->va_end,
				size, align, vstart);
		}
	}
#endif
	if (va->va_start > vstart)
		nva_start_addr = ALIGN(va->va_start, align);
	else
		nva_start_addr = ALIGN(vstart, align);

	/* Check the "vend" restriction. */
	if (nva_start_addr + size > vend)
		return vend;

	/* Update the free vmap_area. */
	ret = va_clip(root, va, nva_start_addr, size, node);
	if (ret)
		return vend;

	return nva_start_addr;
}

int vm_init_free_space(struct bpt_root *root, ulong vstart, ulong vend)
{
	struct vmap_area *va;
	int rv;

	rv = bpt_root_init(root);
	if (rv < 0)
		assert(0);

	va = malloc(sizeof(*va));
	if (!va)
		assert(0);

	va->va_start = vstart;
	va->va_end = vend;
	return bpt_po_insert(root, va);
}

struct vmap_area *
alloc_vmap_area(struct bpt_root *root, ulong size,
		ulong align, ulong vstart, ulong vend)
{
	struct vmap_area *va;
	ulong addr;

	va = malloc(sizeof(*va));
	if (unlikely(!va))
		return NULL;

	addr = va_alloc(root, size, align, vstart, vend);
	if (addr == vend) {
		free(va);
		return NULL;
	}

	va->va_start = addr;
	va->va_end = addr + size;
	return va;
}

int free_vmap_area(struct bpt_root *root, struct vmap_area *va)
{
	if (unlikely(!va))
		return -1;

	return bpt_po_insert(root, va);
}
