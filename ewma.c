#include <stdio.h>
#include <stdlib.h>

#define HIST_SIZE 5

int main(int argc, char **argv)
{
	unsigned int hist[HIST_SIZE] = { 0 };
	unsigned int weight;
	unsigned int ewma, avg;
	int index = 0;
	int i;

	while (1) {
		hist[index] = rand() % 100;
		/* hist[index] = 1; */

		printf("-> i: %d val: %u [ ", index, hist[index]);
		for (i = 0; i < HIST_SIZE; i++) {
			printf("%u ", hist[i]);
		}
		printf("] ");

		for (i = 0, avg = 0, ewma = 0, weight = HIST_SIZE - index - 1; i < HIST_SIZE; i++) {
			printf(" %u ", weight);

			avg += hist[i];
			ewma += hist[i] << weight;
			weight = ++weight % HIST_SIZE;
		}

		ewma /= ((1 << HIST_SIZE) - 1);
		printf(" - ewma: %u, avg: %u\n", ewma, avg / HIST_SIZE);

		/* Next index for value. */
		index = ++index % HIST_SIZE;
	}

	return 0;
}
