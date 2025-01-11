#ifndef __DEBUG__
#define __DEBUG__

#ifdef DEBUG_BP_TREE
extern void dump_tree(struct bpt_root *);
#else
static inline void dump_tree(struct bpt_root *n) {}
#endif

#endif
