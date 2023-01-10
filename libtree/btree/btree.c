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
	while (n->links[n->slot_len])
		n = n->links[n->slot_len];

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
	return (n->slot_len == MAX_NODE_SLOTS);
}

static __always_inline int
is_bt_node_leaf(bt_node *n)
{
	return (n->links[0] == NULL);
}

/* return smallest index i in sorted array such that key <= a[i] */
/* (or n if there is no such index) */
static __always_inline int
bt_search_node_index(bt_node *n, void *key)
{
    int lo, hi;
    int mid;

    /* invariant: a[lo] < key <= a[hi] */
    lo = -1;
    hi = n->slot_len;

	while (lo + 1 < hi) {
		mid = (lo + hi) / 2;

		if (n->slot[mid] == key)
			return mid;
		else if (n->slot[mid] < key)
			lo = mid;
		else
			hi = mid;
	}

    return hi;
}

static __always_inline void
bt_insert_to_node(bt_node *n, int index, void *val)
{
	BUG_ON(index >= MAX_NODE_SLOTS);

	if (index < n->slot_len)
		ARRAY_MOVE(n->slot, index + 1, index,
			n->slot_len);

	n->slot[index] = val;
	n->slot_len++;
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
	ARRAY_COPY(n->slot, child->slot + MIN_DEGREE,
		sizeof(n->slot[0]) * (MIN_DEGREE - 1));

	/* copy links to the new node (right part) */
	if (!is_bt_node_leaf(child))
		ARRAY_COPY(n->links, child->links + MIN_DEGREE,
			sizeof(child->links[0]) * MIN_DEGREE);

	/*
	 * Cut the size of the left child and set
	 * the size of the newly created right one.
	 */
	child->slot_len = MIN_DEGREE - 1;
	n->slot_len = MIN_DEGREE - 1;

	/* Post the new separator key to the parent node. */
	ARRAY_INSERT(parent->slot, pindex,
		parent->slot_len, child->slot[MIN_DEGREE - 1]);

	/* new right kid(n) goes in pindex + 1 */
	ARRAY_INSERT(parent->links, pindex + 1,
		parent->slot_len + 1, n);

	/* Set the parent for both kids */
	n->parent = child->parent = parent;
	parent->slot_len++;
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
static int bt_insert(void *val)
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
		if (index < n->slot_len &&
				n->slot[index] == val)
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
			if (unlikely(n->slot[index] == val))
				break;

			if (val > n->slot[index])
				index++;
		}

		n = n->links[index];
	}

	return -1;
}

static bt_node *
bt_search(bt_node *n, void *val, int *pos)
{
	int index;

	while (1) {
		index = bt_search_node_index(n, val);
		if (index < n->slot_len &&
				n->slot[index] == val) {
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
	if (idx < n->slot_len - 1)
		ARRAY_MOVE(n->slot, idx, idx + 1,
			n->slot_len);

	n->slot_len--;
}

static void bt_move_to_left(bt_node *parent, int idx)
{
	bt_node *l = parent->links[idx];
	bt_node *r = parent->links[idx + 1];

	l->slot[l->slot_len] = parent->slot[idx];
	parent->slot[idx] = r->slot[0];
	ARRAY_MOVE(r->slot, 0, 1, r->slot_len);

	if (!is_bt_node_leaf(r)) {
		l->links[l->slot_len + 1] = r->links[0];
		ARRAY_MOVE(r->links, 0, 1, r->slot_len + 1);
	}

	r->slot_len--;
	l->slot_len++;
}

static void bt_move_to_right(bt_node *parent, int idx)
{
	bt_node *l = parent->links[idx];
	bt_node *r = parent->links[idx + 1];

	ARRAY_INSERT(r->slot, 0, r->slot_len,
		parent->slot[idx]);

	parent->slot[idx] = l->slot[l->slot_len - 1];

	if (!is_bt_node_leaf(r))
		ARRAY_INSERT(r->links, 0, r->slot_len + 1,
			l->links[l->slot_len]);

	l->slot_len--;
	r->slot_len++;
}

static bt_node *
bt_merge_siblings(bt_node *parent, int idx)
{
	bt_node *n1, *n2;

	if (idx == parent->slot_len)
		idx--;

	n1 = parent->links[idx];
	n2 = parent->links[idx + 1];

	BUG_ON(n1->slot_len + n2->slot_len + 1 > MAX_NODE_SLOTS);

	/* copy keys to n1 from the n2 */
	ARRAY_COPY(n1->slot + MIN_DEGREE, n2->slot,
		sizeof(n1->slot[0]) * MIN_NODE_SLOTS);

	/* copy links to n1 from the n2 */
	if (!is_bt_node_leaf(n1))
		ARRAY_COPY(n1->links + MIN_DEGREE, n2->links,
			sizeof(n1->links[0]) * MIN_DEGREE);

	/* copy parent separator key */
	n1->slot[MIN_DEGREE - 1] = parent->slot[idx];
	n1->slot_len = MAX_NODE_SLOTS;

	/* remove copied key from the parent */
	ARRAY_MOVE(parent->slot, idx, idx + 1, parent->slot_len);
	ARRAY_MOVE(parent->links, idx + 1, idx + 2, parent->slot_len + 1);

	parent->links[idx] = n1;
	parent->slot_len--;

	if (!parent->slot_len && root == parent) {
		bt_node_destroy(parent);
		root = n1;
	}

	bt_node_destroy(n2);
	return n1;
}

static int bt_remove(bt_node *root, void *val)
{
	bt_node *sub = root;
	bt_node *parent;
	int idx;

	while (1) {
		idx = bt_search_node_index(sub, val);
		if (idx < sub->slot_len &&
				sub->slot[idx] == val)
			break;

		/* Not found. */
		if (is_bt_node_leaf(sub))
			return -1;

		parent = sub;
		sub = sub->links[idx];

		if (sub->slot_len > MIN_NODE_SLOTS)
			continue;

		if (idx < parent->slot_len &&
				parent->links[idx + 1]->slot_len > MIN_NODE_SLOTS)
			bt_move_to_left(parent, idx);
		else if (idx > 0 && parent->links[idx - 1]->slot_len > MIN_NODE_SLOTS)
			bt_move_to_right(parent, idx - 1);
		else
			sub = bt_merge_siblings(parent, idx);
	}

loop:
	if (is_bt_node_leaf(sub)) {
		bt_node_delete_key(sub, idx);
	} else {
		if (sub->links[idx]->slot_len > MIN_NODE_SLOTS) {
			bt_node *prev = bt_prev_sibling_of(sub, idx);

			sub->slot[idx] = prev->slot[prev->slot_len - 1];
			bt_remove(sub->links[idx], sub->slot[idx]);
		} else if (sub->links[idx + 1]->slot_len > MIN_NODE_SLOTS) {
			bt_node *next = bt_next_sibling_of(sub, idx);

			sub->slot[idx] = next->slot[0];
			bt_remove(sub->links[idx + 1], sub->slot[idx]);
		} else {
			sub = bt_merge_siblings(sub, idx);
			idx = MIN_NODE_SLOTS;
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
		for (i = 0; i < n->slot_len; i++)
			printf("%lu ", (unsigned long) n->slot[i]);

		printf("\"];\n");
		return;
	}

	printf("\tnode%lu[label = \"<p0>", n->num);
	for (i = 0; i < n->slot_len; i++)
		printf(" |%lu| <p%d>", (unsigned long) n->slot[i], i + 1);

	printf("\"];\n");

	for (i = 0; i <= n->slot_len; i++)
		printf("\t\"node%lu\":p%d -> \"node%lu\"\n",
			n->num, i, n->links[i]->num);

	for (i = 0; i <= n->slot_len; i++)
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
	for (i = 0; i <= n->slot_len; i++) {
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

static int bt_depth(bt_node *n)
{
	int depth = 0;

	while (n) {
		n = n->links[0];
		depth++;
	}

	return depth;
}

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

int main(int argc, char **argv)
{
	unsigned int tree_size = 1000000;
	unsigned long *array;
	unsigned long max_nsec, d, diff;
	struct timespec a, b;
	int i, pos, ret;

	root = bt_zalloc_node();
	array = calloc(tree_size, sizeof(unsigned long));
	BUG_ON(array == NULL);

	for (i = 0; i < tree_size; i++)
		array[i] = i;

	shuffle(array, tree_size, sizeof(unsigned long));

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		do {
			time_now(&a);
			ret = bt_insert((void *) array[i]);
			time_now(&b);

			if (ret)
				fprintf(stdout, "-> error to insert %lu\n", array[i]);

		} while (ret);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;
	}

	/* dump_tree(root); */

	/* average */
	d = d / i;
	fprintf(stdout, "insertion: %lu nano/s, %f micro/s, max: %lu nsec\n",
			d, (float) d / 1000, max_nsec);

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		time_now(&a);
		(void) bt_search(root, (void *) array[i], &pos);
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

	fprintf(stdout, "-> btree depth is %d, bt-node size: %ld\n",
		bt_depth(root), sizeof(struct bt_node));

	for (i = 0, max_nsec = 0, d = 0; i < tree_size; i++) {
		time_now(&a);
		bt_remove(root, (void *) array[i]);
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

	/* dump_tree(root); */
	bt_node_destroy(root);
	free(array);

	return 0;
}
