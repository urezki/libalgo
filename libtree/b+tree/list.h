#ifndef __B_PLUS_LIST__
#define __B_PLUS_LIST__

#define list_entry(ptr, type, member)	\
	((type *)((char *)(ptr) - (size_t)(&((type *)0)->member)))

#define list_first_entry(ptr, type, member)	\
	list_entry((ptr)->next, type, member)

#define list_last_entry(ptr, type, member)	\
	list_entry((ptr)->prev, type, member)

#define list_for_each(pos, head)	\
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head)	\
	for (pos = (head)->next, n = pos->next; pos != (head);	\
		 pos = n, n = pos->next)

struct list_head {
	struct list_head *prev, *next;
};

static inline void list_init(struct list_head *link)
{
	link->prev = link;
	link->next = link;
}

static inline void
__list_add(struct list_head *link, struct list_head *prev, struct list_head *next)
{
	link->next = next;
	link->prev = prev;
	next->prev = link;
	prev->next = link;
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
	prev->next = next;
	next->prev = prev;
}

static inline void list_add(struct list_head *link, struct list_head *prev)
{
	__list_add(link, prev, prev->next);
}

static inline void list_add_tail(struct list_head *link, struct list_head *head)
{
	__list_add(link, head->prev, head);
}

static inline void list_del(struct list_head *link)
{
	__list_del(link->prev, link->next);
	list_init(link);
}

static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

static inline struct list_head *
list_prev_or_null(struct list_head *list, struct list_head *head)
{
	if (list_empty(list) || list->prev == head)
		return NULL;

	return list->prev;
}

static inline struct list_head *
list_next_or_null(struct list_head *list, struct list_head *head)
{
	if (list_empty(list) || list->next == head)
		return NULL;

	return list->next;
}

#endif	/* __B_PLUS_LIST__ */
