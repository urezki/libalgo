#ifndef __ARRAY__
#define __ARRAY__

static inline void
array_move(void *array, size_t i, size_t j, size_t entries)
{
	ulong *a = (ulong *) array;
	memmove(a + i, a + j, sizeof(*a) * ((entries) - (j)));
}

static inline void
array_insert(void *array, size_t pos, size_t entries, ulong val)
{
	ulong *a = (ulong *) array;

	if (pos < entries)
		array_move(a, pos + 1, pos, entries);

	a[pos] = val;
}

static inline void
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

static inline void
array_copy(void *dst, void *src, size_t n)
{
	memcpy(dst, src, sizeof(ulong) * n);
}

#endif
