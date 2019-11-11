/*
 * avl.h - header file of the AVL tree implementation
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

#ifndef __AVL_H__
#define __AVL_H__

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef struct avl_node {
	/* left, right, parent */
	struct avl_node *link[3];
	long int key;
	short int bf;
} avl_node;

typedef struct avl_tree {
	struct avl_node *root;
	size_t count;
} avl_tree;

extern int avl_insert(struct avl_node **, struct avl_node *);
extern struct avl_node *avl_lookup(struct avl_node *, long);
extern void avl_remove(struct avl_node **, long);
extern void avl_free(struct avl_node *);
extern struct avl_node *avl_alloc(void);

#endif	/* __AVL_H__ */
