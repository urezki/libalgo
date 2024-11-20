#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

#include "b+tree.h"
#include "debug.h"

int rand_comparison(const void *a, const void *b)
{
	(void)a; (void)b;
	return rand() % 2 ? +1 : -1;
}

void shuffle(void *base, size_t nmemb, size_t size)
{
	srand(time(NULL));
	qsort(base, nmemb, size, rand_comparison);
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
test_random_insert(struct bp_root *root, unsigned long tree_size)
{
	struct timespec a, b;
	unsigned long *array;
	unsigned long max_nsec;
	unsigned long d, diff;
	int i, rv;

	array = calloc(tree_size, sizeof(unsigned long));
	BUG_ON(array == NULL);

	for (i = 0; i < tree_size; i++)
		array[i] = i;

	shuffle(array, tree_size, sizeof(unsigned long));

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		time_now(&a);
		rv = bp_po_insert(root, array[i]);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;

		/* Failed to insert. */
		if (rv)
			break;
	}

	free(array);

#if 1
	/* average */
	d = d / i;
	fprintf(stdout, "insert: %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		d, (float) d / 1000, max_nsec, bp_tree_high(root->node));
#endif
	return rv;
}

static int
test_random_lookup(struct bp_root *root, unsigned long tree_size)
{
	struct timespec a, b;
	unsigned long *array;
	unsigned long max_nsec;
	unsigned long d, diff;
	struct node *n;
	int rv = 0;
	int i;

	array = calloc(tree_size, sizeof(unsigned long));
	BUG_ON(array == NULL);

	for (i = 0; i < tree_size; i++)
		array[i] = i;

	shuffle(array, tree_size, sizeof(unsigned long));

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		time_now(&a);
		n = bp_lookup(root, array[i], NULL);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;

		/* Failed. */
		if (!n) {
			rv = -1;
			break;
		}
	}

	free(array);

#if 1
	/* average */
	d = d / i;
	fprintf(stdout, "lookup: %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		d, (float) d / 1000, max_nsec, bp_tree_high(root->node));
#endif
	return rv;
}

static int
test_random_delete(struct bp_root *root, unsigned long tree_size)
{
	struct timespec a, b;
	unsigned long *array;
	unsigned long max_nsec;
	unsigned long d, diff;
	int i, rv;

	array = calloc(tree_size, sizeof(unsigned long));
	BUG_ON(array == NULL);

	for (i = 0; i < tree_size; i++)
		array[i] = i;

	shuffle(array, tree_size, sizeof(unsigned long));

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		time_now(&a);
		rv = bp_po_delete(root, array[i]);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;

		/* Failed. */
		if (rv)
			break;
	}

	free(array);

#if 1
	/* average */
	d = d / i;
	fprintf(stdout, "delete: %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		d, (float) d / 1000, max_nsec, bp_tree_high(root->node));
#endif
	return rv;
}

static void
do_sanity_check(void)
{
	struct bp_root root;
	unsigned long tree_size;
	int rv;

	rv = bp_root_init(&root);
	if (rv < 0)
		assert(0);

	while (1) {
		srand(time(NULL));
		tree_size = rand() % 1000000;
		fprintf(stdout, "-> Run sanity on %lu tree size...\n", tree_size);

		rv |= test_random_insert(&root, tree_size);
		rv |= test_random_lookup(&root, tree_size);
		rv |= test_random_delete(&root, tree_size);

		if (rv)
			BUG();
	}

	bp_root_destroy(&root);
}

int main(int argc, char **argv)
{
	do_sanity_check();
}
