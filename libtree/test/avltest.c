#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* lib */
#include <avl.h>
#include <debug.h>
#include <timer.h>

static void
avl_dump(avl_node *n, FILE *fp)
{
	if (n) {
		if (n->link[0])
			fprintf(fp, "\t%ld -> %ld\n", n->key, n->link[0]->key);

		if (n->link[1])
			fprintf(fp, "\t%ld -> %ld\n", n->key, n->link[1]->key);

		avl_dump(n->link[0], fp);
		avl_dump(n->link[1], fp);
	}
}

static void
avl_dump_to_file(avl_node *n, const char *file)
{
	FILE *fp, rv;

	fp = fopen(file, "w");
	if (fp) {
		fprintf(fp, "digraph G\n{\n");
		fprintf(fp, "node [shape=\"circle\"];\n");
		avl_dump(n, fp);
		fprintf(fp, "}\n");

		fclose(fp);
	}
}

static void
test_1()
{
	avl_node *root = NULL;
	int i, rv;

	int input[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	/* int input[] = { 100, 20, 150, 6, 26, 27 }; */
	/* int input[] = { 100, 20, 150, 6, 26, 25 }; */

	/* int input[] = { 3769, 4163, 3465, 4143, 4396, 4011 }; */
	/* int input[] = { 3769, 4163, 3465, 4143, 4396, 4144 }; */

	for (i = 0; i < ARRAY_SIZE(input); i++) {
		struct avl_node *n;

		n = avl_alloc();
		n->key = input[i];
		rv = avl_insert(&root, n);
		if (rv == 0)
			fprintf(stdout, "'avl_insert()' error, %ld\n", n->key);
	}

	for (i = 0; i < ARRAY_SIZE(input); i++) {
		struct avl_node *n;

		n = avl_lookup(root, input[i]);
		if (n) {
			struct avl_node *l = n->link[0];
			struct avl_node *r = n->link[1];

			fprintf(stdout, "-> %ld { %ld, %ld } %d\n",
					n->key, l ? l->key:-99, r ? r->key:-99, n->bf);
		}
	}

	avl_dump_to_file(root, __func__);
}

void sort_array(unsigned long *array, unsigned long n)
{
	unsigned long c, d, swap;

	for (c = 0 ; c < n - 1; c++) {
		for (d = 0 ; d < n - c - 1; d++) {
			if (array[d] > array[d+1]) {
				swap       = array[d];
				array[d]   = array[d+1];
				array[d+1] = swap;
			}
		}
	}
}

#include <pthread.h>
pthread_mutex_t lock;

static void
test_2()
{
	struct avl_node *root = NULL;
	struct timespec a;
	struct timespec b;
	uint64_t d = 0;
	int i = 0;
	int rv;
	int error = 0;
	unsigned long max_nsec = 0;
	unsigned int tree_size = 1000000;
	struct avl_node *array;

	(void) pthread_mutex_init(&lock, NULL);

	srand(time(NULL));

	array = calloc(tree_size, sizeof(struct avl_node));

	for (i = 0; i < tree_size; i++)
		array[i].key = rand() % 10000000000;
		/* array[i].key = i; */
		/* array[i].key = tree_size - i; */

	for (i = 0, max_nsec = 0; i < tree_size; i++) {
		pthread_mutex_lock(&lock);
		time_now(&a);
		rv = avl_insert(&root, &array[i]);
		time_now(&b);
		pthread_mutex_unlock(&lock);

		d += time_diff(&a, &b);
		if (d > max_nsec)
			max_nsec = d;
	}

	/* average */
	d = d / i;
	fprintf(stdout, "insertion: %lu nano/s, %f micro/s, max: %lu nsec\n",
			d, (float) d / 1000, max_nsec);

	/* lookup */
	for (i = 0, d = 0, max_nsec = 0; i < tree_size; i++) {
		struct avl_node *tmp;
		size_t key;

		time_now(&a);
		tmp = avl_lookup(root, array[i].key);
		time_now(&b);

		d += time_diff(&a, &b);

		if (d > max_nsec)
			max_nsec = d;
	}

	/* average */
	d = d / i;
	fprintf(stdout, "lookup: %lu nano/s, %f micro/s, max: %lu nsec\n",
			d, (float) d / 1000, max_nsec);

	/* avl_dump_to_file(root, __func__); */
}

/* gcc avltest.c -I ../../include/ -I../avltree/ ../libavl_fpic.a -lrt */
/* dot test_1 -Tpng -o image.png */

int main(int argc, char **argv)
{
	/* test_1(); */
	test_2();

	return 0;
}
