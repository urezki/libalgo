#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <pthread.h>

#include "vm.h"
#include "vm_ops.h"
#include "debug.h"

static struct bpt_root free_area_root;
static pthread_spinlock_t free_area_lock;
ulong free_area_vstart = PAGE_SIZE;
ulong free_area_vend = ULONG_MAX;

static int
ascending_order(const void *a, const void *b)
{
	return (*(int *)a - *(int *)b);
}

static int
reverse_order(const void *a, const void *b)
{
	return (*(int *)b - *(int *)a);
}

static int
random_order(const void *a, const void *b)
{
	(void)a; (void)b;
	return rand() % 2 ? +1 : -1;
}

static void
shuffle(void *base, size_t nmemb, size_t size, int mask)
{
	srand(time(NULL));

	if (mask & 0x1) {
		qsort(base, nmemb, size, ascending_order);
	} else if (mask & 0x2) {
		qsort(base, nmemb, size, reverse_order);
	} else {
		qsort(base, nmemb, size, random_order);
	}
}

static unsigned long
rand_mask(unsigned long mask_len)
{
	srand(time(NULL));
	return (1UL << rand() % mask_len);
}

static inline void
time_now(struct timespec *t)
{
	(void) clock_gettime(CLOCK_MONOTONIC, t);
}

/*
 * returns differences in nanoseconds
 */
static inline unsigned long
time_diff(struct timespec *a, struct timespec *b)
{
	struct timespec res;
	int nsec;

	if (b->tv_nsec < a->tv_nsec) {
		nsec = (a->tv_nsec - b->tv_nsec) / 1000000000 + 1;
		a->tv_nsec -= 1000000000 * nsec;
		a->tv_sec += nsec;
	}

	res.tv_sec = b->tv_sec - a->tv_sec;
	res.tv_nsec = b->tv_nsec - a->tv_nsec;
	return (res.tv_sec * 1000000000) + res.tv_nsec;
}

int
verify_meta_data(struct bpt_root *root)
{
	struct list_head *pos;
	struct bpn *node;
	int i;

	list_for_each(pos, &root->head) {
		struct bpn *n = list_entry(pos, struct bpn, page.external.list);

		for (i = 0; i < n->entries; i++) {
			struct vmap_area *va = bpn_get_val(n, i);
			struct vmap_area *tmp;
			bool va_start_sort_broken = false;
			bool sub_avail_broken = false;

			tmp = lookup_smallest_va(root, va_size(va), 1, va->va_start, &node);
			if (tmp != va)
				sub_avail_broken = true;

			tmp = lookup_smallest_va(root, 1, 1, va->va_start, &node);
			if (tmp != va)
				va_start_sort_broken = true;

			if (va_start_sort_broken || sub_avail_broken) {
				printf("-> MODEL is corrupted: sort-broken: %d, " \
					"sub_avail_broken: %d, va->va_start: %ld, va->va_end: %ld\n",
					   va_start_sort_broken,
					   sub_avail_broken, va->va_start, va->va_end);
				exit(-1);
			}
		}
	}

	return 0;
}

static void *
thread_job(void *arg)
{
	struct vmap_area *va;
	struct vmap_area **array;
	int max_defer_free = 10000;
	int iteration = 100;
	ulong alloc_nsec;
	ulong free_nsec;
	int i, j, k;

	srand(time(NULL));
	array = calloc(max_defer_free, sizeof(struct vmap_area *));
	alloc_nsec = 0;
	free_nsec = 0;
	j = 0;

	while (iteration--) {
		for (i = 0; i < 100000; i++) {
			ulong size = ((rand() % 1000) + 1) * PAGE_SIZE;
			ulong align = ((rand() % 1000) + 1) * PAGE_SIZE;
			int mask = rand_mask(3);
			struct timespec a, b;
			struct list_head *pos;
			int high, nr = 0;

			pthread_spin_lock(&free_area_lock);
			time_now(&a);
			va = alloc_vmap_area(&free_area_root, size, align,
								VMALLOC_START, VMALLOC_END);
			time_now(&b);
			if (va)
				array[j++] = va;
			pthread_spin_unlock(&free_area_lock);

			alloc_nsec += time_diff(&a, &b);

			/* Flush. */
			if (mask & 0x1 || j == max_defer_free) {
				shuffle(array, j, sizeof(ulong *), mask);
				alloc_nsec = alloc_nsec / j;

				pthread_spin_lock(&free_area_lock);
				for (k = 0; k < j; k++) {
					time_now(&a);
					(void) free_vmap_area(&free_area_root, array[k]);
					time_now(&b);

					free_nsec += time_diff(&a, &b);
				}
				pthread_spin_unlock(&free_area_lock);
				free_nsec = free_nsec / k;
				j = 0;
#if DEBUG
				pthread_spin_lock(&free_area_lock);
				high = bpt_high(free_area_root.node);

				list_for_each(pos, &free_area_root.head)
					nr++;

				(void) verify_meta_data(&free_area_root);
				pthread_spin_unlock(&free_area_lock);

				printf("-> Nr nodes: %d, high is: %d, "
					   "node size: %ld, alloc: %ld nsec, free: %ld nsec\n",
					   nr, high, sizeof(struct bpn), alloc_nsec, free_nsec);

				alloc_nsec = 0;
				free_nsec = 0;
#endif
			}
		}

		printf("-> %d [%d] DONE %d loops, left non-freed %d objects\n",
			   gettid(), iteration, i, j);
	}

	if (j) {
		pthread_spin_lock(&free_area_lock);
		for (i = 0; i < j; i++)
			(void) free_vmap_area(&free_area_root, array[i]);
		pthread_spin_unlock(&free_area_lock);
	}

	free(array);
	return NULL;
}

static void test_alloc_free(int nr_jobs)
{
	pthread_t th_array[nr_jobs];
	int i, rv;

	vm_init_free_space(&free_area_root, free_area_vstart, free_area_vend);
	rv = pthread_spin_init(&free_area_lock, PTHREAD_PROCESS_PRIVATE);
	if (rv)
		BUG();

	pthread_spin_lock(&free_area_lock);
	for (i = 0; i < nr_jobs; i++) {
		(void) pthread_create(&th_array[i], NULL, thread_job, NULL);
	}
	pthread_spin_unlock(&free_area_lock);
	printf("-> Started %d jobs...\n", nr_jobs);

	for (i = 0; i < nr_jobs; i++)
		(void) pthread_join(th_array[i], NULL);

	dump_tree(&free_area_root);
}

int main(int argc, char **argv)
{
	test_alloc_free(10);
	return 0;
}
