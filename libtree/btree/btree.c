#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* local */
#include <btree.h>

#define ULONG_MAX	(~0UL)
#define ARRAY_MOVE(a, i, j, n)	\
	memmove((a) + (i), (a) + (j), sizeof(*a) * (n))

static bt_node *root;

static __always_inline bt_node *bt_zalloc_node(void)
{
	bt_node *n = calloc(1, sizeof(struct bt_node));

	if (unlikely(!n)) {
		fprintf(stdout, "error: calloc()\n");
		BUG();
	}

	return n;
}

static __always_inline bt_node *bt_max_node(bt_node *n)
{
	while (n->links[n->num_util_slots])
		n = n->links[root->num_util_slots];

	return n;
}

static __always_inline bt_node *bt_min_node(bt_node *n)
{
	while (n->links[0])
		n = n->links[0];

	return n;
}

/* Can not be called on a leaf node? */
static __always_inline bt_node *
bt_prev_sibling_of(bt_node *n, int index)
{
	return bt_max_node(n->links[index]);
}

/* Can not be called on a leaf node? */
static __always_inline bt_node *
bt_next_sibling_of(bt_node *n, int index)
{
	return bt_min_node(n->links[index + 1]);
}

static __always_inline int
is_bt_node_full(bt_node *n)
{
	return (n->num_util_slots == MAX_UTIL_SLOTS);
}

static __always_inline int
is_bt_node_leaf(bt_node *n)
{
	return (n->links[0] == NULL);
}

static __always_inline int
bt_search_node_index(bt_node *n, unsigned long val)
{
	int i;

	/*
	 * Regular linear search is used within a node.
	 * It is OK and acceptable if the node size is
	 * ~3 cache lines. If it is more, then another
	 * method should be used. For example binary
	 * search.
	 */
	for (i = 0; i < n->num_util_slots; i++) {
		if (val <= n->slots[i].va_start)
			return i;
	}

	return n->num_util_slots;
}

static __always_inline void
bt_insert_to_node(bt_node *n, int index, unsigned long val)
{
	BUG_ON(index >= MAX_UTIL_SLOTS);

	if (index < n->num_util_slots)
		ARRAY_MOVE(n->slots, index + 1, index,
			n->num_util_slots - index);

	n->slots[index].va_start = val;
	n->num_util_slots++;
}

static inline void
bt_node_remove(bt_node *n, int index)
{
	/* TODO */
}

/* Splits the child node into two peaces */
static __always_inline void
bt_split_child_node(bt_node *child, bt_node *parent, int pindex)
{
	bt_node *n;

	BUG_ON(!parent);
	n = bt_zalloc_node();

	/*
	 * 5 - a parent
	 * 7 - a new right child
	 * 3 - a child node that is split
	 *
	 *  3 5 7            (5)
	 * A B C D   ->   3       7
	 *               A B     C D
	 */

	/* right part goes to the new node */
	memcpy(n->slots, child->slots + MIN_DEGREE,
		sizeof(n->slots[0]) * (MIN_DEGREE - 1));

	if (!is_bt_node_leaf(child))
		memcpy(n->links, child->links + MIN_DEGREE,
			sizeof(child->links[0]) * MIN_DEGREE);

	/*
	 * Cut the size of the left child and set
	 * the size of the newly created right one.
	 */
	child->num_util_slots = MIN_DEGREE - 1;
	n->num_util_slots = MIN_DEGREE - 1;

	if (pindex < parent->num_util_slots) {
		/* parent must have at least one spot */
		BUG_ON(pindex + 1 >= MAX_UTIL_SLOTS);

		/* new key goes in pindex */
		ARRAY_MOVE(parent->slots, pindex + 1, pindex,
			parent->num_util_slots - pindex);

		/* new kid(n) goes in pindex + 1 */
		ARRAY_MOVE(parent->links, pindex + 2, pindex + 1,
			parent->num_util_slots - pindex);
	}

	/* Post the new separator key to the parent node. */
	parent->slots[pindex].va_start =
		child->slots[MIN_DEGREE - 1].va_start;

	/* assign left child */
	parent->links[pindex] = child;

	/* assign right child */
	parent->links[pindex + 1] = n;

	/* increase number of slots */
	parent->num_util_slots++;

	/* assign the parent for both kids */
	n->parent = child->parent = parent;
}

/* Splits the root node */
static bt_node *
bt_split_root_node(bt_node *root)
{
	bt_node *new_root = bt_zalloc_node();

	bt_split_child_node(root, new_root, 0);
	return new_root;
}

static int bt_insert(unsigned long val)
{
	bt_node *n;
	int index;

	if (is_bt_node_full(root))
		root = bt_split_root_node(root);

	n = root;

	while (1) {
		index = bt_search_node_index(n, val);
		if (index < n->num_util_slots &&
				n->slots[index].va_start == val)
			break;

		/* Hit the bottom. */
		if (is_bt_node_leaf(n)) {
			bt_insert_to_node(n, index, val);
			return 0;
		}

		if (is_bt_node_full(n->links[index])) {
			/*
			 * After split operation, the parent node (n) gets
			 * the separator key from the child node. The key
			 * is inserted to "index" position of the parent.
			 */
			bt_split_child_node(n->links[index], n, index);

			/*
			 * Compare the separator key with one that is inserted,
			 * i.e. we do not support and maintain duplicated keys.
			 */
			if (unlikely(n->slots[index].va_start == val))
				break;

			if (val > n->slots[index].va_start)
				index++;
		}

		n = n->links[index];
	}

	return -1;
}

static bt_node *
bt_search(bt_node *n, unsigned long val, int *pos)
{
	int index;

	while (1) {
		index = bt_search_node_index(n, val);
		if (index < n->num_util_slots &&
				n->slots[index].va_start == val) {
			*pos = index;
			return n;
		}

		if (is_bt_node_leaf(n))
			break;

		n = n->links[index];
	}

	return NULL;
}

static __always_inline
void bt_node_destroy(bt_node *n)
{
	free(n);
}

static __always_inline void
bt_node_delete_key(bt_node *n, int index)
{
	/*
	 * Example:
	 *  ___ ___ ___
	 * |_5_|_6_|_7_|
	 *
	 * if remove 5, just move left two elements
	 * in array to index number zero, after that
	 * decrease number of elements in it.
	 */
	if (index < n->num_util_slots - 1)
		ARRAY_MOVE(n->slots, index, index + 1,
			n->num_util_slots - 1 - index);

	n->num_util_slots--;
}

static void bt_adjust(bt_node *n)
{
	bt_node *p;

	while (1) {
		p = n->parent;

		/* if root node */
		if (p == NULL) {
			if (!n->num_util_slots) {
				bt_node_destroy(n);
				root = NULL;
			}

			return;
		}
	}
}

static int bt_remove(bt_node *root, unsigned long val)
{
	bt_node *n, *prev, *next;
	int index;

	n = bt_search(root, val, &index);
	if (!n)
		return -1;

	if (is_bt_node_leaf(n)) {
		bt_node_delete_key(n, index);
		goto adjust;
	}

	prev = bt_prev_sibling_of(n, index);
	next = bt_next_sibling_of(n, index);

	/*
	 * Replace the key that is about to be removed,
	 * by the one from the previous or next neighbor,
	 * finally remove borrowed key from the sibling.
	 */
	if (prev->num_util_slots >= next->num_util_slots) {
		n->slots[index].va_start =
			prev->slots[prev->num_util_slots - 1].va_start;
		bt_node_delete_key(prev, prev->num_util_slots - 1);
		n = prev;
	} else {
		n->slots[index].va_start = next->slots[0].va_start;
		bt_node_delete_key(next, 0);
		n = next;
	}

adjust:
	/*
	 * Check if the anti-overflow has happened. It occurs
	 * when the size of the node drops below min_degree - 1,
	 * so the property of the tree has been violated. To fix
	 * that merging or moving operations are required.
	 */
	if (n->num_util_slots < MIN_UTIL_SLOTS)
		bt_adjust(n);

	return 0;
}

#ifdef DEBUG_BTREE
static void build_graph(bt_node *n)
{
	int i;

	if (is_bt_node_leaf(n)) {
		printf("\tnode%lu[label = \"", n->num);
		for (i = 0; i < n->num_util_slots; i++)
			printf("%lu ", n->slots[i].va_start);

		printf("\"];\n");
		return;
	}

	printf("\tnode%lu[label = \"<p0>", n->num);
	for (i = 0; i < n->num_util_slots; i++)
		printf(" |%lu| <p%d>", n->slots[i].va_start, i + 1);

	printf("\"];\n");

	for (i = 0; i <= n->num_util_slots; i++)
		printf("\t\"node%lu\":p%d -> \"node%lu\"\n",
			n->num, i, n->links[i]->num);

	for (i = 0; i <= n->num_util_slots; i++)
		build_graph(n->links[i]);
}

static void assign_node_id(bt_node *n, int *num)
{
	int i;

	if (is_bt_node_leaf(n)) {
		n->num = *num;
		return;
	}

	n->num = *num;
	for (i = 0; i <= n->num_util_slots; i++) {
		(*num)++;
		assign_node_id(n->links[i], num);
	}
}

static void dump_tree(bt_node *root)
{
	int i, num = 0;

	assign_node_id(root, &num);

	fprintf(stdout, "digraph G\n{\n");
	fprintf(stdout, "node [shape = record,height=.1];\n");
	build_graph(root);
	fprintf(stdout, "}\n");

	fprintf(stdout, "# run: ./a.out | dot -Tpng -o btree.png\n");
}
#endif

#include <time.h>

static inline unsigned long
now(void)
{
	struct timespec tp;

	(void) clock_gettime(CLOCK_MONOTONIC, &tp);
	return (tp.tv_sec * 1000000000) + tp.tv_nsec;
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

#include <pthread.h>
pthread_mutex_t lock;

int main(int argc, char **argv)
{
	unsigned long *array;
	unsigned int tree_size = 1000000;
	unsigned long max_nsec, d;
	struct timespec a;
	struct timespec b;
	int i, pos, num;
	int ret;

	(void) pthread_mutex_init(&lock, NULL);
	root = bt_zalloc_node();
	array = calloc(tree_size, sizeof(unsigned long));

	srand(time(NULL));

	for (i = 0; i < tree_size; i++)
		array[i] = rand() % ULONG_MAX;
		/* array[i] = i; */
		/* array[i] = tree_size - i; */

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		int ret;

		pthread_mutex_lock(&lock);
		time_now(&a);
		ret = bt_insert(array[i]);
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

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		time_now(&a);
		if (!bt_search(root, array[i], &pos))
			fprintf(stdout, "val %lu not found\n", array[i]);
		time_now(&b);

		d += time_diff(&a, &b);

		if (d > max_nsec)
			max_nsec = d;
	}

	/* average */
	d = d / i;
	fprintf(stdout, "lookup: %lu nano/s, %f micro/s, max: %lu nsec\n",
			d, (float) d / 1000, max_nsec);

	/* dump_tree(root); */
	return 0;
}
