/*
 * splay.h - header file of the SPLAY tree implementation
 * Copyright (C) 2019 Uladzislau Rezki (urezki@gmail.com)
 */

#ifndef __SPLAY_H__
#define __SPLAY_H__

struct splay_node {
	struct splay_node *left;
	struct splay_node *right;
	struct splay_node *parent;
	unsigned long val;
	unsigned long size;
	unsigned long max_size;
};

extern struct splay_node *splay_insert(struct splay_node *, struct splay_node *);
extern struct splay_node *splay_lookup(unsigned long, struct splay_node *);
extern struct splay_node *splay_remove(struct splay_node *, struct splay_node *);

#endif	/* __SPLAY_H__ */
