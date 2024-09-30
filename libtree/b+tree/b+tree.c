#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

#include "b+tree.h"

#ifdef DEBUG_BP_TREE
static void build_graph(struct node *n)
{
	int i;

	if (is_node_external(n)) {
		printf("\tnode%lu[label = \"", n->num);
		for (i = 0; i < n->entries; i++)
			printf("%lu ", n->slot[i]);

		printf("\"];\n");
		return;
	}

	printf("\tnode%lu[label = \"<p0>", n->num);
	for (i = 0; i < n->entries; i++)
		printf(" |%lu| <p%d>", (unsigned long) n->slot[i], i + 1);

	printf("\"];\n");

	for (i = 0; i <= n->entries; i++)
		printf("\t\"node%lu\":p%d -> \"node%lu\"\n",
			n->num, i, n->page.internal.sub_links[i]->num);

	for (i = 0; i <= n->entries; i++)
		build_graph(n->page.internal.sub_links[i]);
}

static void assign_node_id(struct node *n, int *num)
{
	int i;

	if (is_node_external(n)) {
		n->num = *num;
		return;
	}

	n->num = *num;
	for (i = 0; i <= n->entries; i++) {
		(*num)++;
		assign_node_id(n->page.internal.sub_links[i], num);
	}
}

static void dump_tree(struct node *root)
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

static struct node *bp_calloc_node_init(u8 type)
{
	struct node *n = calloc(1, sizeof(*n));

	if (unlikely(!n))
		assert(0);

	n->info.type = type;

	if (type == BP_TYPE_EXTER)
		list_init(&n->page.external.list);

	return n;
}

static int bp_init_tree(struct bp_root *root)
{
	root->node = bp_calloc_node_init(BP_TYPE_EXTER);
	if (!root->node)
		return -1;

	return 0;
}

/*
 * Return smallest index "i" in a sorted array such that
 * a "key <= a[i]". Otherwise -1 is returned if there is
 * no such index.
 */
static __always_inline int
bp_search_node_index(struct node *n, ulong key)
{
    int lo, hi, mid;

	check_node_geometry(n);

    /* invariant: a[lo] < key <= a[hi] */
    lo = -1;
    hi = n->entries;

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

static __always_inline int
bp_insert_to_node(struct node *n, int index, ulong val)
{
	BUG_ON(index >= MAX_ENTRIES);

	/* No duplicate keys. */
	if (unlikely(n->slot[index] == val))
		return -1;

	if (index < n->entries)
		ARRAY_MOVE(n->slot, index + 1, index,
			n->entries);

	n->slot[index] = val;
	n->entries++;
	return 0;
}

/*
 * "left" is a node which is split.
 */
static __always_inline void
bp_split_internal_node(struct node *left, struct node *right)
{
	int i;

	BUG_ON(!is_node_internal(left));

	/*
	 * Minus one entries is set because, during the split process
	 * of internal node, a separator key is __moved__ to the parent.
	 *
	 * See example:
	 *
	 *  3 5 7             (5)
	 * A B C D   ->    3       7
	 *                A B     C D
	 */
	right->entries = split(MAX_ENTRIES) - 1;
	left->entries = (MAX_ENTRIES - (right->entries + 1));

	/* Copy keys to the new node (right part). */
	for (i = 0; i < right->entries; i++)
		right->slot[i] = left->slot[left->entries + i + 1];

	/* Number of children in a node is node entries + 1. */
	for (i = 0; i < right->entries + 1; i++)
		NODE_SUB_LINK(right, i) = NODE_SUB_LINK(left, left->entries + i + 1);
}

static __always_inline void
bp_split_external_node(struct node *left, struct node *right)
{
	int i;

	BUG_ON(!is_node_external(left));

	/*
	 * During the split process of external node, a separator
	 * key is __copied__ to the parent. Example:
	 *
	 *  3 5 7              (5)
	 * A B C D   ->    3       5 7
	 *                A B     C   D
	 */
	right->entries = split(MAX_ENTRIES);
	left->entries = MAX_ENTRIES - right->entries;

	/*
	 * Copy keys to the new node (right part).
	 */
	for (i = 0; i < right->entries; i++)
		right->slot[i] = left->slot[i + left->entries];
}

static __always_inline void
bp_split_node(struct node *n, struct node *parent, int pindex)
{
	struct node *right;
	ulong split_key;

	right = bp_calloc_node_init(n->info.type);
	if (unlikely(!right))
		return;

	if (is_node_internal(n)) {
		bp_split_internal_node(n, right);
		split_key = n->slot[n->entries]; /* will be moved. */
	} else {
		bp_split_external_node(n, right);
		split_key = right->slot[0];	/* is a __copy__. */
	}

	/* Move the new separator key to the parent node. */
	ARRAY_INSERT(parent->slot, pindex,
		parent->entries, split_key);

	/* new right kid(n) goes in pindex + 1 */
	ARRAY_INSERT(parent->page.internal.sub_links, pindex + 1,
		parent->entries + 1, right);

	/* Set the parent for both kids */
	right->info.parent = n->info.parent = parent;
	parent->entries++;
}

/* Splits the root node */
static struct node *
bp_split_root_node(struct node *root)
{
	struct node *new_root = bp_calloc_node_init(BP_TYPE_INTER);

	if (likely(new_root)) {
		/* Old root becomes left kid. */
		new_root->page.internal.sub_links[0] = root;
		bp_split_node(root, new_root, 0);
	}

	return new_root;
}

static int
bp_insert_non_full(struct node *n, ulong val)
{
	int index;

	while (is_node_internal(n)) {
		index = bp_search_node_index(n, val);

		/* If same key, bail out. */
		if (index < n->entries && n->slot[index] == val)
			return -1;

		if (is_node_full(NODE_SUB_LINK(n, index))) {
			/*
			 * After split operation, the parent node (n) gets
			 * the separator key from the child node. The key
			 * is inserted to "index" position of the parent.
			 */
			bp_split_node(NODE_SUB_LINK(n, index), n, index);

			if (val > n->slot[index])
				index++;
		}

		n = NODE_SUB_LINK(n, index);
	}

	index = bp_search_node_index(n, val);
	return bp_insert_to_node(n, index, val);
}

static int
bp_insert(struct bp_root *root, ulong val)
{
	struct node *n = root->node;

	if (is_node_full(n)) {
		n = bp_split_root_node(n);
		if (unlikely(!n))
			return -1;

		/* Set a new root. */
		root->node = n;
	}

	return bp_insert_non_full(root->node, val);
}

static bool
bp_lookup(struct bp_root *root, ulong val)
{
	struct node *n = root->node;
	int index;

	while (is_node_internal(n)) {
		index = bp_search_node_index(n, val);

		/* If true, "val" is located in a right sub-tree. */
		if (n->slot[index] == val)
			index++;

		n = NODE_SUB_LINK(n, index);
	}

	index = bp_search_node_index(n, val);
	return (n->slot[index] == val);
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
test_random_insert(struct bp_root *root, unsigned int tree_size)
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
		rv = bp_insert(root, array[i]);
		time_now(&b);
		
		if (rv)
			fprintf(stdout, "-> Failed to insert %lu key\n", array[i]);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;
	}

#if 0
	/* average */
	d = d / i;
	fprintf(stdout, "insertion: %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		d, (float) d / 1000, max_nsec, bp_tree_high(root->node));
#endif
}

static void
test_random_lookup(struct bp_root *root, unsigned int tree_size)
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
		rv = bp_lookup(root, array[i]);
		time_now(&b);

		if (!rv)
			fprintf(stdout, "-> Key is not found: %lu\n", array[i]);

		diff = time_diff(&a, &b);
		d += diff;

		if (diff > max_nsec)
			max_nsec = diff;
	}

#if 0
	/* average */
	d = d / i;
	fprintf(stdout, "lookup: %lu nano/s, %f micro/s, max: %lu nsec, tree high: %d\n",
		d, (float) d / 1000, max_nsec, bp_tree_high(root->node));
#endif
}

int main(int argc, char **argv)
{
	struct bp_root root;
	int rv;

	rv = bp_init_tree(&root);
	if (rv < 0)
		assert(0);

	test_random_insert(&root, 50);
	test_random_lookup(&root, 50);
	dump_tree(root.node);
}
