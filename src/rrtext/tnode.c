/*
 * Copyright 2014-2019 Katherine Flavel
 *
 * See LICENCE for the full copyright terms.
 */

#define _BSD_SOURCE

#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "../ast.h"
#include "../xalloc.h"

#include "../rrd/rrd.h"
#include "../rrd/pretty.h"
#include "../rrd/node.h"
#include "../rrd/rrd.h"
#include "../rrd/list.h"
#include "../rrd/stack.h"

#include "io.h"
#include "tnode.h"

#include "../xalloc.h"

static struct tnode *
tnode_create_node(const struct node *node);

static void
tnode_free_tlist(struct tlist *list)
{
	size_t i;

	assert(list != NULL);

	for (i = 0; i < list->n; i++) {
		tnode_free(list->a[i]);
	}

	free(list->a);
}

void
tnode_free(struct tnode *n)
{
	if (n == NULL) {
		return;
	}

	switch (n->type) {
	case TNODE_SKIP:
	case TNODE_LITERAL:
	case TNODE_RULE:
		break;

	case TNODE_ALT:
	case TNODE_ALT_SKIPPABLE:
		tnode_free_tlist(&n->u.alt);
		break;

	case TNODE_SEQ:
		tnode_free_tlist(&n->u.seq);
		break;

	case TNODE_LOOP:
		tnode_free(n->u.loop.forward);
		tnode_free(n->u.loop.backward);
		break;
	}

	free(n);
}

static struct tlist
tnode_create_list(const struct list *list)
{
	const struct list *p;
	struct tlist new;
	size_t i;

	new.n = list_count(list);
	if (new.n == 0) {
		new.a = NULL;
		return new;
	}

	new.a = xmalloc(sizeof *new.a * new.n);

	for (i = 0, p = list; i < new.n; i++, p = p->next) {
		assert(p != NULL);

		new.a[i] = tnode_create_node(p->node);
	}

	return new;
}

static enum tnode_looptype
tnode_looptype(const struct node *loop)
{
	if (loop->u.loop.max == 1 && loop->u.loop.min == 1) {
		return TNODE_LOOP_ONCE;
	} else if (loop->u.loop.max == 0 && loop->u.loop.min > 0) {
		return TNODE_LOOP_ATLEAST;
	} else if (loop->u.loop.max > 0 && loop->u.loop.min == 0) {
		return TNODE_LOOP_UPTO;
	} else if (loop->u.loop.max > 0 && loop->u.loop.min == loop->u.loop.max) {
		return TNODE_LOOP_EXACTLY;
	} else if (loop->u.loop.max > 1 && loop->u.loop.min > 1) {
		return TNODE_LOOP_BETWEEN;
	}

	assert(!"unreached");
	return 0;
}

static struct tnode *
tnode_create_node(const struct node *node)
{
	struct tnode *new;

	new = xmalloc(sizeof *new);

	if (node == NULL) {
		new->type = TNODE_SKIP;
		new->w = 0;
		new->y = 0;
		new->h = 1;

		return new;
	}

	switch (node->type) {
	case NODE_LITERAL:
		new->type = TNODE_LITERAL;
		new->u.literal = node->u.literal;
		new->w = strlen(new->u.literal) + 4;
		new->y = 0;
		new->h = 1;
		break;

	case NODE_RULE:
		new->type = TNODE_RULE;
		new->u.name = node->u.name;
		new->w = strlen(new->u.name) + 2;
		new->y = 0;
		new->h = 1;
		break;

	case NODE_ALT:
	case NODE_ALT_SKIPPABLE:
		new->type = node->type == NODE_ALT ? TNODE_ALT : TNODE_ALT_SKIPPABLE;
		new->u.alt = tnode_create_list(node->u.alt);

		{
			unsigned w;
			size_t i;

			w = 0;

			for (i = 0; i < new->u.alt.n; i++) {
				if (new->u.alt.a[i]->w > w) {
					w = new->u.alt.a[i]->w;
				}
			}

			new->w = w + 6;
		}

		{
			unsigned y;

			assert(new->u.alt.n > 0 && new->u.alt.a[0] != NULL);

			/*
			 * Alt lists hang below the line.
			 * The y-height of this node is the y-height of just the first list item
			 * because the first item is at the top of the list, plus the height of
			 * the skip node above that.
			 */
			y = new->u.alt.a[0]->y;

			if (node->type == NODE_ALT_SKIPPABLE) {
				y += 2;
			}

			new->y = y;
		}

		{
			unsigned h;
			size_t i;

			h = 0;

			for (i = 0; i < new->u.alt.n; i++) {
				h += 1 + new->u.alt.a[i]->h;
			}

			if (node->type == NODE_ALT_SKIPPABLE) {
				h += 2;
			}

			new->h = h - 1;
		}

		break;

	case NODE_SEQ:
		new->type = TNODE_SEQ;
		new->u.seq = tnode_create_list(node->u.seq);

		{
			unsigned w;
			size_t i;

			w = 0;

			for (i = 0; i < new->u.seq.n; i++) {
				w += new->u.seq.a[i]->w + 2;
			}

			new->w = w - 2;
		}

		{
			unsigned y;
			size_t i;

			y = 0;

			for (i = 0; i < new->u.seq.n; i++) {
				if (new->u.seq.a[i]->y > y) {
					y = new->u.seq.a[i]->y;
				}
			}

			new->y = y;
		}

		{
			unsigned top = 0, bot = 1;
			size_t i;

			for (i = 0; i < new->u.seq.n; i++) {
				unsigned y, z;

				y = new->u.seq.a[i]->y;
				if (y > top) {
					top = y;
				}

				z = new->u.seq.a[i]->h;
				if (z - y > bot) {
					bot = z - y;
				}
			}

			new->h = bot + top;
		}

		break;

	case NODE_LOOP:
		new->type = TNODE_LOOP;
		new->u.loop.looptype = tnode_looptype(node);
		new->u.loop.forward  = tnode_create_node(node->u.loop.forward);
		new->u.loop.backward = tnode_create_node(node->u.loop.backward);
		new->u.loop.min = node->u.loop.min;
		new->u.loop.max = node->u.loop.max;

		{
			unsigned w;
			unsigned wf, wb, cw;

			wf = new->u.loop.forward->w;
			wb = new->u.loop.backward->w;

			w = (wf > wb ? wf : wb) + 6;

			cw = loop_label(new, NULL);

			if (cw > 0) {
				if (cw + 6 > w) {
					w = cw + 6;
				}
			}

			new->w = w;
		}

		{
			new->y = new->u.loop.forward->y;
		}

		{
			unsigned h;

			h = new->u.loop.forward->h + new->u.loop.backward->h + 1;

			if (loop_label(new, NULL) > 0) {
				if (new->u.loop.backward->type != TNODE_SKIP) {
					h += 2;
				}
			}

			new->h = h;
		}

		break;
	}

	return new;
}

struct tnode *
rrd_to_tnode(const struct node *node)
{
	return tnode_create_node(node);
}

