#ifndef __DEBUG__
#define __DEBUG__

#ifdef DEBUG_BP_TREE
extern void dump_tree(struct node *);
#else
static inline void dump_tree(struct node *n) {}
#endif

#endif
