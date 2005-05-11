/*
 *  Copyright (C) 2002-2005  Mattia Dongili <malattia@gmail.com>
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

#ifndef __CPUFREQD_H__
#define __CPUFREQD_H__

#define __CPUFREQD_VERSION__  "2.0.0"

/* log levels, for use in the whole application */
#define LOG_EMERG	  0	
#define LOG_ALERT	  1
#define LOG_CRIT	  2
#define LOG_ERR		  3
#define LOG_WARNING	4
#define LOG_NOTICE	5
#define LOG_INFO	  6
#define LOG_DEBUG	  7

#define DEFAULT_POLL 1
#define DEFAULT_VERBOSITY 3

#define MAX_STRING_LEN 255
#define MAX_PATH_LEN 512

#endif /* __CPUFREQD_H__ */
