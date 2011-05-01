/*
 * bstree.h - header file of the BS tree implementation
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

#ifndef __BSTREE_H__
#define __BSTREE_H__

typedef struct bst_node {
	/* left, right, parent */
	struct bst_node *link[3];
	size_t key;
	void *priv_data;
} bst_node;

typedef struct bst_node *bstree;

extern int bst_insert(bst_node **, bst_node *);
extern int bst_remove(bst_node **, size_t);
extern bst_node *bst_lookup(bst_node *, size_t);
extern bst_node *alloc_bst_node(int);
extern void free_bst_node(struct bst_node *);
extern void *bst_node_priv(struct bst_node *);

#endif	/* __BSTREE_H__ */


