#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "vm.h"
#include "debug.h"

static int
ascending_order(const void *a, const void *b)
{
	return (*(int *)a - *(int *)b);
}

static int
reverse_order(const void *a, const void *b)
{
	return (*(int *)b - *(int *)a);
}

static int
random_order(const void *a, const void *b)
{
	(void)a; (void)b;
	return rand() % 2 ? +1 : -1;
}

static void
shuffle(void *base, size_t nmemb, size_t size, int mask)
{
	srand(time(NULL));

	if (mask & 0x1) {
		qsort(base, nmemb, size, ascending_order);
	} else if (mask & 0x2) {
		qsort(base, nmemb, size, reverse_order);
	} else {
		qsort(base, nmemb, size, random_order);
	}
}

static unsigned long
rand_mask(unsigned long mask_len)
{
	srand(time(NULL));
	return (1UL << rand() % mask_len);
}

static inline void
time_now(struct timespec *t)
{
	(void) clock_gettime(CLOCK_MONOTONIC, t);
}

/*
 * returns differences in nanoseconds
 */
static inline unsigned long
time_diff(struct timespec *a, struct timespec *b)
{
	struct timespec res;
	int nsec;

	if (b->tv_nsec < a->tv_nsec) {
		nsec = (a->tv_nsec - b->tv_nsec) / 1000000000 + 1;
		a->tv_nsec -= 1000000000 * nsec;
		a->tv_sec += nsec;
	}

	res.tv_sec = b->tv_sec - a->tv_sec;
	res.tv_nsec = b->tv_nsec - a->tv_nsec;
	return (res.tv_sec * 1000000000) + res.tv_nsec;
}

static int
test_insert(struct bpt_root *root, vmap_area *areas, ulong *keys, ulong entries, int mask)
{
	char method[64] = {'\0'};
	struct timespec a, b;
	unsigned long max_nsec;
	unsigned long d, diff;
	int rv, i;

	if (!entries)
		return 0;

	shuffle(keys, entries, sizeof(ulong), mask);

	for (i = 0; i < entries; i++) {
		areas[i].va_start = keys[i];
		areas[i].va_end = areas[i].va_start + ((rand() % 5) + 1);
	}

	if (mask & 0x1)
		strncpy(method, "ascending", sizeof(method));
	else if (mask & 0x2)
		strncpy(method, "reverse", sizeof(method));
	else
		strncpy(method, "random", sizeof(method));

	for (i = 0, max_nsec = 0, d = 0; i < entries; i++) {
		time_now(&a);
		rv = bpt_po_insert(root, &areas[i]);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;

		/* Failed to insert. */
		if (rv) {
			fprintf(stdout, "error to insert: %lu, already exists: %d\n",
				areas[i].va_start, bpt_lookup(root, areas[i].va_start, NULL) ? 1:0);
			return -1;
		}
	}

#if 1
	/* average */
	d = d / i;
	fprintf(stdout, "insert(%s): %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		method, d, (float) d / 1000, max_nsec, bpt_high(root->node));
#endif
	return 0;
}

static int
test_lookup(struct bpt_root *root, ulong *keys, ulong entries, int mask)
{
	char method[64] = {'\0'};
	struct timespec a, b;
	unsigned long max_nsec;
	unsigned long d, diff;
	struct vmap_area *va;
	int i;

	if (!entries)
		return 0;

	shuffle(keys, entries, sizeof(ulong), mask);

	if (mask & 0x1)
		strncpy(method, "ascending", sizeof(method));
	else if (mask & 0x2)
		strncpy(method, "reverse", sizeof(method));
	else
		strncpy(method, "random", sizeof(method));

	for (i = 0, max_nsec = 0, d = 0; i < entries; i++) {
		time_now(&a);
		va = bpt_lookup(root, keys[i], NULL);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;

		/* Failed. */
		if (!va) {
			fprintf(stdout, "error to lookup: %lu\n", keys[i]);
			return -1;
		}
	}

#if 1
	/* average */
	d = d / i;
	fprintf(stdout, "lookup(%s): %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		method, d, (float) d / 1000, max_nsec, bpt_high(root->node));
#endif
	return 0;
}

static int
test_delete(struct bpt_root *root, ulong *array, ulong entries, int mask)
{
	char method[64] = {'\0'};
	struct timespec a, b;
	struct vmap_area *va;
	unsigned long max_nsec;
	unsigned long d, diff;
	int i;

	if (!entries)
		return 0;

	shuffle(array, entries, sizeof(ulong), mask);

	if (mask & 0x1)
		strncpy(method, "ascending", sizeof(method));
	else if (mask & 0x2)
		strncpy(method, "reverse", sizeof(method));
	else
		strncpy(method, "random", sizeof(method));

	for (i = 0, max_nsec = 0, d = 0; i < entries; i++) {
		time_now(&a);
		va = bpt_po_delete(root, array[i]);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;

		/* Failed. */
		if (!va) {
			fprintf(stdout, "error to delete: %lu\n", array[i]);
			return -1;
		}
	}

#if 1
	/* average */
	d = d / i;
	fprintf(stdout, "delete(%s): %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		method, d, (float) d / 1000, max_nsec, bpt_high(root->node));
#endif
	return 0;
}

static void
do_sanity_check(void)
{
	struct bpt_root root;
	struct vmap_area *areas;
	ulong *keys;
	unsigned long max_entries;
	int rv, i;

	rv = bpt_root_init(&root);
	if (rv < 0)
		assert(0);

	max_entries = 100000;

	areas = calloc(max_entries, sizeof(struct vmap_area));
	keys = calloc(max_entries, sizeof(unsigned long));
	BUG_ON(areas == NULL || keys == NULL);

	/* Initialize. */
	for (i = 0; i < max_entries; i++)
		keys[i] = i;

	for (i = 0; i < 1000000; i++) {
		unsigned long rnd_entries = rand() % max_entries;
		ulong val = keys[rand() % rnd_entries];
		struct vmap_area *va;

		fprintf(stdout, "-> Start exercise tree on %lu size...\n", rnd_entries);

		if (test_insert(&root, areas, keys, rnd_entries, rand_mask(3)))
			BUG();

		va = bpt_po_delete(&root, val);
		if (!va)
			BUG();

		if (bpt_po_insert(&root, va))
			BUG();

		if (val & 0x1) {
			if (test_delete(&root, keys, rnd_entries, rand_mask(3)))
				BUG();

			if (test_insert(&root, areas, keys, rnd_entries, rand_mask(3)))
				BUG();
		} else {
			if (test_lookup(&root, keys, rnd_entries, rand_mask(3)))
				BUG();
		}

		if (test_delete(&root, keys, rnd_entries, rand_mask(3)))
			BUG();
	}

	bpt_root_destroy(&root);
}

static int verify_meta(void)
{
	struct bpt_root root;
	struct vmap_area *areas;
	struct vmap_area *prev_va;
	struct list_head *pos;
	ulong *keys;
	unsigned long max_entries;
	ulong max_va_size;
	int rv, i, j;

	rv = bpt_root_init(&root);
	if (rv < 0)
		assert(0);

	max_entries = 10000000;
	max_va_size = 128;

	areas = calloc(max_entries, sizeof(struct vmap_area));
	BUG_ON(areas == NULL);

	for (i = 0, prev_va = NULL; i < max_entries; i++) {
		if (!prev_va) {
			areas[i].va_start = 1;
			areas[i].va_end = areas[i].va_start + ((rand() % max_va_size) + 1);
		} else {
			areas[i].va_start = prev_va->va_end + (rand() % 64);
			areas[i].va_end = areas[i].va_start + ((rand() % max_va_size) + 1);
		}

		prev_va = &areas[i];
	}

	printf("Start insert...\n");
	for (i = 0; i < max_entries; i++) {
		rv = bpt_po_insert(&root, &areas[i]);
		if (rv)
			BUG();
	}
	printf("Done!\n");
	j = 0;

	/* Now start verification. */
	list_for_each(pos, &root.head) {
		struct bpn *n = list_entry(pos, struct bpn, page.external.list);

		for (i = 0; i < n->entries; i++) {
			struct vmap_area *va = bpn_get_val(n, i);
			struct vmap_area *tmp = bpt_lookup_smallest(&root, va_size(va), va->va_start);

			if (tmp != va) {
				printf("-> %s, Failed!\n", __func__);
				return -1;
			}
		}

		j++;
	}

	printf("-> %s, Success, tree high: %d, leafs nr: %d!\n",
		__func__, bpt_high(root.node), j);

	return 0;
}

static struct vmap_area *
lin_lookup(struct bpt_root *root, ulong size, ulong vstart)
{
	struct vmap_area *va;
	struct list_head *pos;
	struct bpn *n;
	int i;

	/* Now start verification. */
	list_for_each(pos, &root->head) {
		n = list_entry(pos, struct bpn, page.external.list);

		for (i = 0; i < n->entries; i++) {
			va = bpn_get_val(n, i);

			if (is_within_this_va(va, size, 1, vstart))
				return va;
		}
	}

	return NULL;
}

static int verify_smallest_lookup(void)
{
	struct bpt_root root;
	struct vmap_area *areas;
	struct vmap_area *prev_va;
	struct vmap_area *va;
	struct bpn *n;
	ulong max_entries;
	ulong vend;
	ulong max_va_size;
	int rv, i;

	rv = bpt_root_init(&root);
	if (rv < 0)
		assert(0);

	max_entries = 100000;
	max_va_size = 32;

	areas = calloc(max_entries, sizeof(struct vmap_area));
	BUG_ON(areas == NULL);

	for (i = 0, prev_va = NULL; i < max_entries; i++) {
		if (!prev_va) {
			areas[i].va_start = 1;
			areas[i].va_end = areas[i].va_start + ((rand() % max_va_size) + 1);
		} else {
			areas[i].va_start = prev_va->va_end + (rand() % 64);
			areas[i].va_end = areas[i].va_start + ((rand() % max_va_size) + 1);
		}

		prev_va = &areas[i];
	}

	for (i = 0; i < max_entries; i++) {
		rv = bpt_po_insert(&root, &areas[i]);
		if (rv)
			BUG();
	}

	n = list_last_entry(&root.head, struct bpn, page.external.list);
	if (!n)
		BUG();

	va = bpn_get_val(n, n->entries - 1);
	vend = va->va_end;

	/* Now start test. */
	for (i = 0; i < max_entries; i++) {
		ulong vstart = rand() % vend;
		ulong size = rand() % max_va_size + 1;
		struct vmap_area *va_1, *va_2;

		va_1 = lin_lookup(&root, size, vstart);
		va_2 = bpt_lookup_smallest(&root, size, vstart);

		if (va_1 != va_2) {
			printf("-> NOT THE SAME!!!\n");

			printf("\t-> vstart: %lu, req-size: %lu\n",
				   vstart, size);

			if (va_1)
				printf("\t-> VA_1: va_start: %lu, va_end: %lu, size: %lu\n",
					va_1->va_start, va_1->va_end, va_size(va_1));

			if (va_2)
				printf("\t-> VA_2: va_start: %lu, va_end: %lu, size: %lu\n",
					va_2->va_start, va_2->va_end, va_size(va_2));
		}

		if (!(i % 5000))
			printf("-> Loops: %d\n", i);
	}

	return 0;
}

int main(int argc, char **argv)
{
	do_sanity_check();
	verify_meta();
	verify_smallest_lookup();
}
