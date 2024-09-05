#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "btree_blue.h"

static unsigned long *
kmem_cache_alloc(int size)
{
	return malloc(size);
}

static void
kmem_cache_free(void *ptr)
{
	free(ptr);
}

static unsigned long *btree_blue_node_alloc(struct btree_blue_head *head,
					    int level)
{
	unsigned long *node;
	int size;

	if (likely(level == 1)) {
		size = head->leaf_size;
		node = kmem_cache_alloc(size);
	} else {
		size = head->node_size;
		node = kmem_cache_alloc(size);
	}

	if (likely(node))
		memset(node, 0, size);

	return node;
}

static void btree_blue_node_free(unsigned long *node, int level)
{
	kmem_cache_free(node);
}

static int longcmp(const unsigned long *l1, const unsigned long *l2, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (l1[i] < l2[i])
			return -1;
		if (l1[i] > l2[i])
			return 1;
	}
	return 0;
}

static unsigned long *longcpy(unsigned long *dest, const unsigned long *src,
			      size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		dest[i] = src[i];
	return dest;
}

static unsigned long *bkey(struct btree_blue_head *head,
			   struct btree_blue_node_cb *cb, int n)
{
	return cb->slots_base + n * head->slot_width + 1;
}

static void *bval(struct btree_blue_head *head, struct btree_blue_node_cb *cb,
		  int n)
{
	return (void *)(cb->slots_base[n * head->slot_width]);
}

static void setkey(struct btree_blue_head *head, struct btree_blue_node_cb *cb,
		   int n, unsigned long *key)
{
	longcpy(bkey(head, cb, n), key, head->keylen);
}

static void setval(struct btree_blue_head *head, struct btree_blue_node_cb *cb,
		   int n, void *val)
{
	cb->slots_base[n * head->slot_width] = (unsigned long)val;
}

static int keycmp(struct btree_blue_head *head, struct btree_blue_node_cb *cb,
		  int pos, unsigned long *key)
{
	return longcmp(bkey(head, cb, pos), key, head->keylen);
}

static int getpos(struct btree_blue_head *head, struct btree_blue_node_cb *cb,
		  unsigned long *key)
{
	int nr, q, r, i, p, c;

	nr = cb->slots_info.slots_nr;
	q = nr / 8;

	for (i = 1; i <= q; i++) {
		p = i * 8 - 1;
		c = keycmp(head, cb, p, key);
		if (c < 0) {
			p = p - 7;
			for (i = 0; i < 7; i++) {
				c = keycmp(head, cb, p, key);
				if (c <= 0)
					return p;
				p++;
			}
			return p;

		} else if (c == 0)
			return p;
	}

	p = q * 8;
	r = nr % 8;
	for (i = 0; i < r; i++) {
		c = keycmp(head, cb, p, key);
		if (c <= 0)
			return p;
		p++;
	}

	return nr;
}

static int geteqv(struct btree_blue_head *head, struct btree_blue_node_cb *cb,
		  unsigned long *key)
{
	int nr, q, r, i, p, c;

	nr = cb->slots_info.slots_nr;
	q = nr / 8;

	for (i = 1; i <= q; i++) {
		p = i * 8 - 1;
		c = keycmp(head, cb, p, key);
		if (c < 0) {
			p = p - 7;
			for (i = 0; i < 7; i++) {
				c = keycmp(head, cb, p, key);
				if (c == 0)
					return p;
				p++;
			}
			return nr;
		} else if (c == 0)
			return p;
	}

	p = q * 8;
	r = nr % 8;
	for (i = 0; i < r; i++) {
		c = keycmp(head, cb, p, key);
		if (c == 0)
			return p;
		p++;
	}

	return nr;
}

/* binary search */
/*
static int getpos(struct btree_blue_head *head,
		      struct btree_blue_node_cb *cb, unsigned long *key)
{
	int l = 0;
	int h = cb->slots_info.slots_nr;
	int m, ret;

	while (l < h) {
		m = (l + h) / 2;
		ret = keycmp(head, cb, m, key);

		if (ret < 0)
			h = m;
		else if (ret > 0)
			l = m + 1;
		else
			return m;
	}

	return h;
}

static int geteqv(struct btree_blue_head *head,
		      struct btree_blue_node_cb *cb, unsigned long *key)
{
	int l = 0;
	int h, nr = cb->slots_info.slots_nr;
	int m, ret;

	while (l < h) {
		m = (l + h) / 2;
		ret = keycmp(head, cb, m, key);

		if (ret < 0)
			h = m;
		else if (ret > 0)
			l = m + 1;
		else
			return m;
	}

	return nr;
}
 */

static inline struct btree_blue_stub *__get_stub(struct btree_blue_head *head,
						 struct btree_blue_node_cb *cb)
{
	return (struct btree_blue_stub *)((char *)cb + head->stub_base);
}

static inline void _shift_slots(struct btree_blue_head *head,
				struct btree_blue_node_cb *cb, int dest_slot,
				int src_slot, size_t slots_nr)
{
	unsigned long *d = cb->slots_base + dest_slot * head->slot_width;
	unsigned long *s = cb->slots_base + src_slot * head->slot_width;

	size_t n = slots_nr * head->slot_width * sizeof(long);

	memmove(d, s, n);
}

static inline void _transfer_slots(struct btree_blue_head *head,
				   struct btree_blue_node_cb *dest,
				   struct btree_blue_node_cb *src,
				   int dest_slot, int src_slot, size_t slots_nr)
{
	unsigned long *d = dest->slots_base + dest_slot * head->slot_width;
	unsigned long *s = src->slots_base + src_slot * head->slot_width;

	size_t n = slots_nr * head->slot_width * sizeof(long);

	memmove(d, s, n);
}

static inline int shift_slots_on_insert(struct btree_blue_head *head,
					struct btree_blue_node_cb *node,
					int pos, int level)
{
	int slots_nr = node->slots_info.slots_nr;
	_shift_slots(head, node, pos + 1, pos, slots_nr - pos);
	node->slots_info.slots_nr++;
	return pos;
}

static inline void delete_slot(struct btree_blue_head *head,
			       struct btree_blue_node_cb *node, int pos,
			       int level)
{
	int slots_nr = node->slots_info.slots_nr;
	_shift_slots(head, node, pos, pos + 1, slots_nr - pos - 1);
	node->slots_info.slots_nr--;
}

static inline void split_to_empty(struct btree_blue_head *head,
				  struct btree_blue_node_cb *dest,
				  struct btree_blue_node_cb *src, int level)
{
	int slots_nr = src->slots_info.slots_nr / 2;

	_transfer_slots(head, dest, src, 0, 0, slots_nr);
	dest->slots_info.slots_nr += slots_nr;

	_shift_slots(head, src, 0, slots_nr,
		     src->slots_info.slots_nr - slots_nr);
	src->slots_info.slots_nr -= slots_nr;
}

static inline void merge_nodes(struct btree_blue_head *head,
			       struct btree_blue_node_cb *dest,
			       struct btree_blue_node_cb *src, int level)
{
	int dest_nr, src_nr;

	dest_nr = dest->slots_info.slots_nr;
	src_nr = src->slots_info.slots_nr;

	_transfer_slots(head, dest, src, dest_nr, 0, src_nr);
	dest->slots_info.slots_nr += src_nr;
}

void *btree_blue_first(struct btree_blue_head *head, unsigned long *__key)
{
	int height = head->height;
	struct btree_blue_node_cb *node =
		(struct btree_blue_node_cb *)head->node;

	if (height == 0)
		return NULL;

	for (; height > 1; height--)
		node = bval(head, node, node->slots_info.slots_nr - 1);

	longcpy(__key, bkey(head, node, node->slots_info.slots_nr - 1),
		head->keylen);

	return bval(head, node, node->slots_info.slots_nr - 1);
}

void *btree_blue_last(struct btree_blue_head *head, unsigned long *__key)
{
	int height = head->height;
	struct btree_blue_node_cb *node =
		(struct btree_blue_node_cb *)head->node;

	if (height == 0)
		return NULL;
	for (; height > 1; height--)
		node = bval(head, node, 0);

	longcpy(__key, bkey(head, node, 0), head->keylen);

	return bval(head, node, 0);
}

static unsigned long *btree_blue_lookup_node(struct btree_blue_head *head,
					     unsigned long *key)
{
	int pos, height;
	struct btree_blue_node_cb *node;

	height = head->height;
	if (height == 0)
		return NULL;

	node = (struct btree_blue_node_cb *)head->node;

	for (; height > 1; height--) {
		pos = getpos(head, node, key);
		if (pos == node->slots_info.slots_nr)
			return NULL;

		node = bval(head, node, pos);
	}

	return (unsigned long *)node;
}

void *btree_blue_lookup(struct btree_blue_head *head, unsigned long *key)
{
	int pos;
	struct btree_blue_node_cb *node;

	node = (struct btree_blue_node_cb *)btree_blue_lookup_node(head, key);
	if (!node)
		return NULL;

	pos = geteqv(head, node, key);
	if (pos == node->slots_info.slots_nr)
		return NULL;

	return bval(head, node, pos);
}

int btree_blue_update(struct btree_blue_head *head, unsigned long *key,
		      void *val)
{
	int pos;
	struct btree_blue_node_cb *node;

	node = (struct btree_blue_node_cb *)btree_blue_lookup_node(head, key);
	if (!node)
		return -1;

	pos = geteqv(head, node, key);
	/*pos = geteqv_bin(head, node, key);*/

	if (pos == node->slots_info.slots_nr)
		return -1;

	setval(head, node, pos, val);
	return 0;
}

void *btree_blue_prev_or_next(struct btree_blue_head *head,
			      unsigned long *__key, int flag)
{
	int i;
	struct btree_blue_node_cb *node;
	unsigned long key[MAX_KEYLEN];
	int slots_nr;

	if (head->height == 0)
		return NULL;

	longcpy(key, __key, head->keylen);

	node = (struct btree_blue_node_cb *)btree_blue_lookup_node(head, key);
	if (!node)
		return NULL;

	slots_nr = node->slots_info.slots_nr;
	i = geteqv(head, node, key);
	/*
	for (i = 0; i < slots_nr; i++)
		if (keycmp(head, node, i, key) == 0)
			break;
	*/
	if (i == slots_nr)
		return NULL;

	if (flag == GET_PREV) {
		if (++i < slots_nr) {
			longcpy(__key, bkey(head, node, i), head->keylen);
			return bval(head, node, i);
		} else {
			struct btree_blue_stub *stub = __get_stub(head, node);
			if (stub->next) {
				node = (struct btree_blue_node_cb *)(stub->next);
				longcpy(__key, bkey(head, node, 0),
					head->keylen);
				return bval(head, node, 0);
			} else
				return NULL;
		}
	}

	/* GET_NEXT  */

	if (i > 0) {
		--i;
		longcpy(__key, bkey(head, node, i), head->keylen);
		return bval(head, node, i);
	} else {
		struct btree_blue_stub *stub = __get_stub(head, node);
		if (stub->prev) {
			node = (struct btree_blue_node_cb *)(stub->prev);
			longcpy(__key,
				bkey(head, node, node->slots_info.slots_nr - 1),
				head->keylen);
			return bval(head, node, 0);
		} else
			return NULL;
	}
}

void *btree_blue_get_prev(struct btree_blue_head *head, unsigned long *__key)
{
	return btree_blue_prev_or_next(head, __key, GET_PREV);
}

void *btree_blue_get_next(struct btree_blue_head *head, unsigned long *__key)
{
	return btree_blue_prev_or_next(head, __key, GET_NEXT);
}

static unsigned long *find_level(struct btree_blue_head *head,
				 unsigned long *key, int level,
				 struct btree_blue_node_cb **cb_p)
{
	struct btree_blue_node_cb *node =
		(struct btree_blue_node_cb *)head->node;
	struct btree_blue_node_cb *node_p = node;
	int height = head->height;
	int pos;

	for (; height > level; height--) {
		pos = getpos(head, node, key);
		if (pos == node->slots_info.slots_nr) {
			/* right-most key is too large, update it */
			/* FIXME: If the right-most key on higher levels is
			 * always zero, this wouldn't be necessary. */
			pos--;
			setkey(head, node, pos, key);
		}

		BUG_ON(pos < 0);
		node_p = node;
		node = bval(head, node, pos);
	}

	BUG_ON(!node);
	*cb_p = node_p;
	return (unsigned long *)node;
}

static int btree_blue_grow(struct btree_blue_head *head)
{
	struct btree_blue_node_cb *node, *node_h;

	node = (struct btree_blue_node_cb *)btree_blue_node_alloc(
		head, head->height + 1);
	if (!node)
		return -1;

	if (likely(head->node)) {
		node_h = (struct btree_blue_node_cb *)head->node;
		setkey(head, node, 0,
		       bkey(head, node_h, node_h->slots_info.slots_nr - 1));
		setval(head, node, 0, head->node);
		node->slots_info.slots_nr = 1;
	}

	head->node = (unsigned long *)node;
	head->height++;

	return 0;
}

static void btree_blue_shrink(struct btree_blue_head *head)
{
	struct btree_blue_node_cb *node;

	if (head->height <= 1)
		return;

	node = (struct btree_blue_node_cb *)head->node;
	BUG_ON(node->slots_info.slots_nr > 1);

	head->node = bval(head, node, 0);
	btree_blue_node_free((unsigned long *)node, head->height);
	head->height--;

	//mempool_free(node, head->mempool);
}

static int btree_blue_insert_level(struct btree_blue_head *head,
				   unsigned long *key, void *val, int level,
				   struct btree_blue_node_cb *found)
{
	struct btree_blue_node_cb *cb, *cb_new, *cb_p;
	int pos, slots_nr, err;

	BUG_ON(!val);

	if (head->height < level) {
		err = btree_blue_grow(head);
		if (err)
			return err;

		found = 0;
	}

	if (!found)
		cb = (struct btree_blue_node_cb *)find_level(head, key, level, &cb_p);
	else {
		cb = found;
		cb_p = NULL;
	}

	pos = getpos(head, cb, key);
	slots_nr = cb->slots_info.slots_nr;

	/* two identical keys are not allowed */
	BUG_ON(pos < slots_nr && keycmp(head, cb, pos, key) == 0);

	if (slots_nr == head->slot_vols[level]) {
		/* need to split node */
		struct btree_blue_node_cb *cb_prev;
		struct btree_blue_stub *stub, *stub_new, *stub_prev;

		cb_new = (struct btree_blue_node_cb *)btree_blue_node_alloc(
			head, level);
		if (!cb_new)
			return -1;

		err = btree_blue_insert_level(head,
			bkey(head, cb, slots_nr / 2 - 1),
			cb_new, level + 1, cb_p);
		if (err) {
			//mempool_free(cb_new, head->mempool);
			btree_blue_node_free((unsigned long *)cb_new, level);
			return err;
		}

		if (level == 1) {
			stub = __get_stub(head, cb);
			stub_new = __get_stub(head, cb_new);
			stub_new->next = (unsigned long *)cb;

			if (stub->prev) {
				cb_prev = (struct btree_blue_node_cb
						   *)(stub->prev);
				stub_prev = __get_stub(head, cb_prev);
				stub_prev->next = (unsigned long *)cb_new;
				stub_new->prev = stub->prev;
			}
			stub->prev = (unsigned long *)cb_new;
		}

		split_to_empty(head, cb_new, cb, level);

		if (pos <= (slots_nr / 2 - 1)) {
			slots_nr = slots_nr / 2;
			cb = cb_new;
		} else {
			pos = pos - slots_nr / 2;
			slots_nr = slots_nr - slots_nr / 2;
		}
	}

	BUG_ON(slots_nr >= head->slot_vols[level]);

	/* shift and insert */
	//pos = shift_slots_on_insert(head, cb, pos, level);
	_shift_slots(head, cb, pos + 1, pos, slots_nr - pos);
	cb->slots_info.slots_nr++;

	setkey(head, cb, pos, key);
	setval(head, cb, pos, val);

	return 0;
}

int btree_blue_insert(struct btree_blue_head *head, unsigned long *key,
		      void *val)
{
	BUG_ON(!val);
	return btree_blue_insert_level(head, key, val, 1, 0);
}

static void *btree_blue_remove_level(struct btree_blue_head *head,
				     unsigned long *key, int level);

static void merge(struct btree_blue_head *head, int level,
		  struct btree_blue_node_cb *cb_left,
		  struct btree_blue_node_cb *cb_right,
		  struct btree_blue_node_cb *cb_parent, int lpos)
{
	struct btree_blue_node_cb *cb_right_right;

	struct btree_blue_stub *stub_left, *stub_right, *stub_right_right;

	/* Move all keys to the left */
	merge_nodes(head, cb_left, cb_right, level);

	if (level == 1) {
		stub_left = __get_stub(head, cb_left);
		stub_right = __get_stub(head, cb_right);

		if (stub_right->next) {
			stub_left->next = stub_right->next;

			cb_right_right =
				(struct btree_blue_node_cb *)(stub_right->next);
			stub_right_right = __get_stub(head, cb_right_right);
			stub_right_right->prev = (unsigned long *)cb_left;
		} else
			stub_left->next = NULL;
	}

	/* Exchange left and right child in parent */
	setval(head, cb_parent, lpos, cb_right);
	setval(head, cb_parent, lpos + 1, cb_left);
	/* Remove left (formerly right) child from parent */
	btree_blue_remove_level(head, bkey(head, cb_parent, lpos), level + 1);
	//mempool_free(cb_right, head->mempool);
	btree_blue_node_free((unsigned long *)cb_right, level);
}

static void rebalance(struct btree_blue_head *head, unsigned long *key,
		      int level, struct btree_blue_node_cb *cb_child,
		      struct btree_blue_node_cb *cb_p)
{
	struct btree_blue_node_cb *cb_parent, *cb_left, *cb_right;
	struct btree_blue_stub *stub_child, *stub_left, *stub_right;
	int i;
	int slots_nr, slots_nr_left, slots_nr_right;

	slots_nr = cb_child->slots_info.slots_nr;

	if (slots_nr == 0) {
		/* Because we don't steal entries from a neighbour, this case
		 * can happen.  Parent node contains a single child, this
		 * node, so merging with a sibling never happens.
		 */
		btree_blue_remove_level(head, key, level + 1);

		if (level == 1) {
			stub_child = __get_stub(head, cb_child);
			if (stub_child->prev) {
				cb_left = (struct btree_blue_node_cb
						   *)(stub_child->prev);
				stub_left = __get_stub(head, cb_left);
				stub_left->next = stub_child->next ?
								stub_child->next :
								NULL;
			}

			if (stub_child->next) {
				cb_right = (struct btree_blue_node_cb
						    *)(stub_child->next);
				stub_right = __get_stub(head, cb_right);
				stub_right->prev = stub_child->prev ?
								 stub_child->prev :
								 NULL;
			}
		}

		//mempool_free(cb_child, head->mempool);
		btree_blue_node_free((unsigned long *)cb_child, level);
		return;
	}

	cb_parent = cb_p;

	i = getpos(head, cb_parent, key);
	BUG_ON(bval(head, cb_parent, i) != cb_child);

	if (i > 0) {
		cb_left = bval(head, cb_parent, i - 1);
		slots_nr_left = cb_left->slots_info.slots_nr;

		if (slots_nr_left + slots_nr <= head->slot_vols[level]) {
			merge(head, level, cb_left, cb_child, cb_parent, i - 1);
			return;
		}
	}

	if (i + 1 < cb_parent->slots_info.slots_nr) {
		cb_right = bval(head, cb_parent, i + 1);
		slots_nr_right = cb_right->slots_info.slots_nr;

		if (slots_nr + slots_nr_right <= head->slot_vols[level]) {
			merge(head, level, cb_child, cb_right, cb_parent, i);
			return;
		}
	}
	/*
	 * We could also try to steal one entry from the left or right
	 * neighbor.  By not doing so we changed the invariant from
	 * "all nodes are at least half full" to "no two neighboring
	 * nodes can be merged".  Which means that the average fill of
	 * all nodes is still half or better.
	 */
}

static void *btree_blue_remove_level(struct btree_blue_head *head,
				     unsigned long *key, int level)
{
	struct btree_blue_node_cb *cb, *cb_p;
	int pos, slots_nr;
	void *ret;

	if (level > head->height) {
		/* we recursed all the way up */
		head->height = 0;
		head->node = NULL;
		return NULL;
	}

	cb = (struct btree_blue_node_cb *)find_level(head, key, level, &cb_p);
	slots_nr = cb->slots_info.slots_nr;
	pos = getpos(head, cb, key);

	if ((level == 1) && (pos == slots_nr))
		return NULL;

	ret = bval(head, cb, pos);

	/* remove and shift */
	//delete_slot(head, cb, pos, level);
	_shift_slots(head, cb, pos, pos + 1, slots_nr - pos - 1);
	cb->slots_info.slots_nr--;

	if (cb->slots_info.slots_nr < head->slot_vols[level] / 2 - 2) {
		if (level < head->height)
			rebalance(head, key, level, cb, cb_p);
		else if (cb->slots_info.slots_nr == 1)
			btree_blue_shrink(head);
	}

	return ret;
}

void *btree_blue_remove(struct btree_blue_head *head, unsigned long *key)
{
	if (head->height == 0)
		return NULL;

	return btree_blue_remove_level(head, key, 1);
}

int btree_blue_init(struct btree_blue_head *head,
				 int node_size_in_byte, int key_len_in_byte,
				 int flags)
{
	int x;

	if (node_size_in_byte % L1_CACHE_BYTES)
		exit(-1);

	if ((node_size_in_byte < MIN_NODE_SIZE) ||
	    (node_size_in_byte > PAGE_SIZE))
		exit(-1);

	if ((key_len_in_byte != sizeof(unsigned long)) &&
	    (key_len_in_byte != 2 * sizeof(unsigned long)))
		exit(-1);

	head->node = NULL;
	head->height = 0;

	head->keylen = (key_len_in_byte * BITS_PER_BYTE) / BITS_PER_LONG;
	head->slot_width =
		(VALUE_LEN * BITS_PER_BYTE) / BITS_PER_LONG + head->keylen;

	if (node_size_in_byte > 512)
		x = 512;
	else
		x = node_size_in_byte;
	head->node_size = x;
	head->leaf_size = node_size_in_byte;

	for (int i = 2; i < MAX_TREE_HEIGHT + 1; i++) {
		x = (head->node_size - sizeof(struct btree_blue_node_cb)) /
		    (head->slot_width * sizeof(long));
		head->slot_vols[i] = x;
	}

	x = (head->leaf_size - sizeof(struct btree_blue_node_cb) -
	     sizeof(struct btree_blue_stub)) /
	    (head->slot_width * sizeof(long));
	head->slot_vols[0] = head->slot_vols[1] = x;

	head->stub_base =
		sizeof(struct btree_blue_node_cb) +
		head->slot_vols[1] * (head->slot_width * sizeof(long));

	return 0;
}

void btree_blue_destroy(struct btree_blue_head *head)
{
	head->node = NULL;
}

static struct btree_blue_head btree_blue_root;
static int node_size = 512;
static int key_len = sizeof(unsigned long);

struct key_value {
	unsigned long k;
	unsigned long v;
};

int rand_comparison(const void *a, const void *b)
{
	(void)a; (void)b;
	return rand() % 2 ? +1 : -1;
}

void shuffle(void *base, size_t nmemb, size_t size)
{
	srand(time(NULL));
	qsort(base, nmemb, size, rand_comparison);
}

int main(int argc, char **argv)
{
	unsigned int tree_size = 1000000;
	unsigned long *array, *val;
	int i, ret;

	array = calloc(tree_size, sizeof(unsigned long));
	BUG_ON(array == NULL);

	for (i = 0; i < tree_size; i++)
		array[i] = i;

	shuffle(array, tree_size, sizeof(unsigned long));

	ret = btree_blue_init(&btree_blue_root, node_size, key_len, 0);
	if (ret) {
		printf("error: failed to init btree_blue\n");
		exit(-1);
	}

	puts("Start inserting...");

	for (i = 0; i < tree_size; i++) {
		ret = btree_blue_insert(&btree_blue_root, &array[i], &i);
		if (ret) {
			printf("insert error: key is %lu\n", array[i]);
			exit(-1);
		}
	}

	val = btree_blue_remove(&btree_blue_root, &array[0]);
	if (val)
		fprintf(stdout, "-> val found!!!\n");

	return 0;
}
