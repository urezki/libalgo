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
#include <time.h>

/* locals */
#include <avl.h>
#include <debug.h>

struct avl_node *
alloc_avl_node(int sizeof_priv)
{
	struct avl_node *n = NULL;

	n = (struct avl_node *) calloc(1, sizeof(struct avl_node));
	if (unlikely(n == NULL))
		BUG();

	/* allocate place for private data */
	if (sizeof_priv > 0) {
		n->priv_data = calloc(1, sizeof_priv);
		if (n->priv_data == NULL)
			;
	}

	return n;
}

void
free_avl_node(struct avl_node *n)
{
	if (n) {
		if (n->priv_data) {
			free(n->priv_data);
			n->priv_data = NULL;
		}

		free(n);
		n = NULL;
	}
}

void *
get_avl_priv_data(struct avl_node *n)
{
	if (n)
		return n->priv_data;

	return NULL;
}

/*
 * left rotation of node (r), (rc) is (r)'s right child
 */
static inline void
avl_left_pivot(struct avl_node **r)
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
avl_right_pivot(struct avl_node **r)
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
avl_double_right_pivot(struct avl_node **r)
{
	struct avl_node *lc = NULL;
	struct avl_node *nr = NULL;

	/* get left child */
	lc = (*r)->link[0];

	/* get new root */
	nr = lc->link[1];

	/*
	 * new root (nr) is sliding between left child (lc)
	 * and old root (r), given them its left and right
	 * sub-trees
	 */
	lc->link[1] = nr->link[0];
	nr->link[0] = lc;

	(*r)->link[0] = nr->link[1];
	nr->link[1] = *r;

	nr->bf = 0;
	lc->bf = 0;

	/* take care about parent */
	nr->link[2] = (*r)->link[2];

	if (likely((*r)->link[2] != NULL)) {
		if ((*r)->link[2]->link[0] == *r)
			(*r)->link[2]->link[0] = nr;
		else
			(*r)->link[2]->link[1] = nr;
	}

	/* ??? */
	(*r)->bf = 1;

	*r = nr;
}

/*
 * right-left rotation
 */
static inline void
avl_double_left_pivot(struct avl_node **r)
{
	struct avl_node *rc = NULL;
	struct avl_node *nr = NULL;
	
	/* get right child */
	rc = (*r)->link[1];

	/* get new root */
	nr = rc->link[0];

	/*
	 * new root (nr) is sliding between right child (rc)
	 * and old root (r), given them its left and right
	 * sub-trees
	 */
	rc->link[0] = nr->link[1];
	nr->link[1] = rc;

	(*r)->link[1] = nr->link[0];
	nr->link[0] = *r;

	/* balanced */
	nr->bf = 0;
	rc->bf = 0;

	/* take care about parent */
	nr->link[2] = (*r)->link[2];

	if (likely((*r)->link[2] != NULL)) {
		if ((*r)->link[2]->link[0] == *r)
			(*r)->link[2]->link[0] = nr;
		else
			(*r)->link[2]->link[1] = nr;
	}

	/* ??? */
	(*r)->bf = -1;

	*r = nr;
}

static void
do_avl_rebalance(struct avl_node **r, struct avl_node *ub)
{
	int change_root = 0;

	if (*r == ub)
		change_root = 1;

	if (ub->bf < 0) {
		if (ub->link[0]->bf > 0) {
			avl_double_right_pivot(&ub);
		} else {
			avl_right_pivot(&ub);
		}
	} else if (ub->bf > 0) {
		if (ub->link[1]->bf < 0) {
			avl_double_left_pivot(&ub);
		} else {
			avl_left_pivot(&ub);
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
get_unbalanced_node(struct avl_node *f, struct avl_node *t)
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
		}
	}

	return NULL;
}

/*
 * work-flow of insertion a new node into AVL tree:
 *
 * - insertion is done as in the BS tree;
 * - check if a tree has to be balanced;
 * - balance the tree, if needed;
 */
void
avl_insert(struct avl_node **r, struct avl_node *n)
{
	struct avl_node *rp = NULL;
	struct avl_node *ub = NULL;

	if (*r == NULL) {
		*r = n;
		return;
	}

	/*
	 * start searching an appropriate place from root
	 * node going down till the bottom of the tree
	 */
	rp = *r;

	while (1) {
		if (n->key > rp->key) {
			if (rp->link[1]) {
				/* keep trace back */
				rp->link[1]->link[2] = rp;

				/* next right node */
				rp = rp->link[1];
			} else {
				/* keep trace back */
				n->link[2] = rp;

				/* insert and leave */
				rp->link[1] = n;
				break;
			}
		} else if (n->key < rp->key) {
			if (rp->link[0]) {
				/* keep trace back */
				rp->link[0]->link[2] = rp;

				/* next left node */
				rp = rp->link[0];
			} else {
				/* keep trace back */
				n->link[2] = rp;

				/* insert and leave */
				rp->link[0] = n;
				break;
			}
		} else {
			/*
			 * keys are the same
			 */
			return;
		}
	}

	if (!rp->link[0] || !rp->link[1]) {
		ub = get_unbalanced_node(n, *r);
		if (ub) {
			/* r - can be reassigned */
			do_avl_rebalance(r, ub);
		}
	} else {
		/*
		 * parent of the inserted node is balanced
		 */
		rp->bf = 0;
	}
}

void
avl_remove()
{
	
	
	
}

struct avl_node *
avl_lookup(struct avl_node *root, size_t key)
{
	while (root) {
		if (key == root->key)
			return root;

		if (key > root->key) {
			root = root->link[1];
		} else {
			root = root->link[0];
		}
	}

	return NULL;
}

#ifdef TEST
int main(int argc, char **argv)
{
	struct avl_node *root = NULL;
	int i = 0;

	srand(time(0));

	for (i = 0; i < 1000000; i++) {
		struct avl_node *tmp = alloc_avl_node(0);

		tmp->key = rand() % 1000000;
		avl_insert(&root, tmp);
	}

	return 0;
}
#endif	/* TEST */
