/*
 *  Copyright (C) 2005-2006  Mattia Dongili<malattia@linux.it>
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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
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
		clog(LOG_ERR, "Couldn't make enough room for the command '%s'.\n",
				line);
		return -1;
	}
	return 0;
}

/* Grab a command from the queue and execute it
 */
static void *queue_launcher (void __UNUSED__ *arg) {
	struct exec_cmd *etemp = NULL;
	pid_t child_pid = 0;
	int child_ret = 0;
	struct sigaction signal_action;

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
			switch (child_pid = fork()) {
				case -1:
					clog(LOG_ERR, "Unable to fork new process: %s\n",
							strerror(errno));
					break;
				case 0:
					clog(LOG_DEBUG, "child process, exec 'sh -c %s'\n",
							etemp->cmd);
					/* child */
					/* reset signal handlers to default */
					sigemptyset(&signal_action.sa_mask);
					sigaddset(&signal_action.sa_mask, SIGTERM);
					sigaddset(&signal_action.sa_mask, SIGINT);
					sigaddset(&signal_action.sa_mask, SIGHUP);
					sigaddset(&signal_action.sa_mask, SIGALRM);
					signal_action.sa_flags = 0;
					signal_action.sa_handler = SIG_DFL;
					sigaction(SIGTERM, &signal_action, 0);
					sigaction(SIGINT, &signal_action, 0);
					sigaction(SIGHUP, &signal_action, 0);
					sigaction(SIGALRM, &signal_action, 0);

					/* TODO: test if file exists, is executable, etc.*/
					/* perhaps we don't need that, beacause exit status will be logged*/
					child_ret = execl("/bin/sh", "/bin/sh", "-c", etemp->cmd, NULL);
					clog(LOG_ERR, "Unable to execl new process: %s\n",
							strerror(errno));
					exit(1);
				default:
					waitpid(child_pid, &child_ret, 0);
					if(WIFEXITED(child_ret)) {
						clog(LOG_NOTICE, "\"%s\" exited with status %d\n",
								etemp->cmd, WEXITSTATUS(child_ret));
						clog(LOG_DEBUG, "EXE: %s done\n", etemp->cmd);
					} else if(WIFSIGNALED(child_ret)) {
						clog(LOG_NOTICE, "\"%s\" exited on signal %d\n",
								etemp->cmd, WTERMSIG(child_ret));
					} else {
						clog(LOG_WARNING, "\"%s\" exited with status %d\n",
								etemp->cmd, child_ret);
					}

			}
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

/* avoid being called for each cpu profile change,
 * only act on the last one
 */
static int profile_pre_change_calls;
static int profile_post_change_calls;

static void exec_profile_pre_change(void __UNUSED__ *obj,
		const struct cpufreq_policy __UNUSED__ *old,
		const struct cpufreq_policy __UNUSED__ *new,
		const unsigned int __UNUSED__ cpu) {
	struct cpufreqd_info *cinfo = get_cpufreqd_info();
	clog(LOG_DEBUG, "launch counter = %d\n", profile_pre_change_calls);
	if (profile_pre_change_calls == 0 || cinfo->cpufreqd_mode == MODE_MANUAL)
		exec_enqueue((char *)obj);
	profile_pre_change_calls++;
}
static void exec_profile_post_change(void *obj,
		const struct cpufreq_policy __UNUSED__ *old,
		const struct cpufreq_policy __UNUSED__ *new,
		const unsigned int __UNUSED__ cpu) {
	struct cpufreqd_info *cinfo = get_cpufreqd_info();
	clog(LOG_DEBUG, "launch counter = %d\n", profile_post_change_calls);
	if (profile_post_change_calls == 0 || cinfo->cpufreqd_mode == MODE_MANUAL)
		exec_enqueue((char *)obj);
	profile_post_change_calls++;
}

static void exec_rule_change(void *obj,
		const struct rule __UNUSED__ *old,
		const struct rule __UNUSED__ *new) {
	exec_enqueue((char *)obj);
}

static int exec_update(void) {
	profile_pre_change_calls = 0;
	profile_post_change_calls = 0;
	return 0;
}

/* Launch the thread that will wait on the queue
 * to have some command available.
 */
static int exec_init (void) {
	int ret = 0;
	/* launch exec thread */
	if ((ret = pthread_create(&exe_thread, NULL, &queue_launcher, NULL)) != 0) {
		clog(LOG_ERR, "Unable to launch thread: %s\n", strerror(ret));
		return ret;
	}

	return ret;
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
	/* only define post change events */
		.word = "exec_post",
		.parse = exec_parse,
		.evaluate = NULL,
		.profile_pre_change = NULL,
		.profile_post_change = &exec_profile_post_change,
		.rule_pre_change = NULL,
		.rule_post_change = &exec_rule_change,
		.free = NULL,
	},
	{
	/* only define pre change events */
		.word = "exec_pre",
		.parse = exec_parse,
		.evaluate = NULL,
		.profile_pre_change = &exec_profile_pre_change,
		.profile_post_change = NULL,
		.rule_pre_change = &exec_rule_change,
		.rule_post_change = NULL,
		.free = NULL,
	},
	{	.word = NULL, }
};

static struct cpufreqd_plugin plugin =  {
	.plugin_name = "exec",
	.keywords = kw,
	.plugin_init = exec_init,
	.plugin_exit = exec_exit,
	.plugin_update = &exec_update,
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


