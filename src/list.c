/*
 *  Copyright (C) 2002-2005  Mattia Dongili <malattia@linux.it>
 *                           George Staikos <staikos@0wned.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <string.h>
#include "list.h"

int list_free_sublist(struct LIST *l, struct NODE *n) {
	int ret = 0;
	struct NODE *prev = 0L;

	/* return if !n */
	if (!n)
		return 0;

	/* terminate the list just before the subtree to remove */
	if (n->prev) {  /* unless the starting node is the first */
		l->last = n->prev;
		l->last->next = 0L;
	}

	/* free subtree */
	while(n) {
		prev = n;
		n = n->next;
		free(prev);
		ret++;
	}
	return ret;
}

struct LIST *list_new(void) {
	struct LIST *ret = 0L;
	ret = malloc(sizeof(struct LIST));
	if (ret != NULL) {
		memset(ret, 0, sizeof(struct LIST));
	}
	return ret;
}

/*  Removes the node nd and returns the pointer
 *  to the next node
 */
struct NODE *list_remove_node(struct LIST *l, struct NODE *nd) {
	struct NODE *ret = NULL;
	if (nd != NULL) {
		ret = nd->next;
		/* detach the node */
		if (nd->prev != NULL) {
			nd->prev->next = nd->next;
		}
		if (nd->next != NULL) {
			nd->next->prev = nd->prev;
		}
		/* fix the beginning of
		 * the list if necessary
		 */
		if (l->first == nd)
			l->first = nd->next;
		/* fix the end of
		 * the list if necessary
		 */
		if (l->last == nd)
			l->last = nd->prev;
		/* free unused memory */
		node_free(nd);
	}
	return ret;
}

struct NODE *node_new(void *cnt, size_t s) {
	struct NODE *ret = 0L;
	ret = malloc(sizeof(struct NODE) + s);
	if (ret != NULL) {
		memset(ret, 0, sizeof(struct NODE) + s);
		ret->content = ret + 1;
		if (cnt != NULL)
			memcpy(ret->content, cnt, s);
	}
	return ret;
}

void node_free(struct NODE *n) {
	free(n);
}

void list_append(struct LIST *l, struct NODE *n) {
	if (l->first == NULL) {
		l->first = l->last = n;
	} else {
		l->last->next = n;
		n->prev = l->last;
		l->last = n;
	}
}
