/*
 * avl.c - source file of the AVL tree implementation
 * Copyright (C) 2010 Uladzislau Rezki (urezki@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* locals */
#include <avl.h>
#include <debug.h>
#include <timer.h>

#ifdef TEST
static void avl_dump_to_file(avl_node *n, const char *file);
#endif

struct avl_node *
avl_alloc(int data_size)
{
	struct avl_node *n = NULL;

	n = (struct avl_node *) calloc(1, sizeof(struct avl_node));
	if (unlikely(n == NULL))
		BUG();

	/* allocate place for private data */
	if (data_size > 0) {
		n->data = calloc(1, data_size);
		if (n->data == NULL)
			BUG();
	}

	return n;
}

void
avl_free(struct avl_node *n)
{
	if (n) {
		if (n->data) {
			free(n->data);
			n->data = NULL;
		}

		free(n);
		n = NULL;
	}
}

void *
avl_priv_data(struct avl_node *n)
{
	if (n)
		return n->data;

	return NULL;
}

/*
 * left rotation of node (r), (rc) is (r)'s right child
 */
static inline void
do_left_pivot(struct avl_node **r)
{
	struct avl_node *rc = NULL;

	/*
	 * set (rc) to be the new root
	 * set (r)'s right child to be (rc)'s left child
	 * set (rc)'s left child to be (r)
	 */

	/* get new root */
	rc = (*r)->link[1];

	/*
	 * point parent to new left/right child
	 */
	rc->link[2] = (*r)->link[2];

	if (likely((*r)->link[2] != NULL)) {
		if ((*r)->link[2]->link[0] == *r)
			(*r)->link[2]->link[0] = rc;
		else
			(*r)->link[2]->link[1] = rc;
	}

	(*r)->link[1] = rc->link[0];
	rc->link[0] = *r;

	rc->bf = 0;
	(*r)->bf = 0;

	*r = rc;
}

/*
 * right rotation of node (r), (lc) is (r)'s left child
 */
static inline void
do_right_pivot(struct avl_node **r)
{
	struct avl_node *lc = NULL;

	/*
	 * set (lc) to be the new root
	 * set (r)'s left child to be (lc)'s right child
	 * set (lc)'s right child to be (r)
	 */

	/* get new root */
	lc = (*r)->link[0];

	/*
	 * point parent to new left/right child
	 */
	lc->link[2] = (*r)->link[2];

	if (likely((*r)->link[2] != NULL)) {
		if ((*r)->link[2]->link[0] == *r)
			(*r)->link[2]->link[0] = lc;
		else
			(*r)->link[2]->link[1] = lc;
	}

	(*r)->link[0] = lc->link[1];
	lc->link[1] = *r;

	lc->bf = 0;
	(*r)->bf = 0;

	*r = lc;
}

/*
 * left-right rotation
 */
static inline void
do_double_right_pivot(struct avl_node **r)
{
	struct avl_node *lc = NULL;
	struct avl_node *np = NULL;

	/*
	 * get left child
	 */
	lc = (*r)->link[0];

	/*
	 * get new root
	 */
	np = lc->link[1];

	/* right heavier */
	if (np->bf > 0) {
		(*r)->bf = 0;
		lc->bf = -1;
	} else if (np->bf < 0) {
		(*r)->bf = 1;
		lc->bf = 0;
	} else {
		(*r)->bf = 0;
		lc->bf = 0;
	}

	/* balanced */
	np->bf = 0;

	/*
	 * new root (np) is sliding between left child (lc)
	 * and old root (r), given them its left and right
	 * sub-trees
	 */
	lc->link[1] = np->link[0];
	np->link[0] = lc;

	(*r)->link[0] = np->link[1];
	np->link[1] = *r;

	/* take care about parent */
	np->link[2] = (*r)->link[2];

	if (likely((*r)->link[2] != NULL)) {
		if ((*r)->link[2]->link[0] == *r)
			(*r)->link[2]->link[0] = np;
		else
			(*r)->link[2]->link[1] = np;
	}

	*r = np;
}

/*
 * right-left rotation
 */
static inline void
do_double_left_pivot(struct avl_node **r)
{
	struct avl_node *rc = NULL;
	struct avl_node *np = NULL;

	/*
	 * get right child
	 */
	rc = (*r)->link[1];

	/*
	 * get new root
	 */
	np = rc->link[0];

	/* right heavier */
	if (np->bf < 0) {
		(*r)->bf = 0;
		rc->bf = 1;
	} else if (np->bf > 0) {
		(*r)->bf = -1;
		rc->bf = 0;
	} else {
		(*r)->bf = 0;
		rc->bf = 0;
	}

	/*
	 * new root (np) is sliding between right child (rc)
	 * and old root (r), given them its left and right
	 * sub-trees
	 */
	rc->link[0] = np->link[1];
	np->link[1] = rc;

	(*r)->link[1] = np->link[0];
	np->link[0] = *r;

	/* balanced */
	np->bf = 0;

	/* take care about parent */
	np->link[2] = (*r)->link[2];

	if (likely((*r)->link[2] != NULL)) {
		if ((*r)->link[2]->link[0] == *r)
			(*r)->link[2]->link[0] = np;
		else
			(*r)->link[2]->link[1] = np;
	}

	*r = np;
}

static inline void
do_pivot(struct avl_node **r, struct avl_node *ub)
{
	int change_root = 0;

	if (*r == ub)
		change_root = 1;

	if (ub->bf < 0) {
		if (ub->link[0]->bf > 0) {
			do_double_right_pivot(&ub);
		} else {
			do_right_pivot(&ub);
		}
	} else if (ub->bf > 0) {
		if (ub->link[1]->bf < 0) {
			do_double_left_pivot(&ub);
		} else {
			do_left_pivot(&ub);
		}
	} else {
		BUG();
	}

	if (unlikely(change_root)) {
		ub->link[2] = NULL;
		*r = ub;		/* new root */
	}
}

static struct avl_node *
fix_balance(struct avl_node *f, struct avl_node *t)
{
	struct avl_node *p;

	/*
	 * f - from; t - to
	 */
	for (; f != t; f = f->link[2]) {
		p = f->link[2];

		if (p->link[1] == f)
			/* right subtree */
			p->bf++;
		else
			/* left subtree */
			p->bf--;

		/* return unbalanced node */
		if (p->bf == 2 || p->bf == -2) {
			return p;
		} else if (p->bf == 0) {
			/*
			 * after insertion a node is balanced
			 */
			break;
		} else if (p->bf > 2 || p->bf < -2) {
			BUG();
		}
	}

	return NULL;
}

static struct avl_node *
do_simple_insert(struct avl_node *r, struct avl_node *n)
{
	while (1) {
		if (n->key > r->key) {
			if (r->link[1]) {
				/* keep trace back */
				r->link[1]->link[2] = r;

				/* next right node */
				r = r->link[1];
			} else {
				/* keep trace back */
				n->link[2] = r;

				/* insert and leave */
				r->link[1] = n;
				break;
			}
		} else if (n->key < r->key) {
			if (r->link[0]) {
				/* keep trace back */
				r->link[0]->link[2] = r;

				/* next left node */
				r = r->link[0];
			} else {
				/* keep trace back */
				n->link[2] = r;

				/* insert and leave */
				r->link[0] = n;
				break;
			}
		} else {
			/*
			 * keys are the same
			 */
			return NULL;
		}
	}

	return r;
}

/*
 * work-flow of insertion a new node into AVL tree:
 *
 * - insertion is done as in the BS tree;
 * - check if a tree has to be balanced;
 * - balance the tree, if needed;
 */
int
avl_insert(struct avl_node **r, struct avl_node *n)
{
	struct avl_node *np = NULL;
	struct avl_node *ub = NULL;
	int rv = 0;

	if (unlikely(!*r)) {
		*r = n;
		rv = 1;
		goto leave;
	}

	/*
	 * NP is a parent of new node N
	 */
	np = do_simple_insert(*r, n);
	if (np) {
		/*
		 * perform rotations
		 */
		if (!np->link[0] || !np->link[1]) {
			ub = fix_balance(n, *r);
			if (ub)
				/* r - can be reassigned */
				do_pivot(r, ub);
		} else {
			/*
			 * has two leafs
			 */
			np->bf = 0;
		}

		/* success */
		rv = 1;
	}

leave:
	return rv;
}

void
avl_remove(struct avl_node **r, size_t key)
{
	struct avl_node *t = NULL;

	t = avl_lookup(*r, key);
	if (t) {
		/* is not head */
		if (t != *r) {
			struct avl_node *l = t->link[0];
			struct avl_node *r = t->link[1];
			struct avl_node *p = t->link[2];

			if (l && r) {
				/* two children */

			} else if (!l || !r) {
				/* one children */

			} else {
				/* no children */

			}
		} else {
			/*
			 * remove the head of the tree
			 */
		}
	}
}

/*
 * Simple lookup
 */
struct avl_node *
avl_lookup(struct avl_node *root, size_t key)
{
	while (root) {
		if (key > root->key) {
			root = root->link[1];
		} else if (key < root->key) {
			root = root->link[0];
		} else {
			return root;
		}
	}

	return NULL;
}

#ifdef TEST

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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

	/* int input[] = { 100, 20, 150, 6, 26, 27 }; */
	int input[] = { 100, 20, 150, 6, 26, 25 };

	/* int input[] = { 3769, 4163, 3465, 4143, 4396, 4011 }; */
	/* int input[] = { 3769, 4163, 3465, 4143, 4396, 4144 }; */

	for (i = 0; i < ARRAY_SIZE(input); i++) {
		struct avl_node *n;

		n = avl_alloc(0);
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

	srand(time(0));

	/* insert */
	for (i = 0; i < 100; i++) {
		struct avl_node *tmp = avl_alloc(0);

		tmp->key = rand() % 1000000000;
		time_now(&a);
		rv = avl_insert(&root, tmp);
		time_now(&b);

		if (!rv)
			error++;

		d += time_diff(&a, &b);
	}

	/* average */
	d = d / i;
	fprintf(stdout, "insertion: %lu nano/s, %f micro/s, errors: %d\n",
			d, (float) d / 1000, error);

	/* lookup */
	for (i = 0, d = 0, error = 0; i < 100; i++) {
		struct avl_node *tmp;
		size_t key;

		key = rand() % 1000000000;

		time_now(&a);
		tmp = avl_lookup(root, key);
		time_now(&b);

		if (!tmp)
			error++;

		d += time_diff(&a, &b);
	}

	/* average */
	d = d / i;
	fprintf(stdout, "lookup: %lu nano/s, %f micro/s, errors: %d\n",
			d, (float) d / 1000, error);

	avl_dump_to_file(root, __func__);
}

int main(int argc, char **argv)
{
	test_1();
	test_2();

	return 0;
}
#endif	/* TEST */
