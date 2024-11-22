#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "b+tree.h"
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
test_insert(struct bp_root *root, unsigned long *array, unsigned long entries, int mask)
{
	char method[64] = {'\0'};
	struct timespec a, b;
	unsigned long max_nsec;
	unsigned long d, diff;
	int rv, i;

	if (!entries)
		return -1;

	shuffle(array, entries, sizeof(unsigned long), mask);

	if (mask & 0x1)
		strncpy(method, "ascending", sizeof(method));
	else if (mask & 0x2)
		strncpy(method, "reverse", sizeof(method));
	else
		strncpy(method, "random", sizeof(method));

	for (i = 0, max_nsec = 0, d = 0; i < entries; i++) {
		time_now(&a);
		rv = bp_po_insert(root, array[i]);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;

		/* Failed to insert. */
		if (rv) {
			fprintf(stdout, "error to insert: %lu, already exists: %d\n",
				array[i], bp_lookup(root, array[i], NULL) ? 1:0);
			return -1;
		}
	}

#if 1
	/* average */
	d = d / i;
	fprintf(stdout, "insert(%s): %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		method, d, (float) d / 1000, max_nsec, bp_tree_high(root->node));
#endif
	return 0;
}

static int
test_lookup(struct bp_root *root, unsigned long *array, unsigned long entries, int mask)
{
	char method[64] = {'\0'};
	struct timespec a, b;
	unsigned long max_nsec;
	unsigned long d, diff;
	struct node *n;
	int i;

	if (!entries)
		return -1;

	shuffle(array, entries, sizeof(unsigned long), mask);

	if (mask & 0x1)
		strncpy(method, "ascending", sizeof(method));
	else if (mask & 0x2)
		strncpy(method, "reverse", sizeof(method));
	else
		strncpy(method, "random", sizeof(method));

	for (i = 0, max_nsec = 0, d = 0; i < entries; i++) {
		time_now(&a);
		n = bp_lookup(root, array[i], NULL);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;

		/* Failed. */
		if (!n) {
			fprintf(stdout, "error to lookup: %lu\n", array[i]);
			return -1;
		}
	}

#if 1
	/* average */
	d = d / i;
	fprintf(stdout, "lookup(%s): %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		method, d, (float) d / 1000, max_nsec, bp_tree_high(root->node));
#endif
	return 0;
}

static int
test_delete(struct bp_root *root, unsigned long *array, unsigned long entries, int mask)
{
	char method[64] = {'\0'};
	struct timespec a, b;
	unsigned long max_nsec;
	unsigned long d, diff;
	int i, rv;

	if (!entries)
		return 0;

	shuffle(array, entries, sizeof(unsigned long), mask);

	if (mask & 0x1)
		strncpy(method, "ascending", sizeof(method));
	else if (mask & 0x2)
		strncpy(method, "reverse", sizeof(method));
	else
		strncpy(method, "random", sizeof(method));

	for (i = 0, max_nsec = 0, d = 0; i < entries; i++) {
		time_now(&a);
		rv = bp_po_delete(root, array[i]);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;

		/* Failed. */
		if (rv) {
			fprintf(stdout, "error to delete: %lu\n", array[i]);
			return -1;
		}
	}

#if 1
	/* average */
	d = d / i;
	fprintf(stdout, "delete(%s): %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		method, d, (float) d / 1000, max_nsec, bp_tree_high(root->node));
#endif
	return 0;
}

static void
do_sanity_check(void)
{
	struct bp_root root;
	unsigned long *array;
	unsigned long max_entries;
	int rv, i;

	rv = bp_root_init(&root);
	if (rv < 0)
		assert(0);

	max_entries = 100000;

	array = calloc(max_entries, sizeof(unsigned long));
	BUG_ON(array == NULL);

	for (i = 0; i < max_entries; i++)
		array[i] = i;

	while (1) {
		unsigned long rnd_entries = rand() % max_entries + 1;
		unsigned long val = array[rand() % rnd_entries];

		fprintf(stdout, "-> Start exercise tree on %lu size...\n", rnd_entries);

		if (test_insert(&root, array, rnd_entries, rand_mask(3)))
			BUG();

		if (bp_po_delete(&root, val))
			BUG();

		if (bp_po_insert(&root, val))
			BUG();

		if (test_lookup(&root, array, rnd_entries, rand_mask(3)))
			BUG();

		if (val & 0x1) {
			if (test_delete(&root, array, rnd_entries, rand_mask(3)))
				BUG();

			if (test_insert(&root, array, rnd_entries, rand_mask(3)))
				BUG();
		} else {
			if (test_lookup(&root, array, rnd_entries, rand_mask(3)))
				BUG();
		}

		if (test_delete(&root, array, rnd_entries, rand_mask(3)))
			BUG();
	}

	bp_root_destroy(&root);
}

int main(int argc, char **argv)
{
	do_sanity_check();
}
