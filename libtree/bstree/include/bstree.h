#ifndef __BSTREE_H__
#define __BSTREE_H__

typedef struct bst_node {
	/* left, right, parent */
	struct bst_node *link[3];
	size_t key;
	void *data;
} bst_node;

typedef struct bst_node *bstree;

extern int bst_insert(bst_node **, bst_node *);
extern int bst_remove(bst_node **, size_t);
extern bst_node *bst_lookup(bst_node *, size_t);

#endif	/* __BSTREE_H__ */


