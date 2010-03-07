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

#ifndef __CPUFREQD_LOG_H
#define __CPUFREQD_LOG_H

/* log levels, for use in the whole application */
#define LOG_EMERG		0
#define LOG_ALERT		1
#define LOG_CRIT		2
#define LOG_ERR			3
#define LOG_WARNING		4
#define LOG_NOTICE		5
#define LOG_INFO		6
#define LOG_DEBUG		7

extern void cpufreqd_log (const int prio, const char *fmt, ...);

#ifdef __GNUC__
#  define clog(__prio,__fmt,...) \
	cpufreqd_log(__prio, "%-25s: "__fmt, __func__, ## __VA_ARGS__)

#else
#  define clog(__prio,__fmt,...) \
	cpufreqd_log(__prio, __fmt, __VA_ARGS__)
#endif

#endif
