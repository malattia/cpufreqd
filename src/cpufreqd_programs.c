/*
 *  Copyright (C) 2002-2006  Mattia Dongili <malattia@linux.it>
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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cpufreqd_plugin.h"

#define PRG_LENGTH 64

/* a tree structure, contains strings
 * - a binary tree
 * - almost balanced
 * - no duplicates allowed
 * - contains a 'used' short to reuse the allocated
 *   memory through each loop
 */
struct TNODE {
	char name[PRG_LENGTH];
	struct TNODE *left;
	struct TNODE *right;
	struct TNODE *parent;
	unsigned short used;
	unsigned short height;
};
typedef struct TNODE TNODE;
typedef TNODE TREE;

static TREE *running_programs = 0L;

/* create a new node obj */
static TNODE * new_tnode(void) {
	TNODE *ret = (TNODE *)calloc(1, sizeof(TNODE));
	return ret;
}

/* set the usage count to 0 */
static void neglect_node(TNODE **n) {
	if (*n != NULL) {
		(*n)->used = 0;
	}
}

/* free node */
static void free_tnode(TNODE *n) {
	free(n);
}

/* free a full tree */
static void free_tree(TREE *t) {
	if (t != NULL) {
		if (t->right != NULL) {
			free_tree(t->right);
			t->right = NULL;
		}
		if (t->left != NULL) {
			free_tree(t->left);
			t->left = NULL;
		}
		free_tnode(t);
	}
}

static void insert_tnode(TREE **t, const char *c) {
	int cmp = 0;

	if (*t == NULL) {
		*t = new_tnode();
		memcpy((*t)->name, c, PRG_LENGTH);
		(*t)->used = 1;
		clog(LOG_DEBUG, "new node (%s)\n", c);
		return;
	}

	/* insert node */
	cmp = strncmp(c, (*t)->name, PRG_LENGTH);
	if (cmp > 0) {
		insert_tnode(&((*t)->right), c);
		(*t)->right->parent = *t;
		(*t)->right->height = (short unsigned)((*t)->height + 1);
	} else if (cmp < 0) {
		insert_tnode(&((*t)->left), c);
		(*t)->left->parent = *t;
		(*t)->left->height = (short unsigned)((*t)->height + 1);
	} else {
		(*t)->used++;
	}
}
/* find the predecessor of the input node.
 * It must be the rightomost child in the left subtree.
 * NOT RECURSIVE
 */
static TNODE *predecessor(TNODE *n) {
	TNODE *ret = n->left;
	while (ret->right != NULL) {
		ret = ret->right;
	}
	return ret;
}

#if 0
/* find the successor of the input node.
 * It must be the leftomost child in the right subtree
 * NOT RECURSIVE
 */
static TNODE *successor(TNODE *n) {
	TNODE *ret = n->right;
	while (ret->left!=NULL) {
		ret = ret->left;
	}
	return ret;
}
#endif

/* removes an unused node from the tree
*/
static void sweep_unused_node(TNODE **n) {
	TNODE *swap = NULL;

	if (*n != NULL && (*n)->used == 0) {

		/* 1- a node with no child */
		if ((*n)->right == NULL && (*n)->left == NULL) {
			if ((*n)->parent!=NULL) {
				if ((*n)->parent->left == *n) (*n)->parent->left = NULL;
				if ((*n)->parent->right == *n) (*n)->parent->right = NULL;
			} else {
				/* hey i'm removing the root elem */
				running_programs = NULL;
			}
			clog(LOG_DEBUG, "Removed node (%s).\n", (*n)->name);
			free_tnode(*n);
			*n = NULL;
		}

		/* 2- a node with one child */
		else if ((*n)->right == NULL && (*n)->left != NULL) {
			if ((*n)->parent != NULL) {
				if ((*n)->parent->left == *n) {
					(*n)->parent->left = (*n)->left;
				}
				else if ((*n)->parent->right == *n) {
					(*n)->parent->right = (*n)->left;
				}
				(*n)->left->parent = (*n)->parent;
			} else {
				/* hey i'm removing the root elem */
				running_programs = (*n)->left;
			}
			clog(LOG_DEBUG, "Removed node (%s).\n", (*n)->name);
			free_tnode(*n);
			*n = NULL;
		}
		else if ((*n)->left == NULL && (*n)->right != NULL) {
			if ((*n)->parent != NULL) {
				if ((*n)->parent->left == *n) {
					(*n)->parent->left = (*n)->right;
				}
				else if ((*n)->parent->right == *n) {
					(*n)->parent->right = (*n)->right;
				}
				(*n)->right->parent = (*n)->parent;
			} else {
				/* hey i'm removing the root elem */
				running_programs = (*n)->right;
			}
			clog(LOG_DEBUG, "Removed node (%s).\n", (*n)->name);
			free_tnode(*n);
			*n = NULL;
		}

		/* 3- a node with 2 children */
		else {
			/* this actually unbalances the tree */
			swap = predecessor(*n);
			/* consider predecessor subtree (can't have right childs) */
			if (swap->parent->left == swap) {
				swap->parent->left = swap->left;
			} else {
				swap->parent->right = swap->left;
			}
			if (swap->left != NULL)
				swap->left->parent = swap->parent;

			clog(LOG_DEBUG, "Removed node (%s).\n", (*n)->name);
			/* copy node data */
			strncpy((*n)->name, swap->name, PRG_LENGTH);
			(*n)->used = swap->used;
			free_tnode(swap);
			*n = NULL;
		}
	}
}

/* preorder visit
 */
static void preorder_visit(TREE *t, void (*cb)(TNODE **n)) {
	if (t != NULL && t->left != NULL) {
		preorder_visit(t->left, cb);
	}
	if (t != NULL) {
		cb(&t);
	}
	if (t != NULL && t->right != NULL) {
		preorder_visit(t->right, cb);
	}
}

/* find a node */
static TNODE * find_tnode(TREE *t, const char *c) {
	int cmp = 0;
	if (c != NULL && t != NULL) {
		cmp = strncmp(t->name, c, PRG_LENGTH);
		if (cmp > 0) {
			return find_tnode(t->left, c);
		} else if (cmp < 0) {
			return find_tnode(t->right, c);
		} else if (t->used > 0) {
			return t;
		}
	}
	return NULL;
}

#ifdef DEBUG_TREE
static void debug_tnode(TNODE *n) {
	if (n != NULL) {
		clog(LOG_DEBUG, "DEBUG_TREE %s [u:%d] [h:%d]\n", n->name, n->used, n->height);
	}
}

static void print_tree(TNODE *n) {
	char tab[64];
	unsigned int i=0;
	if (n != NULL) {
		for(i = 0; i < n->height && i < 63; i++) {
			tab[i]=' ';
		}
		tab[i] = '\\';
		tab[i+1] = '\0';
		clog(LOG_DEBUG, "DEBUG_TREE %s%s \t%s - [h:%d]\n",
				tab, n->name, n->parent!=NULL?n->parent->name:"nobody", n->height);
	}
}
#endif

/* int numeric_entry(const struct dirent *d)
 *
 * Select function for scandir()
 *
 */
static int numeric_entry(const struct dirent *d) {
	if (isdigit(d->d_name[0])) {
#if 0
		clog(LOG_DEBUG, "directory: %s\n", d->d_name);
#endif
		return 1;
	}
	return 0;
}

/* int get_running_programs(void)
 *
 * looks for running programs and fills the
 * global struct running_programs.
 *
 * Returns the length of the newly created list.
 */
static int programs_update(void) {

	struct dirent **namelist;
	int n = 0, ret = 0, pid = 0;
	char file[PRG_LENGTH];
	char program[PRG_LENGTH];
	char *prg_basename;
	FILE *fp;

	/* reset all nodes  */
	preorder_visit(running_programs, &neglect_node);

	n = scandir("/proc", &namelist, numeric_entry, NULL);
#if 0
	clog(LOG_DEBUG, "directories: %d\n", n);
#endif

	if (n < 0) {
		clog(LOG_ERR, "scandir: %s\n", strerror(errno));

	} else {

		while(n--) {
			snprintf(file, PRG_LENGTH - 1, "/proc/%s/cmdline", namelist[n]->d_name);
#if 0
			clog(LOG_DEBUG, "directory (%3d: %s)\n", n, file);
#endif
			pid = atoi(namelist[n]->d_name);
			free(namelist[n]);

			if (!(fp = fopen(file, "r"))) {
#if 0
				clog(LOG_DEBUG, "%s: %s\n", file, strerror(errno));
#endif
				continue; /* disappeared process?? */
			}

			if (!fgets(program, PRG_LENGTH - 1, fp)) {
#if 0
				clog(LOG_DEBUG, "%s: %s\n", file, strerror(errno));
#endif
				fclose(fp);
				continue;
			}
			fclose(fp);

			/* terminate the string */
			program[PRG_LENGTH - 1] = '\0';
#if 0
			clog(LOG_DEBUG, "read program (%3d %5d: %s)\n", n, pid, program);
#endif
			/* strip stuff after a blank space */
			prg_basename = index(program, ' ');
			if (prg_basename != NULL)
				*prg_basename = '\0';

			/* find basename */
			prg_basename = rindex(program, '/');
			if (prg_basename == NULL)
				prg_basename = program;
			else
				prg_basename++;
			if (*prg_basename == '-')
				prg_basename++;

			insert_tnode(&running_programs, prg_basename);
			ret++;
		}
	}
	free(namelist);
	clog(LOG_INFO, "read %d processes\n", ret);
	preorder_visit(running_programs, &sweep_unused_node);
#ifdef DEBUG_TREE
	preorder_visit(running_programs, &debug_tnode);
	preorder_visit(running_programs, &print_tree);
#endif
	return ret;
}

static int programs_exit(void) {
	clog(LOG_INFO, "called\n");
	free_tree(running_programs);
	return 0;
}

static int programs_parse(const char *ev, void **obj) {
	char str_copy[5*PRG_LENGTH];
	char *t_prog;
	TREE *ret=NULL;

	clog(LOG_DEBUG, "called with entries %s.\n", ev);
	strncpy(str_copy, ev, 5*PRG_LENGTH);

	t_prog = strtok(str_copy,",");
	do {
		if (t_prog == NULL)
			continue;

		insert_tnode(&ret, t_prog);
		clog(LOG_DEBUG, "read program %s\n", t_prog);
	} while ((t_prog = strtok(NULL,",")) != NULL);

	*obj = ret;
	return 0;
}

static void programs_free(void *obj) {
	free_tree(obj);
}

static int find_program(const TNODE *l) {
	clog(LOG_DEBUG, "tree ptr %p\n", l);
	return (find_tnode(running_programs, l->name) != NULL) ? MATCH :
		(l->right != NULL && find_program(l->right) == MATCH) ? MATCH :
		(l->left != NULL && find_program(l->left) == MATCH) ? MATCH : DONT_MATCH;
}

static int programs_evaluate(const void *s) {
	clog(LOG_DEBUG, "tree ptr %p\n", s);
	return find_program((const TNODE *) s);
}

static struct cpufreqd_keyword kw[] = {
	{ .word = "programs", .parse = &programs_parse,   .evaluate = &programs_evaluate, .free=programs_free },
	{ .word = NULL },
};

static struct cpufreqd_plugin programs = {
	.plugin_name      = "programs_plugin",      /* plugin_name */
	.keywords         = kw,                     /* config_keywords */
	.plugin_exit      = &programs_exit,         /* plugin_exit */
	.plugin_update    = &programs_update,       /* plugin_update */
};

/* MUST DEFINE THIS ONE */
struct cpufreqd_plugin *create_plugin (void) {
	return &programs;
}
