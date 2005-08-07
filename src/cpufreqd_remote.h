#ifndef __CPUFREQD_REMOTE_H
#define __CPUFREQD_REMOTE_H

/*
 * Format:
 *	it is an uint32_t used as bitmask
 *	
 *	31-16     15-0 
 *	<command> <arguments>
 * 
 * The response may be longer than a single line and is 
 * terminated by the RESPONSE_END (see defines).
 */

#define CMD_SHIFT		16
#define CMD_UPDATE_STATE	1 /* no arguments */
#define CMD_SET_PROFILE		2 /* <profile index> */
#define CMD_LIST_PROFILES	3 /* no arguments */
#define CMD_SET_RULE		4 /* <rule index> */
#define CMD_LIST_RULES		5 /* no arguments */
#define CMD_SET_MODE		6 /* <mode> */

#define ARG_MASK		0x0000ffff
#define ARG_DYNAMIC		(1)
#define ARG_MANUAL		(2)

#define REMOTE_CMD(c)		(c >> CMD_SHIFT)
#define REMOTE_ARG(c)		(c & ARG_MASK)
#define MAKE_COMMAND(cmd, arg)	((cmd << CMD_SHIFT) | arg)
#define INVALID_CMD		0xffffffff

#endif
