#include <stdio.h>
#include <stdlib.h>

#include "b+tree.h"
#include "debug.h"

#ifdef DEBUG_BP_TREE
static void
build_graph(struct node *n)
{
	int i;

	if (is_node_external(n)) {
		printf("\tnode%lu[label = \"", n->num);
		for (i = 0; i < n->entries; i++)
			printf("%lu ", get_slot_key(n, i));

		printf("\"];\n");
		return;
	}

	printf("\tnode%lu[label = \"<p0>", n->num);
	for (i = 0; i < n->entries; i++)
		printf(" |%lu| <p%d>", (unsigned long) get_slot_key(n, i), i + 1);

	printf("\"];\n");

	for (i = 0; i <= n->entries; i++)
		printf("\t\"node%lu\":p%d -> \"node%lu\"\n",
			n->num, i, n->page.internal.sub_links[i]->num);

	for (i = 0; i <= n->entries; i++)
		build_graph(n->page.internal.sub_links[i]);
}

static void
assign_node_id(struct node *n, int *num)
{
	int i;

	if (is_node_external(n)) {
		n->num = *num;
		return;
	}

	n->num = *num;
	for (i = 0; i <= n->entries; i++) {
		(*num)++;
		assign_node_id(n->page.internal.sub_links[i], num);
	}
}

void dump_tree(struct node *root)
{
	int num = 0;

	assign_node_id(root, &num);

	fprintf(stdout, "digraph G\n{\n");
	fprintf(stdout, "node [shape = record,height=.1];\n");
	build_graph(root);
	fprintf(stdout, "}\n");

	fprintf(stdout, "# run: ./test | dot -Tpng -o btree.png\n");
}
#endif
