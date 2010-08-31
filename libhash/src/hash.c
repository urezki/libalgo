#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* local */
#include <hash.h>

/* +-----+-----+-----+ */
/* |  0  |  a  |  -  | */
/* +-----+-----+-----+ */
/* |  1  |  -  |  -  | */
/* +-----+-----+-----+ */
/* |  2  |  a  |  b  | */
/* +-----+-----+-----+ */
/* |  3  |  a  |  -  | */
/* +-----+-----+-----+ */
/* |  4  |  a  |  -  | */
/* +-----+-----+-----+ */
/* |  5  |  a  |  b  | */
/* +-----+-----+-----+ */
/* |  6  |  a  |  -  | */
/* +-----+-----+-----+ */
/* |  7  |  -  |  -  | */
/* +-----+-----+-----+ */
/* |  8  |  a  |  b  | */
/* +-----+-----+-----+ */
/* |  9  |  -  |  -  | */
/* +-----+-----+-----+ */

#define POISONED ((void *) 0x00100100) /* hit poisoned address */

static inline unsigned int
hash_function(const char *key, int table_size)
{
	unsigned int h = 0;
	unsigned int g = 0;

	while (*key) {
		h = (h << 4U) + *key++;
		if ((g = h & 0xf0000000))
			h ^= g >> 24U;

		h &= ~g;
	}

	return h % table_size;
}

int
hash_del(struct hash *h, const char *key)
{
	struct hash_entry *tmp;
	unsigned int index;

	if (h && key) {
		index = hash_function(key, h->hash_size);
		tmp = h->hash_table[index];
		if (tmp == NULL)
			goto out;

		/* go through the list of collision */
		for (; tmp; tmp = tmp->next) {
			if (!strcmp(tmp->key, key)) {
				int ret = hash_del_entry(h, tmp);

				if (ret)
					return 1;
				else
					goto out;
			}
		}
	}

out:
	return 0;
}

int
hash_del_entry(struct hash *h, struct hash_entry *entry)
{
	if (h && entry) {
		if (entry->prev)
			entry->prev->next = entry->next;
		if (entry->next)
			entry->next->prev = entry->prev;

		/* if it's first, shift head */
		if (entry->prev == NULL)
			h->hash_table[entry->index] = entry->next;

		free(entry);
		return 1;
	}

	return 0;
}

void
hash_destroy(struct hash *h)
{
	struct hash_entry *tmp;
	int i;

	if (h) {
		for (i = 0; h->hash_table[i] != POISONED; i++) {
			tmp = h->hash_table[i];
			while (tmp) {
				(void) hash_del(h, tmp->key);
				tmp = h->hash_table[i];
			}

			h->hash_table[i] = NULL;
		}

		free(h);
	}
}

int
hash_add(struct hash *h, const char *key, void *data)
{
	struct hash_entry *node;
	struct hash_entry *tmp;
	unsigned int index;

	if (h == NULL || key == NULL)
		goto out;

	index = hash_function(key, h->hash_size);
	node = (hash_entry *) calloc(1, sizeof(hash_entry));

	if (node) {
		/* fill out the node */
		(void) strncpy(node->key, key, sizeof(node->key));
		node->born_time = time(NULL);
		node->index = index;
		node->data = data;
		node->next = NULL;
		node->prev = NULL;

		if (h->hash_table[index] == NULL) {
			h->hash_table[index] = node;
		} else {
			tmp = h->hash_table[index];
			while (1) {
				if (!strcmp(tmp->key, key))
					goto out_and_free;

				if (tmp->next)
					/* the latest */
					tmp = tmp->next;
				else
					break;
			}

			/* add to queue */
			tmp->next = node;
			node->next = NULL;
			node->prev = tmp;
		}

		/* success */
		return 1;
	}

out_and_free:
	free(node);
out:
	return 0;
}

struct hash_entry *
hash_lookup(struct hash *h, const char *key)
{
	struct hash_entry *tmp;
	unsigned int index;

	if (h && key) {
		index = hash_function(key, h->hash_size);
		tmp = h->hash_table[index];
		if (tmp == NULL)
			goto out;

		while (tmp) {
			if (!strcmp(tmp->key, key))
				return tmp;

			tmp = tmp->next;
		}
	}

out:
	return NULL;
}

struct hash *
hash_create(int table_size)
{
	struct hash *h;
	int i;

	if (table_size > 0) {
		h = (struct hash *) calloc(1, sizeof(struct hash) + sizeof(void *) * (table_size + 1));
		if (h) {
			for (i = 0; i < table_size; i++)
				h->hash_table[i] = NULL;

			h->hash_table[i] = (hash_entry *) POISONED;
			h->hash_size = table_size;
			return h;
		}
	}

	return NULL;
}

int
hash_resize(struct hash *h, int size)
{
	/* TODO: implement it */
	return 0;
}

int
hash_rehash(struct hash *h)
{
	/*
	 * Algorithm of rehashing is quite simple. This routine does
	 * following things:
	 *
	 * 1) picks up all entries, except for marked as NULL;
	 * 2) generates new index according to the key;
	 * 3) if new index is not equal to old one then make replacing.
	 */
	if (h) {
		for (int i = 0; h->hash_table[i] != (hash_entry *) POISONED && h->hash_table[i]; i++) {
			struct hash_entry *tmp = h->hash_table[i];
			unsigned int index = hash_function(tmp->key, h->hash_size);

			/* rehash everyone who is in queue */
			do {
				if (tmp->index != index) {
					int ret;

					ret = hash_add(h, tmp->key, tmp->data);
					if (ret) {
						ret = hash_del_entry(h, tmp);
					}
				}

			} while (tmp->next);
		}
	}

	return 0;
}

void
hash_dump(struct hash *h)
{
	struct hash_entry *n;
	int i;

	if (h) {
		for (i = 0; h->hash_table[i] != (hash_entry *) POISONED; i++) {
			n = h->hash_table[i];
			if (n) {
				fprintf(stdout, "%d ", i);
				do {
					fprintf(stdout, "+");
				} while ((n = n->next));

				fprintf(stdout, "\n");
			} else {
				fprintf(stdout, "%d -\n", i);
			}
		}
	}
}
