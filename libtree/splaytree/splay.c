/*
 * splay.c - source file of the splay tree implementation
 * Copyright (C) 2019 Uladzislau Rezki (urezki@gmail.com)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* locals */
#include <splay.h>
#include <debug.h>

#define max(a, b) (a) > (b) ? (a) : (b)
#define max3(x, y, z) max((__typeof__(x))max(x, y), z)
#define ULONG_MAX	(~0UL)

#define SP_INIT_NODE(node)  \
	((node)->left = (node)->right = (node)->parent = NULL)

static void
dump_tree(struct splay_node *n, FILE *fp)
{
	if (n) {
		if (n->left)
			fprintf(fp, "\tn%ld_%lu_%lu -> n%ld_%lu_%lu\n",
				n->val, n->size, n->max_size,
				n->left->val, n->left->size, n->left->max_size);

		if (n->right)
			fprintf(fp, "\tn%ld_%lu_%lu -> n%ld_%lu_%lu\n",
				n->val, n->size, n->max_size,
				n->right->val, n->right->size, n->right->max_size);

		dump_tree(n->left, fp);
		dump_tree(n->right, fp);
	}
}

static void
dump_tree_to_file(struct splay_node *n, const char *file)
{
	FILE *fp;

	fp = fopen(file, "w");
	if (fp) {
		fprintf(fp, "digraph G\n{\n");
		fprintf(fp, "node [shape=\"circle\"];\n");
		dump_tree(n, fp);
		fprintf(fp, "}\n");

		fclose(fp);
	}
}

static __always_inline unsigned long
get_subtree_max_size(struct splay_node *node)
{
	return node ? node->max_size : 0;
}

/*
 * Gets called when remove the node and rotate.
 */
static __always_inline unsigned long
compute_subtree_max_size(struct splay_node *node)
{
	return max3(node->size,
		get_subtree_max_size(node->left),
		get_subtree_max_size(node->right));
}

static __always_inline void
augment_tree_propagate_from(struct splay_node *from)
{
	unsigned long new_sub_max_size;

	while (from) {
		new_sub_max_size = compute_subtree_max_size(from);

		/*
		 * If the newly calculated maximum available size of the
		 * subtree is equal to the current one, then it means that
		 * the tree is propagated correctly. So we have to stop at
		 * this point to save cycles.
		 */
		if (from->max_size == new_sub_max_size)
			break;

		from->max_size = new_sub_max_size;
		from = from->parent;
	}
}

static void
check_augment_tree(struct splay_node *n, struct splay_node *root,
	struct splay_node *array, int tree_size)
{
	int i;

	if (n) {
		if (n->max_size != compute_subtree_max_size(n)) {
			fprintf(stdout, "-> %lu != calculated %lu, val %lu\n",
					n->max_size, compute_subtree_max_size(n), n->val);
			dump_tree_to_file(root, __func__);

			for (i = 0; i < tree_size; i++)
				fprintf(stdout, "-> i: %d, val: %lu, size: %lu max_size: %lu\n",
						i, array[i].val, array[i].size, array[i].max_size);

			exit(-1);
		}

		if (n->parent) {
			if (n->parent->left != n) {
				if (n->parent->right != n) {
					fprintf(stdout, "-> parent link error of %lu, n->parent->left->val: %lu, n->parent->right->val %lu\n",
						n->val,
						n->parent->left ? n->parent->left->val:0,
						n->parent->right ? n->parent->right->val:0);
				}
			}
		}

		check_augment_tree(n->left, root, array, tree_size);
		check_augment_tree(n->right, root, array, tree_size);
	}
}

static inline void
set_parent(struct splay_node *n, struct splay_node *p)
{
	if (n)
		n->parent = p;
}

static inline void
change_child(struct splay_node *p,
	struct splay_node *old, struct splay_node *new)
{
	if (p) {
		if (p->left == old)
			p->left = new;
		else
			p->right = new;
	}
}

/*
 * left rotation of node (r), (rc) is (r)'s right child
 */
static inline struct splay_node *
left_pivot(struct splay_node *r)
{
	struct splay_node *rc;

	/*
	 * set (rc) to be the new root
	 */
	rc = r->right;

	/*
	 * point parent to new left/right child
	 */
	rc->parent = r->parent;

	/*
	 * change child of the p->parent.
	 */
	change_child(r->parent, r, rc);

	/*
	 * set (r)'s right child to be (rc)'s left child
	 */
	r->right = rc->left;

	/*
	 * change parent of rc's left child
	 */
	set_parent(rc->left, r);

	/*
	 * set new parent of rotated node
	 */
	r->parent = rc;

	/*
	 * set (rc)'s left child to be (r)
	 */
	rc->left = r;

	/*
	 * return the new root
	 */
	return rc;
}

/*
 * right rotation of node (r), (lc) is (r)'s left child
 */
static inline struct splay_node *
right_pivot(struct splay_node *r)
{
	struct splay_node *lc;

	/*
	 * set (lc) to be the new root
	 */
	lc = r->left;

	/*
	 * point parent to new left/right child
	 */
	lc->parent = r->parent;

	/*
	 * change child of the p->parent.
	 */
	change_child(r->parent, r, lc);

	/*
	 * set (r)'s left child to be (lc)'s right child
	 */
	r->left = lc->right;

	/*
	 * change parent of lc's right child
	 */
	set_parent(lc->right, r);

	/*
	 * set new parent of rotated node
	 */
	r->parent = lc;

	/*
	 * set (lc)'s right child to be (r)
	 */
	lc->right = r;

	/*
	 * return the new root
	 */
	return lc;
}

static struct splay_node *
top_down_splay(unsigned long vstart, struct splay_node *root)
{
	/*
	 * During the splitting process two temporary trees are formed.
	 * "l" contains all keys less than the search key/vstart and "r"
	 * contains all keys greater than the search key/vstart.
	 */
	struct splay_node head, *ltree_max, *rtree_max;
	struct splay_node *ltree_prev, *rtree_prev;

	SP_INIT_NODE(&head);
	ltree_max = rtree_max = &head;
	ltree_prev = rtree_prev = NULL;

	while (1) {
		if (vstart < root->val && root->left) {
			if (vstart < root->left->val) {
				root = right_pivot(root);

#ifdef SPLAY_AUGMENT
				root->max_size = root->right->max_size;
				root->right->max_size = compute_subtree_max_size(root->right);
#endif
				if (root->left == NULL)
					break;
			}

			/*
			 * Build right subtree.
			 */
			rtree_max->left = root;
			rtree_max->left->parent = rtree_prev;
			rtree_max = rtree_max->left;
			rtree_prev = root;
			root = root->left;
		} else if (vstart > root->val && root->right) {
			if (vstart > root->right->val) {
				root = left_pivot(root);

#ifdef SPLAY_AUGMENT
				root->max_size = root->left->max_size;
				root->left->max_size = compute_subtree_max_size(root->left);
#endif
				if (root->right == NULL)
					break;
			}

			/*
			 * Build left subtree.
			 */
			ltree_max->right = root;
			ltree_max->right->parent = ltree_prev;
			ltree_max = ltree_max->right;
			ltree_prev = root;
			root = root->right;
		} else {
			break;
		}
	}

	/*
	 * Assemble the tree.
	 */
	ltree_max->right = root->left;
	rtree_max->left = root->right;
	root->left = head.right;
	root->right = head.left;

	set_parent(ltree_max->right, ltree_max);
	set_parent(rtree_max->left, rtree_max);
	set_parent(root->left, root);
	set_parent(root->right, root);
	root->parent = NULL;

#ifdef SPLAY_AUGMENT
	while (ltree_max) {
		unsigned long new_sub_max_size = compute_subtree_max_size(ltree_max);

		ltree_max->max_size = new_sub_max_size;
		ltree_max = ltree_max->parent;
	}

	while (rtree_max) {
		unsigned long new_sub_max_size = compute_subtree_max_size(rtree_max);

		rtree_max->max_size = new_sub_max_size;
		rtree_max = rtree_max->parent;
	}
#endif

	return root;
}

struct splay_node *
splay_insert(struct splay_node *n, struct splay_node *r)
{
    if (r) {
		r = top_down_splay(n->val, r);

		if (n->val < r->val) {
			n->left = r->left;
			n->right = r;

			set_parent(r->left, n);
			r->parent = n;
			r->left = NULL;
		} else if (n->val > r->val) {
			n->right = r->right;
			n->left = r;

			set_parent(r->right, n);
			r->parent = n;
			r->right = NULL;
		} else {
			/*
			 * Not found, return last accessed node.
			 */
			return r;
		}
	} else {
		/* First element in the tree */
		SP_INIT_NODE(n);
	}

#ifdef SPLAY_AUGMENT
	if (n->left)
		n->left->max_size = compute_subtree_max_size(n->left);

	if (n->right)
		n->right->max_size = compute_subtree_max_size(n->right);

	n->parent = NULL;
	n->max_size = compute_subtree_max_size(n);
#endif

	return n;
}

struct splay_node *
splay_remove_init(struct splay_node *n, struct splay_node *r)
{
	struct splay_node *new_root;

	if (r == NULL)
		return NULL;

	r = top_down_splay(n->val, r);

	if (n->val == r->val) {
		if (r->left == NULL) {
			new_root = r->right;
		} else {
			new_root = top_down_splay(n->val, r->left);
			new_root->right = r->right;
			set_parent(r->right, new_root);
		}

		SP_INIT_NODE(r);

		if (new_root) {
			new_root->max_size = compute_subtree_max_size(new_root);
			new_root->parent = NULL;
		}

		return new_root;
	}

	/*
	 * Not found, return last accessed node.
	 */
	return r;
}

#ifdef TEST
#include <timer.h>
static void
test_1(void)
{
	struct splay_node *root = NULL;
	struct splay_node *n;
	struct timespec a;
	struct timespec b;
	uint64_t d = 0;
	int i;
	unsigned int tree_size = 1000000;
	unsigned long *rand_array;
	unsigned long max_nsec = 0;

	srand(time(NULL));

	rand_array = calloc(tree_size, sizeof(unsigned long));

	for (i = 0; i < tree_size; i++)
		rand_array[i] = rand() % ULONG_MAX;

	max_nsec = 0;

	for (i = 0; i < tree_size; i++) {
		n = (struct splay_node *) calloc(1, sizeof(struct splay_node));

		n->val = rand_array[i];
		time_now(&a);
		root = splay_insert(n, root);
		time_now(&b);

		if (root->val != rand_array[i])
			puts("insert error!");

		d += time_diff(&a, &b);

		if (d > max_nsec)
			max_nsec = d;
	}

	/* average */
	d = d / i;
	fprintf(stdout, "insertion: %lu nano/s, %f micro/s, max: %lu nsec\n",
			d, (float) d / 1000, max_nsec);

	max_nsec = 0;

	for (i = 0; i < tree_size; i++) {
		time_now(&a);
		root = top_down_splay(rand_array[i], root);
		time_now(&b);

		/* if (root->val != rand_array[i]) */
		/* 	puts("lookup error!"); */

		d += time_diff(&a, &b);
		if (d > max_nsec)
			max_nsec = d;
	}

	/* average */
	d = d / i;
	fprintf(stdout, "lookup: %lu nano/s, %f micro/s, max: %lu nsec\n",
			d, (float) d / 1000, max_nsec);

	if (root == NULL)
		printf("-> root is NULL\n");
}

static void
test_2(void)
{
	struct splay_node *root = NULL;
	struct splay_node *array;
	unsigned int tree_size = 50;
	int i;

	srand(time(NULL));

	array = calloc(tree_size, sizeof(struct splay_node));

	for (i = 0; i < tree_size; i++) {
		array[i].val = rand() % tree_size;
		array[i].size = array[i].max_size = rand() % (tree_size * 2);
		root = splay_insert(&array[i], root);
	}

	for (i = 0; i < tree_size; i++) {
		root = top_down_splay(array[i].val, root);
		check_augment_tree(root, root, array, tree_size);
	}

	dump_tree_to_file(root, __func__);
	check_augment_tree(root, root, array, tree_size);
}

int main(int argc, char **argv)
{
	test_1();

	return 0;
}
#endif
