#ifndef __HASH_H__
#define __HASH_H__

#define KEY_SIZE 4096

typedef struct hash_entry {
	struct hash_entry *next;
	struct hash_entry *prev;
	time_t born_time;

	unsigned int index;
	char key[KEY_SIZE];
	void *data;
} hash_entry;

typedef struct hash {
	unsigned int hash_size;
	void *hash_table[0];
} hash;

extern struct hash *hash_create(int);
extern void hash_destroy(struct hash *);
extern struct hash_entry *hash_lookup(struct hash *, const char *);
extern int hash_add(struct hash *, const char *, void *);
extern int hash_del(struct hash *, const char *);
extern int hash_del_entry(struct hash *, struct hash_entry *);
extern void hash_dump(struct hash *);
extern int hash_resize(struct hash *, int);
extern int hash_rehash(struct hash *);

#endif	/* __HASH_H__ */
