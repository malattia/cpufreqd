/*
 *  Copyright (C) 2002,2003,2004  Mattia Dongili<dongili@supereva.it>
 *                                George Staikos <staikos@0wned.org>
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
#include "list.h"

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

#define CPUFREQD_CONFIG   CPUFREQD_CONFDIR"cpufreqd.conf"
#define CPUFREQD_PIDFILE  CPUFREQD_STATEDIR"run/cpufreqd.pid"

#define CPUFREQ_SYSFS_INTERFACE 	        "/sys/devices/system/cpu/cpu0/cpufreq"
#define CPUFREQ_SYSFS_INTERFACE_POLICY    "/sys/devices/system/cpu/cpu%i/cpufreq/scaling_governor"
#define CPUFREQ_SYSFS_INTERFACE_MAX       "/sys/devices/system/cpu/cpu%i/cpufreq/scaling_max_freq"
#define CPUFREQ_SYSFS_INTERFACE_MIN       "/sys/devices/system/cpu/cpu%i/cpufreq/scaling_min_freq"
#define CPUFREQ_SYSFS_INTERFACE_CPUMAX    "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"
#define CPUFREQ_SYSFS_INTERFACE_CPUMIN    "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq"
#define CPUFREQ_PROC_INTERFACE            "/proc/cpufreq"
#define CPUFREQ_PROC_INTERFACE_SPEED      "/proc/sys/cpu/0/speed"
#define CPUFREQ_PROC_INTERFACE_MIN        "/proc/sys/cpu/0/speed-min"
#define CPUFREQ_PROC_INTERFACE_MAX        "/proc/sys/cpu/0/speed-max"

void    print_help            (const char *me);
void    print_version         (const char *me);
int     read_args             (int argc, char *argv[]);
int     init_configuration    (void);
void    free_configuration    (void);
void term_handler             (int sig);
void int_handler              (int sig);
void hup_handler              (int sig);
