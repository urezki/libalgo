#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

/* locals */
#include <hash.h>

static suseconds_t
usec_diff(struct timeval *a, struct timeval *b)
{
	suseconds_t microsec;

	microsec = b->tv_sec - a->tv_sec;
	microsec *= 1000000;
	microsec += b->tv_usec - a->tv_usec;

	return microsec;
}

int main(int argc, char **argv)
{
	suseconds_t adding_time = 0;
	suseconds_t lookup_time = 0;
	suseconds_t delete_time = 0;
	int adding_count = 0;
	int lookup_count = 0;
	int delete_count = 0;
	struct timeval tv_1;
	struct timeval tv_2;
	struct hash *h;
	int ret;

	h = hash_create(1000);
	if (h) {
		for (int i = 0; i < (int) h->hash_size; i++) {
			char key[KEY_SIZE] = {'\0'};

			snprintf(key, sizeof(key), "%s_%d", "test", i);

			gettimeofday(&tv_1, NULL);
			ret = hash_add(h, key, NULL);
			gettimeofday(&tv_2, NULL);

			adding_time += usec_diff(&tv_1, &tv_2);
			adding_count++;
			if (ret < 0)
				;
		}

		hash_dump(h);

		for (int i = 0; i < (int) h->hash_size; i++) {
			char key[KEY_SIZE] = {'\0'};
			struct hash_entry *entry;

			snprintf(key, sizeof(key), "%s_%d", "test", i);

			gettimeofday(&tv_1, NULL);
			entry = hash_lookup(h, key);
			gettimeofday(&tv_2, NULL);

			lookup_time += usec_diff(&tv_1, &tv_2);
			lookup_count++;

			if (entry) {
				fprintf(stdout, "found %p -- %d -- %s\n", entry, i, key);
				gettimeofday(&tv_1, NULL);
				hash_del(h, key);
				gettimeofday(&tv_2, NULL);
				delete_time += usec_diff(&tv_1, &tv_2);
				delete_count++;
			} else {
				fprintf(stdout, "not found %p -- %d -- %s\n", entry, i, key);
			}
		}

		hash_dump(h);
		hash_destroy(h);
	}

	fprintf(stdout, "adding average time: %lf microseconds\n", adding_time/(double)adding_count);
	fprintf(stdout, "lookup average time: %lf microseconds\n", lookup_time/(double)lookup_count);
	fprintf(stdout, "delete average time: %lf microseconds\n", delete_time/(double)delete_count);

	return 0;
}
