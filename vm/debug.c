#include <stdio.h>
#include <stdlib.h>

#include "vm.h"
#include "debug.h"

#ifdef DEBUG_BP_TREE
static void
build_graph(struct bpn *n)
{
	int i;

	if (is_bpn_external(n)) {
		printf("node%lu[fillcolor=yellow, style=\"rounded,filled\", shape = Mrecord,height=.1, label = \"", n->num);
		for (i = 0; i < n->entries; i++) {
			if (i + 1 == n->entries)
				printf("{%lu | %lu}", va_size(bpn_get_val(n, i)), bpn_get_key(n, i));
			else
				printf("{%lu | %lu} |", va_size(bpn_get_val(n, i)), bpn_get_key(n, i));
		}

		printf("\"];\n");
		return;
	}

	printf("node%lu[label=<\n", n->num);

	printf("\t<TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n");
	printf("\t\t<TR>\n");

	for (i = 0; i < n->entries; i++) {
		if (i + 1 == n->entries) {
			printf("\t\t\t<TD PORT=\"p%d\">%lu</TD>\n", i, n->SUB_AVAIL[i]);
			printf("\t\t\t<TD BGCOLOR=\"lightgreen\">%lu</TD>\n",
				   (unsigned long) bpn_get_key(n, i));
			printf("\t\t\t<TD PORT=\"p%d\">%lu</TD>\n", i + 1, n->SUB_AVAIL[i + 1]);
		} else {
			printf("\t\t\t<TD PORT=\"p%d\">%lu</TD>\n", i, n->SUB_AVAIL[i]);
			printf("\t\t\t<TD BGCOLOR=\"lightgreen\">%lu</TD>\n",
				(unsigned long) bpn_get_key(n, i));
		}
	}
	printf("\t\t</TR>\n");
	printf("\t</TABLE>\n");

	printf(">];\n\n");

	for (i = 0; i < n->entries + 1; i++)
		printf("\"node%lu\":p%d -> \"node%lu\"\n",
			n->num, i, ((struct bpn *) n->SUB_LINKS[i])->num);

	for (i = 0; i <= n->entries; i++)
		build_graph(n->SUB_LINKS[i]);
}

static void
assign_node_id(struct bpn *n, int *num)
{
	int i;

	if (is_bpn_external(n)) {
		n->num = *num;
		return;
	}

	n->num = *num;
	for (i = 0; i <= n->entries; i++) {
		(*num)++;
		assign_node_id(n->SUB_LINKS[i], num);
	}
}

void dump_tree(struct bpt_root *root)
{
	struct bpn *node = root->node;
	int num = 0;

	assign_node_id(node, &num);

	fprintf(stdout, "digraph G\n{\n");
	/* fprintf(stdout, "node [shape = Mrecord,height=.1];\n"); */
	fprintf(stdout, "node [shape = plaintext,height=.1];\n");
	build_graph(node);
	fprintf(stdout, "}\n");

	fprintf(stdout, "# run: ./test | dot -Tpng -o btree.png\n");
}
#endif
