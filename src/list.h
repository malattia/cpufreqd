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

#ifndef __LIST_H
#define __LIST_H

#include <stdlib.h>

#define LIST_FOREACH(lst, fun) \
                {\
                  for (struct NODE *n=lst->first; n!=NULL; n=n->next) {\
                    (*fun)(n->content);\
                  }\
                }

#define LIST_FOREACH_NODE(node, list) \
	for (struct NODE *node = (list)->first; node != NULL; node = node->next)

#define LIST_EMPTY(list) ((list)->first==NULL)

/*
 * Doubly linked list
 */
struct LIST {
	struct NODE *first;
	struct NODE *last;
};

/*
 * Node
 */
struct NODE {
	void *content;
	struct NODE *next;
	struct NODE *prev;
};

/*
 * Frees all the elements of the list.
 * Returns the number of freed elements.
 */
extern int list_free_sublist(struct LIST *l, struct NODE *n);

/*
 * Initializes a new string_list and
 * returns a reference to it
 */
extern struct LIST *list_new(void);

/*
 *  Removes the node nd and returns the pointer
 *  to the next node
 */
struct NODE *list_remove_node(struct LIST *l, struct NODE *nd);

/*
 * Initializes a new string_node and
 * returns a reference to it
 */
extern struct NODE *node_new(void *c, size_t s);

/*
 *  Frees a NODE.
 */
extern void node_free(struct NODE *n);

/*
 * Appends a node to the list
 */
extern void list_append(struct LIST *l, struct NODE *n);

#endif
