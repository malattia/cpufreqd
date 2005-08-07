#ifndef __CPUFREQD_REMOTE_H
#define __CPUFREQD_REMOTE_H

/*
 * A command is one of the following.
 *
 * Format:
 *	<command> <arguments>
 * <command> is max 4 chars (see MAX_CMD definition)
 *
 * Max command length:
 *	see definition of MAX_CMD_BUF (see define)
 * so <arguments> can be up to 250 chars long.
 *
 * The response may be longer than a single line and is 
 * temrinated by the RESPONSE_END (see defines).
 */

#define UPDATE_STATE	"UPDS" /* no arguments */
#define SET_PROFILE	"SETP" /* SETP <profile name> */
#define LIST_PROFILES	"LISP" /* no arguments */
#define SET_RULES	"SETR" /* SETR <rule name> */
#define LIST_RULES	"LISR" /* no arguments */

#define RESPONSE_END	"\r\n.\r\n"
#define RESPONSE_END_LEN 5

#define MAX_CMD		4
#define MAX_CMD_BUF	255

#endif
