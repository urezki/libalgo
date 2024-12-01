#ifndef __ARRAY__
#define __ARRAY__

static __always_inline void
array_move(void *array, size_t i, size_t j, size_t entries)
{
	ulong *a = (ulong *) array;
	memmove(a + i, a + j, sizeof(*a) * ((entries) - (j)));
}

static __always_inline void
array_insert(void *array, size_t pos, size_t entries, ulong val)
{
	ulong *a = (ulong *) array;

	if (pos < entries)
		array_move(a, pos + 1, pos, entries);

	a[pos] = val;
}

static __always_inline void
array_remove(void *array, size_t pos, size_t entries)
{
	ulong *a = (ulong *) array;

	/*
	 * No need to move array if last entry. For example.
	 * An array with 4 entries, if we remove 9 number of
	 * entries should be updated only:
	 *
	 * pos: 0 1 2 3
	 *     |2|3|4|9|
	 */
	if (pos + 1 < entries)
		array_move(a, pos, pos + 1, entries);
}

static __always_inline void
array_copy(void *dst, void *src, size_t n)
{
	memcpy(dst, src, sizeof(ulong) * n);
}

static __always_inline void
slot_insert(struct bpn *n, size_t pos, ulong val)
{
	BUG_ON(pos >= MAX_ENTRIES);
	array_insert(n->slot, pos, n->entries, val);
}

static __always_inline void
slot_remove(struct bpn *n, size_t pos)
{
	BUG_ON(pos >= MAX_ENTRIES);
	array_remove(n->slot, pos, n->entries);
}

static __always_inline void
slot_move(struct bpn *n, size_t i, size_t j)
{
	array_move(n->slot, i, j, n->entries);
}

static __always_inline void
slot_copy(struct bpn *dst, size_t i, struct bpn *src, size_t j, size_t entries)
{
	array_copy(dst->slot + i, src->slot + j, entries);
}

static __always_inline void
subl_insert(struct bpn *n, size_t pos, void *val)
{
	BUG_ON(pos >= MAX_CHILDREN);
	array_insert(n->SUB_LINKS, pos, nr_sub_entries(n), (ulong) val);
}

static __always_inline void
subl_move(struct bpn *n, size_t i, size_t j)
{
	array_move(n->SUB_LINKS, i, j, nr_sub_entries(n));
}

static __always_inline void
subl_copy(struct bpn *dst, size_t i, struct bpn *src, size_t j, size_t entries)
{
	array_copy(dst->SUB_LINKS + i, src->SUB_LINKS + j, entries);
}

#endif
