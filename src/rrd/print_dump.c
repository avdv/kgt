/* $Id$ */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../ast.h"

#include "io.h"
#include "rrd.h"
#include "pretty.h"
#include "print.h"
#include "node.h"

static void
print_indent(FILE *f, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		fprintf(f, "    ");
	}
}

static struct node_walker rrd_print;

static int
visit_skip(struct node *n, struct node **np, int depth, void *arg)
{
	FILE *f = arg;

	(void) n;
	(void) np;

	print_indent(f, depth);
	fprintf(f, "NT_SKIP\n");

	return 1;
}

static int
visit_leaf(struct node *n, struct node **np, int depth, void *arg)
{
	FILE *f = arg;

	(void) np;

	print_indent(f, depth);
	if (n->u.leaf.type == LEAF_IDENTIFIER) {
		fprintf(f, "NT_LEAF(IDENTIFIER): %s\n", n->u.leaf.text);
	} else {
		fprintf(f, "NT_LEAF(TERMINAL): \"%s\"\n", n->u.leaf.text);
	}

	return 1;
}

static int
visit_list(struct node *n, struct node **np, int depth, void *arg)
{
	FILE *f = arg;

	(void) np;

	print_indent(f, depth);
	fprintf(f, "NT_LIST(%s): [\n", n->u.list.type == LIST_CHOICE ? "CHOICE" : "SEQUENCE");
	if (!node_walk_list(&n->u.list.list, &rrd_print, depth + 1, arg)) {
		return 0;
	}
	print_indent(f, depth);
	fprintf(f, "]\n");

	return 1;
}

static int
visit_loop(struct node *n, struct node **np, int depth, void *arg)
{
	FILE *f = arg;

	(void) np;

	print_indent(f, depth);
	fprintf(f, "NT_LOOP:\n");
	if (n->u.loop.forward->type != NODE_SKIP) {
		print_indent(f, depth + 1);
		fprintf(f, ".forward:\n");
		if (!node_walk_list(&n->u.loop.forward, &rrd_print, depth + 2, arg)) {
			return 0;
		}
	}

	if (n->u.loop.backward->type != NODE_SKIP) {
		print_indent(f, depth + 1);
		fprintf(f, ".backward:\n");
		if (!node_walk_list(&n->u.loop.backward, &rrd_print, depth + 2, arg)) {
			return 0;
		}
	}

	return 1;
}

static struct node_walker rrd_print = {
	visit_skip,
	visit_leaf, visit_leaf,
	visit_list, visit_list,
	visit_loop
};

void
rrd_print_dump(struct node **n)
{
	node_walk(n, &rrd_print, 1, stdout);
}

