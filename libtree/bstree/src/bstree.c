/*
 * bstree.c - source file of the BS tree implementation
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
#include <bstree.h>

int
bst_insert(bst_node **root, bst_node *new)
{
	struct bst_node *head = *root;

	if (new == NULL)
		return 0;

	if (*root == NULL) {
		*root = new;
		(*root)->link[0] = NULL;
		(*root)->link[1] = NULL;
		(*root)->link[2] = NULL;
		return 1;
	}

	while (1) {
		if (new->key > head->key) {
			if (head->link[1]) {
				head = head->link[1]; /* right */
			} else {
				/* insert */
				head->link[1] = new;
				break;
			}
		} else if (new->key < head->key) {
			if (head->link[0]) {
				head = head->link[0]; /* left */
			} else {
				/* insert */
				head->link[0] = new;
				break;
			}
		} else {
			return 0;
		}
	}

	/* link with parent */
	new->link[2] = head;
	return 1;
}

/*
 * There are four cases we may face while removing:
 *
 * a - victim has two children;
 * b - victim has one left child;
 * c - victim has one right child;
 * d - victim has nothing.
 *
 * The first one, is more difficult case, here we need to find a node (s)
 * that has the smallest value but greater than (v), after that to detach
 * (s) from its position in the tree and put it into the spot formerly
 * occupied by (v), which disappears from the tree, so (s) can't have
 * left child.
 *
 * (v) has no right child. It's trivial to delete a node with no right child.
 * We replace the pointer leading to (v) by (V's) left child, if it has one,
 * or by a NULL pointer, if not. In other words, we replace the deleted node
 * by its left child.
 *
 * (v) has no left child. It's the same with when it has right child, but
 * here we replace the pointer leading to (v) by (V's) right child.
 *
 * (v) has no children, in such case we just point its parent to NULL, if
 * it exists.
 *
 * @root: is a head of the tree;
 * @key: is a key that is in question.
 */
int
bst_remove(bst_node **root, size_t key)
{
	struct bst_node *victim = NULL;

	victim = bst_lookup(*root, key);
	if (victim == NULL)
		goto leave;

	if (victim->link[0] && victim->link[1]) {
		/* take right sub-tree */
		struct bst_node *s = victim->link[1];

		/* find a node with smallest value */
		while (s->link[0])
			s = s->link[0];

		/*
		 * detach (s) from the tree
		 */
		if (s->link[1]) {
			if (s->link[2]->link[1] == s)
				/* right child */
				s->link[2]->link[1] = s->link[1];
			else
				/* left child */
				s->link[2]->link[0] = s->link[1];

			s->link[1]->link[2] = s->link[2];
		} else {
			/* (s) doesn't have children at all */
			if (s->link[2]->link[2]) {
				if (s->link[2]->link[1] == s)
					/* right set to NULL */
					s->link[2]->link[1] = NULL;
				else
					/* left set to NULL */
					s->link[2]->link[0] = NULL;
			}
		}

		/*
		 * attach (s) into the tree
		 */
		s->link[0] = victim->link[0];

		if (victim->link[1] != s)
			s->link[1] = victim->link[1];

		s->link[2] = victim->link[2];

		/* link back left */
		s->link[0]->link[2] = s;

		/* link back right */
		if (s->link[1])
			s->link[1]->link[2] = s;

		/* link (S's) parent with (s) */
		if (victim->link[2]) {
			if (victim->link[2]->link[0] == victim)
				s->link[2]->link[0] = s;
			else
				s->link[2]->link[1] = s;
		}

		/* check for new root */
		if (s->link[2] == NULL)
			*root = s;

		goto free_and_leave;
	} else if (victim->link[0] || victim->link[1]) {
		if (!victim->link[0]) {
			/* victim has right child */
			if (victim->link[2]) {
				if (victim->link[2]->link[0] == victim)
					/* left */
					victim->link[2]->link[0] = victim->link[1];
				else
					/* right */
					victim->link[2]->link[1] = victim->link[1];
			} else {
				*root = victim->link[1];
				(*root)->link[2] = NULL;
			}

			victim->link[1]->link[2] = victim->link[2];
		} else {
			/* victim has left child */
			if (victim->link[2]) {
				if (victim->link[2]->link[0] == victim)
					/* left */
					victim->link[2]->link[0] = victim->link[0];
				else
					/* right */
					victim->link[2]->link[1] = victim->link[0];
			} else {
				*root = victim->link[0];
				(*root)->link[2] = NULL;
			}

			victim->link[0]->link[2] = victim->link[2];
		}

		goto free_and_leave;
	} else {
		if (victim->link[2]) {
			if (victim->link[2]->link[0] == victim) {
				/* left */
				victim->link[2]->link[0] = NULL;
			} else {
				/* right */
				victim->link[2]->link[1] = NULL;
			}
		} else {
			/* a tree is deleted completely */
			*root = NULL;
		}

		goto free_and_leave;
	}

free_and_leave:
	free_bst_node(victim);
	return 1;

leave:
	return -1;
}

bst_node *
bst_lookup(bst_node *root, size_t key)
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

bst_node *
alloc_bst_node(int sizeof_priv)
{
	struct bst_node *n = NULL;

	n = (bst_node *) calloc(1, sizeof(bst_node));
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
free_bst_node(struct bst_node *n)
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
bst_node_priv(struct bst_node *n)
{
	return (n != NULL) ? n->priv_data : NULL;
}

static void
bst_walk_tree_r(bst_node *root)
{
	if (root) {
		bst_walk_tree_r(root->link[0]);
		fprintf(stdout, "\tvalue: %ld\n", root->key);
		bst_walk_tree_r(root->link[1]);
	}
}

#ifdef TEST
int main(int argc, char **argv)
{
	bstree root = NULL;
	struct bst_node *n = NULL;
	int i = 0;
	int ret;

	srand(time(NULL));

	fprintf(stdout, "build tree:\n");
	for (i = 0; i < 10; i++) {
		n = alloc_bst_node(0);

		while (1) {
			n->key = rand() % 10;
			ret = bst_insert(&root, n);
			if (ret) {
				fprintf(stdout, "\tadd -> %ld\n", n->key);
				break;
			}
		}
	}

	fprintf(stdout, "dump tree: \n");
	bst_walk_tree_r(root);

	fprintf(stdout, "delete nodes: \n");
	for (i = 0; i < 10; i++) {
		size_t key = rand() % 10;

		n = bst_lookup(root, key);
		if (n) {
			fprintf(stdout, "\trm -> %ld\n", key);
			ret = bst_remove(&root, key);
		}
	}

	fprintf(stdout, "dump tree: \n");
	bst_walk_tree_r(root);

	return 0;
}
#endif
