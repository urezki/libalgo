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

static void
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

		if (rv)
			fprintf(stdout, "-> Failed to insert %lu key\n", array[i]);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;
	}

	free(array);

#if 1
	/* average */
	d = d / i;
	fprintf(stdout, "insertion: %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		d, (float) d / 1000, max_nsec, bp_tree_high(root->node));
#endif
}

static void
test_random_lookup(struct bp_root *root, unsigned long tree_size)
{
	struct timespec a, b;
	unsigned long *array;
	unsigned long max_nsec;
	unsigned long d, diff;
	struct node *n;
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

		if (!n)
			fprintf(stdout, "-> Key is not found: %lu\n", array[i]);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;
	}

	free(array);

#if 0
	/* average */
	d = d / i;
	fprintf(stdout, "lookup: %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		d, (float) d / 1000, max_nsec, bp_tree_high(root->node));
#endif
}

static void
test_random_delete(struct bp_root *root, unsigned long tree_size)
{
	struct timespec a, b;
	unsigned long *array;
	unsigned long max_nsec;
	unsigned long d, diff;
	struct node *n;
	int i;

	array = calloc(tree_size, sizeof(unsigned long));
	BUG_ON(array == NULL);

	for (i = 0; i < tree_size; i++)
		array[i] = i;

	shuffle(array, tree_size, sizeof(unsigned long));

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		time_now(&a);
		bp_po_delete(root, array[i]);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;
	}

	free(array);

#if 1
	/* average */
	d = d / i;
	fprintf(stdout, "delete: %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		d, (float) d / 1000, max_nsec, bp_tree_high(root->node));
#endif
}

int main(int argc, char **argv)
{
	struct bp_root root;
	int rv;

	rv = bp_root_init(&root);
	if (rv < 0)
		assert(0);

	test_random_insert(&root, 50);
	dump_tree(root.node);
	test_random_delete(&root, 50);
	dump_tree(root.node);

	{
		struct list_head *pos;
		list_for_each(pos, &root.head) {
			struct node *n = list_entry(pos, struct node, page.external.list);
			int i;

			printf("[ ");
			for (i = 0; i < n->entries; i++)
				printf(" %lu ", n->slot[i]);
			printf(" ]");
		}

		puts("");
	}

	bp_root_destroy(&root);
}
