/*
 *  Copyright (C) 2005  Mattia Dongili <malattia@linux.it>
 *                      Hrvoje Zeba <hrvoje@boo.mi2.hr>
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
#define CMD_CUR_PROFILES	7 /* no argument */

#define ARG_MASK		0x0000ffff
#define MODE_DYNAMIC		(1)
#define MODE_MANUAL		(2)

#define REMOTE_CMD(c)		(c >> CMD_SHIFT)
#define REMOTE_ARG(c)		(c & ARG_MASK)
#define MAKE_COMMAND(cmd, arg)	((cmd << CMD_SHIFT) | arg)
#define INVALID_CMD		0xffffffff

#endif
