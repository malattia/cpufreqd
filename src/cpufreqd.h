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

#ifndef __CPUFREQD_H__
#define __CPUFREQD_H__

#define __CPUFREQD_VERSION__	"2.0.0-pre2"
#define __CPUFREQD_MAINTAINER__	"malattia@linux.it"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef CPUFREQD_CONFDIR
#define CPUFREQD_CONFDIR  "/etc/"
#endif

#ifndef CPUFREQD_LIBDIR
#define CPUFREQD_LIBDIR   "/usr/lib/cpufreqd/"
#endif

#ifndef CPUFREQD_STATEDIR
#define CPUFREQD_STATEDIR   "/var/"
#endif

#define CPUFREQD_CONFIG		CPUFREQD_CONFDIR"cpufreqd.conf"
#define CPUFREQD_PIDFILE	CPUFREQD_STATEDIR"run/cpufreqd.pid"
#define CPUFREQD_SOCKFILE	"/tmp/cpufreqd.sock"


#define DEFAULT_POLL		1
#define DEFAULT_VERBOSITY	3

#define MAX_STRING_LEN		255
#define MAX_PATH_LEN		512

#endif /* __CPUFREQD_H__ */
