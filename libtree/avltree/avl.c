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

/* locals */
#include <avl.h>
#include <debug.h>

/*
 * AVL example (7, 8, 9):
 *
 *
 *  7    7     7                 8
 * / \  / \   / \  left pivot   / \
 *         8     8             7   9
 *              / \
 *                 9
 */

avl_node *
alloc_avl_node(int sizeof_priv)
{
	struct avl_node *n = NULL;

	n = (avl_node *) calloc(1, sizeof(avl_node));
	if (n == NULL)
		;

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
	if (n && n->priv_data)
		return n->priv_data;

	return NULL;
}

/*
 * work-flow:
 * - right child of root becomes the new root
 * - old root takes ownership of its right's left child as its right child
 * - new root takes ownership of old root as its left child
 */
static inline void
avl_left_pivot(struct avl_node *r)
{
	if (r) {
		if (r->link[1]) {
			r->link[1]->link[3] = r->link[3];
			r->link[1] = r->link[1]->link[0];
			r->link[1]->link[0] = r;
		} else {
			/* nothing to pivot */
		}
	}
}

static inline void
avl_right_pivot(struct avl_node *r)
{
	if (r) {
		if (r->link[0]) {
			
			
			
		} else {
			/* nothing to pivot */
		}
	}
}

static void
avl_balance(struct avl_node *r)
{
	if (r == NULL)
		return;

	/* TODO: perform balancing */
}

static void
set_heights(struct avl_node *r)
{
	
	
	
	
}

static void
__avl_insert(struct avl_node *r, struct avl_node *n)
{
	struct avl_node *rp;

	/*
	 * save root's pointer, because it requires
	 * to be shifted for insertion into appropriate
	 * place of the tree.
	 */
	rp = r;

	while (1) {
		if (new->key > rp->key) {
			if (rp->link[1]) {
				rp = rp->link[1]; /* right */
			} else {
				/* insert and leave */
				rp->link[1] = n;
				break;
			}
		} else if (new->key < rp->key) {
			if (rp->link[0]) {
				rp = rp->link[0]; /* left */
			} else {
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

	/* link with parent */
	n->link[2] = rp;
}

/*
 * work-flow of insertion a new node into AVL tree:
 *
 * - insertion is done as in the BS tree;
 * - check if a tree has to be balanced;
 * - balance the tree, if needed;
 */
void
avl_insert(struct avl_node *r, struct avl_node *n)
{
	if (r && n) {
		__avl_insert(r, n);
		set_heights(r);
		avl_balance(r);
	}
}

void
avl_remove()
{
	
	
	
}

void
avl_lookup()
{
	
	
	
}

