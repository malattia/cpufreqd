/*
 *  Copyright (C) 2005  Mattia Dongili<malattia@linux.it>
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
 *
 *  EXEC Plugin
 *  -----------
 *
 *  This plugin execute command on cpufreqd events. Command are serialized.
 *
 *  !! WARNING !!
 *  starting tasks when cpufreqd switches between 2 profiles rapidly in
 *  succession may not be what you want, be really careful when defining
 *  exec_* directives.
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpufreqd_plugin.h"

struct exec_cmd {
	const char *cmd;
	struct exec_cmd *next;
};

static struct exec_cmd *exe_q;
static pthread_mutex_t exe_q_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t exe_q_cond = PTHREAD_COND_INITIALIZER;
static pthread_t exe_thread;

static struct exec_cmd exe_exit_cmd = { .cmd = "", .next = NULL };

static int exec_parse (const char *line, void **obj) {
	*obj = strdup(line);
	if (*obj == NULL) {
		clog(LOG_ERR, "Couldn't anough room for the command.\n");
		return -1;
	}
	return 0;
}

/* Grab a command from the queue and execute it
 */
static void *queue_launcher (void *arg) {
	struct exec_cmd *etemp = NULL;

	while (1) {
		pthread_mutex_lock(&exe_q_mtx);
		while (exe_q == NULL) {
			pthread_cond_wait(&exe_q_cond, &exe_q_mtx);
		}
		/* shift queue */
		etemp = exe_q;
		exe_q = exe_q->next;

		pthread_mutex_unlock(&exe_q_mtx);

		if (etemp->cmd[0]) {
			/* fork + excl */
			clog(LOG_DEBUG, "EXE: %s\n", etemp->cmd);
			free(etemp);
		} else
			break;
	}
	return NULL;
}

/* Add a command to the queue and wake the exec_thread
 */
static void exec_enqueue (const char *cmd) {
	struct exec_cmd *etemp = NULL;
	struct exec_cmd *loop = NULL;
	
	pthread_mutex_lock(&exe_q_mtx);

	etemp = calloc(1, sizeof(struct exec_cmd));
	if (etemp == NULL) {
		clog(LOG_ERR, "Couldn't enqueue command \"%s\".\n", cmd);
	} else {
		etemp->cmd = cmd;
		if (exe_q == NULL) {
			exe_q = etemp;
		} else {
			loop = exe_q;
			while (loop->next)
				loop = loop->next;
			loop->next = etemp;
		}
		clog(LOG_DEBUG, "enqueued: %s\n", etemp->cmd);
		pthread_cond_signal(&exe_q_cond);
	}
	pthread_mutex_unlock(&exe_q_mtx);
}

static void change (void *obj, const struct cpufreq_policy *old,
			const struct cpufreq_policy *new) {
	exec_enqueue((char *)obj);
}

/* Launch the thread that will wait on the queue
 * to have some command available.
 */
static int exec_init (void) {

	/* launch exec thread */
	if (pthread_create(&exe_thread, NULL, &queue_launcher, NULL) != 0)
		return -1;

	return 0;
}

static int exec_exit (void) {
	int ret = 0;
	struct exec_cmd *etemp = NULL;
	
	pthread_mutex_lock(&exe_q_mtx);
	/* push exit into queue */
	exe_exit_cmd.next = exe_q;
	exe_q = &exe_exit_cmd;
	
	/* wake the thread */
	pthread_cond_signal(&exe_q_cond);
	pthread_mutex_unlock(&exe_q_mtx);

	/* cleanup */
	ret = pthread_join(exe_thread, NULL);
	if (ret != 0) {
		clog(LOG_ERR, "Couldn't join exec thread.\n");
	}
	
	/* free command list if any no need to acquire the mutex */
	while (exe_q != NULL) {
		etemp = exe_q;
		exe_q = exe_q->next;
		free(etemp);
	}
	return 0;
}

static struct cpufreqd_keyword kw[] =  {
	{
		.word = "exec_post",
		.parse = exec_parse,
		.evaluate = NULL,
		.profile_pre_change = &change,
		.profile_post_change = &change,
		.rule_pre_change = &change,
		.rule_post_change = &change,
		.free = NULL,
	},
	{
		.word = "exec_pre",
		.parse = exec_parse,
		.evaluate = NULL,
		.profile_pre_change = &change,
		.profile_post_change = &change,
		.rule_pre_change = &change,
		.rule_post_change = &change,
		.free = NULL,
	},
	{	.word = NULL, }
};

static struct cpufreqd_plugin plugin =  {
	.plugin_name = "exec",
	.keywords = kw,
	.plugin_init = exec_init,
	.plugin_exit = exec_exit,
	.plugin_update = NULL,
	.plugin_conf = NULL,
	.plugin_post_conf = NULL,
};

/*
 *  A cpufreqd plugin MUST define the following function to provide the
 *  core cpufreqd with the correct struct cpufreqd_plugin structure
 */
struct cpufreqd_plugin *create_plugin(void) {
	return &plugin;
}


