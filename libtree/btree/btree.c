#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* local */
#include <btree.h>

static bt_node *root;

#define ULONG_MAX	(~0UL)

#define ARRAY_MOVE(a, i, j, n)		\
	memmove((a) + (i), (a) + (j), sizeof(*a) * ((n) - (j)))

#define ARRAY_COPY(dest, src, n)	\
	memcpy(dest, src, n)

#define ARRAY_INSERT(a, i, j, n)	\
	do { ARRAY_MOVE(a, (i) + 1, i, j); (a)[i] = (n); } while(0)

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
	while (n->links[n->nr_entries])
		n = n->links[n->nr_entries];

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
	return (n->nr_entries == MAX_UTIL_SLOTS);
}

static __always_inline int
is_bt_node_leaf(bt_node *n)
{
	return (n->links[0] == NULL);
}

static __always_inline int
bt_search_node_index(bt_node *n, unsigned long val)
{
	register int i, entries;

	for (i = 0, entries = n->nr_entries; i < entries; i++) {
		if (val <= n->slots[i].va_start)
			break;
	}

	return i;
}

static __always_inline void
bt_insert_to_node(bt_node *n, int index, unsigned long val)
{
	BUG_ON(index >= MAX_UTIL_SLOTS);

	if (index < n->nr_entries)
		ARRAY_MOVE(n->slots, index + 1, index,
			n->nr_entries);

	n->slots[index].va_start = val;
	n->nr_entries++;
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

	/* copy keys to the new node (right part) */
	ARRAY_COPY(n->slots, child->slots + MIN_DEGREE,
		sizeof(n->slots[0]) * (MIN_DEGREE - 1));

	/* copy links to the new node (right part) */
	if (!is_bt_node_leaf(child))
		ARRAY_COPY(n->links, child->links + MIN_DEGREE,
			sizeof(child->links[0]) * MIN_DEGREE);

	/*
	 * Cut the size of the left child and set
	 * the size of the newly created right one.
	 */
	child->nr_entries = MIN_DEGREE - 1;
	n->nr_entries = MIN_DEGREE - 1;

	/* Post the new separator key to the parent node. */
	ARRAY_INSERT(parent->slots, pindex,
		parent->nr_entries, child->slots[MIN_DEGREE - 1]);

	/* new right kid(n) goes in pindex + 1 */
	ARRAY_INSERT(parent->links, pindex + 1,
		parent->nr_entries + 1, n);

	/* Set the parent for both kids */
	n->parent = child->parent = parent;
	parent->nr_entries++;
}

/* Splits the root node */
static bt_node *
bt_split_root_node(bt_node *root)
{
	bt_node *new_root = bt_zalloc_node();

	/* Old root becomes left kid. */
	new_root->links[0] = root;
	bt_split_child_node(root, new_root, 0);

	return new_root;
}

/*
 * Implements an insert with single pass-down, it is
 * possible if the b-tree is based on minimum degree
 * methodology when the maximum of keys in a node is
 * odd, i.e. 2t - 1. Uses preemptive splitting.
 */
static int bt_insert(unsigned long val)
{
	bt_node *n;
	int index;

	if (is_bt_node_full(root)) {
		root = bt_split_root_node(root);
		if (unlikely(!root))
			return -1;
	}

	n = root;

	while (1) {
		index = bt_search_node_index(n, val);
		if (index < n->nr_entries &&
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
			 * Compare the separator key with one that is
			 * inserted, i.e. two identical keys are not
			 * allowed.
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
		if (index < n->nr_entries &&
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
bt_node_delete_key(bt_node *n, int idx)
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
	if (idx < n->nr_entries - 1)
		ARRAY_MOVE(n->slots, idx, idx + 1,
			n->nr_entries);

	n->nr_entries--;
}

static void bt_move_to_left(bt_node *parent, int idx)
{
	bt_node *l = parent->links[idx];
	bt_node *r = parent->links[idx + 1];

	l->slots[l->nr_entries] = parent->slots[idx];
	parent->slots[idx] = r->slots[0];
	ARRAY_MOVE(r->slots, 0, 1, r->nr_entries);

	if (!is_bt_node_leaf(r)) {
		l->links[l->nr_entries + 1] = r->links[0];
		ARRAY_MOVE(r->links, 0, 1, r->nr_entries + 1);
	}

	r->nr_entries--;
	l->nr_entries++;
}

static void bt_move_to_right(bt_node *parent, int idx)
{
	bt_node *l = parent->links[idx];
	bt_node *r = parent->links[idx + 1];

	ARRAY_INSERT(r->slots, 0, r->nr_entries,
		parent->slots[idx]);

	parent->slots[idx] = l->slots[l->nr_entries - 1];

	if (!is_bt_node_leaf(r))
		ARRAY_INSERT(r->links, 0, r->nr_entries + 1,
			l->links[l->nr_entries]);

	l->nr_entries--;
	r->nr_entries++;
}

static bt_node *
bt_merge_siblings(bt_node *parent, int idx)
{
	bt_node *n1, *n2;

	if (idx == parent->nr_entries)
		idx--;

	n1 = parent->links[idx];
	n2 = parent->links[idx + 1];

	BUG_ON(n1->nr_entries + n2->nr_entries + 1 > MAX_UTIL_SLOTS);

	/* copy keys to n1 from the n2 */
	ARRAY_COPY(n1->slots + MIN_DEGREE, n2->slots,
		sizeof(n1->slots[0]) * MIN_UTIL_SLOTS);

	/* copy links to n1 from the n2 */
	if (!is_bt_node_leaf(n1))
		ARRAY_COPY(n1->links + MIN_DEGREE, n2->links,
			sizeof(n1->links[0]) * MIN_DEGREE);

	/* copy parent separator key */
	n1->slots[MIN_DEGREE - 1] = parent->slots[idx];
	n1->nr_entries = MAX_UTIL_SLOTS;

	/* remove copied key from the parent */
	ARRAY_MOVE(parent->slots, idx, idx + 1, parent->nr_entries);
	ARRAY_MOVE(parent->links, idx + 1, idx + 2, parent->nr_entries + 1);

	parent->links[idx] = n1;
	parent->nr_entries--;

	if (!parent->nr_entries && root == parent) {
		bt_node_destroy(parent);
		root = n1;
	}

	bt_node_destroy(n2);
	return n1;
}

static int bt_remove(bt_node *root, unsigned long val)
{
	bt_node *sub = root;
	bt_node *parent;
	int idx;

	while (1) {
		idx = bt_search_node_index(sub, val);
		if (idx < sub->nr_entries &&
				sub->slots[idx].va_start == val)
			break;

		/* Not found. */
		if (is_bt_node_leaf(sub))
			return -1;

		parent = sub;
		sub = sub->links[idx];

		if (sub->nr_entries > MIN_UTIL_SLOTS)
			continue;

		if (idx < parent->nr_entries &&
				parent->links[idx + 1]->nr_entries > MIN_UTIL_SLOTS)
			bt_move_to_left(parent, idx);
		else if (idx > 0 && parent->links[idx - 1]->nr_entries > MIN_UTIL_SLOTS)
			bt_move_to_right(parent, idx - 1);
		else
			sub = bt_merge_siblings(parent, idx);
	}

loop:
	if (is_bt_node_leaf(sub)) {
		bt_node_delete_key(sub, idx);
	} else {
		if (sub->links[idx]->nr_entries > MIN_UTIL_SLOTS) {
			bt_node *prev = bt_prev_sibling_of(sub, idx);

			sub->slots[idx] = prev->slots[prev->nr_entries - 1];
			bt_remove(sub->links[idx], sub->slots[idx].va_start);
		} else if (sub->links[idx + 1]->nr_entries > MIN_UTIL_SLOTS) {
			bt_node *next = bt_next_sibling_of(sub, idx);

			sub->slots[idx] = next->slots[0];
			bt_remove(sub->links[idx + 1], sub->slots[idx].va_start);
		} else {
			sub = bt_merge_siblings(sub, idx);
			idx = MIN_UTIL_SLOTS;
			goto loop;
		}
	}

	return 0;
}

#ifdef DEBUG_BTREE
static void build_graph(bt_node *n)
{
	int i;

	if (is_bt_node_leaf(n)) {
		printf("\tnode%lu[label = \"", n->num);
		for (i = 0; i < n->nr_entries; i++)
			printf("%lu ", n->slots[i].va_start);

		printf("\"];\n");
		return;
	}

	printf("\tnode%lu[label = \"<p0>", n->num);
	for (i = 0; i < n->nr_entries; i++)
		printf(" |%lu| <p%d>", n->slots[i].va_start, i + 1);

	printf("\"];\n");

	for (i = 0; i <= n->nr_entries; i++)
		printf("\t\"node%lu\":p%d -> \"node%lu\"\n",
			n->num, i, n->links[i]->num);

	for (i = 0; i <= n->nr_entries; i++)
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
	for (i = 0; i <= n->nr_entries; i++) {
		(*num)++;
		assign_node_id(n->links[i], num);
	}
}

static void dump_tree(bt_node *root)
{
	int num = 0;

	assign_node_id(root, &num);

	fprintf(stdout, "digraph G\n{\n");
	fprintf(stdout, "node [shape = record,height=.1];\n");
	build_graph(root);
	fprintf(stdout, "}\n");

	fprintf(stdout, "# run: ./a.out | dot -Tpng -o btree.png\n");
}
#endif

#include <time.h>

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
	unsigned long max_nsec, d, diff;
	struct timespec a, b;
	int i, pos, ret;

	(void) pthread_mutex_init(&lock, NULL);
	root = bt_zalloc_node();
	array = calloc(tree_size, sizeof(unsigned long));

	srand(time(NULL));

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		do {
			array[i] = (rand() % ULONG_MAX);

			pthread_mutex_lock(&lock);
			time_now(&a);
			ret = bt_insert(array[i]);
			time_now(&b);
			pthread_mutex_unlock(&lock);
		} while (ret);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;
	}

	/* average */
	d = d / i;
	fprintf(stdout, "insertion: %lu nano/s, %f micro/s, max: %lu nsec\n",
			d, (float) d / 1000, max_nsec);

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		time_now(&a);
		(void) bt_search(root, array[i], &pos);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;
	}

	/* average */
	d = d / i;
	fprintf(stdout, "lookup: %lu nano/s, %f micro/s, max: %lu nsec\n",
			d, (float) d / 1000, max_nsec);

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		time_now(&a);
		bt_remove(root, array[i]);
		time_now(&b);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;
	}

	/* average */
	d = d / i;
	fprintf(stdout, "delete: %lu nano/s, %f micro/s, max: %lu nsec\n",
			d, (float) d / 1000, max_nsec);

	dump_tree(root);
	bt_node_destroy(root);
	free(array);

	return 0;
}
